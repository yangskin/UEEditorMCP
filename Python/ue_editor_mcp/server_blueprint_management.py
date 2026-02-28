"""
UE Blueprint Management MCP Server — blueprint CRUD, components, structs (18 tools).

Covers: create/compile blueprint, components, properties, physics,
        parent class, interfaces, Make/Break struct, Switch nodes.
"""

from .server_factory import create_mcp_server
from .tools import blueprint, structs

server, main = create_mcp_server("ue-blueprint-management-mcp", [blueprint, structs])

if __name__ == "__main__":
    main()
