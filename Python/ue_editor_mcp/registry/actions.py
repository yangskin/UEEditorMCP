"""
Action definitions — all 112 C++ actions registered with metadata.

Each action maps:
  - A human-friendly ``id`` (e.g. "blueprint.create")
  - To a C++ ``command`` type (e.g. "create_blueprint")
  - With ``tags``, ``description``, ``input_schema``, and ``examples``

Organized by domain for readability.
"""

from __future__ import annotations
from . import ActionDef, ActionRegistry


def register_all_actions(registry: ActionRegistry) -> None:
    """Register every action in the registry."""
    registry.register_many(_BLUEPRINT_ACTIONS)
    registry.register_many(_COMPONENT_ACTIONS)
    registry.register_many(_EDITOR_ACTIONS)
    registry.register_many(_LAYOUT_ACTIONS)
    registry.register_many(_NODE_EVENT_ACTIONS)
    registry.register_many(_NODE_DISPATCHER_ACTIONS)
    registry.register_many(_NODE_FUNCTION_ACTIONS)
    registry.register_many(_NODE_VARIABLE_ACTIONS)
    registry.register_many(_NODE_REFERENCE_ACTIONS)
    registry.register_many(_NODE_FLOW_ACTIONS)
    registry.register_many(_GRAPH_ACTIONS)
    registry.register_many(_VARIABLE_MGMT_ACTIONS)
    registry.register_many(_FUNCTION_MGMT_ACTIONS)
    registry.register_many(_STRUCT_SWITCH_ACTIONS)
    registry.register_many(_MATERIAL_ACTIONS)
    registry.register_many(_WIDGET_ACTIONS)
    registry.register_many(_INPUT_ACTIONS)


# =========================================================================
# Blueprint Management
# =========================================================================
_BLUEPRINT_ACTIONS = [
    ActionDef(
        id="blueprint.create",
        command="create_blueprint",
        tags=("blueprint", "create", "asset"),
        description="Create a new Blueprint class",
        input_schema={
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Name of the Blueprint"},
                "parent_class": {"type": "string", "description": "Parent class (Actor, Pawn, Character, GameModeBase, etc.)"},
                "path": {"type": "string", "description": "Content path (default: /Game/Blueprints)"}
            },
            "required": ["name", "parent_class"]
        },
        examples=(
            {"name": "BP_Player", "parent_class": "Character"},
            {"name": "BP_Projectile", "parent_class": "Actor", "path": "/Game/Weapons"},
        ),
    ),
    ActionDef(
        id="blueprint.compile",
        command="compile_blueprint",
        tags=("blueprint", "compile", "validate"),
        description="Compile a Blueprint and check for errors",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint to compile"}
            },
            "required": ["blueprint_name"]
        },
        examples=({"blueprint_name": "BP_Player"},),
    ),
    ActionDef(
        id="blueprint.set_property",
        command="set_blueprint_property",
        tags=("blueprint", "property", "set", "default"),
        description="Set a property on a Blueprint class default object",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "property_name": {"type": "string", "description": "Name of the property"},
                "property_value": {"type": "string", "description": "Value to set"}
            },
            "required": ["blueprint_name", "property_name", "property_value"]
        },
        examples=({"blueprint_name": "BP_Player", "property_name": "MaxHealth", "property_value": "100.0"},),
    ),
    ActionDef(
        id="blueprint.spawn_actor",
        command="spawn_blueprint_actor",
        tags=("blueprint", "spawn", "actor", "level"),
        description="Spawn an instance of a Blueprint in the level",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint to spawn"},
                "actor_name": {"type": "string", "description": "Name for the spawned actor"},
                "location": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"},
                "rotation": {"type": "array", "items": {"type": "number"}, "description": "[pitch, yaw, roll]"}
            },
            "required": ["blueprint_name", "actor_name"]
        },
        examples=({"blueprint_name": "BP_Enemy", "actor_name": "Enemy1", "location": [100, 0, 50]},),
    ),
    ActionDef(
        id="blueprint.set_parent_class",
        command="set_blueprint_parent_class",
        tags=("blueprint", "parent", "reparent", "inherit"),
        description="Change the parent class of a Blueprint (reparent)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "parent_class": {"type": "string", "description": "New parent class name or full path"}
            },
            "required": ["blueprint_name", "parent_class"]
        },
        examples=({"blueprint_name": "BP_Player", "parent_class": "Character"},),
    ),
    ActionDef(
        id="blueprint.add_interface",
        command="add_blueprint_interface",
        tags=("blueprint", "interface", "add", "implement"),
        description="Add an interface implementation to a Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "interface_name": {"type": "string", "description": "Interface class name or full path"}
            },
            "required": ["blueprint_name", "interface_name"]
        },
        examples=({"blueprint_name": "BP_Chest", "interface_name": "BPI_Interactable"},),
    ),
    ActionDef(
        id="blueprint.remove_interface",
        command="remove_blueprint_interface",
        tags=("blueprint", "interface", "remove"),
        description="Remove an implemented interface from a Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "interface_name": {"type": "string", "description": "Interface class name or path to remove"}
            },
            "required": ["blueprint_name", "interface_name"]
        },
        capabilities=("write", "destructive"),
        risk="moderate",
        examples=({"blueprint_name": "BP_Chest", "interface_name": "BPI_Interactable"},),
    ),
    ActionDef(
        id="blueprint.add_component",
        command="add_component_to_blueprint",
        tags=("blueprint", "component", "add"),
        description="Add a component to a Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "component_type": {"type": "string", "description": "Type of component (StaticMeshComponent, BoxComponent, etc.)"},
                "component_name": {"type": "string", "description": "Name for the component"},
                "location": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"},
                "rotation": {"type": "array", "items": {"type": "number"}, "description": "[pitch, yaw, roll]"},
                "scale": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"},
                "component_properties": {"type": "object", "description": "Additional properties to set"}
            },
            "required": ["blueprint_name", "component_type", "component_name"]
        },
        examples=(
            {"blueprint_name": "BP_Player", "component_type": "CapsuleComponent", "component_name": "Capsule"},
            {"blueprint_name": "BP_Lamp", "component_type": "PointLightComponent", "component_name": "Light", "location": [0, 0, 100]},
        ),
    ),
    ActionDef(
        id="blueprint.get_summary",
        command="get_blueprint_summary",
        tags=("blueprint", "summary", "introspect", "info", "read"),
        description="Get a comprehensive summary of a Blueprint: variables, functions, event graphs, components, parent class, compile status, and interfaces",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint (e.g. BP_Player)"},
                "asset_path": {"type": "string", "description": "Full asset path if name is ambiguous"}
            }
        },
        capabilities=("read",),
        examples=({"blueprint_name": "BP_Player"},),
    ),
    ActionDef(
        id="blueprint.describe_full",
        command="describe_blueprint_full",
        tags=("blueprint", "describe", "full", "snapshot", "introspect", "topology", "graphs", "variables", "components", "read"),
        description=(
            "Single-call comprehensive Blueprint snapshot: summary (variables, components, interfaces, compile status) "
            "+ ALL graph topologies (EventGraph, function graphs, macro graphs) with nodes, pins, edges. "
            "Replaces: 1x blueprint.get_summary + Nx graph.describe. Default compact pin serialization; "
            "use include_pin_details=true for full FEdGraphPinType."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint (e.g. BP_Player)"},
                "asset_path": {"type": "string", "description": "Full asset path if name is ambiguous"},
                "include_pin_details": {
                    "type": "boolean",
                    "description": "If true, serialize full PinType details (sub_category_object, is_array, etc.). Default: false (compact mode).",
                    "default": False
                },
                "include_function_signatures": {
                    "type": "boolean",
                    "description": "If true, inline function signatures for CallFunction nodes. Default: false.",
                    "default": False
                }
            }
        },
        capabilities=("read",),
        examples=(
            {"blueprint_name": "BP_Player"},
            {"blueprint_name": "BP_Enemy", "include_pin_details": True, "include_function_signatures": True},
        ),
    ),
    ActionDef(
        id="blueprint.create_colored_material",
        command="create_colored_material",
        tags=("material", "color", "create", "simple"),
        description="Create a simple colored material asset",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name for the material"},
                "color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B] values 0.0-1.0"},
                "path": {"type": "string", "description": "Content path (default: /Game/Materials)"}
            },
            "required": ["material_name"]
        },
        examples=({"material_name": "M_Red", "color": [1.0, 0.0, 0.0]},),
    ),
]


# =========================================================================
# Component Properties
# =========================================================================
_COMPONENT_ACTIONS = [
    ActionDef(
        id="component.set_property",
        command="set_component_property",
        tags=("component", "property", "set"),
        description="Set a property on a component in a Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "component_name": {"type": "string", "description": "Name of the component"},
                "property_name": {"type": "string", "description": "Name of the property"},
                "property_value": {"type": "string", "description": "Value to set"}
            },
            "required": ["blueprint_name", "component_name", "property_name", "property_value"]
        },
        examples=({"blueprint_name": "BP_Lamp", "component_name": "Light", "property_name": "Intensity", "property_value": "5000"},),
    ),
    ActionDef(
        id="component.set_static_mesh",
        command="set_static_mesh_properties",
        tags=("component", "mesh", "material", "static"),
        description="Set static mesh, material, and overlay material on a StaticMeshComponent",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "component_name": {"type": "string", "description": "Name of the component"},
                "static_mesh": {"type": "string", "description": "Path to static mesh asset"},
                "material": {"type": "string", "description": "Path to material asset"},
                "overlay_material": {"type": "string", "description": "Path to overlay material"}
            },
            "required": ["blueprint_name", "component_name"]
        },
        examples=({"blueprint_name": "BP_Cube", "component_name": "Mesh", "static_mesh": "/Engine/BasicShapes/Cube"},),
    ),
    ActionDef(
        id="component.set_physics",
        command="set_physics_properties",
        tags=("component", "physics", "simulate", "gravity", "mass"),
        description="Set physics properties on a component",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "component_name": {"type": "string", "description": "Name of the component"},
                "simulate_physics": {"type": "boolean", "description": "Enable physics simulation"},
                "gravity_enabled": {"type": "boolean", "description": "Enable gravity"},
                "mass": {"type": "number", "description": "Mass in kg"},
                "linear_damping": {"type": "number", "description": "Linear damping"},
                "angular_damping": {"type": "number", "description": "Angular damping"}
            },
            "required": ["blueprint_name", "component_name"]
        },
        examples=({"blueprint_name": "BP_Ball", "component_name": "Sphere", "simulate_physics": True, "mass": 10.0},),
    ),
]


