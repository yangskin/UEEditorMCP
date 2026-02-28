"""
UE UMG Widget MCP Server — UMG widget blueprint tools (19 tools).

Covers: widget CRUD, text/button/image, property bindings,
        widget tree, reparenting, event binding.
"""

from .server_factory import create_mcp_server
from .tools import umg

server, main = create_mcp_server("ue-umg-mcp", [umg])

if __name__ == "__main__":
    main()
