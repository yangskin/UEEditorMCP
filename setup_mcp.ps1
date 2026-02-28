# ============================================================================
#  UEEditorMCP - One-Click Python Setup (PowerShell)
#  Automatically finds UE engine's built-in Python, creates a venv,
#  and installs the MCP package. No external Python installation required.
# ============================================================================

param(
    [string]$EngineRoot = ""
)

$ErrorActionPreference = "Stop"
$PluginDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$PythonDir  = Join-Path $PluginDir "Python"
$VenvDir    = Join-Path $PythonDir ".venv"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PluginDir)  # Up from Plugins/UEEditorMCP

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host " UEEditorMCP - Python Environment Setup"  -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# --- Step 1: Find UE Engine Python ---
Write-Host "[1/4] Searching for Unreal Engine Python..." -ForegroundColor Yellow

$UEPython = $null

# --- Priority 1: User-provided engine root ---
if ($EngineRoot -ne "") {
    $candidate = Join-Path $EngineRoot "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
    if (Test-Path $candidate) {
        $UEPython = $candidate
        Write-Host "  [user param] $UEPython" -ForegroundColor DarkGray
    }
}

# --- Priority 2: Read .uproject EngineAssociation → Windows Registry ---
if (-not $UEPython) {
    $uprojectFiles = Get-ChildItem -Path $ProjectRoot -Filter "*.uproject" -ErrorAction SilentlyContinue
    foreach ($upf in $uprojectFiles) {
        try {
            $upContent = Get-Content $upf.FullName -Raw | ConvertFrom-Json
            $engineVer = $upContent.EngineAssociation
            if ($engineVer) {
                Write-Host "  Found EngineAssociation: $engineVer" -ForegroundColor DarkGray

                # Try HKLM installed engine (e.g. "5.7" → HKLM\SOFTWARE\EpicGames\Unreal Engine\5.7)
                $regPath = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$engineVer"
                try {
                    $regEntry = Get-ItemProperty $regPath -ErrorAction Stop
                    if ($regEntry.InstalledDirectory) {
                        $candidate = Join-Path $regEntry.InstalledDirectory "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
                        if (Test-Path $candidate) {
                            $UEPython = $candidate
                            Write-Host "  [registry HKLM] $UEPython" -ForegroundColor DarkGray
                        }
                    }
                } catch {}

                # Try HKCU custom builds (source builds register GUIDs here)
                if (-not $UEPython) {
                    try {
                        $builds = Get-ItemProperty "HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds" -ErrorAction Stop
                        $builds.PSObject.Properties | Where-Object { $_.Name -notmatch '^PS' } | ForEach-Object {
                            if (-not $UEPython) {
                                $buildPath = $_.Value
                                # If EngineAssociation is a GUID, the property name is the GUID
                                # If it's a version like "5.7", check if the path contains it
                                if ($_.Name -eq $engineVer -or $buildPath -match [regex]::Escape($engineVer)) {
                                    $candidate = Join-Path $buildPath "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
                                    if (Test-Path $candidate) {
                                        $UEPython = $candidate
                                        Write-Host "  [registry HKCU] $UEPython" -ForegroundColor DarkGray
                                    }
                                }
                            }
                        }
                    } catch {}
                }
            }
        } catch {}
        if ($UEPython) { break }
    }
}

# --- Priority 3: Parse .code-workspace file for engine folder ---
if (-not $UEPython) {
    $workspaceFiles = Get-ChildItem -Path $ProjectRoot -Filter "*.code-workspace" -ErrorAction SilentlyContinue
    foreach ($wsFile in $workspaceFiles) {
        try {
            $wsContent = Get-Content $wsFile.FullName -Raw | ConvertFrom-Json
            foreach ($folder in $wsContent.folders) {
                $folderPath = $folder.path
                # Look for folders referencing UE engine (contain "UE_" or "Engine")
                if ($folderPath -match "UE_\d|Unreal.?Engine|EpicGame") {
                    $absPath = if ([System.IO.Path]::IsPathRooted($folderPath)) { $folderPath } else { Join-Path $ProjectRoot $folderPath }
                    $candidate = Join-Path $absPath "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
                    if (Test-Path $candidate) {
                        $UEPython = $candidate
                        Write-Host "  [.code-workspace] $UEPython" -ForegroundColor DarkGray
                        break
                    }
                }
            }
        } catch {}
        if ($UEPython) { break }
    }
}

# --- Priority 4: UE_ENGINE_DIR environment variable ---
if (-not $UEPython -and $env:UE_ENGINE_DIR) {
    $candidate = Join-Path $env:UE_ENGINE_DIR "Binaries\ThirdParty\Python3\Win64\python.exe"
    if (Test-Path $candidate) {
        $UEPython = $candidate
        Write-Host "  [env UE_ENGINE_DIR] $UEPython" -ForegroundColor DarkGray
    }
}

