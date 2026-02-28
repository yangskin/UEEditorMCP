"""
UE Editor MCP Server - Main entry point.

Registers all tools with the MCP SDK and handles the server lifecycle.
"""

import asyncio
import json
import logging
from typing import Any

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent

from .connection import get_connection, PersistentUnrealConnection, CommandResult

# Import tool modules (UMG is in server_umg.py, nodes in server_blueprint.py)
from .tools import blueprint, editor, project, materials

logger = logging.getLogger(__name__)

# Create MCP server instance
server = Server("ue-editor-v1-mcp")


def _all_tools() -> list[Tool]:
    """Build full tool catalog for this server."""
    tools: list[Tool] = []
    tools.extend(editor.get_tools())
    tools.extend(blueprint.get_tools())
    tools.extend(project.get_tools())
    tools.extend(materials.get_tools())
    return tools


def _discover_tools(arguments: dict[str, Any] | None) -> list[TextContent]:
    """Return searchable tool manifest to improve discoverability under tool-count limits."""
    args = arguments or {}
    query = str(args.get("query", "")).strip().lower()
    limit = int(args.get("limit", 50))
    include_schema = bool(args.get("include_schema", False))

    catalog = []
    for tool in _all_tools():
        searchable = f"{tool.name} {tool.description}".lower()
        if query and query not in searchable:
            continue

        item = {
            "name": tool.name,
            "description": tool.description,
        }
        if include_schema:
            item["inputSchema"] = tool.inputSchema
        catalog.append(item)

    if limit > 0:
        catalog = catalog[:limit]

    return [TextContent(type="text", text=json.dumps({
        "success": True,
        "count": len(catalog),
        "tools": catalog,
    }, indent=2))]


def _result_to_text(result: CommandResult) -> list[TextContent]:
    """Convert CommandResult to MCP text content."""
    import json
    return [TextContent(type="text", text=json.dumps(result.to_dict(), indent=2))]


def _send_command(command_type: str, params: dict | None = None) -> list[TextContent]:
    """Helper to send command and format response."""
    conn = get_connection()
    if not conn.is_connected:
        conn.connect()
    result = conn.send_command(command_type, params)
    return _result_to_text(result)


# =============================================================================
# Connection Tools
# =============================================================================

@server.list_tools()
async def list_tools() -> list[Tool]:
    """List all available tools."""
    tools = []

    # Connection tools
    tools.append(Tool(
        name="ping",
        description="Test connection to Unreal Engine",
        inputSchema={"type": "object", "properties": {}}
    ))

    tools.append(Tool(
        name="get_context",
        description="Get current editor context (active blueprint, graph, etc.)",
        inputSchema={"type": "object", "properties": {}}
    ))

    tools.append(Tool(
        name="discover_tools",
        description="Search and list full tool catalog (including tools not shown in truncated tool UIs).",
        inputSchema={
            "type": "object",
            "properties": {
                "query": {"type": "string", "description": "Optional keyword filter on name/description"},
                "limit": {"type": "number", "description": "Max items to return (default 50)"},
                "include_schema": {"type": "boolean", "description": "Include each tool's input schema"}
            }
        }
    ))

    tools.append(Tool(
        name="invoke_tool",
        description="Invoke any server tool by name, useful when UI tool lists are truncated.",
        inputSchema={
            "type": "object",
            "properties": {
                "tool_name": {"type": "string", "description": "Exact tool name to invoke"},
                "arguments": {"type": "object", "description": "Arguments object for the target tool"}
            },
            "required": ["tool_name"]
        }
    ))

    # Add tools from all modules (nodes moved to server_blueprint.py)
    tools.extend(_all_tools())

    return tools


@server.call_tool()
async def call_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
    """Route tool calls to appropriate handlers."""

    # Connection tools
    if name == "ping":
        conn = get_connection()
        if not conn.is_connected:
            conn.connect()
        success = conn.ping()
        return [TextContent(type="text", text=f'{{"success": {str(success).lower()}, "pong": {str(success).lower()}}}')]

    if name == "get_context":
        return _send_command("get_context")

    if name == "discover_tools":
        return _discover_tools(arguments)

    if name == "invoke_tool":
        target_name = (arguments or {}).get("tool_name")
        target_args = (arguments or {}).get("arguments", {})
        if not target_name:
            return [TextContent(type="text", text='{"success": false, "error": "tool_name is required"}')]
        if target_name in {"invoke_tool", "discover_tools"}:
            return [TextContent(type="text", text='{"success": false, "error": "invoke_tool cannot recursively invoke meta tools"}')]
        return await call_tool(str(target_name), target_args if isinstance(target_args, dict) else {})

    # Route to tool modules
    if name in editor.TOOL_HANDLERS:
        return await editor.handle_tool(name, arguments)

    if name in blueprint.TOOL_HANDLERS:
        return await blueprint.handle_tool(name, arguments)

    if name in project.TOOL_HANDLERS:
        return await project.handle_tool(name, arguments)

    if name in materials.TOOL_HANDLERS:
        return await materials.handle_tool(name, arguments)

    return [TextContent(type="text", text=f'{{"success": false, "error": "Unknown tool: {name}"}}')]


async def run_server():
    """Run the MCP server."""
    logger.info("Starting UE Editor MCP server...")

    # Establish connection to Unreal
    conn = get_connection()
    conn.connect()

    async with stdio_server() as (read_stream, write_stream):
        await server.run(read_stream, write_stream, server.create_initialization_options())


def main():
    """Main entry point."""
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )

    try:
        asyncio.run(run_server())
    except KeyboardInterrupt:
        logger.info("Server stopped by user")
    finally:
        # Clean up connection
        conn = get_connection()
        conn.disconnect()


if __name__ == "__main__":
    main()
