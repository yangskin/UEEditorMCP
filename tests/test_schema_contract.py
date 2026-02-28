#!/usr/bin/env python3
"""
Schema Contract Test — Python tool schema ↔ C++ Action 一致性验证

检查项:
  1. Python tool name 必须在 C++ RegisterActions 中有对应注册（1:1 或 handler 映射）
  2. C++ 注册的 action 在 Python tool 中应有暴露（排除已知的内部 action）
  3. Python required 参数必须在 C++ Validate 中有对应的 GetRequiredString / TryGetStringField 检查
  4. Python schema properties 中的参数名不应与 C++ 端出现拼写漂移

运行:
  cd Plugins/UEEditorMCP
  python -m tests.test_schema_contract          # 作为模块
  python tests/test_schema_contract.py          # 直接执行
"""

from __future__ import annotations

import importlib
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
PLUGIN_ROOT = Path(__file__).resolve().parent.parent
PYTHON_PKG = PLUGIN_ROOT / "Python" / "ue_editor_mcp"
CPP_ACTIONS_DIR = PLUGIN_ROOT / "Source" / "UEEditorMCP" / "Private" / "Actions"
CPP_BRIDGE = PLUGIN_ROOT / "Source" / "UEEditorMCP" / "Private" / "MCPBridge.cpp"

# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass
class PythonTool:
    name: str
    properties: Dict[str, dict] = field(default_factory=dict)
    required: List[str] = field(default_factory=list)
    handler_command: Optional[str] = None   # TOOL_HANDLERS 映射的 C++ command


@dataclass
class CppAction:
    name: str                              # RegisterActions 中的 key
    class_name: str = ""                   # C++ 类名
    required_params: Set[str] = field(default_factory=set)   # Validate 中 GetRequiredString 的参数
    checked_params: Set[str] = field(default_factory=set)    # Validate 中 TryGetStringField 的参数
    source_file: str = ""


@dataclass
class Issue:
    severity: str   # ERROR | WARN | INFO
    category: str
    message: str


# ---------------------------------------------------------------------------
# 1. Extract Python tool schemas
# ---------------------------------------------------------------------------

def _extract_tools_from_module(module_path: Path) -> Tuple[List[PythonTool], Dict[str, str]]:
    """从单个 Python tool 模块中提取 Tool 列表和 TOOL_HANDLERS 映射。"""
    source = module_path.read_text(encoding="utf-8")
    tools: List[PythonTool] = []
    handlers: Dict[str, str] = {}

    # 提取 TOOL_HANDLERS 字典
    handler_match = re.search(
        r'TOOL_HANDLERS\s*=\s*\{([^}]+)\}', source, re.DOTALL
    )
    if handler_match:
        for m in re.finditer(r'"([^"]+)"\s*:\s*"([^"]+)"', handler_match.group(1)):
            handlers[m.group(1)] = m.group(2)

    # 提取 Tool(...) 定义
    # 使用分步解析：找 name= 和 inputSchema=
    tool_pattern = re.compile(
        r'Tool\s*\(\s*name\s*=\s*"([^"]+)".*?inputSchema\s*=\s*(\{)',
        re.DOTALL
    )

    for m in tool_pattern.finditer(source):
        tool_name = m.group(1)
        # 从 inputSchema 的 '{' 开始匹配完整 JSON 字典
        schema_start = m.start(2)
        schema_str = _extract_balanced_braces(source, schema_start)
        if schema_str:
            try:
                schema = json.loads(schema_str)
                properties = schema.get("properties", {})
                required = schema.get("required", [])
                handler_cmd = handlers.get(tool_name, tool_name)
                tools.append(PythonTool(
                    name=tool_name,
                    properties=properties,
                    required=required,
                    handler_command=handler_cmd,
                ))
            except json.JSONDecodeError:
                tools.append(PythonTool(name=tool_name, handler_command=handlers.get(tool_name, tool_name)))
        else:
            tools.append(PythonTool(name=tool_name, handler_command=handlers.get(tool_name, tool_name)))

    return tools, handlers


