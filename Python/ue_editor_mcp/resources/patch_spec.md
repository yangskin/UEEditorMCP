# Patch Spec — Declarative Graph Editing (Phase 3)

> **Status**: Implemented in Phase 3.  
> **Actions**: `graph.apply_patch`, `graph.validate_patch`, `graph.describe_enhanced`

## Overview

The Patch System provides a high-level declarative API for modifying Blueprint graphs.
Instead of calling individual node/connect/pin actions separately, you describe all
desired changes in a single JSON "patch document" and submit it via `graph.apply_patch`.

## Patch Document Format

```json
{
  "blueprint_name": "BP_MyActor",
  "graph_name": "EventGraph",
  "continue_on_error": false,
  "ops": [
    { "op": "add_node", ... },
    { "op": "connect", ... },
    { "op": "set_pin_default", ... }
  ]
}
```

### Top-Level Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `blueprint_name` | string | **yes** | Target Blueprint name or asset path |
| `graph_name` | string | no | Target graph (default: EventGraph) |
| `continue_on_error` | boolean | no | If true, continue executing remaining ops when one fails (default: false) |
| `ops` | array | **yes** | Ordered list of patch operations |

## Supported Op Types

### `add_node` — Create a new node

| Param | Type | Required | Description |
|---|---|---|---|
| `id` | string | **yes** | Temporary ID for this node (referenced by other ops in same patch) |
| `node_type` | string | **yes** | One of: `Event`, `CustomEvent`, `FunctionCall`, `Branch`, `VariableGet`, `VariableSet`, `Cast`, `Self`, `Reroute`, `MacroInstance` |
| `event_name` | string | for Event/CustomEvent | Event name (e.g. `BeginPlay`, `Tick`) |
| `function_name` | string | for FunctionCall | Function name (e.g. `PrintString`, `GetActorLocation`) |
| `target_class` | string | no | Owning class for FunctionCall (`self`, `GameplayStatics`, `Math`, `System`, etc.) |
| `defaults` | object | no | Pin default values as `{pin_name: value}` (for FunctionCall) |
| `variable_name` | string | for VariableGet/Set | Blueprint variable name |
| `class_name` | string | for Cast | Target class name |
| `macro_name` | string | for MacroInstance | Macro name (e.g. `ForEachLoop`) |
| `pos_x` | number | no | Node X position |
| `pos_y` | number | no | Node Y position |

**Example:**
```json
{"op": "add_node", "id": "evt", "node_type": "Event", "event_name": "BeginPlay"}
{"op": "add_node", "id": "print", "node_type": "FunctionCall", "function_name": "PrintString",
 "target_class": "KismetSystemLibrary", "defaults": {"InString": "Hello!"}, "pos_x": 400, "pos_y": 0}
```

### `remove_node` — Delete a node

| Param | Type | Required | Description |
|---|---|---|---|
| `node` | string | **yes** | Node reference (temp ID, GUID, or `$last_node`) |

All pin connections are automatically broken before removal.

### `set_node_property` — Set a node property

| Param | Type | Required | Description |
|---|---|---|---|
| `node` | string | **yes** | Node reference |
| `property` | string | **yes** | Property name: `Comment` or `EnabledState` |
| `value` | string | **yes** | Value (`Enabled`, `Disabled`, `DevelopmentOnly` for EnabledState) |

### `connect` — Connect two pins

| Param | Type | Required | Description |
|---|---|---|---|
| `from` | object | **yes** | `{"node": "...", "pin": "..."}` — source (output pin) |
| `to` | object | **yes** | `{"node": "...", "pin": "..."}` — target (input pin) |

### `disconnect` — Break pin connections

| Param | Type | Required | Description |
|---|---|---|---|
| `node` | string | **yes** | Node reference |
| `pin` | string | **yes** | Pin name |
| `target_node` | string | no | Specific target node (if omitted, breaks all connections) |
| `target_pin` | string | no | Specific target pin |

### `add_variable` — Add a Blueprint variable

