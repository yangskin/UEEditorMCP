"""
UE Blueprint Node Creation MCP Server — add nodes to graphs (21 tools).

Covers: event nodes, custom events, enhanced input, dispatchers,
        function nodes, variable get/set, references, flow control, spawn.
"""

from .server_factory import create_mcp_server
from .tools import nodes_create

server, main = create_mcp_server("ue-bp-nodes-mcp", [nodes_create])

if __name__ == "__main__":
    main()