# --- Priority 5: Scan common installation directories ---
if (-not $UEPython) {
    # Build list of candidate drives
    $drives = @("C:", "D:", "E:", "F:")
    $patterns = @(
        "{0}\EpicGame\UE_*",
        "{0}\Program Files\Epic Games\UE_*",
        "{0}\UnrealEngine\UE_*"
    )
    foreach ($drive in $drives) {
        if (-not (Test-Path "$drive\")) { continue }
        foreach ($pattern in $patterns) {
            $globPath = $pattern -f $drive
            $found = Get-ChildItem -Path $globPath -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending
            foreach ($dir in $found) {
                $candidate = Join-Path $dir.FullName "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
                if (Test-Path $candidate) {
                    $UEPython = $candidate
                    Write-Host "  [disk scan] $UEPython" -ForegroundColor DarkGray
                    break
                }
            }
            if ($UEPython) { break }
        }
        if ($UEPython) { break }
    }
}

if (-not $UEPython) {
    Write-Host ""
    Write-Host "  Could not auto-detect UE Engine Python." -ForegroundColor Red
    Write-Host "  Re-run with: .\setup_mcp.ps1 -EngineRoot 'E:\EpicGame\UE_5.7'" -ForegroundColor Yellow
    Write-Host ""
    exit 1
}

Write-Host "  Found: $UEPython" -ForegroundColor Green
$version = & $UEPython --version 2>&1
Write-Host "  Version: $version" -ForegroundColor Green
Write-Host ""

# --- Step 2: Create virtual environment ---
Write-Host "[2/4] Creating virtual environment..." -ForegroundColor Yellow

if (Test-Path $VenvDir) {
    Write-Host "  Removing existing venv..."
    Remove-Item -Recurse -Force $VenvDir
}

& $UEPython -m venv $VenvDir
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ERROR: Failed to create virtual environment." -ForegroundColor Red
    exit 1
}
Write-Host "  Created: $VenvDir" -ForegroundColor Green
Write-Host ""

# --- Step 3: Install dependencies ---
Write-Host "[3/4] Installing MCP package..." -ForegroundColor Yellow

$pipExe = Join-Path $VenvDir "Scripts\pip.exe"
$reqFile = Join-Path $PythonDir "requirements.txt"
$vendorDir = Join-Path $PythonDir "vendor"
$installed = $false

# 优先在线安装（带重试）
& $pipExe install -r $reqFile --quiet --retries 3 --timeout 30 2>$null
if ($LASTEXITCODE -eq 0) {
    $installed = $true
    Write-Host "  Dependencies installed (online)." -ForegroundColor Green
}

# 在线失败 → 离线回退
if (-not $installed -and (Test-Path $vendorDir)) {
    Write-Host "  Online install failed, trying offline vendor wheels..." -ForegroundColor Yellow
    & $pipExe install -r $reqFile --no-index --find-links $vendorDir --quiet
    if ($LASTEXITCODE -eq 0) {
        $installed = $true
        Write-Host "  Dependencies installed (offline vendor)." -ForegroundColor Green
    }
}

if (-not $installed) {
    Write-Host "  ERROR: Failed to install dependencies (online & offline)." -ForegroundColor Red
    Write-Host "  Run scripts\Download-Wheels.ps1 to prepare offline packages." -ForegroundColor Yellow
    exit 1
}
Write-Host ""

# --- Step 4: Generate .vscode/mcp.json ---
Write-Host "[4/4] Generating .vscode/mcp.json..." -ForegroundColor Yellow

$venvPython = (Join-Path $VenvDir "Scripts\python.exe").Replace('\', '/')
$pythonPath = $PythonDir.Replace('\', '/')

# Write JSON directly to avoid PowerShell's ConvertTo-Json alignment quirks
$mcpJson = @"
{
  "servers": {
    "ue-editor-mcp": {
      "command": "$venvPython",
      "args": ["-m", "ue_editor_mcp.server_unified"],
      "env": {
        "PYTHONPATH": "$pythonPath"
      }
        },
        "ue-editor-mcp-logs": {
            "command": "$venvPython",
            "args": ["-m", "ue_editor_mcp.server_unreal_logs"],
            "env": {
                "PYTHONPATH": "$pythonPath"
            }
    }
  }
}
"@

$vscodePath = Join-Path $ProjectRoot ".vscode"
if (-not (Test-Path $vscodePath)) {
    New-Item -ItemType Directory -Path $vscodePath -Force | Out-Null
}

$mcpJsonPath = Join-Path $vscodePath "mcp.json"
[System.IO.File]::WriteAllText($mcpJsonPath, $mcpJson, [System.Text.UTF8Encoding]::new($false))

Write-Host "  Generated: $mcpJsonPath" -ForegroundColor Green
Write-Host ""

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " Setup Complete!" -ForegroundColor Cyan
Write-Host " No external Python installation needed." -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""
Write-Host " Next steps:" -ForegroundColor Yellow
Write-Host "   1. Open your UE project in the Editor"
Write-Host "   2. Open VS Code - both ue-editor-mcp and ue-editor-mcp-logs servers will auto-start"
Write-Host "   3. Use Copilot Chat to control Blueprints"
Write-Host ""
