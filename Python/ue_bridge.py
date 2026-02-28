#!/usr/bin/env python3
"""
UE Bridge — 完整的 Unreal Engine MCPBridge Python 接口。

具备与 MCP 工具完全相同的能力（98 个命令），通过 TCP 55558 端口直接通信。
可作为 Python 模块导入，也可通过 CLI 调用。

============================================================
用法 1: CLI 模式（PowerShell）
============================================================
  # 无参数命令
  python ue_bridge.py ping
  python ue_bridge.py save_all
  python ue_bridge.py get_actors_in_level

  # 管道传参 (推荐，避免 PowerShell 转义)
  '{"blueprint_name":"BP_X","node_id":"GUID","pin_name":"Value","default_value":"42"}' | python ue_bridge.py set_node_pin_default

  # 交互式 REPL
  python ue_bridge.py --repl

============================================================
用法 2: Python 模块导入
============================================================
  from ue_bridge import UEBridge
  ue = UEBridge()

  ue.ping()
  ue.create_blueprint("BP_Test", "Actor")
  ue.add_component_to_blueprint("BP_Test", "StaticMeshComponent", "Mesh")
  ue.add_blueprint_event_node("BP_Test", "ReceiveBeginPlay")
  ue.compile_blueprint("BP_Test")
  ue.save_all()

============================================================
用法 3: 批量命令（Python 脚本）
============================================================
  from ue_bridge import UEBridge
  ue = UEBridge()

  # 创建完整蓝图
  ue.create_blueprint("BP_Enemy", "Actor")
  ue.add_component_to_blueprint("BP_Enemy", "StaticMeshComponent", "Mesh")
  ue.set_static_mesh_properties("BP_Enemy", "Mesh", static_mesh="/Engine/BasicShapes/Sphere.Sphere")
  ue.add_blueprint_variable("BP_Enemy", "Health", "Float", is_exposed=True)
  ue.set_blueprint_variable_default("BP_Enemy", "Health", "100")
  ue.set_variable_metadata("BP_Enemy", "Health", category="Stats", tooltip="HP")
  ue.add_blueprint_event_node("BP_Enemy", "ReceiveBeginPlay")
  result = ue.add_blueprint_function_node("BP_Enemy", "KismetSystemLibrary", "PrintString")
  node_id = result.get("node_id")
  ue.set_node_pin_default("BP_Enemy", node_id, "InString", "Enemy Spawned!")
  ue.compile_blueprint("BP_Enemy")
  ue.save_all()
"""

import json
import socket
import sys
try:
    import readline  # noqa: F401 — enables arrow-key editing in REPL
except ImportError:
    pass  # Windows: readline not available, arrow keys still work via pyreadline3 if installed
from typing import Any, Dict, List, Optional, Union

HOST = "127.0.0.1"
PORT = 55558
TIMEOUT = 120.0


# ═══════════════════════════════════════════════════════════
# TCP Transport Layer
# ═══════════════════════════════════════════════════════════

class UEConnection:
    """Persistent TCP connection to UE MCPBridge."""

    def __init__(self, host: str = HOST, port: int = PORT, timeout: float = TIMEOUT):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock: Optional[socket.socket] = None

    def connect(self):
        if self._sock:
            return
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.settimeout(self.timeout)
        self._sock.connect((self.host, self.port))

    def close(self):
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    def send(self, command_type: str, params: Optional[dict] = None) -> dict:
        """Send command and return response. Auto-connects if needed."""
        if not self._sock:
            self.connect()

        command: Dict[str, Any] = {"type": command_type}
        if params:
            # Strip None values
            command["params"] = {k: v for k, v in params.items() if v is not None}

        payload = json.dumps(command).encode("utf-8")

        try:
            self._sock.sendall(len(payload).to_bytes(4, byteorder="big"))
            self._sock.sendall(payload)

            length_bytes = self._recv_exact(4)
            length = int.from_bytes(length_bytes, byteorder="big")
            body = self._recv_exact(length)
            return json.loads(body.decode("utf-8"))
        except (ConnectionError, BrokenPipeError, OSError):
            # Reconnect once on failure
            self.close()
            self.connect()
            self._sock.sendall(len(payload).to_bytes(4, byteorder="big"))
            self._sock.sendall(payload)
            length_bytes = self._recv_exact(4)
            length = int.from_bytes(length_bytes, byteorder="big")
            body = self._recv_exact(length)
            return json.loads(body.decode("utf-8"))

    def _recv_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Socket closed by remote")
            buf.extend(chunk)
        return bytes(buf)

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.close()


# ═══════════════════════════════════════════════════════════
# UEBridge — Full API (98 commands)
# ═══════════════════════════════════════════════════════════

