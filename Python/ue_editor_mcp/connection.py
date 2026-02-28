"""
Persistent connection to Unreal Engine MCP Bridge.

Unlike the original implementation that reconnected for each command,
this maintains a persistent socket with heartbeat and auto-reconnect.
"""

import json
import socket
import threading
import time
import logging
from typing import Any, Optional
from dataclasses import dataclass, field
from enum import Enum

logger = logging.getLogger(__name__)


class ConnectionState(Enum):
    """Connection lifecycle states."""
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    RECONNECTING = "reconnecting"
    ERROR = "error"


@dataclass
class ConnectionConfig:
    """Configuration for the Unreal connection."""
    host: str = "127.0.0.1"
    port: int = 55558  # New UEEditorMCP plugin port
    timeout: float = 120.0
    heartbeat_interval: float = 5.0
    max_reconnect_attempts: int = 5
    reconnect_base_delay: float = 1.0
    reconnect_max_delay: float = 30.0


@dataclass
class CommandResult:
    """Result of a command execution."""
    success: bool
    data: dict = field(default_factory=dict)
    error: Optional[str] = None
    recoverable: bool = True

    def to_dict(self) -> dict:
        result = {"success": self.success}
        if self.success:
            result.update(self.data)
        else:
            result["error"] = self.error
            result["recoverable"] = self.recoverable
            # Preserve structured data on failure too (e.g. batch partial
            # failures carry results/total/executed/failed fields).
            if self.data:
                result.update(self.data)
        return result


