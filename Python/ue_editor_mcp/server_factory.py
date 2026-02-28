"""
MCP Server Factory — shared boilerplate for all UE MCP servers.

Each concrete server file calls `create_mcp_server(name, modules)` and
gets back a fully-wired (server, main) pair.  This eliminates the ~80
lines of duplicated scaffolding that previously lived in every server_*.py.
"""

import asyncio
import json
import logging
from typing import Any

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent

from .connection import get_connection

logger = logging.getLogger(__name__)


# ── Meta tool definitions (shared by every server) ──────────────────────────

_PING_TOOL = Tool(
    name="ping",
    description="Test connection to Unreal Engine",
    inputSchema={"type": "object", "properties": {}},
)

_GET_CONTEXT_TOOL = Tool(
    name="get_context",
    description="Get current editor context (active blueprint, graph, etc.)",
    inputSchema={"type": "object", "properties": {}},
)


def create_mcp_server(
    server_name: str,
    tool_modules: list,
) -> tuple[Server, "Callable[[], None]"]:
    """Create an MCP server exposing tools from *tool_modules*.

    Parameters
    ----------
    server_name : str
        Human-readable server id (e.g. ``"ue-editor-mcp"``).
    tool_modules : list
        Each element must expose ``get_tools()``, ``TOOL_HANDLERS``,
        and ``handle_tool(name, arguments)``.

    Returns
    -------
    (server, main)
        *server* – the ``mcp.server.Server`` instance.
        *main*   – a zero-arg function suitable for ``if __name__ …``.
    """
    server = Server(server_name)

    # ── helpers ──────────────────────────────────────────────────────────

    def _all_tools() -> list[Tool]:
        tools: list[Tool] = []
        for mod in tool_modules:
            tools.extend(mod.get_tools())
        return tools

    def _send_command(command_type: str, params: dict | None = None) -> list[TextContent]:
        conn = get_connection()
        if not conn.is_connected:
            conn.connect()
        result = conn.send_command(command_type, params)
        return [TextContent(type="text", text=json.dumps(result.to_dict(), indent=2))]

    # ── MCP handlers ────────────────────────────────────────────────────

    @server.list_tools()
    async def list_tools() -> list[Tool]:
        tools: list[Tool] = [_PING_TOOL, _GET_CONTEXT_TOOL]
        tools.extend(_all_tools())
        return tools

    @server.call_tool()
    async def call_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
        # --- meta tools ---
        if name == "ping":
            conn = get_connection()
            if not conn.is_connected:
                conn.connect()
            ok = conn.ping()
            return [TextContent(
                type="text",
                text=f'{{"success": {str(ok).lower()}, "pong": {str(ok).lower()}}}',
            )]

        if name == "get_context":
            return _send_command("get_context")

        # --- domain tools (first matching module wins) ---
        for mod in tool_modules:
            if name in mod.TOOL_HANDLERS:
                return await mod.handle_tool(name, arguments)

        return [TextContent(
            type="text",
            text=f'{{"success": false, "error": "Unknown tool: {name}"}}',
        )]

    # ── lifecycle ───────────────────────────────────────────────────────

    async def _run():
        logger.info("Starting %s …", server_name)
        conn = get_connection()
        conn.connect()
        async with stdio_server() as (read_stream, write_stream):
            await server.run(
                read_stream,
                write_stream,
                server.create_initialization_options(),
            )

    def main():
        logging.basicConfig(
            level=logging.INFO,
            format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
        )
        try:
            asyncio.run(_run())
        except KeyboardInterrupt:
            logger.info("%s stopped by user", server_name)
        finally:
            conn = get_connection()
            conn.disconnect()

    return server, main