def _extract_balanced_braces(text: str, start: int) -> Optional[str]:
    """提取从 start 位置起的完整花括号对。"""
    if start >= len(text) or text[start] != '{':
        return None
    depth = 0
    in_string = False
    escape_next = False
    for i in range(start, len(text)):
        ch = text[i]
        if escape_next:
            escape_next = False
            continue
        if ch == '\\':
            escape_next = True
            continue
        if ch == '"' and not escape_next:
            in_string = not in_string
            continue
        if in_string:
            continue
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return None


def collect_python_tools() -> Dict[str, PythonTool]:
    """收集所有 Python tool 模块的 tool 定义。"""
    tools_dir = PYTHON_PKG / "tools"
    all_tools: Dict[str, PythonTool] = {}

    for py_file in sorted(tools_dir.glob("*.py")):
        if py_file.name.startswith("__"):
            continue
        module_tools, _ = _extract_tools_from_module(py_file)
        for t in module_tools:
            if t.name in all_tools:
                # 重复定义 (如 spawn_blueprint_actor 在两个模块)
                pass
            all_tools[t.name] = t

    return all_tools


# ---------------------------------------------------------------------------
# 2. Extract C++ action registrations & Validate params
# ---------------------------------------------------------------------------

def collect_cpp_registrations() -> Dict[str, str]:
    """从 MCPBridge.cpp 提取 action_name → class_name 映射。"""
    registrations: Dict[str, str] = {}
    if not CPP_BRIDGE.exists():
        return registrations

    source = CPP_BRIDGE.read_text(encoding="utf-8")
    # ActionHandlers.Add(TEXT("action_name"), MakeShared<FClassName>());
    pattern = re.compile(
        r'ActionHandlers\.Add\s*\(\s*TEXT\s*\(\s*"([^"]+)"\s*\)\s*,\s*MakeShared\s*<\s*(\w+)\s*>\s*\(\s*\)\s*\)',
    )
    for m in pattern.finditer(source):
        registrations[m.group(1)] = m.group(2)

    return registrations


def _extract_validate_params(source: str, class_name: str) -> Tuple[Set[str], Set[str]]:
    """提取指定 C++ 类 Validate 方法中检查的参数名。

    返回 (required_params, checked_params):
      - required_params: GetRequiredString 中的参数名（硬性必需）
      - checked_params: TryGetStringField 中的参数名（可能是软性必需 + 别名兼容）
    """
    required: Set[str] = set()
    checked: Set[str] = set()

    # 查找 ClassName::Validate 的方法体
    validate_pattern = re.compile(
        rf'{class_name}\s*::\s*Validate\s*\(.*?\)\s*\{{',
        re.DOTALL
    )
    match = validate_pattern.search(source)
    if not match:
        return required, checked

    # 提取方法体
    body_start = match.end() - 1  # 指向 '{'
    body = _extract_balanced_braces(source, body_start)
    if not body:
        return required, checked

    # GetRequiredString(Params, TEXT("param_name"), ...)
    for m in re.finditer(r'GetRequiredString\s*\([^,]*,\s*TEXT\s*\(\s*"([^"]+)"\s*\)', body):
        required.add(m.group(1))

    # TryGetStringField(TEXT("param_name"), ...)
    for m in re.finditer(r'TryGetStringField\s*\(\s*TEXT\s*\(\s*"([^"]+)"\s*\)', body):
        checked.add(m.group(1))

    return required, checked


def collect_cpp_actions() -> Dict[str, CppAction]:
    """从所有 C++ Action 源文件中提取 Validate 参数检查。"""
    registrations = collect_cpp_registrations()
    actions: Dict[str, CppAction] = {}

    # 首先为所有注册项创建 CppAction
    for action_name, class_name in registrations.items():
        actions[action_name] = CppAction(
            name=action_name,
            class_name=class_name,
        )

    # 然后从每个 cpp 文件中提取 Validate 参数
    for cpp_file in sorted(CPP_ACTIONS_DIR.glob("*.cpp")):
        source = cpp_file.read_text(encoding="utf-8")
        for action in actions.values():
            if action.class_name and action.class_name in source:
                req, chk = _extract_validate_params(source, action.class_name)
                action.required_params = req
                action.checked_params = chk
                action.source_file = cpp_file.name

    return actions


