"""
Tool modules for UE Editor MCP.

Each module provides:
- get_tools(): Returns list of Tool definitions
- TOOL_HANDLERS: Dict mapping tool names to handler functions
- handle_tool(name, args): Async function to execute tool

After the server-split refactor, ``nodes_create`` and ``nodes_graph``
are thin views over ``nodes`` that partition its 35 tools into two
smaller sets so that each MCP server stays under the VS Code Copilot
tool-visibility threshold.
"""

from . import (
    blueprint,
    editor,
    materials,
    nodes,
    nodes_create,
    nodes_graph,
    project,
    structs,
    umg,
)

__all__ = [
    "blueprint",
    "editor",
    "materials",
    "nodes",
    "nodes_create",
    "nodes_graph",
    "project",
    "structs",
    "umg",
]
