# Error Codes & Troubleshooting

## Connection Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `Not connected to Unreal` | Unreal Editor not running or MCP Bridge plugin not loaded | Start UE Editor with the project, ensure UEEditorMCP plugin is enabled |
| `Connection lost and reconnect failed` | UE Editor crashed or network issue | Restart UE Editor, the server will auto-reconnect |
| `Connection refused` | TCP port 55558 not listening | Check UE Editor is running and MCP Bridge is active |
| `Timeout` | Command took too long (>120s default) | UE may be busy (compiling shaders, etc.). Wait and retry. |

## Action Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `Unknown action: X` | Invalid action_id | Use `ue_actions_search` to find correct ID |
| `Unknown tool: X` | Tool name not recognized | Use one of the 7 tools: ue_ping, ue_actions_search, ue_actions_schema, ue_actions_run, ue_batch, ue_resources_read, ue_logs_tail |
| `Blueprint 'X' not found` | Blueprint doesn't exist at expected path | Check spelling, use `editor.list_assets` to find it |
| `Node 'X' not found` | Invalid node GUID | Use `graph.find_nodes` to get current node IDs |
| `Unknown component_type: X` | Invalid UMG component type | Use `ue_actions_schema("widget.add_component")` to see supported types |

## Batch Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `Max N actions per batch` | Batch exceeds 20 action limit | Split into multiple batches |
| `failed: N` in response | Some actions in batch failed | Check individual results, fix failing actions, set `continue_on_error: true` if partial success is OK |

## Common Gotchas

1. **Node positions overlap**: Always specify `node_position` to avoid nodes stacking at [0,0]. Use `layout.auto_selected` after placing nodes.

2. **Blueprint not compiled**: After adding nodes/variables, always end with `blueprint.compile` to validate.

3. **Connect after create**: `graph.connect_nodes` requires node GUIDs. Create nodes first, capture their IDs from the response, then connect.

4. **Case sensitivity**: Blueprint names, variable names, and function names are **case-sensitive**.

5. **Widget hierarchy**: After adding widget components, use `widget.add_child` to build the tree structure. Components are added to root by default.