# ---------------------------------------------------------------------------
# 3. Contract checks
# ---------------------------------------------------------------------------

# UMG 工具在 Python 端合并为 add_widget_component，实际对应多个 C++ action
# 这些 C++ action 不需要 Python 端有同名 tool
UMG_INTERNAL_ACTIONS = {
    "add_text_block_to_widget",
    "add_button_to_widget",
    "add_image_to_widget",
    "add_border_to_widget",
    "add_overlay_to_widget",
    "add_horizontal_box_to_widget",
    "add_vertical_box_to_widget",
    "add_slider_to_widget",
    "add_progress_bar_to_widget",
    "add_size_box_to_widget",
    "add_scale_box_to_widget",
    "add_canvas_panel_to_widget",
    "add_combo_box_to_widget",
    "add_check_box_to_widget",
    "add_spin_box_to_widget",
    "add_editable_text_box_to_widget",
    "add_generic_widget_to_widget",
}

# 已知的 Python 聚合 tool（映射到多个 C++ action）
PYTHON_AGGREGATE_TOOLS = {
    "add_widget_component",   # UMG 聚合 tool
}

# 已知的 C++ 内部 action（不需要 Python 暴露）
KNOWN_CPP_ONLY_ACTIONS: Set[str] = {
    "add_blueprint_get_subsystem_node",  # 内部使用
}

# blueprint_name 不算显式 required（通过基类 ValidateBlueprint 可选验证）
BASE_CLASS_PARAMS = {"blueprint_name"}


def run_checks(
    py_tools: Dict[str, PythonTool],
    cpp_actions: Dict[str, CppAction],
) -> List[Issue]:
    """执行全部契约检查，返回问题列表。"""
    issues: List[Issue] = []

    cpp_action_names = set(cpp_actions.keys())
    py_tool_names = set(py_tools.keys())

    # --- Check 1: Python tool → C++ action 映射 ---
    for tool_name, tool in py_tools.items():
        if tool_name in PYTHON_AGGREGATE_TOOLS:
            continue
        cmd = tool.handler_command or tool_name
        if cmd not in cpp_action_names:
            issues.append(Issue(
                "ERROR",
                "MISSING_CPP_ACTION",
                f"Python tool '{tool_name}' maps to C++ action '{cmd}' which is not registered in MCPBridge",
            ))

    # --- Check 2: C++ action → Python tool 覆盖 ---
    for action_name in cpp_action_names:
        if action_name in UMG_INTERNAL_ACTIONS:
            continue
        if action_name in KNOWN_CPP_ONLY_ACTIONS:
            continue
        # 检查是否有任何 Python tool 通过 handler_command 映射到此 action
        mapped = any(
            (t.handler_command or t.name) == action_name
            for t in py_tools.values()
        )
        if not mapped:
            issues.append(Issue(
                "WARN",
                "CPP_NOT_EXPOSED",
                f"C++ action '{action_name}' ({cpp_actions[action_name].class_name}) "
                f"is registered but has no Python tool exposing it",
            ))

    # --- Check 3: Required 参数一致性 ---
    for tool_name, tool in py_tools.items():
        if tool_name in PYTHON_AGGREGATE_TOOLS:
            continue
        cmd = tool.handler_command or tool_name
        action = cpp_actions.get(cmd)
        if not action:
            continue

        py_required = set(tool.required) - BASE_CLASS_PARAMS
        cpp_required = action.required_params - BASE_CLASS_PARAMS

        # Python 标记 required 但 C++ 未用 GetRequiredString 验证
        for param in py_required:
            if param not in cpp_required and param not in action.checked_params:
                issues.append(Issue(
                    "WARN",
                    "REQUIRED_MISMATCH",
                    f"'{tool_name}': Python marks '{param}' as required, "
                    f"but C++ Validate ({action.class_name}) does not check it with GetRequiredString "
                    f"(C++ required: {sorted(cpp_required)}, checked: {sorted(action.checked_params)})",
                ))

        # C++ GetRequiredString 但 Python 未标记 required
        for param in cpp_required:
            if param not in set(tool.required):
                issues.append(Issue(
                    "ERROR",
                    "REQUIRED_MISMATCH",
                    f"'{tool_name}': C++ GetRequiredString checks '{param}' "
                    f"but Python schema does not list it in required "
                    f"(Python required: {sorted(tool.required)})",
                ))

    # --- Check 4: Property 名称存在性 ---
    for tool_name, tool in py_tools.items():
        if tool_name in PYTHON_AGGREGATE_TOOLS:
            continue
        cmd = tool.handler_command or tool_name
        action = cpp_actions.get(cmd)
        if not action:
            continue

        # 只检查 required 参数是否在 properties 中定义
        py_props = set(tool.properties.keys())
        for param in tool.required:
            if param not in py_props:
                issues.append(Issue(
                    "ERROR",
                    "SCHEMA_INTEGRITY",
                    f"'{tool_name}': required param '{param}' not declared in properties",
                ))

    return issues


