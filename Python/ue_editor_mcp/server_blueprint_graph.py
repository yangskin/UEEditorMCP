"""
UE Blueprint Graph Operations MCP Server — graph manipulation (18 tools).

Covers: connect/find/delete nodes, pins, move, reroute, comments,
        local variables, pin defaults, variable/function management.
"""

from .server_factory import create_mcp_server
from .tools import nodes_graph

server, main = create_mcp_server("ue-bp-graph-mcp", [nodes_graph])

if __name__ == "__main__":
    main()
