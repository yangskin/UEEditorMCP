"""
Blueprint graph *operation* tools — connecting, finding, deleting,
moving nodes, managing variables/functions, and pin utilities.

This is a thin view over ``nodes.py`` that exposes only the 16 tools
responsible for **manipulating an existing graph** (as opposed to
creating new nodes, which live in ``nodes_create.py``).
"""

from typing import Any
from mcp.types import TextContent

from . import nodes as _nodes

# ── Tool name whitelist (16 tools) ──────────────────────────────────────────

TOOL_NAMES: frozenset[str] = frozenset({
    # Graph operations (4)
    "connect_blueprint_nodes",
    "find_blueprint_nodes",
    "delete_blueprint_node",
    "get_node_pins",
    # Graph enhancements — P2 (3)
    "disconnect_blueprint_pin",
    "move_node",
    "add_reroute_node",
    # Comments (1)
    "add_blueprint_comment",
    # Local variables (1)
    "add_function_local_variable",
    # Pin / value defaults (3)
    "set_node_pin_default",
    "set_object_property",
    "set_blueprint_variable_default",
    # Variable & function management — P1 (4)
    "delete_blueprint_variable",
    "rename_blueprint_variable",
    "set_variable_metadata",
    "delete_blueprint_function",
})

# ── Public API (same interface as every tools/*.py module) ──────────────────

def get_tools():
    """Return the subset of node tools that *manipulate* the graph."""
    return [t for t in _nodes.get_tools() if t.name in TOOL_NAMES]


TOOL_HANDLERS: dict[str, str] = {
    k: v for k, v in _nodes.TOOL_HANDLERS.items() if k in TOOL_NAMES
}


async def handle_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
    """Delegate to the canonical handler in nodes.py."""
    return await _nodes.handle_tool(name, arguments)