class PersistentUnrealConnection:
    """
    Maintains a persistent connection to the Unreal MCP Bridge.

    Features:
    - Keeps socket alive between commands (no reconnect per command)
    - Heartbeat ping every N seconds to detect stale connections
    - Auto-reconnect with exponential backoff on failure
    - Thread-safe command execution
    - Queues commands during reconnection
    """

    def __init__(self, config: Optional[ConnectionConfig] = None):
        self.config = config or ConnectionConfig()
        self._socket: Optional[socket.socket] = None
        self._state = ConnectionState.DISCONNECTED
        self._lock = threading.RLock()
        self._heartbeat_thread: Optional[threading.Thread] = None
        self._stop_heartbeat = threading.Event()
        self._last_activity = time.time()
        self._reconnect_attempts = 0

    @property
    def state(self) -> ConnectionState:
        """Current connection state."""
        return self._state

    @property
    def is_connected(self) -> bool:
        """Whether actively connected to Unreal."""
        return self._state == ConnectionState.CONNECTED and self._socket is not None

    def connect(self) -> bool:
        """
        Establish connection to Unreal Engine.

        Returns:
            True if connection successful, False otherwise.
        """
        with self._lock:
            if self._state == ConnectionState.CONNECTED:
                return True

            self._state = ConnectionState.CONNECTING

            try:
                self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self._socket.settimeout(self.config.timeout)
                self._socket.connect((self.config.host, self.config.port))

                self._state = ConnectionState.CONNECTED
                self._reconnect_attempts = 0
                self._last_activity = time.time()

                # Start heartbeat thread
                self._start_heartbeat()

                logger.info(f"Connected to Unreal at {self.config.host}:{self.config.port}")
                return True

            except (socket.error, socket.timeout, ConnectionRefusedError) as e:
                self._state = ConnectionState.ERROR
                logger.error(f"Failed to connect to Unreal: {e}")
                self._cleanup_socket()
                return False

    def disconnect(self):
        """Gracefully disconnect from Unreal."""
        with self._lock:
            self._stop_heartbeat.set()
            if self._heartbeat_thread:
                self._heartbeat_thread.join(timeout=2.0)

            # Send close command if connected
            if self._socket and self._state == ConnectionState.CONNECTED:
                try:
                    self._send_raw({"type": "close"})
                except Exception:
                    pass

            self._cleanup_socket()
            self._state = ConnectionState.DISCONNECTED
            logger.info("Disconnected from Unreal")

    def send_command(self, command_type: str, params: Optional[dict] = None) -> CommandResult:
        """
        Send a command to Unreal and wait for response.

        Args:
            command_type: The command type (e.g., "create_blueprint", "ping")
            params: Optional parameters for the command

        Returns:
            CommandResult with success/failure and data/error
        """
        with self._lock:
            # Ensure connected
            if not self.is_connected:
                if not self._try_reconnect():
                    return CommandResult(
                        success=False,
                        error="Not connected to Unreal and reconnect failed",
                        recoverable=True
                    )

            command = {"type": command_type}
            if params:
                command["params"] = params

            try:
                # Log outgoing command (truncate large params)
                params_preview = json.dumps(params)[:200] if params else "none"
                logger.debug(f">>> Sending command '{command_type}' with params: {params_preview}")

                # Send command
                self._send_raw(command)

                # Receive response
                response = self._receive_raw()
                self._last_activity = time.time()

                # Log incoming response (truncate large responses)
                if response:
                    response_preview = json.dumps(response)[:500]
                    logger.warning(f"<<< [{command_type}] Response: {response_preview}")

                if response is None:
                    # Connection died, try reconnect
                    self._state = ConnectionState.ERROR
                    if self._try_reconnect():
                        # Retry the command once
                        return self.send_command(command_type, params)
                    return CommandResult(
                        success=False,
                        error="Connection lost and reconnect failed",
                        recoverable=True
                    )

                # Parse response - handle both formats:
                # Format 1 (EditorAction): {"success": true, ...data...}
                # Format 2 (MCPBridge legacy): {"status": "success", "result": {...}}
                #
                # IMPORTANT: Check "success" field FIRST because some responses
                # have both "success" and "status" where "status" is a data field
                # (e.g., compilation status "UpToDate"), not a success indicator.

                # Check Format 1 first (success bool field)
                if "success" in response:
                    if response.get("success") is True:
                        # Extract all fields except 'success' as data
                        data = {k: v for k, v in response.items() if k != "success"}
                        return CommandResult(
                            success=True,
                            data=data
                        )
                    else:
                        error_msg = response.get("error", "Unknown error (no error message in response)")
                        error_type = response.get("error_type", "unknown")
                        # Include structured error details (e.g., compilation errors array, patch results)
                        error_details = {}
                        for detail_key in ("errors", "warnings", "error_count", "warning_count",
                                           "name", "status", "results", "total", "executed",
                                           "succeeded", "failed", "compiled", "compile_error"):
                            if detail_key in response:
                                error_details[detail_key] = response[detail_key]
                        detail_str = ""
                        if error_details:
                            detail_str = f" | DETAILS: {json.dumps(error_details)}"
                        full_error = f"[{error_type}] {error_msg}{detail_str}"
                        logger.error(f"Command '{command_type}' failed: {full_error}")
                        # Preserve all structured data so callers (e.g. ue_batch)
                        # can still access results/total/executed/failed fields.
                        data = {k: v for k, v in response.items()
                                if k not in ("success", "error", "error_type")}
                        return CommandResult(
                            success=False,
                            data=data,
                            error=full_error,
                            recoverable=response.get("recoverable", True)
                        )

                # Check Format 2 (legacy status field - only if no success field)
                elif "status" in response:
                    if response.get("status") == "success":
                        return CommandResult(
                            success=True,
                            data=response.get("result", {})
                        )
                    else:
                        error_msg = response.get("error", "Unknown error (no error message in response)")
                        error_type = response.get("error_type", "unknown")
                        # Include structured error details (e.g., patch results)
                        error_details = {}
                        for detail_key in ("errors", "warnings", "error_count", "warning_count",
                                           "name", "status", "results", "total", "executed",
                                           "succeeded", "failed", "compiled", "compile_error"):
                            if detail_key in response:
                                error_details[detail_key] = response[detail_key]
                        detail_str = ""
                        if error_details:
                            detail_str = f" | DETAILS: {json.dumps(error_details)}"
                        full_error = f"[{error_type}] {error_msg}{detail_str}"
                        logger.error(f"Command '{command_type}' failed: {full_error}")
                        data = {k: v for k, v in response.items()
                                if k not in ("status", "error", "error_type")}
                        return CommandResult(
                            success=False,
                            data=data,
                            error=full_error,
                            recoverable=response.get("recoverable", True)
                        )

                # Unknown response format - log the raw response for debugging
                else:
                    logger.error(f"Command '{command_type}' returned unknown response format: {json.dumps(response)[:500]}")
                    return CommandResult(
                        success=False,
                        error=f"Unknown response format from Unreal. Raw: {json.dumps(response)[:500]}",
                        recoverable=True
                    )

            except socket.timeout:
                logger.warning(f"Command '{command_type}' timed out")
                return CommandResult(
                    success=False,
                    error=f"Command '{command_type}' timed out after {self.config.timeout}s",
                    recoverable=True
                )
            except (socket.error, BrokenPipeError, ConnectionResetError) as e:
                logger.error(f"Socket error during command '{command_type}': {e}")
                self._state = ConnectionState.ERROR
                self._cleanup_socket()
                return CommandResult(
                    success=False,
                    error=str(e),
                    recoverable=True
                )

    def ping(self) -> bool:
        """
        Send a ping to check connection health.

        Returns:
            True if Unreal responded, False otherwise.
        """
        result = self.send_command("ping")
        return result.success and result.data.get("pong", False)

    def get_context(self) -> CommandResult:
        """
        Get the current editor context from Unreal.

        Returns:
            CommandResult with context data (current blueprint, graph, etc.)
        """
        return self.send_command("get_context")

    def _send_raw(self, data: dict):
        """Send raw JSON data over socket."""
        if not self._socket:
            raise ConnectionError("Socket not connected")

        json_str = json.dumps(data)
        message = json_str.encode('utf-8')

        # Send length prefix (4 bytes, big endian)
        length = len(message)
        self._socket.sendall(length.to_bytes(4, byteorder='big'))
        self._socket.sendall(message)

    def _receive_raw(self) -> Optional[dict]:
        """Receive raw JSON data from socket."""
        if not self._socket:
            return None

        try:
            # Receive length prefix
            length_bytes = self._recv_exact(4)
            if not length_bytes:
                return None

            length = int.from_bytes(length_bytes, byteorder='big')

            # Sanity check length
            if length <= 0 or length > 100 * 1024 * 1024:  # Max 100MB
                logger.error(f"Invalid message length: {length}")
                return None

            # Receive message
            message_bytes = self._recv_exact(length)
            if not message_bytes:
                return None

            return json.loads(message_bytes.decode('utf-8'))

        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            logger.error(f"Failed to parse response: {e}")
            return None

    def _recv_exact(self, num_bytes: int) -> Optional[bytes]:
        """Receive exact number of bytes from socket."""
        if not self._socket:
            return None

        data = bytearray()
        while len(data) < num_bytes:
            try:
                chunk = self._socket.recv(num_bytes - len(data))
                if not chunk:
                    return None  # Connection closed
                data.extend(chunk)
            except socket.timeout:
                return None
            except socket.error:
                return None

        return bytes(data)

    def _cleanup_socket(self):
        """Clean up socket resources."""
        if self._socket:
            try:
                self._socket.close()
            except Exception:
                pass
            self._socket = None

    def _try_reconnect(self) -> bool:
        """
        Attempt to reconnect with exponential backoff.

        Returns:
            True if reconnection successful, False otherwise.
        """
        self._state = ConnectionState.RECONNECTING
        self._cleanup_socket()

        while self._reconnect_attempts < self.config.max_reconnect_attempts:
            self._reconnect_attempts += 1

            # Calculate delay with exponential backoff
            delay = min(
                self.config.reconnect_base_delay * (2 ** (self._reconnect_attempts - 1)),
                self.config.reconnect_max_delay
            )

            logger.info(f"Reconnect attempt {self._reconnect_attempts}/{self.config.max_reconnect_attempts} in {delay:.1f}s")
            time.sleep(delay)

            if self.connect():
                return True

        self._state = ConnectionState.ERROR
        logger.error(f"Failed to reconnect after {self.config.max_reconnect_attempts} attempts")
        return False

    def _start_heartbeat(self):
        """Start the heartbeat thread."""
        self._stop_heartbeat.clear()
        self._heartbeat_thread = threading.Thread(target=self._heartbeat_loop, daemon=True)
        self._heartbeat_thread.start()

    def _heartbeat_loop(self):
        """Background thread that sends periodic pings."""
        while not self._stop_heartbeat.is_set():
            self._stop_heartbeat.wait(self.config.heartbeat_interval)

            if self._stop_heartbeat.is_set():
                break

            # Check if we need to ping (no recent activity)
            elapsed = time.time() - self._last_activity
            if elapsed >= self.config.heartbeat_interval:
                try:
                    if not self.ping():
                        logger.warning("Heartbeat ping returned false, marking connection stale")
                        # Don't reconnect from heartbeat thread - let the next
                        # send_command handle reconnection to avoid blocking
                        with self._lock:
                            if self._state == ConnectionState.CONNECTED:
                                self._state = ConnectionState.ERROR
                                self._cleanup_socket()
                except Exception as e:
                    logger.error(f"Heartbeat error: {e}")
                    with self._lock:
                        if self._state == ConnectionState.CONNECTED:
                            self._state = ConnectionState.ERROR
                            self._cleanup_socket()

    def __enter__(self):
        """Context manager entry - connect."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit - disconnect."""
        self.disconnect()
        return False


# Global connection instance for convenience
_global_connection: Optional[PersistentUnrealConnection] = None


def get_connection() -> PersistentUnrealConnection:
    """Get or create the global connection instance."""
    global _global_connection
    if _global_connection is None:
        _global_connection = PersistentUnrealConnection()
    return _global_connection


def reset_connection():
    """Reset the global connection (for testing or reconfiguration)."""
    global _global_connection
    if _global_connection:
        _global_connection.disconnect()
    _global_connection = None
