"""
Project tools - Input mappings, Enhanced Input system, and project settings.
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
    """Get all project tools."""
    return [
        # Legacy Input System
        Tool(
            name="create_input_mapping",
            description="Create a legacy input mapping (Action or Axis).",
            inputSchema={
                "type": "object",
                "properties": {
                    "action_name": {"type": "string", "description": "Name of the input action"},
                    "key": {"type": "string", "description": "Key to bind (SpaceBar, W, LeftMouseButton, etc.)"},
                    "input_type": {"type": "string", "description": "Type: Action or Axis"},
                    "scale": {"type": "number", "description": "Scale for Axis mappings (1.0 or -1.0)"}
                },
                "required": ["action_name", "key"]
            }
        ),

        # Enhanced Input System
        Tool(
            name="create_input_action",
            description="Create an Enhanced Input Action asset.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the Input Action (e.g., IA_Move)"},
                    "value_type": {"type": "string", "description": "Value type: Boolean, Axis1D/Float, Axis2D/Vector2D, Axis3D/Vector"},
                    "path": {"type": "string", "description": "Content browser path (default: /Game/Input)"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="create_input_mapping_context",
            description="Create an Enhanced Input Mapping Context asset.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the IMC (e.g., IMC_Default)"},
                    "path": {"type": "string", "description": "Content browser path (default: /Game/Input)"}
                },
                "required": ["name"]
            }
        ),
        Tool(
            name="add_key_mapping_to_context",
            description="Add a key mapping to an Input Mapping Context with optional modifiers.",
            inputSchema={
                "type": "object",
                "properties": {
                    "context_name": {"type": "string", "description": "Name of the IMC asset"},
                    "action_name": {"type": "string", "description": "Name of the Input Action asset"},
                    "key": {"type": "string", "description": "Key to bind (W, A, SpaceBar, etc.)"},
                    "modifiers": {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": "Modifier names: Negate, SwizzleYXZ, SwizzleZYX, SwizzleXZY, SwizzleYZX, SwizzleZXY"
                    },
                    "context_path": {"type": "string", "description": "Path to IMC (default: /Game/Input)"},
                    "action_path": {"type": "string", "description": "Path to IA (default: /Game/Input)"}
                },
                "required": ["context_name", "action_name", "key"]
            }
        ),
    ]


TOOL_HANDLERS = {
    "create_input_mapping": "create_input_mapping",
    "create_input_action": "create_input_action",
    "create_input_mapping_context": "create_input_mapping_context",
    "add_key_mapping_to_context": "add_key_mapping_to_context",
}


async def handle_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
    """Handle a project tool call."""
    command_type = TOOL_HANDLERS.get(name)
    if not command_type:
        return [TextContent(type="text", text=f'{{"success": false, "error": "Unknown tool: {name}"}}')]

    return _send_command(command_type, arguments if arguments else None)