| Param | Type | Required | Description |
|---|---|---|---|
| `name` | string | **yes** | Variable name |
| `type` | string | **yes** | Type: `Boolean`, `Integer`, `Float`, `String`, `Name`, `Text`, `Vector`, `Rotator`, `Transform`, `LinearColor`, `Byte`, `Int64`, or a UClass name |
| `default_value` | string | no | Default value |
| `category` | string | no | Variable category |
| `is_instance_editable` | boolean | no | Expose to Details panel |

### `set_variable_default` — Set variable default value

| Param | Type | Required | Description |
|---|---|---|---|
| `name` | string | **yes** | Variable name |
| `value` | string | **yes** | Default value as string |

### `set_pin_default` — Set a pin's default value

| Param | Type | Required | Description |
|---|---|---|---|
| `node` | string | **yes** | Node reference |
| `pin` | string | **yes** | Pin name |
| `value` | string | **yes** | Default value |

## Node Reference Rules

Within a patch, nodes are referenced by:

- **Temp ID** (`"my_node"`) — assigned via `add_node`'s `id` field, valid within the same patch
- **GUID** (`"A1B2C3D4-..."`) — from `graph.describe` or `graph.describe_enhanced`
- **`$last_node`** — alias for the most recently created node (via Context)

## Execution Flow

```
graph.validate_patch(patch)          ← dryRun: validate all ops
    │  ↓ all OK
graph.apply_patch(patch)             ← execute for real
    │
    ├── Phase A: Validate All Ops    ← read-only checks
    │   ├── Check op params are present
    │   ├── Check no duplicate temp IDs
    │   └── Check op types are valid
    │
    ├── Phase B: Execute Ops         ← sequential execution
    │   ├── add_node → create node, register temp ID
    │   ├── connect → resolve refs, TryCreateConnection
    │   ├── set_pin_default → find pin, set value
    │   └── ... each op produces a result
    │
    └── Phase C: Post-Execute
        ├── MarkBlueprintModified()
        ├── CompileBlueprint()
        └── Return full results
```

## Response Format

### `graph.apply_patch` Response

```json
{
  "success": true,
  "total": 4,
  "executed": 4,
  "succeeded": 4,
  "failed": 0,
  "compiled": true,
  "results": [
    {"op_index": 0, "op_type": "add_node", "success": true, "message": "Created Event node", "node_id": "A1B2..."},
    {"op_index": 1, "op_type": "add_node", "success": true, "message": "Created FunctionCall node", "node_id": "C3D4..."},
    {"op_index": 2, "op_type": "connect", "success": true, "message": "Connected"},
    {"op_index": 3, "op_type": "set_pin_default", "success": true, "message": "Pin default set"}
  ]
}
```

### `graph.validate_patch` Response

```json
{
  "success": true,
  "total": 3,
  "valid": 3,
  "errors": 0,
  "all_valid": true,
  "results": [
    {"op_index": 0, "op_type": "add_node", "success": true, "message": "OK"},
    {"op_index": 1, "op_type": "add_node", "success": true, "message": "OK"},
    {"op_index": 2, "op_type": "connect", "success": true, "message": "OK"}
  ]
}
```

## Complete Example: BeginPlay → PrintString

```json
{
  "blueprint_name": "BP_MyActor",
  "graph_name": "EventGraph",
  "ops": [
    {
      "op": "add_node",
      "id": "new_beginplay",
      "node_type": "Event",
      "event_name": "BeginPlay"
    },
    {
      "op": "add_node",
      "id": "new_print",
      "node_type": "FunctionCall",
      "function_name": "PrintString",
      "defaults": {
        "InString": "Hello from Patch!"
      },
      "pos_x": 400,
      "pos_y": 0
    },
    {
      "op": "connect",
      "from": {"node": "new_beginplay", "pin": "then"},
      "to": {"node": "new_print", "pin": "execute"}
    },
    {
      "op": "set_pin_default",
      "node": "new_print",
      "pin": "bPrintToScreen",
      "value": "true"
    }
  ]
}
```

## Constraints & Limits

- Maximum 100 ops per patch
- Patch is **not atomic** (no rollback on partial failure)
- `continue_on_error: false` (default) stops at first failure
- Auto-compiles after all ops complete
- Event nodes are deduplicated (reuse existing if found)