# ---------------------------------------------------------------------------
# 4. Report
# ---------------------------------------------------------------------------

def print_report(
    py_tools: Dict[str, PythonTool],
    cpp_actions: Dict[str, CppAction],
    issues: List[Issue],
) -> int:
    """打印报告并返回 exit code (0=pass, 1=has errors)。"""
    print("=" * 72)
    print("  Schema Contract Test Report")
    print("=" * 72)
    print()

    # 概览
    print(f"  Python tools:      {len(py_tools)}")
    print(f"  C++ actions:       {len(cpp_actions)}")
    print(f"  UMG internal:      {len(UMG_INTERNAL_ACTIONS)} (excluded from coverage check)")
    print()

    errors = [i for i in issues if i.severity == "ERROR"]
    warns = [i for i in issues if i.severity == "WARN"]
    infos = [i for i in issues if i.severity == "INFO"]

    if errors:
        print(f"  ❌ ERRORS: {len(errors)}")
    if warns:
        print(f"  ⚠️  WARNINGS: {len(warns)}")
    if infos:
        print(f"  ℹ️  INFO: {len(infos)}")
    if not issues:
        print("  ✅ All checks passed!")

    print()

    # 按严重度和类别分组输出
    for severity, label in [("ERROR", "ERRORS"), ("WARN", "WARNINGS"), ("INFO", "INFO")]:
        group = [i for i in issues if i.severity == severity]
        if not group:
            continue
        print(f"--- {label} ---")
        by_cat: Dict[str, List[Issue]] = {}
        for i in group:
            by_cat.setdefault(i.category, []).append(i)
        for cat, items in sorted(by_cat.items()):
            print(f"\n  [{cat}]")
            for item in items:
                print(f"    • {item.message}")
        print()

    # 工具覆盖率
    total_cpp = len(cpp_actions) - len(UMG_INTERNAL_ACTIONS) - len(KNOWN_CPP_ONLY_ACTIONS)
    covered = sum(
        1 for a in cpp_actions
        if a not in UMG_INTERNAL_ACTIONS
        and a not in KNOWN_CPP_ONLY_ACTIONS
        and any((t.handler_command or t.name) == a for t in py_tools.values())
    )
    if total_cpp > 0:
        pct = covered / total_cpp * 100
        print(f"  Coverage: {covered}/{total_cpp} C++ actions exposed via Python ({pct:.1f}%)")
    print()
    print("=" * 72)

    return 1 if errors else 0


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    # Validate paths
    if not PYTHON_PKG.exists():
        print(f"ERROR: Python package not found at {PYTHON_PKG}")
        return 2
    if not CPP_ACTIONS_DIR.exists():
        print(f"ERROR: C++ actions directory not found at {CPP_ACTIONS_DIR}")
        return 2
    if not CPP_BRIDGE.exists():
        print(f"ERROR: MCPBridge.cpp not found at {CPP_BRIDGE}")
        return 2

    # Collect data
    print("Collecting Python tool schemas...")
    py_tools = collect_python_tools()
    print(f"  Found {len(py_tools)} tools")

    print("Collecting C++ action registrations...")
    cpp_actions = collect_cpp_actions()
    print(f"  Found {len(cpp_actions)} actions")
    print()

    # Run checks
    issues = run_checks(py_tools, cpp_actions)

    # Report
    return print_report(py_tools, cpp_actions, issues)


if __name__ == "__main__":
    sys.exit(main())
