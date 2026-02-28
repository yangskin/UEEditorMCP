"""
UE Materials & Project MCP Server — materials, input assets, project utils (15 tools).

Covers: create/compile materials, material instances, material expressions,
        input actions/mapping contexts, create post-process volume.
"""

from .server_factory import create_mcp_server
from .tools import materials, project

server, main = create_mcp_server("ue-materials-mcp", [materials, project])

if __name__ == "__main__":
    main()