# =========================================================================
# Editor / Level Actors / Viewport
# =========================================================================
_EDITOR_ACTIONS = [
    ActionDef(
        id="editor.get_actors",
        command="get_actors_in_level",
        tags=("editor", "actors", "level", "list", "read"),
        description="Get a list of all actors in the current level",
        input_schema={"type": "object", "properties": {}},
        capabilities=("read",),
    ),
    ActionDef(
        id="editor.find_actors",
        command="find_actors_by_name",
        tags=("editor", "actors", "find", "search", "read"),
        description="Find actors by name pattern",
        input_schema={
            "type": "object",
            "properties": {
                "pattern": {"type": "string", "description": "Name pattern to search for"}
            },
            "required": ["pattern"]
        },
        capabilities=("read",),
        examples=({"pattern": "Player"},),
    ),
    ActionDef(
        id="editor.spawn_actor",
        command="spawn_actor",
        tags=("editor", "actor", "spawn", "create"),
        description="Spawn a new actor in the current level",
        input_schema={
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Name for the actor"},
                "type": {"type": "string", "description": "Type (StaticMeshActor, PointLight, etc.)"},
                "location": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"},
                "rotation": {"type": "array", "items": {"type": "number"}, "description": "[pitch, yaw, roll]"}
            },
            "required": ["name", "type"]
        },
        examples=({"name": "MyLight", "type": "PointLight", "location": [0, 0, 200]},),
    ),
    ActionDef(
        id="editor.delete_actor",
        command="delete_actor",
        tags=("editor", "actor", "delete", "remove"),
        description="Delete an actor from the level by name",
        input_schema={
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Name of the actor to delete"}
            },
            "required": ["name"]
        },
        capabilities=("write", "destructive"),
        risk="moderate",
    ),
    ActionDef(
        id="editor.set_actor_transform",
        command="set_actor_transform",
        tags=("editor", "actor", "transform", "position", "move"),
        description="Set the transform (location/rotation/scale) of an actor",
        input_schema={
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Name of the actor"},
                "location": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"},
                "rotation": {"type": "array", "items": {"type": "number"}, "description": "[pitch, yaw, roll]"},
                "scale": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"}
            },
            "required": ["name"]
        },
        examples=({"name": "MyActor", "location": [100, 0, 50]},),
    ),
    ActionDef(
        id="editor.get_actor_properties",
        command="get_actor_properties",
        tags=("editor", "actor", "properties", "read"),
        description="Get all properties of an actor",
        input_schema={
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Name of the actor"}
            },
            "required": ["name"]
        },
        capabilities=("read",),
    ),
    ActionDef(
        id="editor.set_actor_property",
        command="set_actor_property",
        tags=("editor", "actor", "property", "set"),
        description="Set a property on an actor in the level",
        input_schema={
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Name of the actor"},
                "property_name": {"type": "string", "description": "Property name"},
                "property_value": {"type": "string", "description": "Value to set"}
            },
            "required": ["name", "property_name", "property_value"]
        },
    ),
    ActionDef(
        id="editor.focus_viewport",
        command="focus_viewport",
        tags=("editor", "viewport", "camera", "focus"),
        description="Focus the viewport on a specific actor or location",
        input_schema={
            "type": "object",
            "properties": {
                "target": {"type": "string", "description": "Name of actor to focus on"},
                "location": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"},
                "distance": {"type": "number", "description": "Distance from target"},
                "orientation": {"type": "array", "items": {"type": "number"}, "description": "[pitch, yaw, roll]"}
            }
        },
    ),
    ActionDef(
        id="editor.get_viewport_transform",
        command="get_viewport_transform",
        tags=("editor", "viewport", "camera", "read"),
        description="Get the current viewport camera location and rotation",
        input_schema={"type": "object", "properties": {}},
        capabilities=("read",),
    ),
    ActionDef(
        id="editor.set_viewport_transform",
        command="set_viewport_transform",
        tags=("editor", "viewport", "camera", "set"),
        description="Set the viewport camera location and/or rotation",
        input_schema={
            "type": "object",
            "properties": {
                "location": {"type": "array", "items": {"type": "number"}, "description": "[x, y, z]"},
                "rotation": {"type": "array", "items": {"type": "number"}, "description": "[pitch, yaw, roll]"}
            }
        },
    ),
    ActionDef(
        id="editor.save_all",
        command="save_all",
        tags=("editor", "save", "all"),
        description="Save all dirty packages (blueprints, levels, assets)",
        input_schema={"type": "object", "properties": {}},
    ),
    ActionDef(
        id="editor.list_assets",
        command="list_assets",
        tags=("editor", "assets", "list", "browse", "read"),
        description="List assets under a Content path with optional filtering",
        input_schema={
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Content path (e.g. /Game/Blueprints)"},
                "recursive": {"type": "boolean", "description": "Search sub-folders (default: true)"},
                "class_filter": {"type": "string", "description": "Filter by class (Blueprint, Material, Texture2D, etc.)"},
                "name_contains": {"type": "string", "description": "Filter by name substring"},
                "max_results": {"type": "integer", "description": "Max results (default: 500)"}
            },
            "required": ["path"]
        },
        capabilities=("read",),
        examples=({"path": "/Game/Blueprints", "class_filter": "Blueprint"},),
    ),
    ActionDef(
        id="editor.rename_assets",
        command="rename_assets",
        tags=("editor", "assets", "rename", "redirector", "fixup", "refactor"),
        description=(
            "Rename one or more assets and optionally fix redirectors automatically. "
            "Supports single-item params or batch items[] in one call. "
            "When allow_ui_prompts=false (default), runs non-interactive and silently auto-deletes only unreferenced redirectors."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "old_asset_path": {"type": "string", "description": "Single mode: source object path to rename (e.g. /Game/UI/WBP_Menu.WBP_Menu)"},
                "new_package_path": {"type": "string", "description": "Single mode: destination package path (e.g. /Game/UI/New)"},
                "new_name": {"type": "string", "description": "Single mode: destination asset name (without path)"},
                "items": {
                    "type": "array",
                    "description": "Batch mode: list of rename operations",
                    "items": {
                        "type": "object",
                        "properties": {
                            "old_asset_path": {"type": "string"},
                            "new_package_path": {"type": "string"},
                            "new_name": {"type": "string"},
                        },
                        "required": ["old_asset_path", "new_package_path", "new_name"],
                    },
                },
                "auto_fixup_redirectors": {"type": "boolean", "description": "Fix redirectors after rename (default: true)"},
                "allow_ui_prompts": {
                    "type": "boolean",
                    "description": "Allow UI dialogs during fixup (default: false). Keep false for unattended automation and batch runs.",
                },
                "fixup_mode": {
                    "type": "string",
                    "description": "Redirector handling mode: delete | leave | prompt (default: delete). If allow_ui_prompts=false, prompt is forced to delete.",
                    "enum": ["delete", "leave", "prompt"],
                },
                "checkout_dialog_prompt": {
                    "type": "boolean",
                    "description": "Show source-control checkout dialog during fixup (default: false). Only effective when allow_ui_prompts=true.",
                },
            },
        },
        capabilities=("write",),
        risk="moderate",
        examples=(
            {
                "old_asset_path": "/Game/P110_2/Blueprints/UI/WBP_Old.WBP_Old",
                "new_package_path": "/Game/P110_2/Blueprints/UI",
                "new_name": "WBP_New",
            },
            {
                "items": [
                    {
                        "old_asset_path": "/Game/P110_2/Blueprints/UI/WBP_A.WBP_A",
                        "new_package_path": "/Game/P110_2/Blueprints/UI",
                        "new_name": "WBP_A1",
                    },
                    {
                        "old_asset_path": "/Game/P110_2/Blueprints/UI/WBP_B.WBP_B",
                        "new_package_path": "/Game/P110_2/Blueprints/UI",
                        "new_name": "WBP_B1",
                    },
                ],
                "auto_fixup_redirectors": True,
                "allow_ui_prompts": False,
                "fixup_mode": "delete",
            },
        ),
    ),
    ActionDef(
        id="editor.get_selected_asset_thumbnail",
        command="get_selected_asset_thumbnail",
        tags=("editor", "assets", "selection", "thumbnail", "image", "base64", "read"),
        description=(
            "Get base64-encoded PNG thumbnails for selected Content Browser assets, "
            "or for explicit asset path/id inputs. Supports batch output in one call."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "asset_path": {
                    "type": "string",
                    "description": "Optional single full asset path (e.g. /Game/P110_2/Art/T_Icon.T_Icon).",
                },
                "asset_paths": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Optional multiple full asset paths.",
                },
                "asset_ids": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Optional asset id/path list (alias of asset_paths).",
                },
                "ids": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Optional id list (alias of asset_paths).",
                },
                "size": {
                    "type": "integer",
                    "description": "Thumbnail target size in pixels (default 256, clamp 1..256)",
                },
            },
        },
        capabilities=("read",),
        examples=(
            {},
            {"size": 256},
            {"asset_paths": ["/Game/P110_2/Art/T_IconA.T_IconA", "/Game/P110_2/Art/T_IconB.T_IconB"]},
            {"asset_ids": ["/Game/P110_2/Art/T_Icon.T_Icon"], "size": 256},
        ),
    ),
    ActionDef(
        id="editor.diff_against_depot",
        command="diff_against_depot",
        tags=("editor", "diff", "source-control", "svn", "revision", "compare", "debug", "read"),
        description=(
            "Diff an asset against its latest source-control (SVN/Perforce/Git) depot revision. "
            "For Blueprints: returns per-graph node-level changes (added/removed/modified/moved nodes, pin changes). "
            "For generic assets: returns property-level differences. "
            "Requires Source Control to be connected in the editor."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "asset_path": {
                    "type": "string",
                    "description": "Full asset path (e.g. /Game/P110_2/Blueprints/BP_Foo)",
                },
                "revision": {
                    "type": "integer",
                    "description": "Optional specific revision number to diff against (default: latest)",
                },
            },
            "required": ["asset_path"],
        },
        capabilities=("read",),
        risk="safe",
        examples=(
            {"asset_path": "/Game/P110_2/Blueprints/Character/Player/BP_SideScrollingCharacter"},
            {"asset_path": "/Game/P110_2/Blueprints/BP_SideScrollingGameMode", "revision": 42},
        ),
    ),
    ActionDef(
        id="editor.get_logs",
        command="get_editor_logs",
        tags=("editor", "logs", "debug", "read"),
        description="Retrieve recent UE editor log entries from the in-memory ring buffer. Supports filtering by category and verbosity.",
        input_schema={
            "type": "object",
            "properties": {
                "count": {"type": "integer", "description": "Number of entries to return (default: 50, max: 500)"},
                "category": {"type": "string", "description": "Filter by log category (e.g. LogMCP, LogBlueprint)"},
                "min_verbosity": {"type": "string", "description": "Minimum verbosity: Fatal, Error, Warning, Display, Log, Verbose, VeryVerbose"}
            }
        },
        capabilities=("read",),
        examples=(
            {},
            {"count": 100, "category": "LogMCP"},
            {"count": 20, "min_verbosity": "Warning"},
        ),
    ),
    ActionDef(
        id="editor.is_ready",
        command="is_ready",
        tags=("editor", "status", "health", "ready", "read"),
        description="Check if the UE editor is fully initialized and ready for commands. Returns readiness of editor, world, and asset registry.",
        input_schema={"type": "object", "properties": {}},
        capabilities=("read",),
        risk="safe",
        examples=({},),
    ),
    ActionDef(
        id="editor.request_shutdown",
        command="request_shutdown",
        tags=("editor", "shutdown", "exit", "close"),
        description="Request the editor to shut down. Use force=true to skip save dialogs and exit immediately.",
        input_schema={
            "type": "object",
            "properties": {
                "force": {"type": "boolean", "description": "Force immediate exit without save prompts (default: false)"}
            }
        },
        risk="destructive",
        examples=(
            {},
            {"force": True},
        ),
    ),
    # =========================================================================
    # P6: PIE Control Actions
    # =========================================================================
    ActionDef(
        id="editor.start_pie",
        command="start_pie",
        tags=("editor", "pie", "play", "start", "test", "run"),
        description=(
            "Start a Play In Editor (PIE) session. "
            "Mode: SelectedViewport (default), NewWindow, or Simulate. "
            "PIE starts asynchronously; use editor.get_pie_state to poll readiness."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "mode": {
                    "type": "string",
                    "enum": ["SelectedViewport", "NewWindow", "Simulate"],
                    "description": "PIE launch mode (default: SelectedViewport)",
                },
            },
        },
        risk="moderate",
        examples=(
            {},
            {"mode": "SelectedViewport"},
            {"mode": "NewWindow"},
            {"mode": "Simulate"},
        ),
    ),
    ActionDef(
        id="editor.stop_pie",
        command="stop_pie",
        tags=("editor", "pie", "stop", "end", "test"),
        description="Stop the current PIE session without closing the editor. Safe to call when no session is running.",
        input_schema={"type": "object", "properties": {}},
        risk="safe",
        examples=({},),
    ),
    ActionDef(
        id="editor.get_pie_state",
        command="get_pie_state",
        tags=("editor", "pie", "state", "status", "query", "read"),
        description=(
            "Query the current PIE session state. Returns Running/Stopped, world name, "
            "pause state, and whether simulation mode is active."
        ),
        input_schema={"type": "object", "properties": {}},
        capabilities=("read",),
        risk="safe",
        examples=({},),
    ),
    # =========================================================================
    # P6: Log Enhancement Actions
    # =========================================================================
    ActionDef(
        id="editor.clear_logs",
        command="clear_logs",
        tags=("editor", "logs", "clear", "reset", "session"),
        description=(
            "Clear the MCP log capture ring buffer. Optionally insert a session tag marker "
            "before clearing for session segmentation. Returns the previous cursor for reference."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "tag": {
                    "type": "string",
                    "description": "Optional session tag to insert before clearing (e.g. 'test_run_01')",
                },
            },
        },
        risk="moderate",
        examples=(
            {},
            {"tag": "test_run_01"},
        ),
    ),
    ActionDef(
        id="editor.assert_log",
        command="assert_log",
        tags=("editor", "logs", "assert", "test", "validate", "verify", "pass", "fail"),
        description=(
            "Assertion-based log validation. Check log entries for keyword occurrences "
            "and return pass/fail for each assertion. Supports comparison operators "
            "(==, >=, <=, >, <) and optional cursor-based range filtering."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "assertions": {
                    "type": "array",
                    "description": "List of assertions to check",
                    "items": {
                        "type": "object",
                        "properties": {
                            "keyword": {"type": "string", "description": "Substring to search for in log messages"},
                            "expected_count": {"type": "integer", "description": "Expected number of occurrences"},
                            "comparison": {
                                "type": "string",
                                "enum": ["==", ">=", "<=", ">", "<"],
                                "description": "Comparison operator (default: >=)",
                            },
                            "category": {"type": "string", "description": "Optional log category filter"},
                        },
                        "required": ["keyword", "expected_count"],
                    },
                },
                "since_cursor": {
                    "type": "string",
                    "description": "Only check logs after this cursor (live:<seq> format)",
                },
            },
            "required": ["assertions"],
        },
        capabilities=("read",),
        risk="safe",
        examples=(
            {
                "assertions": [
                    {"keyword": "HelloWorld", "expected_count": 1, "comparison": ">="},
                    {"keyword": "ForLoop", "expected_count": 3, "comparison": "=="},
                ]
            },
            {
                "assertions": [{"keyword": "Error", "expected_count": 0, "comparison": "=="}],
                "since_cursor": "live:100",
            },
        ),
    ),
    # =========================================================================
    # P6: Outliner Management Actions
    # =========================================================================
    ActionDef(
        id="editor.rename_actor_label",
        command="rename_actor_label",
        tags=("editor", "actor", "rename", "label", "outliner"),
        description=(
            "Rename an actor's display label in the World Outliner. "
            "Supports single item (actor_name + new_label) or batch (items[])."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "actor_name": {"type": "string", "description": "Current actor name or label"},
                "new_label": {"type": "string", "description": "New display label"},
                "items": {
                    "type": "array",
                    "description": "Batch rename operations",
                    "items": {
                        "type": "object",
                        "properties": {
                            "actor_name": {"type": "string"},
                            "new_label": {"type": "string"},
                        },
                        "required": ["actor_name", "new_label"],
                    },
                },
            },
        },
        examples=(
            {"actor_name": "BP_Fly_C_0", "new_label": "Flying Enemy Alpha"},
            {
                "items": [
                    {"actor_name": "BP_Fly_C_0", "new_label": "Flyer 01"},
                    {"actor_name": "BP_Fly_C_1", "new_label": "Flyer 02"},
                ]
            },
        ),
    ),
    ActionDef(
        id="editor.set_actor_folder",
        command="set_actor_folder",
        tags=("editor", "actor", "folder", "outliner", "organize", "move"),
        description=(
            "Move actors into Outliner folders (auto-creates folders). "
            "Supports single item or batch items[]. Use empty folder_path to unfolder."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "actor_name": {"type": "string", "description": "Actor name or label"},
                "folder_path": {"type": "string", "description": "Target folder path (e.g. 'Enemies/Flying')"},
                "items": {
                    "type": "array",
                    "description": "Batch folder operations",
                    "items": {
                        "type": "object",
                        "properties": {
                            "actor_name": {"type": "string"},
                            "folder_path": {"type": "string"},
                        },
                        "required": ["actor_name", "folder_path"],
                    },
                },
            },
        },
        examples=(
            {"actor_name": "BP_Fly_C_0", "folder_path": "Enemies/Flying"},
            {
                "items": [
                    {"actor_name": "BP_Fly_C_0", "folder_path": "Enemies/Flying"},
                    {"actor_name": "BP_SideScrolling_NPC_C_0", "folder_path": "Enemies/Ground"},
                ]
            },
        ),
    ),
    ActionDef(
        id="editor.select_actors",
        command="select_actors",
        tags=("editor", "actor", "select", "selection", "outliner"),
        description=(
            "Select or deselect actors in the editor. "
            "Mode: set (replace selection), add, remove, toggle."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "actor_names": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Actor names or labels to select",
                },
                "mode": {
                    "type": "string",
                    "enum": ["set", "add", "remove", "toggle"],
                    "description": "Selection mode (default: set)",
                },
            },
            "required": ["actor_names"],
        },
        capabilities=("read",),
        risk="safe",
        examples=(
            {"actor_names": ["BP_Fly_C_0", "BP_Fly_C_1"]},
            {"actor_names": ["PlayerStart0"], "mode": "add"},
        ),
    ),
    ActionDef(
        id="editor.get_outliner_tree",
        command="get_outliner_tree",
        tags=("editor", "outliner", "tree", "hierarchy", "folder", "actors", "read"),
        description=(
            "Get the World Outliner actor hierarchy organized by folders. "
            "Returns folders with their actors and unfoldered actors separately. "
            "Supports class and folder prefix filtering."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "class_filter": {"type": "string", "description": "Filter actors by class name substring"},
                "folder_filter": {"type": "string", "description": "Filter by folder path prefix"},
            },
        },
        capabilities=("read",),
        risk="safe",
        examples=(
            {},
            {"class_filter": "BP_Fly"},
            {"folder_filter": "Enemies"},
        ),
    ),
    # =========================================================================
    # P7: Asset Editor Actions
    # =========================================================================
    ActionDef(
        id="editor.open_asset_editor",
        command="open_asset_editor",
        tags=("editor", "asset", "open", "focus", "window", "tab"),
        description=(
            "Open the asset editor for a given asset and optionally bring it to focus. "
            "Works for Blueprints, Materials, Widget Blueprints, Data Assets, etc."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "asset_path": {
                    "type": "string",
                    "description": "Full content path of the asset, e.g. '/Game/Characters/BP_Hero'",
                },
                "focus": {
                    "type": "boolean",
                    "description": "Whether to focus the editor window after opening (default: true)",
                },
            },
            "required": ["asset_path"],
        },
        capabilities=("write",),
        risk="safe",
        examples=(
            {"asset_path": "/Game/Characters/BP_Hero"},
            {"asset_path": "/Game/Materials/M_Base", "focus": False},
        ),
    ),
    ActionDef(
        id="batch.execute",
        command="batch_execute",
        tags=("batch", "execute", "multi", "pipeline"),
        description="Execute multiple actions in a single TCP round-trip. Non-atomic: already-succeeded commands are NOT rolled back on failure.",
        input_schema={
            "type": "object",
            "properties": {
                "commands": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "type": {"type": "string", "description": "Action command name"},
                            "params": {"type": "object", "description": "Action parameters"}
                        },
                        "required": ["type"]
                    },
                    "description": "Array of commands to execute sequentially"
                },
                "stop_on_error": {"type": "boolean", "description": "Stop on first error (default: true)"}
            },
            "required": ["commands"]
        },
        risk="moderate",
        examples=(
            {"commands": [{"type": "compile_blueprint", "params": {"blueprint_name": "BP_Player"}}]},
            {"commands": [
                {"type": "create_blueprint", "params": {"name": "BP_Test", "parent_class": "Actor"}},
                {"type": "compile_blueprint", "params": {"blueprint_name": "BP_Test"}},
            ], "stop_on_error": True},
        ),
    ),
]