class UEBridge:
    """
    Complete Python interface to UE MCPBridge.
    All 98 MCP commands as typed Python methods.
    """

    def __init__(self, host: str = HOST, port: int = PORT):
        self._conn = UEConnection(host, port)

    def call(self, command: str, **params) -> dict:
        """Generic command call. Use this for any command not wrapped below."""
        return self._conn.send(command, params if params else None)

    def close(self):
        self._conn.close()

    # ───────────────────────────────────────────────────────
    # Meta
    # ───────────────────────────────────────────────────────

    def ping(self) -> dict:
        return self.call("ping")

    def get_context(self) -> dict:
        return self.call("get_context")

    # ───────────────────────────────────────────────────────
    # Editor — Actors
    # ───────────────────────────────────────────────────────

    def get_actors_in_level(self) -> dict:
        return self.call("get_actors_in_level")

    def find_actors_by_name(self, pattern: str) -> dict:
        return self.call("find_actors_by_name", pattern=pattern)

    def spawn_actor(self, name: str, type: str, *,
                    location: List[float] = None, rotation: List[float] = None) -> dict:
        return self.call("spawn_actor", name=name, type=type,
                         location=location, rotation=rotation)

    def delete_actor(self, name: str) -> dict:
        return self.call("delete_actor", name=name)

    def set_actor_transform(self, name: str, *,
                            location: List[float] = None,
                            rotation: List[float] = None,
                            scale: List[float] = None) -> dict:
        return self.call("set_actor_transform", name=name,
                         location=location, rotation=rotation, scale=scale)

    def get_actor_properties(self, name: str) -> dict:
        return self.call("get_actor_properties", name=name)

    def set_actor_property(self, name: str, property_name: str, property_value: str) -> dict:
        return self.call("set_actor_property", name=name,
                         property_name=property_name, property_value=property_value)

    # ───────────────────────────────────────────────────────
    # Editor — Viewport
    # ───────────────────────────────────────────────────────

    def focus_viewport(self, *, target: str = None, location: List[float] = None,
                       distance: float = None, orientation: List[float] = None) -> dict:
        return self.call("focus_viewport", target=target, location=location,
                         distance=distance, orientation=orientation)

    def get_viewport_transform(self) -> dict:
        return self.call("get_viewport_transform")

    def set_viewport_transform(self, *, location: List[float] = None,
                               rotation: List[float] = None) -> dict:
        return self.call("set_viewport_transform", location=location, rotation=rotation)

    # ───────────────────────────────────────────────────────
    # Editor — Utility
    # ───────────────────────────────────────────────────────

    def save_all(self) -> dict:
        return self.call("save_all")

    def list_assets(self, path: str, *, recursive: bool = None,
                    class_filter: str = None, name_contains: str = None,
                    max_results: int = None) -> dict:
        return self.call("list_assets", path=path, recursive=recursive,
                         class_filter=class_filter, name_contains=name_contains,
                         max_results=max_results)

    def get_blueprint_summary(self, *, blueprint_name: str = None,
                              asset_path: str = None) -> dict:
        return self.call("get_blueprint_summary",
                         blueprint_name=blueprint_name, asset_path=asset_path)

    # ───────────────────────────────────────────────────────
    # Editor — Layout
    # ───────────────────────────────────────────────────────

    def auto_layout_selected(self, *, mode: str = None, blueprint_name: str = None,
                             graph_name: str = None, node_ids: List[str] = None,
                             layer_spacing: float = None, row_spacing: float = None) -> dict:
        return self.call("auto_layout_selected", mode=mode, blueprint_name=blueprint_name,
                         graph_name=graph_name, node_ids=node_ids,
                         layer_spacing=layer_spacing, row_spacing=row_spacing)

    def auto_layout_subtree(self, *, root_node_id: str = None, blueprint_name: str = None,
                            graph_name: str = None, max_pure_depth: int = None,
                            layer_spacing: float = None, row_spacing: float = None) -> dict:
        return self.call("auto_layout_subtree", root_node_id=root_node_id,
                         blueprint_name=blueprint_name, graph_name=graph_name,
                         max_pure_depth=max_pure_depth,
                         layer_spacing=layer_spacing, row_spacing=row_spacing)

    # ───────────────────────────────────────────────────────
    # Blueprint — CRUD
    # ───────────────────────────────────────────────────────

    def create_blueprint(self, name: str, parent_class: str, *, path: str = None) -> dict:
        return self.call("create_blueprint", name=name, parent_class=parent_class, path=path)

    def compile_blueprint(self, blueprint_name: str) -> dict:
        return self.call("compile_blueprint", blueprint_name=blueprint_name)

    def set_blueprint_property(self, blueprint_name: str, property_name: str,
                               property_value: str) -> dict:
        return self.call("set_blueprint_property", blueprint_name=blueprint_name,
                         property_name=property_name, property_value=property_value)

    def set_blueprint_parent_class(self, blueprint_name: str, parent_class: str) -> dict:
        return self.call("set_blueprint_parent_class",
                         blueprint_name=blueprint_name, parent_class=parent_class)

    def add_blueprint_interface(self, blueprint_name: str, interface_name: str) -> dict:
        return self.call("add_blueprint_interface",
                         blueprint_name=blueprint_name, interface_name=interface_name)

    def remove_blueprint_interface(self, blueprint_name: str, interface_name: str) -> dict:
        return self.call("remove_blueprint_interface",
                         blueprint_name=blueprint_name, interface_name=interface_name)

    # ───────────────────────────────────────────────────────
    # Blueprint — Components
    # ───────────────────────────────────────────────────────

    def add_component_to_blueprint(self, blueprint_name: str, component_type: str,
                                   component_name: str, *,
                                   location: List[float] = None,
                                   rotation: List[float] = None,
                                   scale: List[float] = None,
                                   component_properties: dict = None) -> dict:
        return self.call("add_component_to_blueprint",
                         blueprint_name=blueprint_name, component_type=component_type,
                         component_name=component_name, location=location,
                         rotation=rotation, scale=scale,
                         component_properties=component_properties)

    def set_static_mesh_properties(self, blueprint_name: str, component_name: str, *,
                                   static_mesh: str = None, material: str = None,
                                   overlay_material: str = None) -> dict:
        return self.call("set_static_mesh_properties",
                         blueprint_name=blueprint_name, component_name=component_name,
                         static_mesh=static_mesh, material=material,
                         overlay_material=overlay_material)

    def set_component_property(self, blueprint_name: str, component_name: str,
                               property_name: str, property_value: str) -> dict:
        return self.call("set_component_property",
                         blueprint_name=blueprint_name, component_name=component_name,
                         property_name=property_name, property_value=property_value)

    def set_physics_properties(self, blueprint_name: str, component_name: str, *,
                               simulate_physics: bool = None, gravity_enabled: bool = None,
                               mass: float = None, linear_damping: float = None,
                               angular_damping: float = None) -> dict:
        return self.call("set_physics_properties",
                         blueprint_name=blueprint_name, component_name=component_name,
                         simulate_physics=simulate_physics, gravity_enabled=gravity_enabled,
                         mass=mass, linear_damping=linear_damping,
                         angular_damping=angular_damping)

    def spawn_blueprint_actor(self, blueprint_name: str, actor_name: str, *,
                              location: List[float] = None,
                              rotation: List[float] = None) -> dict:
        return self.call("spawn_blueprint_actor",
                         blueprint_name=blueprint_name, actor_name=actor_name,
                         location=location, rotation=rotation)

    def create_colored_material(self, material_name: str, *,
                                color: List[float] = None, path: str = None) -> dict:
        return self.call("create_colored_material",
                         material_name=material_name, color=color, path=path)

    # ───────────────────────────────────────────────────────
    # Blueprint — Struct & Switch Nodes
    # ───────────────────────────────────────────────────────

    def add_make_struct_node(self, blueprint_name: str, struct_type: str, *,
                            pin_defaults: dict = None, node_position: List[float] = None,
                            graph_name: str = None) -> dict:
        return self.call("add_make_struct_node",
                         blueprint_name=blueprint_name, struct_type=struct_type,
                         pin_defaults=pin_defaults, node_position=node_position,
                         graph_name=graph_name)

    def add_break_struct_node(self, blueprint_name: str, struct_type: str, *,
                              node_position: List[float] = None,
                              graph_name: str = None) -> dict:
        return self.call("add_break_struct_node",
                         blueprint_name=blueprint_name, struct_type=struct_type,
                         node_position=node_position, graph_name=graph_name)

    def add_switch_on_string_node(self, blueprint_name: str, *,
                                  cases: List[str] = None,
                                  node_position: List[float] = None,
                                  graph_name: str = None) -> dict:
        return self.call("add_switch_on_string_node",
                         blueprint_name=blueprint_name, cases=cases,
                         node_position=node_position, graph_name=graph_name)

    def add_switch_on_int_node(self, blueprint_name: str, *,
                               start_index: int = None, cases: List[int] = None,
                               node_position: List[float] = None,
                               graph_name: str = None) -> dict:
        return self.call("add_switch_on_int_node",
                         blueprint_name=blueprint_name, start_index=start_index,
                         cases=cases, node_position=node_position, graph_name=graph_name)

    # ───────────────────────────────────────────────────────
    # Nodes — Events
    # ───────────────────────────────────────────────────────

    def add_blueprint_event_node(self, blueprint_name: str, event_name: str, *,
                                 node_position: str = None) -> dict:
        return self.call("add_blueprint_event_node",
                         blueprint_name=blueprint_name, event_name=event_name,
                         node_position=node_position)

    def add_blueprint_custom_event(self, blueprint_name: str, event_name: str, *,
                                   node_position: str = None,
                                   parameters: List[dict] = None) -> dict:
        return self.call("add_blueprint_custom_event",
                         blueprint_name=blueprint_name, event_name=event_name,
                         node_position=node_position, parameters=parameters)

    def add_blueprint_input_action_node(self, blueprint_name: str, action_name: str, *,
                                        node_position: str = None) -> dict:
        return self.call("add_blueprint_input_action_node",
                         blueprint_name=blueprint_name, action_name=action_name,
                         node_position=node_position)

    def add_enhanced_input_action_node(self, blueprint_name: str, action_name: str, *,
                                       action_path: str = None,
                                       node_position: str = None) -> dict:
        return self.call("add_enhanced_input_action_node",
                         blueprint_name=blueprint_name, action_name=action_name,
                         action_path=action_path, node_position=node_position)

    # ───────────────────────────────────────────────────────
    # Nodes — Event Dispatchers
    # ───────────────────────────────────────────────────────

    def add_event_dispatcher(self, blueprint_name: str, dispatcher_name: str, *,
                             parameters: List[dict] = None) -> dict:
        return self.call("add_event_dispatcher",
                         blueprint_name=blueprint_name, dispatcher_name=dispatcher_name,
                         parameters=parameters)

    def call_event_dispatcher(self, blueprint_name: str, dispatcher_name: str, *,
                              node_position: List[float] = None,
                              graph_name: str = None) -> dict:
        return self.call("call_event_dispatcher",
                         blueprint_name=blueprint_name, dispatcher_name=dispatcher_name,
                         node_position=node_position, graph_name=graph_name)

    def bind_event_dispatcher(self, blueprint_name: str, dispatcher_name: str, *,
                              target_blueprint: str = None,
                              node_position: List[float] = None,
                              graph_name: str = None) -> dict:
        return self.call("bind_event_dispatcher",
                         blueprint_name=blueprint_name, dispatcher_name=dispatcher_name,
                         target_blueprint=target_blueprint,
                         node_position=node_position, graph_name=graph_name)

    def create_event_delegate(self, blueprint_name: str, function_name: str, *,
                              connect_to_node_id: str = None,
                              connect_to_pin: str = None,
                              node_position: List[float] = None,
                              graph_name: str = None) -> dict:
        """Create a 'Create Event' (K2Node_CreateDelegate) node.
        Works in function graphs where CustomEvent is unavailable."""
        return self.call("create_event_delegate",
                         blueprint_name=blueprint_name, function_name=function_name,
                         connect_to_node_id=connect_to_node_id,
                         connect_to_pin=connect_to_pin,
                         node_position=node_position, graph_name=graph_name)

    # ───────────────────────────────────────────────────────
    # Nodes — Functions
    # ───────────────────────────────────────────────────────

    def add_blueprint_function_node(self, blueprint_name: str, target: str,
                                    function_name: str, *,
                                    params: str = None, node_position: str = None,
                                    graph_name: str = None) -> dict:
        return self.call("add_blueprint_function_node",
                         blueprint_name=blueprint_name, target=target,
                         function_name=function_name, params=params,
                         node_position=node_position, graph_name=graph_name)

    def create_blueprint_function(self, blueprint_name: str, function_name: str, *,
                                  inputs: List[dict] = None, outputs: List[dict] = None,
                                  is_pure: bool = None) -> dict:
        return self.call("create_blueprint_function",
                         blueprint_name=blueprint_name, function_name=function_name,
                         inputs=inputs, outputs=outputs, is_pure=is_pure)

    def call_blueprint_function(self, blueprint_name: str, target_blueprint: str,
                                function_name: str, *,
                                node_position: List[float] = None,
                                graph_name: str = None) -> dict:
        return self.call("call_blueprint_function",
                         blueprint_name=blueprint_name, target_blueprint=target_blueprint,
                         function_name=function_name, node_position=node_position,
                         graph_name=graph_name)

    # ───────────────────────────────────────────────────────
    # Nodes — Variables
    # ───────────────────────────────────────────────────────

    def add_blueprint_variable(self, blueprint_name: str, variable_name: str,
                               variable_type: str, *, is_exposed: bool = None) -> dict:
        return self.call("add_blueprint_variable",
                         blueprint_name=blueprint_name, variable_name=variable_name,
                         variable_type=variable_type, is_exposed=is_exposed)

    def add_blueprint_variable_get(self, blueprint_name: str, variable_name: str, *,
                                   node_position: str = None,
                                   graph_name: str = None) -> dict:
        return self.call("add_blueprint_variable_get",
                         blueprint_name=blueprint_name, variable_name=variable_name,
                         node_position=node_position, graph_name=graph_name)

    def add_blueprint_variable_set(self, blueprint_name: str, variable_name: str, *,
                                   node_position: str = None,
                                   graph_name: str = None) -> dict:
        return self.call("add_blueprint_variable_set",
                         blueprint_name=blueprint_name, variable_name=variable_name,
                         node_position=node_position, graph_name=graph_name)

    # ───────────────────────────────────────────────────────
    # Nodes — References & Casting
    # ───────────────────────────────────────────────────────

    def add_blueprint_self_reference(self, blueprint_name: str, *,
                                     node_position: str = None,
                                     graph_name: str = None) -> dict:
        return self.call("add_blueprint_self_reference",
                         blueprint_name=blueprint_name, node_position=node_position,
                         graph_name=graph_name)

    def add_blueprint_get_self_component_reference(self, blueprint_name: str,
                                                    component_name: str, *,
                                                    node_position: str = None,
                                                    graph_name: str = None) -> dict:
        return self.call("add_blueprint_get_self_component_reference",
                         blueprint_name=blueprint_name, component_name=component_name,
                         node_position=node_position, graph_name=graph_name)

    def add_blueprint_cast_node(self, blueprint_name: str, target_class: str, *,
                                pure_cast: bool = None,
                                node_position: List[float] = None) -> dict:
        return self.call("add_blueprint_cast_node",
                         blueprint_name=blueprint_name, target_class=target_class,
                         pure_cast=pure_cast, node_position=node_position)

    # ───────────────────────────────────────────────────────
    # Nodes — Flow Control
    # ───────────────────────────────────────────────────────

    def add_blueprint_branch_node(self, blueprint_name: str, *,
                                  graph_name: str = None,
                                  node_position: str = None) -> dict:
        return self.call("add_blueprint_branch_node",
                         blueprint_name=blueprint_name, graph_name=graph_name,
                         node_position=node_position)

    def add_macro_instance_node(self, blueprint_name: str, macro_name: str, *,
                                graph_name: str = None,
                                node_position: List[float] = None) -> dict:
        return self.call("add_macro_instance_node",
                         blueprint_name=blueprint_name, macro_name=macro_name,
                         graph_name=graph_name, node_position=node_position)

    def add_spawn_actor_from_class_node(self, blueprint_name: str, class_to_spawn: str, *,
                                        node_position: List[float] = None,
                                        graph_name: str = None) -> dict:
        return self.call("add_spawn_actor_from_class_node",
                         blueprint_name=blueprint_name, class_to_spawn=class_to_spawn,
                         node_position=node_position, graph_name=graph_name)

    # ───────────────────────────────────────────────────────
    # Graph — Wiring & Inspection
    # ───────────────────────────────────────────────────────

    def connect_blueprint_nodes(self, blueprint_name: str,
                                source_node_id: str, source_pin: str,
                                target_node_id: str, target_pin: str, *,
                                graph_name: str = None) -> dict:
        return self.call("connect_blueprint_nodes",
                         blueprint_name=blueprint_name,
                         source_node_id=source_node_id, source_pin=source_pin,
                         target_node_id=target_node_id, target_pin=target_pin,
                         graph_name=graph_name)

    def find_blueprint_nodes(self, blueprint_name: str, *,
                             graph_name: str = None, node_type: str = None,
                             event_type: str = None) -> dict:
        return self.call("find_blueprint_nodes",
                         blueprint_name=blueprint_name, graph_name=graph_name,
                         node_type=node_type, event_type=event_type)

    def delete_blueprint_node(self, blueprint_name: str, node_id: str, *,
                              graph_name: str = None) -> dict:
        return self.call("delete_blueprint_node",
                         blueprint_name=blueprint_name, node_id=node_id,
                         graph_name=graph_name)

    def get_node_pins(self, blueprint_name: str, node_id: str, *,
                      graph_name: str = None) -> dict:
        return self.call("get_node_pins",
                         blueprint_name=blueprint_name, node_id=node_id,
                         graph_name=graph_name)

    def disconnect_blueprint_pin(self, blueprint_name: str, node_id: str,
                                 pin_name: str, *, graph_name: str = None) -> dict:
        return self.call("disconnect_blueprint_pin",
                         blueprint_name=blueprint_name, node_id=node_id,
                         pin_name=pin_name, graph_name=graph_name)

    # ───────────────────────────────────────────────────────
    # Graph — Layout & Comments
    # ───────────────────────────────────────────────────────

    def move_node(self, blueprint_name: str, node_id: str,
                  node_position: List[float], *, graph_name: str = None) -> dict:
        return self.call("move_node",
                         blueprint_name=blueprint_name, node_id=node_id,
                         node_position=node_position, graph_name=graph_name)

    def add_reroute_node(self, blueprint_name: str, *,
                         node_position: List[float] = None,
                         graph_name: str = None) -> dict:
        return self.call("add_reroute_node",
                         blueprint_name=blueprint_name,
                         node_position=node_position, graph_name=graph_name)

    def add_blueprint_comment(self, blueprint_name: str, comment_text: str, *,
                              graph_name: str = None, node_position: List[float] = None,
                              size: List[float] = None, color: List[float] = None) -> dict:
        return self.call("add_blueprint_comment",
                         blueprint_name=blueprint_name, comment_text=comment_text,
                         graph_name=graph_name, node_position=node_position,
                         size=size, color=color)

    # ───────────────────────────────────────────────────────
    # Graph — Pin Defaults (KEY: often unreachable via MCP tools)
    # ───────────────────────────────────────────────────────

    def set_node_pin_default(self, blueprint_name: str, node_id: str,
                             pin_name: str, default_value: str, *,
                             graph_name: str = None) -> dict:
        return self.call("set_node_pin_default",
                         blueprint_name=blueprint_name, node_id=node_id,
                         pin_name=pin_name, default_value=default_value,
                         graph_name=graph_name)

    def set_object_property(self, blueprint_name: str, owner_class: str,
                            property_name: str, *,
                            node_position: List[float] = None,
                            graph_name: str = None) -> dict:
        return self.call("set_object_property",
                         blueprint_name=blueprint_name, owner_class=owner_class,
                         property_name=property_name, node_position=node_position,
                         graph_name=graph_name)

    # ───────────────────────────────────────────────────────
    # Graph — Variable Management
    # ───────────────────────────────────────────────────────

    def set_blueprint_variable_default(self, blueprint_name: str,
                                       variable_name: str, default_value: str) -> dict:
        return self.call("set_blueprint_variable_default",
                         blueprint_name=blueprint_name, variable_name=variable_name,
                         default_value=default_value)

    def add_function_local_variable(self, blueprint_name: str, function_name: str,
                                    variable_name: str, variable_type: str, *,
                                    default_value: str = None) -> dict:
        return self.call("add_function_local_variable",
                         blueprint_name=blueprint_name, function_name=function_name,
                         variable_name=variable_name, variable_type=variable_type,
                         default_value=default_value)

    def delete_blueprint_variable(self, blueprint_name: str, variable_name: str) -> dict:
        return self.call("delete_blueprint_variable",
                         blueprint_name=blueprint_name, variable_name=variable_name)

    def rename_blueprint_variable(self, blueprint_name: str,
                                  old_name: str, new_name: str) -> dict:
        return self.call("rename_blueprint_variable",
                         blueprint_name=blueprint_name, old_name=old_name, new_name=new_name)

    def set_variable_metadata(self, blueprint_name: str, variable_name: str, *,
                              category: str = None, tooltip: str = None,
                              instance_editable: bool = None,
                              blueprint_read_only: bool = None,
                              expose_on_spawn: bool = None,
                              replicated: bool = None, private: bool = None) -> dict:
        return self.call("set_variable_metadata",
                         blueprint_name=blueprint_name, variable_name=variable_name,
                         category=category, tooltip=tooltip,
                         instance_editable=instance_editable,
                         blueprint_read_only=blueprint_read_only,
                         expose_on_spawn=expose_on_spawn,
                         replicated=replicated, private=private)

    def delete_blueprint_function(self, blueprint_name: str, function_name: str) -> dict:
        return self.call("delete_blueprint_function",
                         blueprint_name=blueprint_name, function_name=function_name)

    # ───────────────────────────────────────────────────────
    # Materials
    # ───────────────────────────────────────────────────────

    def create_material(self, material_name: str, *, path: str = None,
                        domain: str = None, blend_mode: str = None) -> dict:
        return self.call("create_material", material_name=material_name,
                         path=path, domain=domain, blend_mode=blend_mode)

    def add_material_expression(self, material_name: str, expression_class: str,
                                node_name: str, *, position: List[float] = None,
                                properties: dict = None) -> dict:
        return self.call("add_material_expression",
                         material_name=material_name, expression_class=expression_class,
                         node_name=node_name, position=position, properties=properties)

    def connect_material_expressions(self, material_name: str,
                                     source_node: str, target_node: str,
                                     target_input: str, *,
                                     source_output_index: int = None) -> dict:
        return self.call("connect_material_expressions",
                         material_name=material_name, source_node=source_node,
                         target_node=target_node, target_input=target_input,
                         source_output_index=source_output_index)

    def connect_to_material_output(self, material_name: str, source_node: str,
                                   material_property: str, *,
                                   source_output_index: int = None) -> dict:
        return self.call("connect_to_material_output",
                         material_name=material_name, source_node=source_node,
                         material_property=material_property,
                         source_output_index=source_output_index)

    def set_material_expression_property(self, material_name: str, node_name: str,
                                         property_name: str, property_value: str) -> dict:
        return self.call("set_material_expression_property",
                         material_name=material_name, node_name=node_name,
                         property_name=property_name, property_value=property_value)

    def compile_material(self, material_name: str) -> dict:
        return self.call("compile_material", material_name=material_name)

    def set_material_property(self, material_name: str, property_name: str,
                              property_value: str) -> dict:
        return self.call("set_material_property", material_name=material_name,
                         property_name=property_name, property_value=property_value)

    def create_material_instance(self, instance_name: str, parent_material: str, *,
                                 path: str = None, scalar_parameters: dict = None,
                                 vector_parameters: dict = None) -> dict:
        return self.call("create_material_instance",
                         instance_name=instance_name, parent_material=parent_material,
                         path=path, scalar_parameters=scalar_parameters,
                         vector_parameters=vector_parameters)

    def create_post_process_volume(self, name: str, *, location: List[float] = None,
                                   infinite_extent: bool = None, priority: float = None,
                                   post_process_materials: List[str] = None) -> dict:
        return self.call("create_post_process_volume", name=name,
                         location=location, infinite_extent=infinite_extent,
                         priority=priority, post_process_materials=post_process_materials)

    # ───────────────────────────────────────────────────────
    # Input System
    # ───────────────────────────────────────────────────────

    def create_input_mapping(self, action_name: str, key: str, *,
                             input_type: str = None, scale: float = None) -> dict:
        return self.call("create_input_mapping", action_name=action_name, key=key,
                         input_type=input_type, scale=scale)

    def create_input_action(self, name: str, *, value_type: str = None,
                            path: str = None) -> dict:
        return self.call("create_input_action", name=name,
                         value_type=value_type, path=path)

    def create_input_mapping_context(self, name: str, *, path: str = None) -> dict:
        return self.call("create_input_mapping_context", name=name, path=path)

    def add_key_mapping_to_context(self, context_name: str, action_name: str, key: str, *,
                                   modifiers: List[str] = None,
                                   context_path: str = None,
                                   action_path: str = None) -> dict:
        return self.call("add_key_mapping_to_context",
                         context_name=context_name, action_name=action_name, key=key,
                         modifiers=modifiers, context_path=context_path,
                         action_path=action_path)

    # ───────────────────────────────────────────────────────
    # UMG Widgets
    # ───────────────────────────────────────────────────────

    def create_umg_widget_blueprint(self, widget_name: str, *,
                                    parent_class: str = None, path: str = None) -> dict:
        return self.call("create_umg_widget_blueprint",
                         widget_name=widget_name, parent_class=parent_class, path=path)

    def delete_umg_widget_blueprint(self, widget_name: str) -> dict:
        return self.call("delete_umg_widget_blueprint", widget_name=widget_name)

    def list_widget_components(self, widget_name: str) -> dict:
        return self.call("list_widget_components", widget_name=widget_name)

    def get_widget_tree(self, widget_name: str) -> dict:
        return self.call("get_widget_tree", widget_name=widget_name)

    def add_widget_component(self, widget_name: str, component_type: str,
                             component_name: str, **kwargs) -> dict:
        """Add any of 24 widget types. Pass type-specific params as kwargs."""
        return self.call("add_widget_component",
                         widget_name=widget_name, component_type=component_type,
                         component_name=component_name, **kwargs)

    def bind_widget_event(self, widget_name: str, widget_component_name: str,
                          event_name: str) -> dict:
        return self.call("bind_widget_event",
                         widget_name=widget_name,
                         widget_component_name=widget_component_name,
                         event_name=event_name)

    def add_widget_to_viewport(self, widget_name: str, *, z_order: int = None) -> dict:
        return self.call("add_widget_to_viewport",
                         widget_name=widget_name, z_order=z_order)

    def set_text_block_binding(self, widget_name: str, text_block_name: str,
                               binding_property: str, *,
                               binding_type: str = None) -> dict:
        return self.call("set_text_block_binding",
                         widget_name=widget_name, text_block_name=text_block_name,
                         binding_property=binding_property, binding_type=binding_type)

    def reparent_widgets(self, widget_name: str, target_container_name: str, *,
                         container_type: str = None, children: List[str] = None,
                         filter_class: str = None, position: List[float] = None,
                         size: List[float] = None, z_order: int = None) -> dict:
        return self.call("reparent_widgets",
                         widget_name=widget_name,
                         target_container_name=target_container_name,
                         container_type=container_type, children=children,
                         filter_class=filter_class, position=position,
                         size=size, z_order=z_order)

    def set_widget_properties(self, widget_name: str, target: str, **kwargs) -> dict:
        """Set any widget property. Pass slot/render/type-specific params as kwargs."""
        return self.call("set_widget_properties",
                         widget_name=widget_name, target=target, **kwargs)

    def set_widget_text(self, widget_name: str, target: str, *,
                        text: str = None, font_size: int = None,
                        color: List[float] = None, justification: str = None,
                        background_color: List[float] = None) -> dict:
        return self.call("set_widget_text",
                         widget_name=widget_name, target=target,
                         text=text, font_size=font_size, color=color,
                         justification=justification, background_color=background_color)

    def set_slider_properties(self, widget_name: str, target: str, *,
                              value: float = None, min_value: float = None,
                              max_value: float = None, step_size: float = None,
                              locked: bool = None) -> dict:
        return self.call("set_slider_properties",
                         widget_name=widget_name, target=target,
                         value=value, min_value=min_value, max_value=max_value,
                         step_size=step_size, locked=locked)

    def set_combo_box_options(self, widget_name: str, target: str, *,
                              mode: str = None, options: List[str] = None,
                              selected_option: str = None) -> dict:
        return self.call("set_combo_box_options",
                         widget_name=widget_name, target=target,
                         mode=mode, options=options, selected_option=selected_option)

    def rename_widget_in_blueprint(self, widget_name: str, target: str,
                                   new_name: str) -> dict:
        return self.call("rename_widget_in_blueprint",
                         widget_name=widget_name, target=target, new_name=new_name)

    def add_widget_child(self, widget_name: str, child: str, parent: str) -> dict:
        return self.call("add_widget_child",
                         widget_name=widget_name, child=child, parent=parent)

    def delete_widget_from_blueprint(self, widget_name: str, target: str) -> dict:
        return self.call("delete_widget_from_blueprint",
                         widget_name=widget_name, target=target)


