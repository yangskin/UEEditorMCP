# UE MCP Conventions

## Action ID Naming

Action IDs use **dot notation**: `domain.verb` or `domain.noun_verb`.

| Domain       | Description                  |
|-------------|------------------------------|
| `blueprint` | Blueprint asset management   |
| `component` | Component property management|
| `editor`    | Level actors & viewport      |
| `layout`    | Node auto-layout             |
| `node`      | Blueprint graph nodes        |
| `variable`  | Variable CRUD                |
| `function`  | Function CRUD                |
| `dispatcher`| Event dispatcher management  |
| `graph`     | Graph editing operations     |
| `material`  | Material creation & editing  |
| `widget`    | UMG Widget Blueprints        |
| `input`     | Input mapping & actions      |

## AI Workflow

```
ue_actions_search("create blueprint")  →  find action IDs
ue_actions_schema("blueprint.create")  →  learn parameters
ue_actions_run("blueprint.create", {…})  →  execute
```

For batch operations:
```
ue_batch([
  {"action_id": "blueprint.create", "params": {…}},
  {"action_id": "variable.create", "params": {…}},
  {"action_id": "blueprint.compile", "params": {…}}
])
```

## Common Patterns

### Create a Blueprint with a component
```json
[
  {"action_id": "blueprint.create", "params": {"name": "BP_Lamp", "parent_class": "Actor"}},
  {"action_id": "blueprint.add_component", "params": {"blueprint_name": "BP_Lamp", "component_type": "PointLightComponent", "component_name": "Light"}},
  {"action_id": "blueprint.compile", "params": {"blueprint_name": "BP_Lamp"}}
]
```

### Add BeginPlay logic
```json
[
  {"action_id": "node.add_event", "params": {"blueprint_name": "BP_Lamp", "event_name": "ReceiveBeginPlay"}},
  {"action_id": "node.add_function_call", "params": {"blueprint_name": "BP_Lamp", "target": "Light", "function_name": "SetIntensity"}},
  {"action_id": "graph.connect_nodes", "params": {"blueprint_name": "BP_Lamp", "source_node_id": "...", "source_pin": "Then", "target_node_id": "...", "target_pin": "execute"}}
]
```

### Create a UI widget
```json
[
  {"action_id": "widget.create", "params": {"widget_name": "WBP_HUD"}},
  {"action_id": "widget.add_component", "params": {"widget_name": "WBP_HUD", "component_type": "TextBlock", "component_name": "ScoreText", "text": "Score: 0", "font_size": 24}},
  {"action_id": "widget.add_component", "params": {"widget_name": "WBP_HUD", "component_type": "Button", "component_name": "RestartBtn", "text": "Restart"}}
]
```

## Risk Levels

- **safe** — read-only or easily reversible (default)
- **moderate** — modifies or deletes something, but scoped to named object
- **destructive** — deletes entire assets, hard to undo

## Position Arrays

- **Node position**: `[X, Y]` in graph space (integers)
- **Actor location**: `[X, Y, Z]` in world space (floats)
- **Actor rotation**: `[Pitch, Yaw, Roll]` in degrees
- **Color**: `[R, G, B]` or `[R, G, B, A]` — values 0.0–1.0