# =========================================================================
# Layout
# =========================================================================
_LAYOUT_ACTIONS = [
    ActionDef(
        id="layout.auto_selected",
        command="auto_layout_selected",
        tags=("layout", "auto", "arrange", "nodes", "graph"),
        description="Auto-layout selected nodes or all nodes in a graph/blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "mode": {"type": "string", "enum": ["selected", "graph", "all"], "description": "Layout granularity"},
                "blueprint_name": {"type": "string", "description": "Blueprint name (optional)"},
                "graph_name": {"type": "string", "description": "Graph name (optional)"},
                "node_ids": {"type": "array", "items": {"type": "string"}, "description": "Node GUIDs for 'selected' mode"},
                "layer_spacing": {"type": "number", "description": "Horizontal spacing (>0 fixed px, <=0 auto width-aware; default: 0)"},
                "row_spacing": {"type": "number", "description": "Vertical spacing (>0 fixed px, <=0 auto height-aware; default: 0)"},
                "horizontal_gap": {"type": "number", "description": "Gap between layers in auto mode (default: 250)"},
                "vertical_gap": {"type": "number", "description": "Gap between rows in auto mode (default: 100)"},
                "crossing_passes": {"type": "integer", "description": "Barycenter optimization passes (default: 4)"},
                "pin_align_pure": {"type": "boolean", "description": "Align pure nodes to consumer input pin Y (default: true)"},
                "avoid_surrounding": {"type": "boolean", "description": "Avoid non-participating nodes in the same graph (default: false)"},
                "include_pure_deps": {"type": "boolean", "description": "Auto-include pure dependency nodes in selected mode (default: false)"},
                "surrounding_margin": {"type": "number", "description": "Obstacle margin when avoid_surrounding is enabled (default: 60)"},
                "preserve_comments": {"type": "boolean", "description": "Resize comment boxes to fit moved child nodes (default: true)"}
            }
        },
        examples=({"mode": "graph", "blueprint_name": "BP_Player"},),
    ),
    ActionDef(
        id="layout.auto_subtree",
        command="auto_layout_subtree",
        tags=("layout", "auto", "subtree", "arrange", "nodes"),
        description="Auto-layout an exec subtree starting from a root node",
        input_schema={
            "type": "object",
            "properties": {
                "root_node_id": {"type": "string", "description": "GUID of the root node"},
                "blueprint_name": {"type": "string", "description": "Blueprint name (optional)"},
                "graph_name": {"type": "string", "description": "Graph name (optional)"},
                "max_pure_depth": {"type": "integer", "description": "Max pure dependency depth (default: 3)"},
                "layer_spacing": {"type": "number", "description": "Horizontal spacing (>0 fixed px, <=0 auto width-aware; default: 0)"},
                "row_spacing": {"type": "number", "description": "Vertical spacing (>0 fixed px, <=0 auto height-aware; default: 0)"},
                "horizontal_gap": {"type": "number", "description": "Gap between layers in auto mode (default: 250)"},
                "vertical_gap": {"type": "number", "description": "Gap between rows in auto mode (default: 100)"},
                "crossing_passes": {"type": "integer", "description": "Barycenter optimization passes (default: 4)"},
                "pin_align_pure": {"type": "boolean", "description": "Align pure nodes to consumer input pin Y (default: true)"},
                "avoid_surrounding": {"type": "boolean", "description": "Avoid non-participating nodes in the same graph (default: false)"},
                "surrounding_margin": {"type": "number", "description": "Obstacle margin when avoid_surrounding is enabled (default: 60)"},
                "preserve_comments": {"type": "boolean", "description": "Resize comment boxes to fit moved child nodes (default: true)"}
            }
        },
    ),
    ActionDef(
        id="layout.auto_blueprint",
        command="auto_layout_blueprint",
        tags=("layout", "auto", "blueprint", "all", "graphs", "arrange"),
        description="Auto-layout ALL graphs in a Blueprint (EventGraph, Functions, Macros)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Blueprint name (optional, defaults to focused editor)"},
                "layer_spacing": {"type": "number", "description": "Horizontal spacing (0=auto, default: auto)"},
                "row_spacing": {"type": "number", "description": "Vertical spacing (0=auto, default: auto)"},
                "horizontal_gap": {"type": "number", "description": "Gap between layers in auto mode (default: 250)"},
                "vertical_gap": {"type": "number", "description": "Gap between rows in auto mode (default: 100)"},
                "crossing_passes": {"type": "integer", "description": "Barycenter optimization passes (default: 4)"},
                "pin_align_pure": {"type": "boolean", "description": "Align pure nodes with consumer pin Y (default: true)"},
                "preserve_comments": {"type": "boolean", "description": "Resize comment boxes to fit children (default: true)"}
            }
        },
        examples=({"blueprint_name": "BP_Player"},),
    ),
    ActionDef(
        id="layout.layout_and_comment",
        command="layout_and_comment",
        tags=("layout", "comment", "auto", "group", "arrange", "non-overlap", "batch"),
        description=(
            "Combined action: auto-layout nodes then create non-overlapping comment boxes "
            "for each functional group. Solves the comment-overlap problem by inserting "
            "inter-group spacing BEFORE placing comments. Each group specifies its node_ids "
            "and comment_text; groups are separated vertically so comments never overlap."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Blueprint name (optional, defaults to focused editor)"},
                "graph_name": {"type": "string", "description": "Graph name (optional, defaults to focused graph)"},
                "groups": {
                    "type": "array",
                    "description": "Array of group objects, each defining a set of nodes and its comment",
                    "items": {
                        "type": "object",
                        "properties": {
                            "node_ids": {"type": "array", "items": {"type": "string"}, "description": "Node GUIDs belonging to this group"},
                            "comment_text": {"type": "string", "description": "Comment text for the group"},
                            "color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] 0.0-1.0"}
                        },
                        "required": ["node_ids", "comment_text"]
                    }
                },
                "group_spacing": {"type": "number", "description": "Min Y gap between group AABBs in px (default: 80)"},
                "auto_layout": {"type": "boolean", "description": "Run auto-layout before commenting (default: true)"},
                "clear_existing": {"type": "boolean", "description": "Remove all existing comments first (default: false)"},
                "padding": {"type": "number", "description": "Comment box padding in px (default: 40)"},
                "title_height": {"type": "number", "description": "Comment title height in px (default: 36)"},
                "layer_spacing": {"type": "number", "description": "Horizontal spacing forwarded to auto-layout (default: 0=auto)"},
                "row_spacing": {"type": "number", "description": "Vertical spacing forwarded to auto-layout (default: 0=auto)"},
                "crossing_passes": {"type": "integer", "description": "Barycenter passes forwarded to auto-layout (default: 4)"}
            },
            "required": ["groups"]
        },
        examples=(
            {
                "blueprint_name": "BP_Player",
                "groups": [
                    {"node_ids": ["GUID1", "GUID2"], "comment_text": "初始化逻辑", "color": [0.15, 0.55, 0.25, 1]},
                    {"node_ids": ["GUID3", "GUID4", "GUID5"], "comment_text": "移动处理", "color": [0.15, 0.35, 0.65, 1]}
                ]
            },
        ),
    ),
]


