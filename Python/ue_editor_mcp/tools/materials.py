"""
Material tools - Material creation, expressions, and post-process.
"""

import json
from typing import Any
from mcp.types import Tool, TextContent

from ..connection import get_connection, CommandResult


def _send_command(command_type: str, params: dict | None = None) -> list[TextContent]:
    """Helper to send command and format response."""
    conn = get_connection()
    if not conn.is_connected:
        conn.connect()
    result = conn.send_command(command_type, params)
    return [TextContent(type="text", text=json.dumps(result.to_dict(), indent=2))]


def get_tools() -> list[Tool]:
    """Get all material tools."""
    return [
        Tool(
            name="create_material",
            description="Create a new Material asset.",
            inputSchema={
                "type": "object",
                "properties": {
                    "material_name": {"type": "string", "description": "Name of the Material"},
                    "path": {"type": "string", "description": "Content path (default: /Game/Materials)"},
                    "domain": {
                        "type": "string",
                        "description": "Material domain (Surface, PostProcess, DeferredDecal, LightFunction, UI)"
                    },
                    "blend_mode": {
                        "type": "string",
                        "description": "Blend mode (Opaque, Masked, Translucent, Additive, Modulate)"
                    }
                },
                "required": ["material_name"]
            }
        ),
        Tool(
            name="add_material_expression",
            description="Add an expression node to a Material's graph.",
            inputSchema={
                "type": "object",
                "properties": {
                    "material_name": {"type": "string", "description": "Name of the target Material"},
                    "expression_class": {
                        "type": "string",
                        "description": "Type of expression (SceneTexture, Time, Noise, Add, Multiply, Lerp, Constant, ScalarParameter, VectorParameter, Custom, etc.)"
                    },
                    "node_name": {"type": "string", "description": "Unique name for this node (for later connection/reference)"},
                    "position": {
                        "type": "array",
                        "items": {"type": "number"},
                        "description": "[X, Y] editor position"
                    },
                    "properties": {
                        "type": "object",
                        "description": "Property name/value pairs to set on the expression"
                    }
                },
                "required": ["material_name", "expression_class", "node_name"]
            }
        ),
        Tool(
            name="connect_material_expressions",
            description="Connect output of one expression to input of another.",
            inputSchema={
                "type": "object",
                "properties": {
                    "material_name": {"type": "string", "description": "Name of the Material"},
                    "source_node": {"type": "string", "description": "Name of the source expression"},
                    "source_output_index": {"type": "integer", "description": "Output pin index (default: 0)"},
                    "target_node": {"type": "string", "description": "Name of the target expression"},
                    "target_input": {"type": "string", "description": "Input pin name (A, B, Alpha, Input, etc.)"}
                },
                "required": ["material_name", "source_node", "target_node", "target_input"]
            }
        ),
        Tool(
            name="connect_to_material_output",
            description="Connect an expression to a material's main output (BaseColor, EmissiveColor, etc.).",
            inputSchema={
                "type": "object",
                "properties": {
                    "material_name": {"type": "string", "description": "Name of the Material"},
                    "source_node": {"type": "string", "description": "Name of the source expression"},
                    "source_output_index": {"type": "integer", "description": "Output pin index (default: 0)"},
                    "material_property": {
                        "type": "string",
                        "description": "Material property (BaseColor, EmissiveColor, Metallic, Roughness, Normal, Opacity, etc.)"
                    }
                },
                "required": ["material_name", "source_node", "material_property"]
            }
        ),
        Tool(
            name="set_material_expression_property",
            description="Set a property on an existing material expression.",
            inputSchema={
                "type": "object",
                "properties": {
                    "material_name": {"type": "string", "description": "Name of the Material"},
                    "node_name": {"type": "string", "description": "Name of the expression node"},
                    "property_name": {"type": "string", "description": "Property to set"},
                    "property_value": {"type": "string", "description": "Value to set (as string)"}
                },
                "required": ["material_name", "node_name", "property_name", "property_value"]
            }
        ),
        Tool(
            name="compile_material",
            description="Compile a material and check for errors.",
            inputSchema={
                "type": "object",
                "properties": {
                    "material_name": {"type": "string", "description": "Name of the Material to compile"}
                },
                "required": ["material_name"]
            }
        ),
        Tool(
            name="create_material_instance",
            description="Create a Material Instance from a parent material with parameter overrides.",
            inputSchema={
                "type": "object",
                "properties": {
                    "instance_name": {"type": "string", "description": "Name for the material instance"},
                    "parent_material": {"type": "string", "description": "Name of the parent material"},
                    "path": {"type": "string", "description": "Content path (default: /Game/Materials)"},
                    "scalar_parameters": {
                        "type": "object",
                        "description": "Scalar parameter overrides {name: value}"
                    },
                    "vector_parameters": {
                        "type": "object",
                        "description": "Vector parameter overrides {name: [R, G, B, A]}"
                    }
                },
                "required": ["instance_name", "parent_material"]
            }
        ),
        Tool(
            name="set_material_property",
            description="Set a property on a Material asset (ShadingModel, TwoSided, BlendMode, etc.).",
            inputSchema={
                "type": "object",
                "properties": {
                    "material_name": {"type": "string", "description": "Name of the Material"},
                    "property_name": {
                        "type": "string",
                        "description": "Property to set (ShadingModel, TwoSided, BlendMode, DitheredLODTransition, AllowNegativeEmissiveColor, OpacityMaskClipValue)"
                    },
                    "property_value": {
                        "type": "string",
                        "description": "Value to set (e.g., 'Unlit', 'true', 'Masked', '0.5')"
                    }
                },
                "required": ["material_name", "property_name", "property_value"]
            }
        ),
        Tool(
            name="create_post_process_volume",
            description="Create a Post Process Volume actor in the level.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Name for the volume actor"},
                    "location": {
                        "type": "array",
                        "items": {"type": "number"},
                        "description": "[X, Y, Z] world location"
                    },
                    "infinite_extent": {"type": "boolean", "description": "Whether to apply everywhere (default: true)"},
                    "priority": {"type": "number", "description": "Priority value (default: 0.0)"},
                    "post_process_materials": {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": "Array of material names to add"
                    }
                },
                "required": ["name"]
            }
        ),
    ]


# Map tool names to command types
TOOL_HANDLERS = {
    "create_material": "create_material",
    "add_material_expression": "add_material_expression",
    "connect_material_expressions": "connect_material_expressions",
    "connect_to_material_output": "connect_to_material_output",
    "set_material_expression_property": "set_material_expression_property",
    "set_material_property": "set_material_property",
    "compile_material": "compile_material",
    "create_material_instance": "create_material_instance",
    "create_post_process_volume": "create_post_process_volume",
}


async def handle_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
    """Handle a material tool call."""
    command_type = TOOL_HANDLERS.get(name)
    if not command_type:
        return [TextContent(type="text", text=f'{{"success": false, "error": "Unknown tool: {name}"}}')]

    return _send_command(command_type, arguments if arguments else None)
