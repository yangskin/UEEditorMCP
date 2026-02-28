"""
Blueprint tools - Blueprint creation, components, compilation, and properties.
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
    """Get all blueprint tools."""
    return [
        # Blueprint creation and management
        Tool(
            name="create_blueprint",
            description="Create a new Blueprint class.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name of the Blueprint"},
                    "parent_class": {"type": "string", "description": "Parent class (Actor, Pawn, GameStateBase, etc.)"},
                    "path": {"type": "string", "description": "Content path (default: /Game/Blueprints)"}
                },
                "required": ["name", "parent_class"]
            }
        ),
        Tool(
            name="compile_blueprint",
            description="Compile a Blueprint. Returns error details if compilation fails.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint to compile"}
                },
                "required": ["blueprint_name"]
            }
        ),
        Tool(
            name="set_blueprint_property",
            description="Set a property on a Blueprint class default object.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "property_name": {"type": "string", "description": "Name of the property"},
                    "property_value": {"type": "string", "description": "Value to set"}
                },
                "required": ["blueprint_name", "property_name", "property_value"]
            }
        ),

        # Components
        Tool(
            name="add_component_to_blueprint",
            description="Add a component to a Blueprint.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "component_type": {"type": "string", "description": "Type of component (StaticMeshComponent, BoxComponent, etc.)"},
                    "component_name": {"type": "string", "description": "Name for the component"},
                    "location": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"},
                    "rotation": {"type": "array", "items": {"type": "number"}, "description": "[pitch, yaw, roll]"},
                    "scale": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"},
                    "component_properties": {"type": "object", "description": "Additional properties to set"}
                },
                "required": ["blueprint_name", "component_type", "component_name"]
            }
        ),
        Tool(
            name="set_static_mesh_properties",
            description="Set static mesh, material, and overlay material on a StaticMeshComponent.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "component_name": {"type": "string", "description": "Name of the component"},
                    "static_mesh": {"type": "string", "description": "Path to static mesh asset"},
                    "material": {"type": "string", "description": "Path to material asset"},
                    "overlay_material": {"type": "string", "description": "Path to overlay material (for outline effects)"}
                },
                "required": ["blueprint_name", "component_name"]
            }
        ),
        Tool(
            name="set_component_property",
            description="Set a property on a component in a Blueprint.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "component_name": {"type": "string", "description": "Name of the component"},
                    "property_name": {"type": "string", "description": "Name of the property"},
                    "property_value": {"type": "string", "description": "Value to set"}
                },
                "required": ["blueprint_name", "component_name", "property_name", "property_value"]
            }
        ),
        Tool(
            name="set_physics_properties",
            description="Set physics properties on a component.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "component_name": {"type": "string", "description": "Name of the component"},
                    "simulate_physics": {"type": "boolean", "description": "Enable physics simulation"},
                    "gravity_enabled": {"type": "boolean", "description": "Enable gravity"},
                    "mass": {"type": "number", "description": "Mass in kg"},
                    "linear_damping": {"type": "number", "description": "Linear damping"},
                    "angular_damping": {"type": "number", "description": "Angular damping"}
                },
                "required": ["blueprint_name", "component_name"]
            }
        ),

        # Spawning
        Tool(
            name="spawn_blueprint_actor",
            description="Spawn an instance of a Blueprint in the level.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint to spawn"},
                    "actor_name": {"type": "string", "description": "Name for the spawned actor"},
                    "location": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"},
                    "rotation": {"type": "array", "items": {"type": "number"}, "description": "[pitch, yaw, roll]"}
                },
                "required": ["blueprint_name", "actor_name"]
            }
        ),

        # Materials
        Tool(
            name="create_colored_material",
            description="Create a simple colored material asset.",
            inputSchema={
                "type": "object",
                "properties": {
                    "material_name": {"type": "string", "description": "Name for the material"},
                    "color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B] values 0.0-1.0"},
                    "path": {"type": "string", "description": "Content path (default: /Game/Materials)"}
                },
                "required": ["material_name"]
            }
        ),

        # =====================================================================
        # Parent Class & Interfaces
        # =====================================================================
        Tool(
            name="set_blueprint_parent_class",
            description="Change the parent class of a Blueprint (reparent).",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "parent_class": {"type": "string", "description": "New parent class name (Actor, Pawn, Character, PlayerController, GameModeBase, etc.) or full path"}
                },
                "required": ["blueprint_name", "parent_class"]
            }
        ),
        Tool(
            name="add_blueprint_interface",
            description="Add an interface implementation to a Blueprint.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "interface_name": {"type": "string", "description": "Interface class name or full path (e.g., /Game/Interfaces/BPI_Interactable)"}
                },
                "required": ["blueprint_name", "interface_name"]
            }
        ),
        Tool(
            name="remove_blueprint_interface",
            description="Remove an implemented interface from a Blueprint.",
            inputSchema={
                "type": "object",
                "properties": {
                    "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                    "interface_name": {"type": "string", "description": "Interface class name or path to remove"}
                },
                "required": ["blueprint_name", "interface_name"]
            }
        ),
    ]


TOOL_HANDLERS = {
    "create_blueprint": "create_blueprint",
    "compile_blueprint": "compile_blueprint",
    "set_blueprint_property": "set_blueprint_property",
    "add_component_to_blueprint": "add_component_to_blueprint",
    "set_static_mesh_properties": "set_static_mesh_properties",
    "set_component_property": "set_component_property",
    "set_physics_properties": "set_physics_properties",
    "spawn_blueprint_actor": "spawn_blueprint_actor",
    "create_colored_material": "create_colored_material",
    "set_blueprint_parent_class": "set_blueprint_parent_class",
    "add_blueprint_interface": "add_blueprint_interface",
    "remove_blueprint_interface": "remove_blueprint_interface",
}


async def handle_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
    """Handle a blueprint tool call."""
    command_type = TOOL_HANDLERS.get(name)
    if not command_type:
        return [TextContent(type="text", text=f'{{"success": false, "error": "Unknown tool: {name}"}}')]

    return _send_command(command_type, arguments if arguments else None)