# =========================================================================
# Node — Events
# =========================================================================
_NODE_EVENT_ACTIONS = [
    ActionDef(
        id="node.add_event",
        command="add_blueprint_event_node",
        tags=("node", "event", "beginplay", "tick", "graph"),
        description="Add an event node (ReceiveBeginPlay, ReceiveTick, etc.) to a Blueprint event graph",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "event_name": {"type": "string", "description": "Event name (ReceiveBeginPlay, ReceiveTick, etc.)"},
                "node_position": {"type": "string", "description": "[X, Y] position"}
            },
            "required": ["blueprint_name", "event_name"]
        },
        examples=({"blueprint_name": "BP_Player", "event_name": "ReceiveBeginPlay"},),
    ),
    ActionDef(
        id="node.add_custom_event",
        command="add_blueprint_custom_event",
        tags=("node", "event", "custom", "delegate"),
        description="Add a Custom Event node with optional parameters",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "event_name": {"type": "string", "description": "Name for the custom event"},
                "node_position": {"type": "string", "description": "[X, Y] position"},
                "parameters": {"type": "array", "items": {"type": "object"}, "description": "Parameters with 'name' and 'type' keys"}
            },
            "required": ["blueprint_name", "event_name"]
        },
        examples=({"blueprint_name": "BP_Player", "event_name": "OnDamaged", "parameters": [{"name": "Damage", "type": "Float"}]},),
    ),
    ActionDef(
        id="node.add_custom_event_for_delegate",
        command="add_custom_event_for_delegate",
        tags=("node", "event", "custom", "delegate", "signature", "bind"),
        description=(
            "Add a Custom Event node whose signature automatically matches a delegate. "
            "Supports two modes: (1) Class property mode: delegate_class + delegate_name, "
            "(2) Node pin mode: source_node_id + source_pin_name. "
            "The resulting event's pins are locked to the delegate signature and can be "
            "directly connected to the delegate pin."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "event_name": {"type": "string", "description": "Name for the custom event"},
                "delegate_class": {
                    "type": "string",
                    "description": "Class that owns the delegate (e.g., 'PrimitiveComponent', 'Actor'). Used with delegate_name."
                },
                "delegate_name": {
                    "type": "string",
                    "description": "Delegate property name (e.g., 'OnComponentBeginOverlap'). If delegate_class is omitted, searches the Blueprint's own class."
                },
                "source_node_id": {
                    "type": "string",
                    "description": "Node GUID to resolve delegate signature from and optionally connect to"
                },
                "source_pin_name": {
                    "type": "string",
                    "description": "Pin name on source node (defaults to first unconnected delegate input pin)"
                },
                "auto_connect": {
                    "type": "boolean",
                    "description": "Connect delegate output to source pin (default: true, only for source_node_id mode)"
                },
                "node_position": {"type": "string", "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional target graph name"}
            },
            "required": ["blueprint_name", "event_name"]
        },
        examples=(
            {
                "blueprint_name": "BP_Player",
                "event_name": "OnOverlap",
                "delegate_class": "PrimitiveComponent",
                "delegate_name": "OnComponentBeginOverlap"
            },
            {
                "blueprint_name": "BP_Player",
                "event_name": "OnMyDispatcher",
                "delegate_name": "MyEventDispatcher"
            },
            {
                "blueprint_name": "BP_Player",
                "event_name": "OnDelegateEvent",
                "source_node_id": "<node-guid>",
                "source_pin_name": "Event"
            },
        ),
    ),
    ActionDef(
        id="node.add_input_action",
        command="add_blueprint_input_action_node",
        tags=("node", "input", "action", "legacy"),
        description="Add an input action event node (legacy input system)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "action_name": {"type": "string", "description": "Name of the input action"},
                "node_position": {"type": "string", "description": "[X, Y] position"}
            },
            "required": ["blueprint_name", "action_name"]
        },
    ),
    ActionDef(
        id="node.add_enhanced_input_action",
        command="add_enhanced_input_action_node",
        tags=("node", "input", "enhanced", "action", "started", "triggered"),
        description="Add an Enhanced Input Action event node with Started/Triggered/Completed exec pins",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "action_name": {"type": "string", "description": "Name of the Input Action asset (e.g., IA_Move)"},
                "action_path": {"type": "string", "description": "Content path to asset (default: /Game/Input)"},
                "node_position": {"type": "string", "description": "[X, Y] position"}
            },
            "required": ["blueprint_name", "action_name"]
        },
        examples=({"blueprint_name": "BP_Player", "action_name": "IA_Move"},),
    ),
]


# =========================================================================
# Node — Event Dispatchers
# =========================================================================
_NODE_DISPATCHER_ACTIONS = [
    ActionDef(
        id="dispatcher.create",
        command="add_event_dispatcher",
        tags=("dispatcher", "event", "multicast", "delegate", "create"),
        description="Add an Event Dispatcher (multicast delegate) to a Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "dispatcher_name": {"type": "string", "description": "Name for the dispatcher"},
                "parameters": {"type": "array", "items": {"type": "object"}, "description": "Parameters with 'name' and 'type' keys"}
            },
            "required": ["blueprint_name", "dispatcher_name"]
        },
        examples=({"blueprint_name": "BP_Door", "dispatcher_name": "OnDoorOpened"},),
    ),
    ActionDef(
        id="dispatcher.call",
        command="call_event_dispatcher",
        tags=("dispatcher", "event", "call", "broadcast"),
        description="Add a Call node for an Event Dispatcher (broadcasts the event)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "dispatcher_name": {"type": "string", "description": "Name of the dispatcher"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "dispatcher_name"]
        },
    ),
    ActionDef(
        id="dispatcher.bind",
        command="bind_event_dispatcher",
        tags=("dispatcher", "event", "bind", "listen"),
        description="Add a Bind node for an Event Dispatcher (creates bind node + matching custom event)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Blueprint where to add the bind node"},
                "dispatcher_name": {"type": "string", "description": "Name of the dispatcher to bind"},
                "target_blueprint": {"type": "string", "description": "Blueprint that owns the dispatcher"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "dispatcher_name"]
        },
    ),
    ActionDef(
        id="dispatcher.create_event",
        command="create_event_delegate",
        tags=("dispatcher", "delegate", "event", "create", "function", "bind"),
        description="Create a 'Create Event' (K2Node_CreateDelegate) node that binds a function to a delegate pin. Works inside function graphs where CustomEvent is unavailable.",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "function_name": {"type": "string", "description": "Name of the function to bind as delegate"},
                "connect_to_node_id": {"type": "string", "description": "Node GUID to auto-connect delegate output to"},
                "connect_to_pin": {"type": "string", "description": "Target delegate pin name (default: 'Event')"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional function graph name"}
            },
            "required": ["blueprint_name", "function_name"]
        },
        examples=({"blueprint_name": "BP_Player", "function_name": "OnTTSEnvelope", "graph_name": "SetupTTS"},),
    ),
    ActionDef(
        id="component.bind_event",
        command="bind_component_event",
        tags=("component", "delegate", "event", "bind", "actor", "signal"),
        description=(
            "Bind a component delegate event (e.g., OnComponentBeginOverlap, OnTTSEnvelope). "
            "Creates a UK2Node_ComponentBoundEvent in the Blueprint event graph. "
            "The component must exist as a UPROPERTY on the Blueprint's GeneratedClass "
            "(added via SCS in editor or declared in C++ parent)."
        ),
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "component_name": {"type": "string", "description": "Component variable name on the Blueprint (e.g., 'AIPortComponent', 'CapsuleComponent')"},
                "event_name": {"type": "string", "description": "Delegate name on the component class (e.g., 'OnTTSEnvelope', 'OnComponentBeginOverlap')"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name (defaults to EventGraph)"}
            },
            "required": ["blueprint_name", "component_name", "event_name"]
        },
        examples=(
            {"blueprint_name": "BP_SideScrollingCharacter", "component_name": "AIPortComponent", "event_name": "OnTTSEnvelope"},
            {"blueprint_name": "BP_Player", "component_name": "CapsuleComponent", "event_name": "OnComponentBeginOverlap"},
        ),
    ),
]


# =========================================================================
# Node — Functions
# =========================================================================
_NODE_FUNCTION_ACTIONS = [
    ActionDef(
        id="node.add_function_call",
        command="add_blueprint_function_node",
        tags=("node", "function", "call", "method"),
        description="Add a function call node to a Blueprint graph",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "target": {"type": "string", "description": "Target object (component name or self)"},
                "function_name": {"type": "string", "description": "Name of the function to call"},
                "params": {"type": "string", "description": "Parameters as JSON string"},
                "node_position": {"type": "string", "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "target", "function_name"]
        },
        examples=({"blueprint_name": "BP_Player", "target": "self", "function_name": "PrintString"},),
    ),
    ActionDef(
        id="node.add_spawn_actor",
        command="add_spawn_actor_from_class_node",
        tags=("node", "spawn", "actor", "class"),
        description="Add a SpawnActorFromClass node for runtime actor spawning",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "class_to_spawn": {"type": "string", "description": "Class to spawn (e.g., BP_Enemy)"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "class_to_spawn"]
        },
    ),
    ActionDef(
        id="node.set_pin_default",
        command="set_node_pin_default",
        tags=("node", "pin", "default", "value", "set"),
        description="Set the default value of a pin on an existing node",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "node_id": {"type": "string", "description": "GUID of the node"},
                "pin_name": {"type": "string", "description": "Name of the pin"},
                "default_value": {"type": "string", "description": "Default value as string"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "node_id", "pin_name", "default_value"]
        },
    ),
    ActionDef(
        id="node.set_object_property",
        command="set_object_property",
        tags=("node", "property", "set", "external", "object"),
        description="Create a Set Property node for an external object (e.g., bShowMouseCursor on PlayerController)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "owner_class": {"type": "string", "description": "Class that owns the property"},
                "property_name": {"type": "string", "description": "Property to set"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "owner_class", "property_name"]
        },
    ),
    ActionDef(
        id="node.add_get_subsystem",
        command="add_blueprint_get_subsystem_node",
        tags=("node", "subsystem", "get", "reference"),
        description="Add a GetSubsystem node to get a reference to a subsystem",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "subsystem_class": {"type": "string", "description": "Class name of the subsystem"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "subsystem_class"]
        },
    ),
]


# =========================================================================
# Node — Variables
# =========================================================================
_NODE_VARIABLE_ACTIONS = [
    ActionDef(
        id="variable.create",
        command="add_blueprint_variable",
        tags=("variable", "create", "add", "member"),
        description="Add a member variable to a Blueprint (supports Boolean, Int, Float, Double, String, Vector, Rotator, Transform, and UObject types)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "variable_name": {"type": "string", "description": "Name of the variable"},
                "variable_type": {"type": "string", "description": "Type (Boolean, Integer, Float, Vector, String, Rotator, Transform, etc.)"},
                "is_exposed": {"type": "boolean", "description": "Expose to editor"}
            },
            "required": ["blueprint_name", "variable_name", "variable_type"]
        },
        examples=({"blueprint_name": "BP_Player", "variable_name": "Health", "variable_type": "Float"},),
    ),
    ActionDef(
        id="variable.add_getter",
        command="add_blueprint_variable_get",
        tags=("variable", "get", "getter", "node"),
        description="Add a Variable Get node to a Blueprint graph",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "variable_name": {"type": "string", "description": "Name of the variable"},
                "node_position": {"type": "string", "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "variable_name"]
        },
    ),
    ActionDef(
        id="variable.add_setter",
        command="add_blueprint_variable_set",
        tags=("variable", "set", "setter", "node"),
        description="Add a Variable Set node to a Blueprint graph",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "variable_name": {"type": "string", "description": "Name of the variable"},
                "node_position": {"type": "string", "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "variable_name"]
        },
    ),
    ActionDef(
        id="variable.add_local",
        command="add_function_local_variable",
        tags=("variable", "local", "function", "add"),
        description="Add a local variable to a Blueprint function",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "function_name": {"type": "string", "description": "Function name"},
                "variable_name": {"type": "string", "description": "Variable name"},
                "variable_type": {"type": "string", "description": "Type (Boolean, Integer, Float, Vector, etc.)"},
                "default_value": {"type": "string", "description": "Optional default value"}
            },
            "required": ["blueprint_name", "function_name", "variable_name", "variable_type"]
        },
    ),
]


