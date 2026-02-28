"""
Blueprint struct and switch node tools - Make/Break struct, Switch on String/Int.
"""

import json
from typing import Any
from mcp.types import Tool, TextContent

from ..connection import get_connection


def _send_command(command_type: str, params: dict | None = None) -> list[TextContent]:
    """Helper to send command and format response."""
    conn = get_connection()
    if not conn.is_connected:
        conn.connect()
    result = conn.send_command(command_type, params)
    return [TextContent(type="text", text=json.dumps(result.to_dict(), indent=2))]


def get_tools() -> list[Tool]:
    """Get all struct and switch node tools."""
    return [
        # =====================================================================
        # Struct Nodes
        # =====================================================================
        Tool(
            name="add_make_struct_node",
            description="Add a Make Struct node (e.g., Make IntPoint, Make Vector, Make LinearColor).",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "struct_type": {
                        "type": "string",
                        "description": "Struct type: IntPoint, Vector, Vector2D, Rotator, Transform, LinearColor, Color"
                    },
                    "pin_defaults": {
                        "type": "object",
                        "description": "Optional default values for pins (e.g., {'X': '1920', 'Y': '1080'})"
                    },
                    "node_position": {
                        "type": "array",
                        "items": {"type": "number"},
                        "description": "[X, Y] position in graph"
                    },
                    "graph_name": {"type": "string", "description": "Optional function graph name (defaults to event graph)"}
                },
                "required": ["blueprint_name", "struct_type"]
            }
        ),
        Tool(
            name="add_break_struct_node",
            description="Add a Break Struct node (e.g., Break IntPoint, Break Vector).",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "struct_type": {
                        "type": "string",
                        "description": "Struct type: IntPoint, Vector, Vector2D, Rotator, Transform, LinearColor, Color"
                    },
                    "node_position": {
                        "type": "array",
                        "items": {"type": "number"},
                        "description": "[X, Y] position in graph"
                    },
                    "graph_name": {"type": "string", "description": "Optional function graph name (defaults to event graph)"}
                },
                "required": ["blueprint_name", "struct_type"]
            }
        ),

        # =====================================================================
        # Switch Nodes
        # =====================================================================
        Tool(
            name="add_switch_on_string_node",
            description="Add a Switch on String node with specified case options.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "cases": {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": "List of string cases (e.g., ['Low', 'Medium', 'High'])"
                    },
                    "node_position": {
                        "type": "array",
                        "items": {"type": "number"},
                        "description": "[X, Y] position in graph"
                    },
                    "graph_name": {"type": "string", "description": "Optional function graph name (defaults to event graph)"}
                },
                "required": ["blueprint_name"]
            }
        ),
        Tool(
            name="add_switch_on_int_node",
            description="Add a Switch on Int node with specified case options.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "start_index": {
                        "type": "integer",
                        "description": "Starting index for cases (default: 0)"
                    },
                    "cases": {
                        "type": "array",
                        "items": {"type": "integer"},
                        "description": "Number of cases to create"
                    },
                    "node_position": {
                        "type": "array",
                        "items": {"type": "number"},
                        "description": "[X, Y] position in graph"
                    },
                    "graph_name": {"type": "string", "description": "Optional function graph name (defaults to event graph)"}
                },
                "required": ["blueprint_name"]
            }
        ),
    ]


TOOL_HANDLERS = {
    "add_make_struct_node": "add_make_struct_node",
    "add_break_struct_node": "add_break_struct_node",
    "add_switch_on_string_node": "add_switch_on_string_node",
    "add_switch_on_int_node": "add_switch_on_int_node",
}


async def handle_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
    """Handle a struct/switch node tool call."""
    command_type = TOOL_HANDLERS.get(name)
    if not command_type:
        return [TextContent(type="text", text=f'{{"success": false, "error": "Unknown tool: {name}"}}')]

    return _send_command(command_type, arguments if arguments else None)