# ═══════════════════════════════════════════════════════════
# CLI Entry Point
# ═══════════════════════════════════════════════════════════

def _cli_main():
    """CLI: python ue_bridge.py <command> or pipe JSON for params."""
    if len(sys.argv) < 2 or sys.argv[1] in ("--help", "-h"):
        print(__doc__)
        sys.exit(0)

    if sys.argv[1] == "--repl":
        _repl()
        return

    command = sys.argv[1]
    params = None

    # Try joining remaining args as JSON
    if len(sys.argv) >= 3:
        raw = " ".join(sys.argv[2:])
        try:
            params = json.loads(raw)
        except json.JSONDecodeError:
            pass

    # Fallback: read from stdin
    if params is None and not sys.stdin.isatty():
        try:
            raw = sys.stdin.read().strip()
            if raw:
                params = json.loads(raw)
        except json.JSONDecodeError as e:
            print(f"ERROR: Invalid JSON: {e}", file=sys.stderr)
            sys.exit(1)

    ue = UEBridge()
    try:
        result = ue.call(command, **(params or {}))
        print(json.dumps(result, indent=2, ensure_ascii=False))
    except ConnectionRefusedError:
        print("ERROR: Cannot connect to UE MCPBridge (port 55558). Is Unreal Editor running?",
              file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        ue.close()


def _repl():
    """Interactive REPL for testing commands."""
    print("UE Bridge REPL — type 'help' or 'quit'")
    ue = UEBridge()
    try:
        while True:
            try:
                line = input("ue> ").strip()
            except (EOFError, KeyboardInterrupt):
                break
            if not line:
                continue
            if line in ("quit", "exit"):
                break
            if line == "help":
                print("Usage: <command> [json_params]")
                print("Examples:")
                print('  ping')
                print('  save_all')
                print('  get_actors_in_level')
                print('  compile_blueprint {"blueprint_name":"BP_Test"}')
                continue

            parts = line.split(None, 1)
            cmd = parts[0]
            params = None
            if len(parts) > 1:
                try:
                    params = json.loads(parts[1])
                except json.JSONDecodeError as e:
                    print(f"JSON error: {e}")
                    continue
            try:
                result = ue.call(cmd, **(params or {}))
                print(json.dumps(result, indent=2, ensure_ascii=False))
            except Exception as e:
                print(f"Error: {e}")
    finally:
        ue.close()
        print("\nDisconnected.")


if __name__ == "__main__":
    _cli_main()