# =========================================================================
# Node — References
# =========================================================================
_NODE_REFERENCE_ACTIONS = [
    ActionDef(
        id="node.add_self_reference",
        command="add_blueprint_self_reference",
        tags=("node", "self", "reference", "actor"),
        description="Add a 'Get Self' node that returns a reference to this actor",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "node_position": {"type": "string", "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name"]
        },
    ),
    ActionDef(
        id="node.add_component_reference",
        command="add_blueprint_get_self_component_reference",
        tags=("node", "component", "reference", "get"),
        description="Add a node that gets a reference to a component owned by the Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "component_name": {"type": "string", "description": "Name of the component"},
                "node_position": {"type": "string", "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "component_name"]
        },
    ),
    ActionDef(
        id="node.add_cast",
        command="add_blueprint_cast_node",
        tags=("node", "cast", "type", "convert"),
        description="Add a Cast node for type casting (e.g., GameStateBase to BP_MyGameState)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "target_class": {"type": "string", "description": "Class to cast to"},
                "pure_cast": {"type": "boolean", "description": "Create as pure cast (no exec pins)"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"}
            },
            "required": ["blueprint_name", "target_class"]
        },
    ),
]


# =========================================================================
# Node — Flow Control
# =========================================================================
_NODE_FLOW_ACTIONS = [
    ActionDef(
        id="node.add_branch",
        command="add_blueprint_branch_node",
        tags=("node", "branch", "if", "condition", "flow"),
        description="Add a Branch (If/Then/Else) node",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "graph_name": {"type": "string", "description": "Optional graph name"},
                "node_position": {"type": "string", "description": "[X, Y] position"}
            },
            "required": ["blueprint_name"]
        },
    ),
    ActionDef(
        id="node.add_macro",
        command="add_macro_instance_node",
        tags=("node", "macro", "loop", "foreach", "forloop", "whileloop", "doonce", "gate", "flow"),
        description="Add a macro instance node (ForEachLoop, ForLoop, WhileLoop, DoOnce, Gate, etc.)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "macro_name": {"type": "string", "description": "Macro name (ForEachLoop, ForLoop, WhileLoop, DoOnce, Gate, etc.)"},
                "graph_name": {"type": "string", "description": "Optional graph name"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"}
            },
            "required": ["blueprint_name", "macro_name"]
        },
        examples=({"blueprint_name": "BP_Player", "macro_name": "ForEachLoop"},),
    ),
    ActionDef(
        id="node.add_sequence",
        command="add_sequence_node",
        tags=("node", "sequence", "flow", "control", "exec"),
        description="Add an Execution Sequence node (native K2 node with multiple Then outputs)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "graph_name": {"type": "string", "description": "Optional graph name"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"}
            },
            "required": ["blueprint_name"]
        },
        examples=({"blueprint_name": "BP_Player"},),
    ),
]


# =========================================================================
# Graph Operations
# =========================================================================
_GRAPH_ACTIONS = [
    ActionDef(
        id="graph.connect_nodes",
        command="connect_blueprint_nodes",
        tags=("graph", "connect", "wire", "link", "pin"),
        description="Connect two nodes in a Blueprint graph",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "source_node_id": {"type": "string", "description": "GUID of the source node"},
                "source_pin": {"type": "string", "description": "Name of the output pin"},
                "target_node_id": {"type": "string", "description": "GUID of the target node"},
                "target_pin": {"type": "string", "description": "Name of the input pin"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "source_node_id", "source_pin", "target_node_id", "target_pin"]
        },
    ),
    ActionDef(
        id="graph.find_nodes",
        command="find_blueprint_nodes",
        tags=("graph", "find", "search", "nodes", "read"),
        description="Find nodes in a Blueprint graph by type or event type",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "graph_name": {"type": "string", "description": "Optional graph name"},
                "node_type": {"type": "string", "description": "Type of node (Event, Function, Variable, etc.)"},
                "event_type": {"type": "string", "description": "Specific event type (BeginPlay, Tick, etc.)"}
            },
            "required": ["blueprint_name"]
        },
        capabilities=("read",),
    ),
    ActionDef(
        id="graph.delete_node",
        command="delete_blueprint_node",
        tags=("graph", "delete", "remove", "node"),
        description="Delete a node from a Blueprint graph",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "node_id": {"type": "string", "description": "GUID of the node to delete"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "node_id"]
        },
        capabilities=("write", "destructive"),
        risk="moderate",
    ),
    ActionDef(
        id="graph.get_node_pins",
        command="get_node_pins",
        tags=("graph", "node", "pins", "debug", "read"),
        description="Get all pins on a node for debugging connection issues",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "node_id": {"type": "string", "description": "GUID of the node"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "node_id"]
        },
        capabilities=("read",),
    ),
    ActionDef(
        id="graph.get_selected_nodes",
        command="get_selected_nodes",
        tags=("graph", "selected", "nodes", "editor", "read", "introspect"),
        description="Get information about the currently selected nodes in the focused Blueprint graph editor. Returns node GUID/class/title/position/pins/edges for each selected node. Falls back to the active editor when blueprint_name is omitted.",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Optional Blueprint name — if omitted, uses the currently focused editor"},
                "graph_name": {"type": "string", "description": "Optional graph name — if omitted, uses the focused graph"},
                "include_hidden_pins": {"type": "boolean", "description": "Include hidden pins (default: false)"}
            }
        },
        capabilities=("read",),
        examples=(
            {},
            {"blueprint_name": "BP_Player"},
            {"blueprint_name": "BP_Player", "graph_name": "EventGraph", "include_hidden_pins": True},
        ),
    ),
    ActionDef(
        id="graph.collapse_selection_to_function",
        command="collapse_selection_to_function",
        tags=("graph", "collapse", "selection", "function", "refactor", "editor"),
        description="Collapse currently selected nodes in the focused Blueprint graph into a new function using Unreal's native collapse-to-function flow.",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Optional Blueprint name/path to target a specific open Blueprint editor"}
            }
        },
        capabilities=("write",),
        risk="moderate",
        examples=(
            {},
            {"blueprint_name": "BP_Player"},
        ),
    ),
    ActionDef(
        id="graph.collapse_selection_to_macro",
        command="collapse_selection_to_macro",
        tags=("graph", "collapse", "selection", "macro", "refactor", "editor"),
        description="Collapse currently selected nodes in the focused Blueprint graph into a new macro using Unreal's native collapse-to-macro flow. Macros support latent nodes (e.g. Delay). Not supported in AnimGraphs.",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Optional Blueprint name/path to target a specific open Blueprint editor"}
            }
        },
        capabilities=("write",),
        risk="moderate",
        examples=(
            {},
            {"blueprint_name": "BP_Player"},
        ),
    ),
    ActionDef(
        id="graph.set_selected_nodes",
        command="set_selected_nodes",
        tags=("graph", "select", "selection", "nodes", "editor", "write"),
        description="Programmatically set the selection in the focused Blueprint graph editor by providing an array of node GUIDs. Clears the previous selection and selects only the specified nodes. Use 'append' to add to existing selection instead of replacing it.",
        input_schema={
            "type": "object",
            "properties": {
                "node_ids": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Array of node GUID strings to select"
                },
                "blueprint_name": {"type": "string", "description": "Optional Blueprint name — if omitted, uses the currently focused editor"},
                "graph_name": {"type": "string", "description": "Optional graph name — if omitted, uses the focused graph"},
                "append": {"type": "boolean", "description": "If true, add to existing selection instead of replacing it (default: false)"}
            },
            "required": ["node_ids"]
        },
        capabilities=("write",),
        risk="safe",
        examples=(
            {"node_ids": ["A1B2C3D4-E5F6-7890-ABCD-EF1234567890"]},
            {"node_ids": ["GUID1", "GUID2", "GUID3"], "blueprint_name": "BP_Player"},
            {"node_ids": ["GUID1"], "append": True},
        ),
    ),
    ActionDef(
        id="graph.batch_select_and_act",
        command="batch_select_and_act",
        tags=("graph", "batch", "select", "selection", "group", "collapse", "comment", "refactor", "automation"),
        description="Batch grouped selection + action execution. For each group: clears selection, selects the group's nodes, executes the specified action (e.g. collapse_selection_to_function, auto_comment), and collects the result. Enables fully automated per-group operations without manual selection.",
        input_schema={
            "type": "object",
            "properties": {
                "groups": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "node_ids": {
                                "type": "array",
                                "items": {"type": "string"},
                                "description": "Array of node GUID strings for this group"
                            },
                            "action": {"type": "string", "description": "Action command to execute on this selection (e.g. 'collapse_selection_to_function', 'auto_comment')"},
                            "action_params": {"type": "object", "description": "Optional extra params to pass to the action (e.g. comment_text, color for auto_comment)"}
                        },
                        "required": ["node_ids", "action"]
                    },
                    "description": "Array of group objects, each with node_ids and an action to perform"
                },
                "blueprint_name": {"type": "string", "description": "Optional Blueprint name"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["groups"]
        },
        capabilities=("write",),
        risk="moderate",
        examples=(
            {
                "groups": [
                    {"node_ids": ["GUID1", "GUID2"], "action": "collapse_selection_to_function"},
                    {"node_ids": ["GUID3", "GUID4"], "action": "auto_comment", "action_params": {"comment_text": "Movement Logic", "color": [0.15, 0.35, 0.65, 1]}}
                ]
            },
            {
                "blueprint_name": "BP_Player",
                "groups": [
                    {"node_ids": ["GUID1", "GUID2", "GUID3"], "action": "collapse_selection_to_function"}
                ]
            },
        ),
    ),
    ActionDef(
        id="graph.disconnect_pin",
        command="disconnect_blueprint_pin",
        tags=("graph", "disconnect", "pin", "unlink"),
        description="Disconnect all connections on a specific pin of a node",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "node_id": {"type": "string", "description": "GUID of the node"},
                "pin_name": {"type": "string", "description": "Name of the pin"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "node_id", "pin_name"]
        },
    ),
    ActionDef(
        id="graph.move_node",
        command="move_node",
        tags=("graph", "move", "position", "node"),
        description="Move (reposition) a node in a Blueprint graph",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "node_id": {"type": "string", "description": "GUID of the node"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] new position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "node_id", "node_position"]
        },
    ),
    ActionDef(
        id="graph.add_reroute",
        command="add_reroute_node",
        tags=("graph", "reroute", "knot", "wire"),
        description="Add a Reroute (Knot) node for cleaner wire routing",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name"]
        },
    ),
    ActionDef(
        id="graph.add_comment",
        command="add_blueprint_comment",
        tags=("graph", "comment", "annotation", "note"),
        description="Add a comment box node to a Blueprint graph",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "comment_text": {"type": "string", "description": "Comment text"},
                "graph_name": {"type": "string", "description": "Optional graph name"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "size": {"type": "array", "items": {"type": "number"}, "description": "[Width, Height]"},
                "color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] 0.0-1.0"}
            },
            "required": ["blueprint_name", "comment_text"]
        },
    ),
    ActionDef(
        id="graph.auto_comment",
        command="auto_comment",
        tags=("graph", "comment", "auto", "annotation", "wrap", "bounding"),
        description="Auto-create a comment box that precisely wraps nodes. When node_ids is provided, wraps only those nodes. When node_ids is omitted, wraps ALL non-comment nodes in the graph.",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "graph_name": {"type": "string", "description": "Optional graph name (default: EventGraph)"},
                "node_ids": {"type": "array", "items": {"type": "string"}, "description": "Node GUIDs to wrap. OPTIONAL — omit to wrap all nodes in the graph."},
                "comment_text": {"type": "string", "description": "Comment text"},
                "color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] 0.0-1.0 per Color Scheme"},
                "padding": {"type": "number", "description": "Extra padding in px around nodes (default: 40)"},
                "title_height": {"type": "number", "description": "Space reserved for comment title text (default: 36)"}
            },
            "required": ["blueprint_name", "comment_text"]
        },
        examples=(
            {"blueprint_name": "BP_Player", "graph_name": "UpdateRandomFloating", "comment_text": "浮动更新全流程", "color": [0.15, 0.35, 0.65, 1]},
            {"blueprint_name": "BP_Player", "node_ids": ["GUID1", "GUID2"], "comment_text": "BeginPlay Init", "color": [0.15, 0.55, 0.25, 1]},
        ),
    ),
    ActionDef(
        id="graph.describe",
        command="describe_graph",
        tags=("graph", "describe", "topology", "dump", "read"),
        description="Dump full graph topology: all nodes (GUID/type/title/position), all pins, all edges. Designed for AI-driven graph analysis.",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Blueprint package name or asset path"},
                "graph_name": {"type": "string", "description": "Target graph name (EventGraph, function name, etc.)"},
                "include_hidden_pins": {"type": "boolean", "description": "Include hidden pins (default: false)"},
                "include_orphan_pins": {"type": "boolean", "description": "Include orphan pins (default: false)"}
            },
            "required": ["blueprint_name"]
        },
        capabilities=("read",),
        examples=(
            {"blueprint_name": "BP_Player"},
            {"blueprint_name": "BP_Player", "graph_name": "EventGraph", "include_hidden_pins": True},
        ),
    ),
    # -- P3: Enhanced Graph Description --
    ActionDef(
        id="graph.describe_enhanced",
        command="describe_graph_enhanced",
        tags=("graph", "describe", "enhanced", "topology", "variables", "metadata", "read", "compact"),
        description="Enhanced graph topology dump: full PinType serialization, variable reference tracking, function signature expansion, and node metadata (breakpoints, enabled state). Use compact=true to omit metadata/function_signature/variable_references and use lightweight pin serialization (reduces 50-100KB → 10-20KB for large graphs).",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Blueprint package name or asset path"},
                "graph_name": {"type": "string", "description": "Target graph name (EventGraph, function name, etc.)"},
                "include_hidden_pins": {"type": "boolean", "description": "Include hidden pins (default: false)"},
                "include_orphan_pins": {"type": "boolean", "description": "Include orphan pins (default: false)"},
                "compact": {"type": "boolean", "description": "Compact mode: omit metadata, function_signature, variable_references and use lightweight pin serialization (default: false)"}
            },
            "required": ["blueprint_name"]
        },
        capabilities=("read",),
        examples=(
            {"blueprint_name": "BP_Player"},
            {"blueprint_name": "BP_Player", "graph_name": "EventGraph", "compact": True},
            {"blueprint_name": "BP_Player", "graph_name": "EventGraph", "include_hidden_pins": True, "include_orphan_pins": True},
        ),
    ),
    # -- P3: Patch System --
    ActionDef(
        id="graph.apply_patch",
        command="apply_graph_patch",
        tags=("graph", "patch", "apply", "declarative", "batch", "nodes", "connect"),
        description="Apply a declarative patch document to a Blueprint graph. Supports ops: add_node, remove_node, set_node_property, connect, disconnect, add_variable, set_variable_default, set_pin_default. Auto-compiles after execution.",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "graph_name": {"type": "string", "description": "Target graph (default: EventGraph)"},
                "continue_on_error": {"type": "boolean", "description": "Continue executing remaining ops if one fails (default: false)"},
                "ops": {
                    "type": "array",
                    "description": "Ordered list of patch operations",
                    "items": {
                        "type": "object",
                        "properties": {
                            "op": {"type": "string", "enum": ["add_node", "remove_node", "set_node_property", "connect", "disconnect", "add_variable", "set_variable_default", "set_pin_default"]},
                            "id": {"type": "string", "description": "(add_node) Temp ID for referencing within this patch"},
                            "node_type": {"type": "string", "description": "(add_node) Event, CustomEvent, FunctionCall, Branch, VariableGet, VariableSet, Cast, Self, Reroute, MacroInstance"},
                            "event_name": {"type": "string", "description": "(add_node/Event) Event name e.g. BeginPlay"},
                            "function_name": {"type": "string", "description": "(add_node/FunctionCall) Function name"},
                            "target_class": {"type": "string", "description": "(add_node/FunctionCall) Owning class (self, GameplayStatics, Math, etc.)"},
                            "variable_name": {"type": "string", "description": "(add_node/VariableGet|Set) Variable name"},
                            "defaults": {"type": "object", "description": "(add_node/FunctionCall) Pin default values {pin_name: value}"},
                            "node": {"type": "string", "description": "(various ops) Node reference: temp ID, GUID, or $last_node"},
                            "from": {"type": "object", "description": "(connect) {node, pin}"},
                            "to": {"type": "object", "description": "(connect) {node, pin}"},
                            "pin": {"type": "string", "description": "(disconnect/set_pin_default) Pin name"},
                            "property": {"type": "string", "description": "(set_node_property) Property name"},
                            "value": {"type": "string", "description": "(set_node_property/set_pin_default/set_variable_default) Value"},
                            "name": {"type": "string", "description": "(add_variable/set_variable_default) Variable name"},
                            "type": {"type": "string", "description": "(add_variable) Variable type"},
                            "pos_x": {"type": "number", "description": "(add_node) X position"},
                            "pos_y": {"type": "number", "description": "(add_node) Y position"}
                        },
                        "required": ["op"]
                    }
                }
            },
            "required": ["blueprint_name", "ops"]
        },
        capabilities=("write",),
        risk="moderate",
        examples=(
            {
                "blueprint_name": "BP_MyActor",
                "ops": [
                    {"op": "add_node", "id": "evt", "node_type": "Event", "event_name": "BeginPlay"},
                    {"op": "add_node", "id": "print", "node_type": "FunctionCall", "function_name": "PrintString", "defaults": {"InString": "Hello from Patch!"}},
                    {"op": "connect", "from": {"node": "evt", "pin": "then"}, "to": {"node": "print", "pin": "execute"}},
                    {"op": "set_pin_default", "node": "print", "pin": "bPrintToScreen", "value": "true"},
                ]
            },
        ),
    ),
    ActionDef(
        id="graph.validate_patch",
        command="validate_graph_patch",
        tags=("graph", "patch", "validate", "dryrun", "check", "read"),
        description="Dry-run validation of a patch document. Returns per-op validation results without modifying the graph.",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "graph_name": {"type": "string", "description": "Target graph (default: EventGraph)"},
                "ops": {
                    "type": "array",
                    "description": "Ordered list of patch operations (same format as graph.apply_patch)",
                    "items": {
                        "type": "object",
                        "properties": {
                            "op": {"type": "string"}
                        },
                        "required": ["op"]
                    }
                }
            },
            "required": ["blueprint_name", "ops"]
        },
        capabilities=("read",),
        examples=(
            {
                "blueprint_name": "BP_MyActor",
                "ops": [
                    {"op": "add_node", "id": "evt", "node_type": "Event", "event_name": "BeginPlay"},
                    {"op": "add_node", "id": "print", "node_type": "FunctionCall", "function_name": "PrintString"},
                    {"op": "connect", "from": {"node": "evt", "pin": "then"}, "to": {"node": "print", "pin": "execute"}},
                ]
            },
        ),
    ),
]


