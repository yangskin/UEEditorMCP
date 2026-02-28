"""
Blueprint node *creation* tools — events, dispatchers, functions,
variables, references, flow control, and spawning.

This is a thin view over ``nodes.py`` that exposes only the 20 tools
responsible for **adding new nodes** to a Blueprint graph.
It exists so that the MCP server ``server_blueprint_nodes.py`` stays small
enough for VS Code Copilot Chat to surface every tool directly
(without hiding them behind ``activate_*`` groups).
"""

from typing import Any
from mcp.types import TextContent

from . import nodes as _nodes

# ── Tool name whitelist (19 tools) ──────────────────────────────────────────

TOOL_NAMES: frozenset[str] = frozenset({
    # Events (4)
    "add_blueprint_event_node",
    "add_blueprint_custom_event",
    "add_blueprint_input_action_node",
    "add_enhanced_input_action_node",
    # Event Dispatchers (4)
    "add_event_dispatcher",
    "call_event_dispatcher",
    "bind_event_dispatcher",
    "create_event_delegate",
    # Functions (3)
    "add_blueprint_function_node",
    "create_blueprint_function",
    "call_blueprint_function",
    # Variables — creation only (3)
    "add_blueprint_variable",
    "add_blueprint_variable_get",
    "add_blueprint_variable_set",
    # References (3)
    "add_blueprint_self_reference",
    "add_blueprint_get_self_component_reference",
    "add_blueprint_cast_node",
    # Flow Control (2)
    "add_blueprint_branch_node",
    "add_macro_instance_node",
    # Spawning (1)
    "add_spawn_actor_from_class_node",
})

# ── Public API (same interface as every tools/*.py module) ──────────────────

def get_tools():
    """Return the subset of node tools that *create* nodes."""
    return [t for t in _nodes.get_tools() if t.name in TOOL_NAMES]


TOOL_HANDLERS: dict[str, str] = {
    k: v for k, v in _nodes.TOOL_HANDLERS.items() if k in TOOL_NAMES
}


async def handle_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
    """Delegate to the canonical handler in nodes.py."""
    return await _nodes.handle_tool(name, arguments)
