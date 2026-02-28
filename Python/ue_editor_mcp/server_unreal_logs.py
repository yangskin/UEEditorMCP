from __future__ import annotations

import asyncio
import hashlib
import json
import logging
import os
import re
import socket
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent, ImageContent

from .connection import get_connection, CommandResult

logger = logging.getLogger(__name__)


def _to_serializable(obj: Any) -> Any:
    """Recursively convert CommandResult objects to dicts so json.dumps never fails."""
    if isinstance(obj, CommandResult):
        return _to_serializable(obj.to_dict())
    if isinstance(obj, dict):
        return {k: _to_serializable(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [_to_serializable(item) for item in obj]
    return obj

_DEFAULT_TAIL_LINES = 200
_DEFAULT_MAX_BYTES = 65536
_MIN_TAIL_LINES = 20
_MAX_TAIL_LINES = 2000
_MIN_MAX_BYTES = 8192
_MAX_MAX_BYTES = 1048576
_LIVE_RECENT_SECONDS = 2.0


@dataclass
class LogFilter:
    min_verbosity: str | None = None
    categories: list[str] | None = None
    contains: str | None = None


def _utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def _clamp_int(value: Any, default_value: int, min_value: int, max_value: int) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        parsed = default_value
    return max(min_value, min(max_value, parsed))


def _is_ue_reachable(host: str = "127.0.0.1", port: int = 55558, timeout: float = 0.35) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def _parse_filter(raw_filter: Any) -> LogFilter:
    if not isinstance(raw_filter, dict):
        return LogFilter()

    min_verbosity = raw_filter.get("minVerbosity")

    categories: list[str] = []
    category_value = raw_filter.get("category")
    if isinstance(category_value, str) and category_value:
        categories.append(category_value)
    elif isinstance(category_value, list):
        categories.extend([str(item) for item in category_value if isinstance(item, str) and item])

    contains = raw_filter.get("contains")
    if not isinstance(contains, str):
        contains = None

    return LogFilter(
        min_verbosity=min_verbosity if isinstance(min_verbosity, str) else None,
        categories=categories or None,
        contains=contains,
    )


def _infer_workspace_root() -> Path | None:
    env_keys = [
        "UE_MCP_WORKSPACE_ROOT",
        "WORKSPACE_ROOT",
        "VSCODE_WORKSPACE_FOLDER",
        "PROJECT_ROOT",
    ]
    for key in env_keys:
        value = os.environ.get(key)
        if value:
            p = Path(value)
            if p.exists():
                return p

    # Plugin-relative fallback: .../Plugins/UEEditorMCP/Python/ue_editor_mcp
    # -> project root at parents[4].
    try:
        candidate = Path(__file__).resolve().parents[4]
        if (candidate / "Saved").exists() and (candidate / "Content").exists():
            return candidate
    except Exception:
        return None

    return None


def _resolve_workspace_root(explicit_root: Any) -> Path | None:
    if isinstance(explicit_root, str) and explicit_root.strip():
        path = Path(explicit_root.strip())
        if path.exists():
            return path
        return None
    return _infer_workspace_root()


def _decode_log_bytes(data: bytes) -> str:
    if not data:
        return ""

    for encoding in ("utf-8", "utf-8-sig", "utf-16", "utf-16-le", "utf-16-be", "cp1252"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            continue

    return data.decode("utf-8", errors="replace")


def _parse_cursor(cursor: str | None) -> tuple[str | None, int | None, int | None, int | None]:
    if not cursor or not cursor.startswith("file:"):
        return None, None, None, None

    parts = cursor.split(":")
    if len(parts) != 5:
        return None, None, None, None

    _, digest, offset, mtime_ns, file_size = parts
    try:
        return digest, int(offset), int(mtime_ns), int(file_size)
    except ValueError:
        return None, None, None, None


def _build_file_cursor(file_path: Path, offset: int) -> str:
    stat = file_path.stat()
    digest = hashlib.sha1(str(file_path).lower().encode("utf-8")).hexdigest()[:12]
    return f"file:{digest}:{offset}:{stat.st_mtime_ns}:{stat.st_size}"


def _pick_latest_log_file(log_dir: Path, project_name: str | None) -> Path | None:
    if not log_dir.exists() or not log_dir.is_dir():
        return None

    candidates: list[Path] = []
    for ext in ("*.log", "*.txt"):
        candidates.extend(log_dir.glob(ext))

    if not candidates:
        return None

    filtered = []
    for path in candidates:
        lower_name = path.name.lower()
        if "backup" in lower_name or "-backup-" in lower_name:
            continue
        filtered.append(path)

    if not filtered:
        return None

    if project_name:
        pn = project_name.lower()
        preferred = [p for p in filtered if pn in p.name.lower()]
        if preferred:
            preferred.sort(key=lambda p: p.stat().st_mtime_ns, reverse=True)
            return preferred[0]

    filtered.sort(key=lambda p: p.stat().st_mtime_ns, reverse=True)
    return filtered[0]


def _severity_rank(level: str) -> int:
    mapping = {
        "verbose": 0,
        "log": 1,
        "warning": 2,
        "error": 3,
        "fatal": 4,
    }
    return mapping.get(level.lower(), 1)


def _extract_saved_line_meta(line: str) -> tuple[str, str]:
    # Typical UE line: [..]LogCategory: Warning: message
    category_match = re.search(r"\](Log[^:\]]+)\s*:", line)
    category = category_match.group(1) if category_match else "SavedLog"

    if "fatal" in line.lower():
        verbosity = "Fatal"
    elif "error" in line.lower():
        verbosity = "Error"
    elif "warning" in line.lower():
        verbosity = "Warning"
    elif "verbose" in line.lower():
        verbosity = "Verbose"
    else:
        verbosity = "Log"

    return verbosity, category


def _apply_saved_filters(lines: list[str], log_filter: LogFilter) -> list[str]:
    min_rank = _severity_rank(log_filter.min_verbosity) if log_filter.min_verbosity else None
    categories_lower = [c.lower() for c in (log_filter.categories or []) if c]
    contains_lower = log_filter.contains.lower() if log_filter.contains else None

    result: list[str] = []
    for line in lines:
        verbosity, category = _extract_saved_line_meta(line)

        if min_rank is not None and _severity_rank(verbosity) < min_rank:
            continue

        if categories_lower:
            category_l = category.lower()
            if not any(cat in category_l for cat in categories_lower):
                continue

        if contains_lower and contains_lower not in line.lower():
            continue

        result.append(f"[{_utc_now_iso()}][{verbosity}][{category}] {line}")

    return result


def _tail_latest_text(file_path: Path, tail_lines: int, max_bytes: int) -> tuple[str, int, int, bool]:
    with file_path.open("rb") as handle:
        handle.seek(0, os.SEEK_END)
        file_size = handle.tell()

        block_size = 65536
        pos = file_size
        chunks: list[bytes] = []
        total_scanned = 0
        newline_count = 0
        hard_scan_limit = max(max_bytes * 8, 512 * 1024)

        while pos > 0:
            read_size = min(block_size, pos)
            pos -= read_size
            handle.seek(pos)
            chunk = handle.read(read_size)
            chunks.append(chunk)
            total_scanned += len(chunk)
            newline_count += chunk.count(b"\n")

            if newline_count >= tail_lines and total_scanned >= max_bytes:
                break
            if total_scanned >= hard_scan_limit:
                break

    merged = b"".join(reversed(chunks))
    text = _decode_log_bytes(merged)
    lines = text.splitlines()
    selected_lines = lines[-tail_lines:] if lines else []

    content = "\n".join(selected_lines)
    encoded = content.encode("utf-8")
    truncated = False
    if len(encoded) > max_bytes:
        encoded = encoded[-max_bytes:]
        content = encoded.decode("utf-8", errors="ignore")
        truncated = True

    return content, len(content.encode("utf-8")), len(content.splitlines()) if content else 0, truncated


def _read_saved_logs(
    workspace_root: Path,
    project_name: str | None,
    tail_lines: int,
    max_bytes: int,
    cursor: str | None,
    log_filter: LogFilter,
    source_label: str,
    notes: list[str],
) -> dict[str, Any]:
    project_log_dir = workspace_root / "Saved" / "Logs"
    if not project_log_dir.exists():
        return {
            "success": False,
            "error": f"Logs directory not found: {project_log_dir}",
            "notes": notes + ["需要提供正确的 workspaceRoot，且目录中包含 Saved/Logs"],
        }

    log_file = _pick_latest_log_file(project_log_dir, project_name)
    if not log_file:
        return {
            "success": False,
            "error": f"No log files found in {project_log_dir}",
            "notes": notes,
        }

    stat = log_file.stat()
    digest, offset, cursor_mtime_ns, cursor_size = _parse_cursor(cursor)
    current_digest = hashlib.sha1(str(log_file).lower().encode("utf-8")).hexdigest()[:12]

    incremental = False
    data_bytes: bytes = b""
    truncated = False

    if digest and offset is not None and cursor_mtime_ns is not None and cursor_size is not None:
        if digest != current_digest:
            notes.append("cursor 指向旧日志文件，已重置到最新文件")
        elif stat.st_size < offset:
            notes.append("检测到日志轮转或截断（size < offset），已重置 cursor")
        elif stat.st_mtime_ns != cursor_mtime_ns and stat.st_size == cursor_size:
            notes.append("日志元信息变化，按当前位置继续读取")
        else:
            incremental = True
            with log_file.open("rb") as handle:
                handle.seek(offset)
                data_bytes = handle.read(max_bytes)
                if offset + len(data_bytes) < stat.st_size:
                    truncated = True

    if not incremental:
        tail_text, _, _, tail_truncated = _tail_latest_text(log_file, tail_lines, max_bytes)
        data_bytes = tail_text.encode("utf-8")
        truncated = truncated or tail_truncated

    text = _decode_log_bytes(data_bytes)
    raw_lines = text.splitlines()
    filtered_lines = _apply_saved_filters(raw_lines, log_filter)

    if len(filtered_lines) > tail_lines:
        filtered_lines = filtered_lines[-tail_lines:]
        truncated = True

    content = "\n".join(filtered_lines)
    content_bytes = content.encode("utf-8")
    if len(content_bytes) > max_bytes:
        content_bytes = content_bytes[-max_bytes:]
        content = content_bytes.decode("utf-8", errors="ignore")
        truncated = True

    next_offset = stat.st_size if not incremental else min(stat.st_size, (offset or 0) + len(data_bytes))
    next_cursor = _build_file_cursor(log_file, next_offset)

    return {
        "success": True,
        "source": source_label,
        "isLive": False,
        "filePath": str(log_file),
        "projectLogDir": str(project_log_dir),
        "cursor": next_cursor,
        "truncated": truncated,
        "linesReturned": len(content.splitlines()) if content else 0,
        "bytesReturned": len(content.encode("utf-8")),
        "lastUpdateUtc": datetime.fromtimestamp(stat.st_mtime, tz=timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z"),
        "content": content,
        "notes": notes,
    }


def _fetch_live_logs(
    tail_lines: int,
    max_bytes: int,
    cursor: str | None,
    include_meta: bool,
    log_filter: LogFilter,
    require_recent: bool,
) -> dict[str, Any]:
    conn = get_connection()

    params: dict[str, Any] = {
        "tail_lines": tail_lines,
        "max_bytes": max_bytes,
        "include_meta": include_meta,
        "require_recent": require_recent,
        "recent_window_seconds": _LIVE_RECENT_SECONDS,
    }
    if cursor:
        params["cursor"] = cursor
    if log_filter.min_verbosity:
        params["filter_min_verbosity"] = log_filter.min_verbosity
    if log_filter.contains:
        params["filter_contains"] = log_filter.contains
    if log_filter.categories:
        if len(log_filter.categories) == 1:
            params["filter_category"] = log_filter.categories[0]
        else:
            params["filter_categories"] = log_filter.categories

    result = conn.send_command("get_unreal_logs", params).to_dict()
    if not result.get("success"):
        return {"success": False, "error": result.get("error", "live log fetch failed")}

    result["source"] = "live"
    result["isLive"] = True
    if "notes" not in result:
        result["notes"] = []
    return result


def _handle_unreal_logs_get(args: dict[str, Any]) -> dict[str, Any]:
    mode = str(args.get("mode", "auto")).lower()
    if mode not in {"auto", "live", "saved"}:
        mode = "auto"

    tail_lines = _clamp_int(args.get("tailLines"), _DEFAULT_TAIL_LINES, _MIN_TAIL_LINES, _MAX_TAIL_LINES)
    max_bytes = _clamp_int(args.get("maxBytes"), _DEFAULT_MAX_BYTES, _MIN_MAX_BYTES, _MAX_MAX_BYTES)
    cursor = args.get("cursor") if isinstance(args.get("cursor"), str) else None
    include_meta = bool(args.get("includeMeta", True))
    project_name = args.get("projectName") if isinstance(args.get("projectName"), str) else None
    workspace_root = _resolve_workspace_root(args.get("workspaceRoot"))
    log_filter = _parse_filter(args.get("filter"))

    notes: list[str] = []

    ue_reachable = _is_ue_reachable()

    if mode == "live":
        if not ue_reachable:
            return {
                "success": False,
                "error": "UE 不可达，mode=live 无法读取实时缓冲区",
                "notes": ["可改用 mode=saved 并提供 workspaceRoot 读取 Saved/Logs"],
            }

        live_result = _fetch_live_logs(
            tail_lines=tail_lines,
            max_bytes=max_bytes,
            cursor=cursor,
            include_meta=include_meta,
            log_filter=log_filter,
            require_recent=False,
        )
        return live_result

    if mode == "auto" and ue_reachable:
        live_result = _fetch_live_logs(
            tail_lines=tail_lines,
            max_bytes=max_bytes,
            cursor=cursor,
            include_meta=include_meta,
            log_filter=log_filter,
            require_recent=True,
        )
        if live_result.get("success"):
            notes.extend(live_result.get("notes", []))
            live_result["notes"] = notes
            return live_result
        notes.append(f"live 不可用，回退到 Saved/Logs: {live_result.get('error', 'unknown')}" )

    if workspace_root is None:
        return {
            "success": False,
            "error": "无法推断 workspaceRoot，且未显式提供。",
            "notes": notes + ["请传入 workspaceRoot，例如 C:/work/SVN/p110_2"],
        }

    source_label = "saved" if ue_reachable else "offline_saved"
    return _read_saved_logs(
        workspace_root=workspace_root,
        project_name=project_name,
        tail_lines=tail_lines,
        max_bytes=max_bytes,
        cursor=cursor,
        log_filter=log_filter,
        source_label=source_label,
        notes=notes,
    )


def _handle_asset_diff_get(args: dict[str, Any]) -> dict[str, Any]:
    """Get structured diff of an asset against its latest source-control revision."""
    if not _is_ue_reachable():
        return {
            "success": False,
            "error": "UE 不可达，无法执行 Diff Against Depot",
            "notes": ["请先启动 Unreal Editor 并确保 UEEditorMCP 连接可用，且 Source Control 已连接"],
        }

    asset_path = args.get("assetPath")
    if not isinstance(asset_path, str) or not asset_path.strip():
        return {
            "success": False,
            "error": "缺少必填参数 assetPath",
            "notes": ["请提供完整资产路径，如 /Game/P110_2/Blueprints/Character/Player/BP_SideScrollingCharacter"],
        }

    params: dict[str, Any] = {"asset_path": asset_path.strip()}

    revision = args.get("revision")
    if isinstance(revision, int) and revision > 0:
        params["revision"] = revision

    result = get_connection().send_command("diff_against_depot", params).to_dict()
    if result.get("success") and "notes" not in result:
        result["notes"] = []
    return result


def _handle_asset_thumbnail_get(args: dict[str, Any]) -> dict[str, Any]:
    if not _is_ue_reachable():
        return {
            "success": False,
            "error": "UE 不可达，无法读取编辑器资产缩略图",
            "notes": ["请先启动 Unreal Editor 并确保 UEEditorMCP 连接可用"],
        }

    params: dict[str, Any] = {}

    asset_path = args.get("assetPath")
    if isinstance(asset_path, str) and asset_path.strip():
        params["asset_path"] = asset_path.strip()

    asset_paths = args.get("assetPaths")
    if isinstance(asset_paths, list):
        normalized_paths = [item.strip() for item in asset_paths if isinstance(item, str) and item.strip()]
        if normalized_paths:
            params["asset_paths"] = normalized_paths

    asset_ids = args.get("assetIds")
    if isinstance(asset_ids, list):
        normalized_ids = [item.strip() for item in asset_ids if isinstance(item, str) and item.strip()]
        if normalized_ids:
            params["asset_ids"] = normalized_ids

    ids = args.get("ids")
    if isinstance(ids, list):
        normalized_raw_ids = [item.strip() for item in ids if isinstance(item, str) and item.strip()]
        if normalized_raw_ids:
            params["ids"] = normalized_raw_ids

    size = args.get("size")
    if isinstance(size, int):
        params["size"] = size

    result = get_connection().send_command("get_selected_asset_thumbnail", params or None).to_dict()
    if result.get("success") and "notes" not in result:
        result["notes"] = []
    return result


TOOLS = [
    Tool(
        name="unreal.asset_diff.get",
        description="Diff an asset against its latest source-control (SVN/Perforce/Git) revision. Returns structured diff data: for Blueprints it reports per-graph node-level changes (added/removed/modified/moved nodes, pin changes); for generic assets it reports property-level differences. Requires UE Editor running with Source Control connected.",
        inputSchema={
            "type": "object",
            "properties": {
                "assetPath": {
                    "type": "string",
                    "description": "Full asset path, e.g. /Game/P110_2/Blueprints/BP_Foo",
                },
                "revision": {
                    "type": "integer",
                    "description": "Optional specific revision number to diff against (default: latest)",
                },
            },
            "required": ["assetPath"],
        },
    ),
    Tool(
        name="unreal.logs.get",
        description="Get Unreal logs for AI context. Supports live ring buffer, Saved/Logs tail, and offline fallback when UE is not running.",
        inputSchema={
            "type": "object",
            "properties": {
                "mode": {
                    "type": "string",
                    "enum": ["auto", "live", "saved"],
                    "description": "log source mode (default: auto)",
                },
                "tailLines": {
                    "type": "integer",
                    "description": "lines to return (default 200, range 20..2000)",
                },
                "maxBytes": {
                    "type": "integer",
                    "description": "max UTF-8 bytes returned (default 65536, range 8192..1048576)",
                },
                "cursor": {
                    "type": "string",
                    "description": "cursor for incremental reads (live:<seq> or file:<hash>:<offset>:<mtime_ns>:<size>)",
                },
                "workspaceRoot": {
                    "type": "string",
                    "description": "project root for offline Saved/Logs lookup",
                },
                "projectName": {
                    "type": "string",
                    "description": "optional project name hint for log file selection",
                },
                "includeMeta": {
                    "type": "boolean",
                    "description": "include metadata fields (default true)",
                },
                "filter": {
                    "type": "object",
                    "properties": {
                        "minVerbosity": {
                            "type": "string",
                            "enum": ["Verbose", "Log", "Warning", "Error", "Fatal"],
                        },
                        "category": {
                            "oneOf": [
                                {"type": "string"},
                                {"type": "array", "items": {"type": "string"}},
                            ],
                        },
                        "contains": {"type": "string"},
                    },
                },
            },
        },
    ),
    Tool(
        name="unreal.asset_thumbnail.get",
        description="Get selected/specified asset thumbnails from Unreal Editor and return PNG base64 images (single or batch).",
        inputSchema={
            "type": "object",
            "properties": {
                "assetPath": {
                    "type": "string",
                    "description": "Optional single full asset path.",
                },
                "assetPaths": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Optional multiple full asset paths.",
                },
                "assetIds": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Optional multiple asset ids/paths (alias of assetPaths).",
                },
                "ids": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Optional generic id list (alias of assetPaths).",
                },
                "size": {
                    "type": "integer",
                    "description": "Thumbnail target size in pixels (default 256, clamp 1..256).",
                },
            },
        },
    ),
]


server = Server("ue-editor-mcp-logs")


@server.list_tools()
async def list_tools() -> list[Tool]:
    return TOOLS


@server.call_tool()
async def call_tool(name: str, arguments: dict[str, Any]) -> list[TextContent | ImageContent]:
    if name not in {"unreal.logs.get", "unreal.asset_thumbnail.get", "unreal.asset_diff.get"}:
        text = json.dumps({"success": False, "error": f"Unknown tool: {name}"}, ensure_ascii=False)
        return [TextContent(type="text", text=text)]

    try:
        if name == "unreal.logs.get":
            result = _handle_unreal_logs_get(arguments or {})
        elif name == "unreal.asset_diff.get":
            result = _handle_asset_diff_get(arguments or {})
        else:
            result = _handle_asset_thumbnail_get(arguments or {})
    except Exception as exc:
        logger.exception("%s failed", name)
        result = {"success": False, "error": str(exc), "notes": ["unexpected exception"]}

    contents: list[TextContent | ImageContent] = []
    image_blocks_count = 0

    # If this is a thumbnail request and it succeeded, extract the images
    if name == "unreal.asset_thumbnail.get" and result.get("success"):
        seen_images: set[tuple[str, str]] = set()

        def _extract_image(item: dict):
            if "image_base64" in item:
                base64_data = item.get("image_base64")
                mime_type = item.get("mime_type", "image/png")
                if base64_data:
                    if base64_data.startswith("data:"):
                        base64_data = base64_data.split(",", 1)[1]
                    image_key = (mime_type, base64_data)
                    if image_key not in seen_images:
                        contents.append(ImageContent(type="image", data=base64_data, mimeType=mime_type))
                        seen_images.add(image_key)
                        nonlocal image_blocks_count
                        image_blocks_count += 1
                item["image_base64"] = "<base64_data_extracted_to_image_content>"

        has_thumbnail_list = isinstance(result.get("thumbnails"), list) and len(result.get("thumbnails", [])) > 0
        if has_thumbnail_list:
            for thumb in result.get("thumbnails", []):
                if isinstance(thumb, dict):
                    _extract_image(thumb)
        else:
            _extract_image(result)

        # Always redact top-level singleton payload too, even when list-form thumbnails are used.
        if "image_base64" in result:
            result["image_base64"] = "<base64_data_extracted_to_image_content>"

        result["image_blocks_count"] = image_blocks_count

    safe_result = _to_serializable(result)
    text = json.dumps(safe_result, indent=2, ensure_ascii=False)
    contents.append(TextContent(type="text", text=text))
    
    return contents


async def _run() -> None:
    logger.info("Starting ue-editor-mcp-logs server")
    async with stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream,
            write_stream,
            server.create_initialization_options(),
        )


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    )
    try:
        asyncio.run(_run())
    except KeyboardInterrupt:
        logger.info("ue-editor-mcp-logs stopped by user")


if __name__ == "__main__":
    main()