# =========================================================================
# Variable Management
# =========================================================================
_VARIABLE_MGMT_ACTIONS = [
    ActionDef(
        id="variable.set_default",
        command="set_blueprint_variable_default",
        tags=("variable", "default", "value", "set"),
        description="Set the default value of a Blueprint member variable",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "variable_name": {"type": "string", "description": "Name of the variable"},
                "default_value": {"type": "string", "description": "Default value as string"}
            },
            "required": ["blueprint_name", "variable_name", "default_value"]
        },
    ),
    ActionDef(
        id="variable.delete",
        command="delete_blueprint_variable",
        tags=("variable", "delete", "remove"),
        description="Delete a member variable from a Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "variable_name": {"type": "string", "description": "Name of the variable to delete"}
            },
            "required": ["blueprint_name", "variable_name"]
        },
        capabilities=("write", "destructive"),
        risk="moderate",
    ),
    ActionDef(
        id="variable.rename",
        command="rename_blueprint_variable",
        tags=("variable", "rename", "refactor"),
        description="Rename a member variable in a Blueprint (updates all getter/setter references)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "old_name": {"type": "string", "description": "Current variable name"},
                "new_name": {"type": "string", "description": "New variable name"}
            },
            "required": ["blueprint_name", "old_name", "new_name"]
        },
    ),
    ActionDef(
        id="variable.set_metadata",
        command="set_variable_metadata",
        tags=("variable", "metadata", "category", "tooltip", "replicated"),
        description="Set metadata on a Blueprint variable: category, tooltip, instance_editable, replicated, private, etc.",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "variable_name": {"type": "string", "description": "Name of the variable"},
                "category": {"type": "string", "description": "Variable category"},
                "tooltip": {"type": "string", "description": "Tooltip description"},
                "instance_editable": {"type": "boolean", "description": "Expose to details panel"},
                "blueprint_read_only": {"type": "boolean", "description": "Read-only in graph"},
                "expose_on_spawn": {"type": "boolean", "description": "Expose on SpawnActor node"},
                "replicated": {"type": "boolean", "description": "Enable network replication"},
                "private": {"type": "boolean", "description": "Accessible only within this Blueprint"}
            },
            "required": ["blueprint_name", "variable_name"]
        },
    ),
]


# =========================================================================
# Function Management
# =========================================================================
_FUNCTION_MGMT_ACTIONS = [
    ActionDef(
        id="function.create",
        command="create_blueprint_function",
        tags=("function", "create", "graph", "blueprint"),
        description="Create a new function graph in a Blueprint with inputs/outputs",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "function_name": {"type": "string", "description": "Name of the function"},
                "inputs": {"type": "array", "items": {"type": "object"}, "description": "Input parameters with 'name' and 'type' keys"},
                "outputs": {"type": "array", "items": {"type": "object"}, "description": "Output parameters with 'name' and 'type' keys"},
                "is_pure": {"type": "boolean", "description": "Create as pure function (no exec pins)"}
            },
            "required": ["blueprint_name", "function_name"]
        },
        examples=({"blueprint_name": "BP_Player", "function_name": "GetHealthPercent", "outputs": [{"name": "Percent", "type": "Float"}], "is_pure": True},),
    ),
    ActionDef(
        id="function.call",
        command="call_blueprint_function",
        tags=("function", "call", "invoke", "node"),
        description="Add a node that calls a custom Blueprint function",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Blueprint where to add the node"},
                "target_blueprint": {"type": "string", "description": "Blueprint containing the function"},
                "function_name": {"type": "string", "description": "Name of the function"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "target_blueprint", "function_name"]
        },
    ),
    ActionDef(
        id="function.delete",
        command="delete_blueprint_function",
        tags=("function", "delete", "remove"),
        description="Delete a custom function from a Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "function_name": {"type": "string", "description": "Name of the function to delete"}
            },
            "required": ["blueprint_name", "function_name"]
        },
        capabilities=("write", "destructive"),
        risk="moderate",
    ),
    ActionDef(
        id="function.rename",
        command="rename_blueprint_function",
        tags=("function", "rename", "refactor"),
        description="Rename a custom function graph in a Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "function_name": {"type": "string", "description": "Current function name"},
                "new_name": {"type": "string", "description": "New function name"}
            },
            "required": ["blueprint_name", "function_name", "new_name"]
        },
    ),
    ActionDef(
        id="macro.rename",
        command="rename_blueprint_macro",
        tags=("macro", "rename", "refactor"),
        description="Rename a custom macro graph in a Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "macro_name": {"type": "string", "description": "Current macro name"},
                "new_name": {"type": "string", "description": "New macro name"}
            },
            "required": ["blueprint_name", "macro_name", "new_name"]
        },
    ),
]


# =========================================================================
# Struct & Switch Nodes
# =========================================================================
_STRUCT_SWITCH_ACTIONS = [
    ActionDef(
        id="node.add_make_struct",
        command="add_make_struct_node",
        tags=("node", "struct", "make", "construct"),
        description="Add a Make Struct node (Make Vector, Make IntPoint, Make LinearColor, etc.)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "struct_type": {"type": "string", "description": "Struct type: IntPoint, Vector, Vector2D, Rotator, Transform, LinearColor, Color"},
                "pin_defaults": {"type": "object", "description": "Default pin values (e.g., {'X': '1920', 'Y': '1080'})"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "struct_type"]
        },
    ),
    ActionDef(
        id="node.add_break_struct",
        command="add_break_struct_node",
        tags=("node", "struct", "break", "decompose"),
        description="Add a Break Struct node (Break Vector, Break IntPoint, etc.)",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "struct_type": {"type": "string", "description": "Struct type: IntPoint, Vector, Vector2D, Rotator, Transform, LinearColor, Color"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name", "struct_type"]
        },
    ),
    ActionDef(
        id="node.add_switch_string",
        command="add_switch_on_string_node",
        tags=("node", "switch", "string", "case", "flow"),
        description="Add a Switch on String node with case options",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "cases": {"type": "array", "items": {"type": "string"}, "description": "String case values"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name"]
        },
    ),
    ActionDef(
        id="node.add_switch_int",
        command="add_switch_on_int_node",
        tags=("node", "switch", "int", "case", "flow"),
        description="Add a Switch on Int node with case options",
        input_schema={
            "type": "object",
            "properties": {
                "blueprint_name": {"type": "string", "description": "Name of the Blueprint"},
                "start_index": {"type": "integer", "description": "Starting index (default: 0)"},
                "cases": {"type": "array", "items": {"type": "integer"}, "description": "Number of cases"},
                "node_position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "graph_name": {"type": "string", "description": "Optional graph name"}
            },
            "required": ["blueprint_name"]
        },
    ),
]


