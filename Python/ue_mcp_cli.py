#!/usr/bin/env python3
"""
UE MCP Bridge CLI — 直接通过 TCP 调用 MCPBridge 命令，绕过 VS Code 工具槽位限制。

用法:
    python ue_mcp_cli.py <command> [json_params]

示例:
    python ue_mcp_cli.py ping
    python ue_mcp_cli.py save_all
    python ue_mcp_cli.py set_node_pin_default '{"blueprint_name":"BP_X","node_id":"GUID","pin_name":"InString","default_value":"Hello"}'
    python ue_mcp_cli.py set_variable_metadata '{"blueprint_name":"BP_X","variable_name":"Health","category":"Stats","tooltip":"HP"}'
    python ue_mcp_cli.py set_object_property '{"blueprint_name":"BP_X","owner_class":"PlayerController","property_name":"bShowMouseCursor"}'
"""

import json
import socket
import sys
from typing import Optional


HOST = "127.0.0.1"
PORT = 55558
TIMEOUT = 30.0


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    """Receive exactly n bytes from socket."""
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Socket closed by remote")
        buf.extend(chunk)
    return bytes(buf)


def send_command(command_type: str, params: Optional[dict] = None) -> dict:
    """Send a single command to UE MCPBridge and return the response."""
    command = {"type": command_type}
    if params:
        command["params"] = params

    payload = json.dumps(command).encode("utf-8")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(TIMEOUT)
        sock.connect((HOST, PORT))

        # Send: 4-byte big-endian length prefix + JSON payload
        sock.sendall(len(payload).to_bytes(4, byteorder="big"))
        sock.sendall(payload)

        # Receive: 4-byte length prefix
        length_bytes = _recv_exact(sock, 4)
        length = int.from_bytes(length_bytes, byteorder="big")

        # Receive: message body
        body = _recv_exact(sock, length)
        return json.loads(body.decode("utf-8"))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    command_type = sys.argv[1]
    params = None

    if len(sys.argv) >= 3:
        # Join all remaining args (handles PowerShell splitting)
        raw = " ".join(sys.argv[2:])
        try:
            params = json.loads(raw)
        except json.JSONDecodeError:
            # Fallback: try reading from stdin if arg parse fails
            pass

    # If no params from args and stdin has data, read from stdin
    if params is None and not sys.stdin.isatty():
        try:
            raw_stdin = sys.stdin.read().strip()
            if raw_stdin:
                params = json.loads(raw_stdin)
        except json.JSONDecodeError as e:
            print(f"ERROR: Invalid JSON params: {e}", file=sys.stderr)
            sys.exit(1)

    try:
        result = send_command(command_type, params)
        print(json.dumps(result, indent=2, ensure_ascii=False))
    except ConnectionRefusedError:
        print("ERROR: Cannot connect to UE MCPBridge (port 55558). Is Unreal Editor running?", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
