"""
UE Editor MCP - Unreal Engine Blueprint manipulation via Model Context Protocol

A robust MCP server for controlling Unreal Engine's Blueprint editor with:
- Persistent TCP connections (no reconnect per command)
- Crash protection via defensive programming + platform-specific handlers
- Stateful editor context for command chaining
- Comprehensive error reporting
"""

__version__ = "0.1.0"
__author__ = "zolnoor"

from .connection import PersistentUnrealConnection
from .server import main
from .server_umg import main as main_umg

__all__ = ["PersistentUnrealConnection", "main", "main_umg", "__version__"]