# =========================================================================
# Materials
# =========================================================================
_MATERIAL_ACTIONS = [
    ActionDef(
        id="material.create",
        command="create_material",
        tags=("material", "create", "asset", "shader"),
        description="Create a new Material asset",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material"},
                "path": {"type": "string", "description": "Content path (default: /Game/Materials)"},
                "domain": {"type": "string", "description": "Material domain (Surface, PostProcess, DeferredDecal, LightFunction, UI)"},
                "blend_mode": {"type": "string", "description": "Blend mode (Opaque, Masked, Translucent, Additive, Modulate)"}
            },
            "required": ["material_name"]
        },
        examples=({"material_name": "M_Glow", "domain": "Surface", "blend_mode": "Translucent"},),
    ),
    ActionDef(
        id="material.add_expression",
        command="add_material_expression",
        tags=("material", "expression", "node", "graph"),
        description="Add an expression node to a Material graph",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material"},
                "expression_class": {"type": "string", "description": "Type (ScalarParameter, VectorParameter, Add, Multiply, Lerp, etc.)"},
                "node_name": {"type": "string", "description": "Unique name for this node"},
                "position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] position"},
                "properties": {"type": "object", "description": "Property name/value pairs"}
            },
            "required": ["material_name", "expression_class", "node_name"]
        },
    ),
    ActionDef(
        id="material.connect_expressions",
        command="connect_material_expressions",
        tags=("material", "connect", "wire", "expression"),
        description="Connect output of one material expression to input of another",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material"},
                "source_node": {"type": "string", "description": "Source expression name"},
                "source_output_index": {"type": "integer", "description": "Output pin index (default: 0)"},
                "target_node": {"type": "string", "description": "Target expression name"},
                "target_input": {"type": "string", "description": "Input pin name (A, B, Alpha, etc.)"}
            },
            "required": ["material_name", "source_node", "target_node", "target_input"]
        },
    ),
    ActionDef(
        id="material.connect_to_output",
        command="connect_to_material_output",
        tags=("material", "connect", "output", "basecolor", "emissive"),
        description="Connect an expression to the material's main output (BaseColor, EmissiveColor, Metallic, etc.)",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material"},
                "source_node": {"type": "string", "description": "Source expression name"},
                "source_output_index": {"type": "integer", "description": "Output pin index (default: 0)"},
                "material_property": {"type": "string", "description": "Material property (BaseColor, EmissiveColor, Metallic, Roughness, Normal, Opacity, etc.)"}
            },
            "required": ["material_name", "source_node", "material_property"]
        },
    ),
    ActionDef(
        id="material.set_expression_property",
        command="set_material_expression_property",
        tags=("material", "expression", "property", "set"),
        description="Set a property on an existing material expression",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material"},
                "node_name": {"type": "string", "description": "Expression node name"},
                "property_name": {"type": "string", "description": "Property to set"},
                "property_value": {"type": "string", "description": "Value to set"}
            },
            "required": ["material_name", "node_name", "property_name", "property_value"]
        },
    ),
    ActionDef(
        id="material.compile",
        command="compile_material",
        tags=("material", "compile", "validate", "error", "diagnostic"),
        description="Compile a material, wait for shader compilation to finish, and return real compile errors with associated expression info. Returns errors[] array with message, expression_name, expression_class, and optional node_name for each error.",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material (optional; uses current material if omitted)"}
            },
            "required": []
        },
    ),
    ActionDef(
        id="material.create_instance",
        command="create_material_instance",
        tags=("material", "instance", "create", "parameter"),
        description="Create a Material Instance with parameter overrides",
        input_schema={
            "type": "object",
            "properties": {
                "instance_name": {"type": "string", "description": "Name for the instance"},
                "parent_material": {"type": "string", "description": "Parent material name"},
                "path": {"type": "string", "description": "Content path (default: /Game/Materials)"},
                "scalar_parameters": {"type": "object", "description": "Scalar parameter overrides {name: value}"},
                "vector_parameters": {"type": "object", "description": "Vector parameter overrides {name: [R,G,B,A]}"},
                "texture_parameters": {"type": "object", "description": "Texture parameter overrides {name: asset_path}"},
                "static_switch_parameters": {"type": "object", "description": "Static switch parameter overrides {name: bool}"}
            },
            "required": ["instance_name", "parent_material"]
        },
    ),
    ActionDef(
        id="material.set_property",
        command="set_material_property",
        tags=("material", "property", "set", "shading"),
        description="Set a property on a Material asset (ShadingModel, TwoSided, BlendMode, etc.)",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material"},
                "property_name": {"type": "string", "description": "Property (ShadingModel, TwoSided, BlendMode, etc.)"},
                "property_value": {"type": "string", "description": "Value to set"}
            },
            "required": ["material_name", "property_name", "property_value"]
        },
    ),
    ActionDef(
        id="material.create_post_process_volume",
        command="create_post_process_volume",
        tags=("material", "postprocess", "volume", "level"),
        description="Create a Post Process Volume actor in the level",
        input_schema={
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Name for the volume"},
                "location": {"type": "array", "items": {"type": "number"}, "description": "[X, Y, Z]"},
                "infinite_extent": {"type": "boolean", "description": "Apply everywhere (default: true)"},
                "priority": {"type": "number", "description": "Priority (default: 0.0)"},
                "post_process_materials": {"type": "array", "items": {"type": "string"}, "description": "Materials to add"}
            },
            "required": ["name"]
        },
    ),
    # Phase 4 Material Actions
    ActionDef(
        id="material.get_summary",
        command="get_material_summary",
        tags=("material", "summary", "inspect", "graph", "debug"),
        description="Get full material graph structure: all expressions, connections, properties, and comments",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material"}
            },
            "required": ["material_name"]
        },
        capabilities=("read",),
    ),
    ActionDef(
        id="material.remove_expression",
        command="remove_material_expression",
        tags=("material", "expression", "remove", "delete", "node"),
        description="Remove one or more expressions from a material by node_name",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material"},
                "node_name": {"type": "string", "description": "Single node name to remove"},
                "node_names": {"type": "array", "items": {"type": "string"}, "description": "Array of node names to remove"}
            },
            "required": ["material_name"]
        },
        capabilities=("write", "destructive"),
        risk="destructive",
    ),
    ActionDef(
        id="material.auto_layout",
        command="auto_layout_material",
        tags=("material", "layout", "auto", "graph", "organize"),
        description="Auto-layout material graph nodes using data-flow topological sort",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material"},
                "layer_spacing": {"type": "number", "description": "Horizontal spacing (>0=fixed px, 0=auto)"},
                "row_spacing": {"type": "number", "description": "Vertical spacing (>0=fixed px, 0=auto)"}
            },
            "required": ["material_name"]
        },
    ),
    ActionDef(
        id="material.auto_comment",
        command="auto_comment_material",
        tags=("material", "comment", "auto", "annotate", "graph"),
        description="Add a comment node wrapping specified (or all) material expressions. Supports overwrite (replace same-text comment) and clear_all (remove all comments first).",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material"},
                "comment_text": {"type": "string", "description": "Comment text"},
                "node_names": {"type": "array", "items": {"type": "string"}, "description": "Node names to wrap ($expr_N, param names, $selected). Default: all"},
                "color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A] (0-1)"},
                "padding": {"type": "number", "description": "Padding around nodes in pixels (default: 40)"},
                "use_selected": {"type": "boolean", "description": "Wrap currently selected nodes in editor (default: false)"},
                "overwrite": {"type": "boolean", "description": "Remove existing comments with same text before creating (default: false)"},
                "clear_all": {"type": "boolean", "description": "Remove ALL existing comments before creating (default: false)"}
            },
            "required": ["material_name", "comment_text"]
        },
    ),
    # Phase 5: Material apply actions
    ActionDef(
        id="material.apply_to_component",
        command="apply_material_to_component",
        tags=("material", "apply", "component", "actor", "assign"),
        description="Apply a material to a specific component on a level actor. Finds actor by name, optionally targets a specific component, and sets the material at the given slot index.",
        input_schema={
            "type": "object",
            "properties": {
                "actor_name": {"type": "string", "description": "Name of the Actor in the level"},
                "material_path": {"type": "string", "description": "Asset path of the material (e.g. /Game/Materials/M_Example)"},
                "component_name": {"type": "string", "description": "Component name (default: first PrimitiveComponent)"},
                "slot_index": {"type": "integer", "description": "Material slot index (default: 0)"}
            },
            "required": ["actor_name", "material_path"]
        },
    ),
    ActionDef(
        id="material.apply_to_actor",
        command="apply_material_to_actor",
        tags=("material", "apply", "actor", "assign", "all", "components"),
        description="Apply a material to ALL PrimitiveComponents on a level actor at the given slot index. Useful for quickly replacing all materials on an actor.",
        input_schema={
            "type": "object",
            "properties": {
                "actor_name": {"type": "string", "description": "Name of the Actor in the level"},
                "material_path": {"type": "string", "description": "Asset path of the material"},
                "slot_index": {"type": "integer", "description": "Material slot index (default: 0)"}
            },
            "required": ["actor_name", "material_path"]
        },
    ),
    ActionDef(
        id="material.refresh_editor",
        command="refresh_material_editor",
        tags=("material", "refresh", "editor", "update", "ui", "graph", "rebuild"),
        description="Refresh the Material Editor UI for a given material. Call after a batch of graph modifications (add_expression, connect, set_property, etc.) to make changes visible in the open editor without closing and reopening it. Rebuilds the graph, re-links expressions, refreshes previews, and notifies the editor.",
        input_schema={
            "type": "object",
            "properties": {
                "material_name": {"type": "string", "description": "Name of the Material to refresh"}
            },
            "required": ["material_name"]
        },
    ),
]


# =========================================================================
# UMG Widgets
# =========================================================================
_WIDGET_ACTIONS = [
    ActionDef(
        id="widget.create",
        command="create_umg_widget_blueprint",
        tags=("widget", "umg", "ui", "create", "blueprint"),
        description="Create a new UMG Widget Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the widget blueprint"},
                "parent_class": {"type": "string", "description": "Parent class (default: UserWidget)"},
                "path": {"type": "string", "description": "Content path (default: /Game/UI)"}
            },
            "required": ["widget_name"]
        },
        examples=({"widget_name": "WBP_HUD"},),
    ),
    ActionDef(
        id="widget.delete",
        command="delete_umg_widget_blueprint",
        tags=("widget", "umg", "delete", "remove"),
        description="Delete a UMG Widget Blueprint asset",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint to delete"}
            },
            "required": ["widget_name"]
        },
        capabilities=("write", "destructive"),
        risk="destructive",
    ),
    ActionDef(
        id="widget.add_component",
        command="add_widget_component",
        tags=("widget", "umg", "component", "add", "textblock", "button", "image"),
        description="Add a widget component (TextBlock, Button, Image, Border, Overlay, HorizontalBox, VerticalBox, Slider, ProgressBar, etc.)",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "component_type": {"type": "string", "enum": ["TextBlock", "Button", "Image", "Border", "Overlay", "HorizontalBox", "VerticalBox", "Slider", "ProgressBar", "SizeBox", "ScaleBox", "CanvasPanel", "ComboBox", "CheckBox", "SpinBox", "EditableTextBox", "ScrollBox", "WidgetSwitcher", "BackgroundBlur", "UniformGridPanel", "Spacer", "RichTextBlock", "WrapBox", "CircularThrobber"]},
                "component_name": {"type": "string", "description": "Name for the component"},
                "text": {"type": "string", "description": "Text content (TextBlock, Button)"},
                "position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y]"},
                "size": {"type": "array", "items": {"type": "number"}, "description": "[Width, Height]"},
                "font_size": {"type": "integer", "description": "Font size"},
                "color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A]"}
            },
            "required": ["widget_name", "component_type", "component_name"]
        },
    ),
    ActionDef(
        id="widget.bind_event",
        command="bind_widget_event",
        tags=("widget", "umg", "event", "bind", "onclick"),
        description="Bind an event on a widget component (e.g., button OnClicked)",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "widget_component_name": {"type": "string", "description": "Component name (e.g., RestartButton)"},
                "event_name": {"type": "string", "description": "Event (OnClicked, OnPressed, OnReleased, etc.)"}
            },
            "required": ["widget_name", "widget_component_name", "event_name"]
        },
    ),
    ActionDef(
        id="widget.add_to_viewport",
        command="add_widget_to_viewport",
        tags=("widget", "umg", "viewport", "display", "show"),
        description="Add a Widget Blueprint instance to the viewport",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "z_order": {"type": "integer", "description": "Z-order (higher = on top)"}
            },
            "required": ["widget_name"]
        },
    ),
    ActionDef(
        id="widget.set_text_binding",
        command="set_text_block_binding",
        tags=("widget", "umg", "text", "binding", "data"),
        description="Set up a property binding for a Text Block widget",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "text_block_name": {"type": "string", "description": "Name of the Text Block"},
                "binding_property": {"type": "string", "description": "Property to bind to"},
                "binding_type": {"type": "string", "description": "Binding type (Text, Visibility, etc.)"}
            },
            "required": ["widget_name", "text_block_name", "binding_property"]
        },
    ),
    ActionDef(
        id="widget.list_components",
        command="list_widget_components",
        tags=("widget", "umg", "list", "components", "read"),
        description="List all components in a UMG Widget Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"}
            },
            "required": ["widget_name"]
        },
        capabilities=("read",),
    ),
    ActionDef(
        id="widget.get_tree",
        command="get_widget_tree",
        tags=("widget", "umg", "tree", "hierarchy", "read"),
        description="Get the full widget tree with hierarchy, classes, slot info, and render transforms",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"}
            },
            "required": ["widget_name"]
        },
        capabilities=("read",),
    ),
    ActionDef(
        id="widget.set_properties",
        command="set_widget_properties",
        tags=("widget", "umg", "properties", "slot", "transform"),
        description="Set properties on a widget: slot, render transform, visibility, and type-specific properties",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "target": {"type": "string", "description": "Name of the widget to modify"},
                "position": {"type": "array", "items": {"type": "number"}, "description": "[X, Y] (CanvasPanel slot)"},
                "size": {"type": "array", "items": {"type": "number"}, "description": "[W, H] (CanvasPanel slot)"},
                "visibility": {"type": "string", "description": "Visible, Hidden, Collapsed, HitTestInvisible, SelfHitTestInvisible"},
                "is_enabled": {"type": "boolean", "description": "Whether widget is enabled"},
                "h_align": {"type": "string", "description": "Horizontal alignment: Fill, Left, Center, Right"},
                "v_align": {"type": "string", "description": "Vertical alignment: Fill, Top, Center, Bottom"},
                "padding": {"type": "array", "items": {"type": "number"}, "description": "[Left, Top, Right, Bottom]"}
            },
            "required": ["widget_name", "target"]
        },
    ),
    ActionDef(
        id="widget.set_text",
        command="set_widget_text",
        tags=("widget", "umg", "text", "set", "textblock"),
        description="Set text/style on a TextBlock or update a Button's child TextBlock",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "target": {"type": "string", "description": "TextBlock or Button name"},
                "text": {"type": "string", "description": "Text to set"},
                "font_size": {"type": "integer", "description": "Font size"},
                "color": {"type": "array", "items": {"type": "number"}, "description": "[R, G, B, A]"},
                "justification": {"type": "string", "enum": ["Left", "Center", "Right"]}
            },
            "required": ["widget_name", "target"]
        },
    ),
    ActionDef(
        id="widget.set_combo_options",
        command="set_combo_box_options",
        tags=("widget", "umg", "combobox", "options", "dropdown"),
        description="Set/clear/add/remove options on a ComboBoxString widget",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "target": {"type": "string", "description": "ComboBoxString widget name"},
                "mode": {"type": "string", "enum": ["replace", "add", "remove", "clear"]},
                "options": {"type": "array", "items": {"type": "string"}, "description": "Options to apply"},
                "selected_option": {"type": "string", "description": "Option to select"}
            },
            "required": ["widget_name", "target"]
        },
    ),
    ActionDef(
        id="widget.set_slider",
        command="set_slider_properties",
        tags=("widget", "umg", "slider", "range", "value"),
        description="Set value/range/step on a Slider widget",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "target": {"type": "string", "description": "Slider widget name"},
                "value": {"type": "number", "description": "Slider value"},
                "min_value": {"type": "number"}, "max_value": {"type": "number"},
                "step_size": {"type": "number"}, "locked": {"type": "boolean"}
            },
            "required": ["widget_name", "target"]
        },
    ),
    ActionDef(
        id="widget.reparent",
        command="reparent_widgets",
        tags=("widget", "umg", "reparent", "hierarchy", "container"),
        description="Move widgets into a target container (VerticalBox, HorizontalBox, etc.)",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "target_container_name": {"type": "string", "description": "Target container name"},
                "container_type": {"type": "string", "description": "Container type (VerticalBox, HorizontalBox, etc.)"},
                "children": {"type": "array", "items": {"type": "string"}, "description": "Widget names to move"},
                "filter_class": {"type": "string", "description": "Move only widgets of this class"}
            },
            "required": ["widget_name", "target_container_name"]
        },
    ),
    ActionDef(
        id="widget.add_child",
        command="add_widget_child",
        tags=("widget", "umg", "child", "parent", "hierarchy"),
        description="Move an existing widget to become a child of a specified parent container",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "child": {"type": "string", "description": "Widget to move"},
                "parent": {"type": "string", "description": "Target parent container"}
            },
            "required": ["widget_name", "child", "parent"]
        },
    ),
    ActionDef(
        id="widget.delete_component",
        command="delete_widget_from_blueprint",
        tags=("widget", "umg", "delete", "component", "remove"),
        description="Delete a widget component from a UMG Widget Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "target": {"type": "string", "description": "Widget component to delete"}
            },
            "required": ["widget_name", "target"]
        },
        capabilities=("write", "destructive"),
        risk="moderate",
    ),
    ActionDef(
        id="widget.rename_component",
        command="rename_widget_in_blueprint",
        tags=("widget", "umg", "rename", "component"),
        description="Rename a widget component in a UMG Widget Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "target": {"type": "string", "description": "Current component name"},
                "new_name": {"type": "string", "description": "New name"}
            },
            "required": ["widget_name", "target", "new_name"]
        },
    ),
    # --- MVVM Actions ---
    ActionDef(
        id="widget.mvvm_add_viewmodel",
        command="mvvm_add_viewmodel",
        tags=("widget", "umg", "mvvm", "viewmodel", "binding", "view"),
        description="Associate a ViewModel class with a Widget Blueprint (MVVM). Creates the MVVM extension and adds the ViewModel context.",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "viewmodel_class": {"type": "string", "description": "ViewModel class name (must implement INotifyFieldValueChanged / derive from UMVVMViewModelBase)"},
                "viewmodel_name": {"type": "string", "description": "Display name for this ViewModel in the Widget (defaults to class name)"},
                "creation_type": {
                    "type": "string",
                    "enum": ["CreateInstance", "Manual", "GlobalViewModelCollection", "PropertyPath", "Resolver"],
                    "description": "How the ViewModel is created at runtime (default: CreateInstance)"
                },
                "create_setter": {"type": "boolean", "description": "Generate a public setter function (default: false)"},
                "create_getter": {"type": "boolean", "description": "Generate a public getter function (default: true)"}
            },
            "required": ["widget_name", "viewmodel_class"]
        },
        examples=(
            {"widget_name": "WBP_HUD", "viewmodel_class": "StatusViewModel", "viewmodel_name": "StatusVM", "creation_type": "CreateInstance"},
        ),
    ),
    ActionDef(
        id="widget.mvvm_add_binding",
        command="mvvm_add_binding",
        tags=("widget", "umg", "mvvm", "binding", "property", "viewmodel", "data"),
        description="Add a MVVM property binding between a ViewModel property and a Widget property",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "viewmodel_name": {"type": "string", "description": "Name of the ViewModel (as registered via mvvm_add_viewmodel)"},
                "source_property": {"type": "string", "description": "Property name on the ViewModel (source)"},
                "destination_widget": {"type": "string", "description": "Name of the target widget in the Widget Tree"},
                "destination_property": {"type": "string", "description": "Property name on the target widget (e.g. Text, Percent, Visibility)"},
                "binding_mode": {
                    "type": "string",
                    "enum": ["OneTimeToDestination", "OneWayToDestination", "TwoWay", "OneTimeToSource", "OneWayToSource"],
                    "description": "Binding direction (default: OneWayToDestination)"
                },
                "execution_mode": {
                    "type": "string",
                    "enum": ["Immediate", "Delayed", "Tick", "Auto"],
                    "description": "When to execute the binding (optional, uses engine default if omitted)"
                }
            },
            "required": ["widget_name", "viewmodel_name", "source_property", "destination_widget", "destination_property"]
        },
        examples=(
            {
                "widget_name": "WBP_HUD",
                "viewmodel_name": "StatusVM",
                "source_property": "HealthPercent",
                "destination_widget": "HealthBar",
                "destination_property": "Percent",
                "binding_mode": "OneWayToDestination"
            },
        ),
    ),
    ActionDef(
        id="widget.mvvm_get_bindings",
        command="mvvm_get_bindings",
        tags=("widget", "umg", "mvvm", "binding", "viewmodel", "read", "introspect"),
        description="Read all MVVM ViewModels and Bindings configured on a Widget Blueprint",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"}
            },
            "required": ["widget_name"]
        },
        examples=(
            {"widget_name": "WBP_HUD"},
        ),
        capabilities=("read",),
    ),
    ActionDef(
        id="widget.mvvm_remove_binding",
        command="mvvm_remove_binding",
        tags=("widget", "umg", "mvvm", "binding", "remove", "delete"),
        description="Remove a MVVM binding from a Widget Blueprint by binding_id",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "binding_id": {"type": "string", "description": "Binding ID to remove (from mvvm_get_bindings)"}
            },
            "required": ["widget_name", "binding_id"]
        },
        examples=(
            {"widget_name": "WBP_HUD", "binding_id": "102D0F354A7C97A46E6E22B42A0C9394"},
        ),
    ),
    ActionDef(
        id="widget.mvvm_remove_viewmodel",
        command="mvvm_remove_viewmodel",
        tags=("widget", "umg", "mvvm", "viewmodel", "remove", "delete"),
        description="Remove a MVVM ViewModel from a Widget Blueprint by viewmodel_name",
        input_schema={
            "type": "object",
            "properties": {
                "widget_name": {"type": "string", "description": "Name of the Widget Blueprint"},
                "viewmodel_name": {"type": "string", "description": "ViewModel name to remove"}
            },
            "required": ["widget_name", "viewmodel_name"]
        },
        examples=(
            {"widget_name": "WBP_HUD", "viewmodel_name": "StatusVM"},
        ),
    ),
]


# =========================================================================
# Input Mappings
# =========================================================================
_INPUT_ACTIONS = [
    ActionDef(
        id="input.create_mapping",
        command="create_input_mapping",
        tags=("input", "mapping", "legacy", "key", "bind"),
        description="Create a legacy input mapping (Action or Axis)",
        input_schema={
            "type": "object",
            "properties": {
                "action_name": {"type": "string", "description": "Input action name"},
                "key": {"type": "string", "description": "Key (SpaceBar, W, LeftMouseButton, etc.)"},
                "input_type": {"type": "string", "description": "Type: Action or Axis"},
                "scale": {"type": "number", "description": "Scale for Axis (1.0 or -1.0)"}
            },
            "required": ["action_name", "key"]
        },
    ),
    ActionDef(
        id="input.create_action",
        command="create_input_action",
        tags=("input", "action", "enhanced", "create"),
        description="Create an Enhanced Input Action asset",
        input_schema={
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Name (e.g., IA_Move)"},
                "value_type": {"type": "string", "description": "Boolean, Axis1D/Float, Axis2D/Vector2D, Axis3D/Vector"},
                "path": {"type": "string", "description": "Content path (default: /Game/Input)"}
            },
            "required": ["name"]
        },
        examples=({"name": "IA_Move", "value_type": "Axis2D"},),
    ),
    ActionDef(
        id="input.create_mapping_context",
        command="create_input_mapping_context",
        tags=("input", "mapping", "context", "enhanced", "create"),
        description="Create an Enhanced Input Mapping Context asset",
        input_schema={
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "Name (e.g., IMC_Default)"},
                "path": {"type": "string", "description": "Content path (default: /Game/Input)"}
            },
            "required": ["name"]
        },
        examples=({"name": "IMC_Default"},),
    ),
    ActionDef(
        id="input.add_key_mapping",
        command="add_key_mapping_to_context",
        tags=("input", "key", "mapping", "bind", "modifier"),
        description="Add a key mapping to an Input Mapping Context with optional modifiers",
        input_schema={
            "type": "object",
            "properties": {
                "context_name": {"type": "string", "description": "IMC asset name"},
                "action_name": {"type": "string", "description": "Input Action asset name"},
                "key": {"type": "string", "description": "Key (W, A, SpaceBar, etc.)"},
                "modifiers": {"type": "array", "items": {"type": "string"}, "description": "Modifiers: Negate, SwizzleYXZ, etc."},
                "context_path": {"type": "string", "description": "Path to IMC"},
                "action_path": {"type": "string", "description": "Path to IA"}
            },
            "required": ["context_name", "action_name", "key"]
        },
    ),
]
