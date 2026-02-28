# UEEditorMCP - 开发计划

> 创建日期: 2026-02-13  
> 最后更新: 2026-02-23  
> 目标: 7 固定 MCP 工具 + Action Registry 架构

## 0) 状态快照（2026-02-22）

- 当前里程碑：Phase 1 / Phase 2 / Phase 3 / Phase 4 / Phase 5 / **Phase 6 均已完成**。
- **性能优化**（2026-02-15）：`ue_batch` 已从 N 次 TCP 往返优化为 **单次 TCP 往返**（通过 C++ `batch_execute`）；批处理上限从 20 提升到 50。
- 布局系统：Layout V2（Enhanced Sugiyama）已落地到 `LayoutActions.h/.cpp`。
- 当前可检索 Action 数：**141**（运行时 `ue_actions_search` 预期，重启 MCP 后生效）。
- **选区编程写入**（2026-02-17）：新增 `graph.set_selected_nodes`（按节点 ID 设置选区）和 `graph.batch_select_and_act`（批量分组选区 + 按组执行动作），支持全自动按用途拆分后逐组封装（collapse_selection_to_function / collapse_selection_to_macro / auto_comment 等）。
- **折叠为宏**（2026-02-19）：新增 `graph.collapse_selection_to_macro`，将当前选中节点折叠为蓝图宏（Macro）。复用引擎原生 `CollapseSelectionToMacro` 命令流，返回新建宏名称及前后宏数量；宏支持 Latent 节点（Delay 等），不适用于 AnimGraph。
- 新增引擎生命周期 Action：`editor.is_ready`（就绪检测）、`editor.request_shutdown`（优雅/强制关闭）。
- MVVM 扩展：新增 `widget.mvvm_remove_binding` 与 `widget.mvvm_remove_viewmodel`，用于安全清理绑定/上下文。
- 编译诊断建议：`blueprint.compile` 提供状态与计数，详细编译文本以 `editor.get_logs` / `ue_logs_tail(source="editor")` 为准。
- 自动布局命令：Blueprint Editor `Edit -> Auto Layout` 菜单入口已简化为单命令 `Auto Layout`，默认快捷键 `Ctrl+Alt+L`（有选区布局选区；无选区布局整图）。
- 文档治理：本文件为 UEEditorMCP **唯一**开发计划来源。
- 新增 `create_event_delegate` Action（2026-02-17）：创建 K2Node_CreateDelegate（"Create Event"）节点，支持在函数图内将函数绑定到 delegate 引脚，解决 CustomEvent 在函数图中不可用的问题。Patch 系统 `add_node` 同步支持 `CreateDelegate` 节点类型。
- 新增 `component.bind_event` Action（2026-02-17）：创建 `UK2Node_ComponentBoundEvent` 绑定任意 Actor 组件上的 `BlueprintAssignable` 委托（如 `OnTTSEnvelope`、`OnComponentBeginOverlap`），使用引擎标准 `InitializeComponentBoundEventParams` 初始化。同步修复 `widget.bind_event` 初始化缺陷。
- 临时计划文档（例如专题 DevPlan）在完成后需合并回本文件并删除。
- **资产缩略图能力**（2026-02-20）：新增 `editor.get_selected_asset_thumbnail` Action（返回 `image/png` 的 `base64`），并在 `ue-editor-mcp-logs` 暴露 `unreal.asset_thumbnail.get` 工具（支持 `assetPath/assetPaths/assetIds/ids` 批量输入，返回 `thumbnails[]`，尺寸限制 `<=256`）。
- **资产重命名与重定向器修复**（2026-02-20）：新增 `editor.rename_assets` Action，支持单个/批量资产重命名（`items[]`）并可自动 `FixupReferencers`（`fixup_mode: delete|leave|prompt`）。
- **Diff Against Depot 能力**（2026-02-22）：新增 `diff_against_depot` C++ Action（基于引擎 `FGraphDiffControl::DiffGraphs` 与 `DiffUtils::CompareUnrelatedObjects`），并在 `ue-editor-mcp-logs` 暴露 `unreal.asset_diff.get` 工具。支持 Blueprint 图级别节点差异（增删改动）和通用资产属性级 diff，返回结构化 JSON。Build.cs 新增 `SourceControl` 模块依赖。
- **材质系统增强**（2026-02-22）：Phase 4 已完成全部 8 项任务（P4.1-P4.8）。现有材质 Action 覆盖完整创建-编辑-编译-实例化管线。`material.set_expression_property` 已增加 UObject 引用属性支持（P5 热修，Texture / MaterialFunction / 通用 UObject）。
- **Phase 5 完成**（2026-02-22）：材质编译诊断增强（真实 `CompileErrors` + `ErrorExpressions` 返回）、`material.apply_to_component` / `material.apply_to_actor`（材质应用到关卡 Actor）、材质编辑器 Auto Layout 菜单注册——全部 4 项任务已完成。
- **Phase 6 完成**（2026-02-23）：PIE 控制（启动/停止/状态查询）、日志清空与会话分段、断言型验证工具、World Outliner 管理（Actor 重命名/文件夹组织/选择/层级查询）。新增 9 个 Action，总计 141 个。

---

## 一、架构概览 v2 — 7-Tool Unified Server

### 设计哲学

**工具数永远固定为 7 个**，不管底层 action 从 111 增长到 700。  
AI 通过 `search → schema → run` 三步流程找到并执行任何操作。

### 7 个 MCP 工具

| # | Tool | 功能 | 类型 |
|---|------|------|------|
| 1 | `ue_ping` | 连通性检查 / 版本 / 引擎版本 / 项目名 | 读 |
| 2 | `ue_actions_search` | 在 Action Registry 中检索最相关的 action | 读 |
| 3 | `ue_actions_schema` | 返回该 action 的 JSON Schema + 示例 | 读 |
| 4 | `ue_actions_run` | 执行（或 dryRun 校验）单个 action | 写 |
| 5 | `ue_batch` | 批量执行多个 action（减少多轮对话开销） | 写 |
| 6 | `ue_resources_read` | 按需读取资源（patch 规范、错误码、项目约定） | 读 |
| 7 | `ue_logs_tail` | 拉取 Editor 输出/编译错误（让模型自修复） | 读 |

### 架构图

```
VS Code / MCP Client (GitHub Copilot Chat)
        │
        │  MCP Protocol (stdio)    ← 7 tools
        v
  server_unified.py  (单进程, 7 MCP tools)
        │
        ├── Action Registry (Python-only 元数据)
        │     ├── 131 action 定义 (id, tags, schema, examples)
        │     └── 关键词搜索引擎 (keyword matching)
        │
        ├── Resources (静态文档)
        │     ├── patch_spec.md
        │     ├── error_codes.md
        │     └── conventions.md
        │
        │  TCP/JSON (port 55558)   ← 现有协议不变
        v
  C++ Plugin (FMCPServer → FMCPClientHandler)
        │
        │  GameThread dispatch
        v
  FEditorAction subclasses (~150 个)
        │
        v
  Unreal Editor
```

### AI 使用流程

```
1. ue_ping()                                    → 确认连接
2. ue_actions_search("create actor blueprint")   → 返回匹配的 action ID 列表
3. ue_actions_schema("blueprint.create")         → 返回 JSON Schema + 示例
4. ue_actions_run("blueprint.create", {...})     → 执行
   或
   ue_batch([                                    → 批量执行
     {"action": "blueprint.create", "args": {...}},
     {"action": "blueprint.add_component", "args": {...}},
     {"action": "blueprint.compile", "args": {...}},
   ])
```

---

## 二、Action Registry 设计

### Action 定义结构

```python
{
    "id": "blueprint.create",           # 唯一标识 (域.动词)
    "command": "create_blueprint",      # C++ 命令类型 (不变)
    "tags": ["blueprint", "create", "asset"],
    "description": "Create a new Blueprint asset",
    "input_schema": { ... },            # JSON Schema
    "examples": [                       # 2-3 个最小可用例
        {"name": "BP_Player", "parent_class": "Character"}
    ],
    "capabilities": ["write"],          # read / write / destructive
    "risk": "safe"                      # safe / moderate / destructive
}
```

### Action ID 命名规范

| 域 | 前缀 | 示例 |
|---|------|------|
| 蓝图管理 | `blueprint.*` | `blueprint.create`, `blueprint.compile` |
| 组件 | `component.*` | `component.set_property`, `component.set_physics`, `component.bind_event` |
| 节点创建 | `node.*` | `node.add_event`, `node.add_function_call` |
| 变量 | `variable.*` | `variable.create`, `variable.set_default` |
| 函数 | `function.*` | `function.create`, `function.call`, `function.rename` |
| 事件派发器 | `dispatcher.*` | `dispatcher.create`, `dispatcher.call`, `dispatcher.bind`, `dispatcher.create_event` |
| 图操作 | `graph.*` | `graph.connect_nodes`, `graph.describe`, `graph.auto_comment`, `graph.collapse_selection_to_function`, `graph.collapse_selection_to_macro`, `graph.set_selected_nodes`, `graph.batch_select_and_act` |
| 编辑器 | `editor.*` | `editor.get_actors`, `editor.save_all`, `editor.rename_assets`, `editor.start_pie`, `editor.stop_pie`, `editor.get_pie_state`, `editor.clear_logs`, `editor.assert_log`, `editor.rename_actor_label`, `editor.set_actor_folder`, `editor.select_actors`, `editor.get_outliner_tree` |
| 材质 | `material.*` | `material.create`, `material.compile`, `material.get_summary`, `material.auto_layout`, `material.auto_comment` |
| 控件(UMG) | `widget.*` | `widget.create`, `widget.add_component` |
| MVVM 绑定 | `widget.mvvm_*` | `widget.mvvm_add_viewmodel`, `widget.mvvm_add_binding`, `widget.mvvm_get_bindings`, `widget.mvvm_remove_binding`, `widget.mvvm_remove_viewmodel` |
| 输入 | `input.*` | `input.create_action`, `input.create_mapping` |
| 布局 | `layout.*` | `layout.auto_selected`, `layout.auto_subtree`, `layout.auto_blueprint` |

### 检索算法

简单关键词匹配（无外部依赖）：

1. 将 query 分词为 keywords
2. 对每个 action 计算得分 = Σ(keyword 在 id/tags/description 中的命中次数)
3. 按得分降序返回 topK 结果
4. 返回结果包含 id + description + tags + capabilities

---

## 三、实现计划

### Phase 1: 统一 Server + Action Registry（纯 Python，零 C++ 改动）

| # | 任务 | 文件 | 状态 |
|---|------|------|------|
| 1 | 创建 Action Registry 数据结构 | `registry/__init__.py` | ✅ |
| 2 | 注册全部 action 定义（初始 111，当前 131） | `registry/actions.py` | ✅ |

### Phase 3.1: 维护修复（2026-02-14）

| # | 任务 | 文件 | 说明 | 状态 |
|---|------|------|------|------|
| **M3.1** | MVVM 变更广播一致性 | `Actions/UMGActions.cpp` | 在 `mvvm_add_viewmodel` / `mvvm_add_binding` 后广播 `OnBindingsUpdated`，降低 UMG 编译期状态不一致风险。 | ✅ |
| **M3.2** | MVVM 清理能力补齐 | `Actions/UMGActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 新增 `widget.mvvm_remove_viewmodel`；并保留 `widget.mvvm_remove_binding`。 | ✅ |
| **M3.3** | UFUNCTION 绑定支持 | `Actions/UMGActions.cpp` | `mvvm_add_binding` 支持 ViewModel 源字段为 `FProperty` 或 `UFunction(FieldNotify)`。 | ✅ |
| **M3.4** | 编译错误可观测性口径 | `README.md`, `DEVPLAN.md`, `SKILL.md` | 明确 `blueprint.compile` 与 `editor.get_logs` 的职责分工。 | ✅ |
| 3 | 实现关键词搜索 | `registry/__init__.py` | ✅ |
| 4 | 创建 7-tool 统一 server | `server_unified.py` | ✅ |
| 5 | 实现 `ue_batch` 批量执行 | `server_unified.py` | ✅ |
| 6 | 实现 `ue_resources_read` | `server_unified.py` + `resources/` | ✅ |
| 7 | 实现 `ue_logs_tail` | `server_unified.py` | ✅ |
| 8 | 创建资源文档 | `resources/*.md` | ✅ |
| 9 | 更新 `mcp.json` | `.vscode/mcp.json` | ✅ |

### Phase 3.2: 性能优化（2026-02-15）

| # | 任务 | 文件 | 说明 | 状态 |
|---|------|------|------|------|
| **O1** | `ue_batch` 真批处理 | `server_unified.py` | 重写 `ue_batch`：从 Python 层逐条 TCP 往返改为通过 C++ `batch_execute` 单次 TCP 往返。N 个 action 从 N 次 TCP 降为 1 次 TCP。 | ✅ |
| **O2** | 批处理上限提升 | `server_unified.py` | `_MAX_BATCH` 从 20 提升到 50（与 C++ 侧 `MaxBatchSize=50` 对齐）。 | ✅ |
| **O3** | 快速路径文档 | `SKILL.md` | 新增 Fast Path 工作流：当 action ID 和参数已知（SKILL.md Part C）时，跳过 `search`/`schema`，直接 `ue_batch`。减少 2 次 MCP 工具调用。 | ✅ |
| **O4** | `_resolve_action_to_cpp_command` | `server_unified.py` | 提取 action_id → C++ command 解析（含 UMG dispatch）为独立函数，供 `_execute_action` 和 `ue_batch` 共用。 | ✅ |
| **O5** | `blueprint.describe_full` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 新增 C++ Action，一次调用返回蓝图完整信息（summary + 所有 graph 拓扑 + variables + components），消除"1 summary + N describe"的多次往返。支持 `include_pin_details` 和 `include_function_signatures` 精简/详细模式切换。 | ✅ |
| **O6** | `graph.describe` 精简模式 | `Actions/GraphActions.h/.cpp`, `registry/actions.py` | 为 `graph.describe_enhanced` 增加 `compact` 参数：`compact=true` 时省略 `metadata`、`function_signature`、`variable_references`，Pin 使用轻量序列化（category + direction + linked_to + default_value），大图响应从 50-100KB 降至 10-20KB。 | ✅ |

### Phase 3.3: 日志上下文独立服务（2026-02-20）

| # | 任务 | 文件 | 说明 | 状态 |
|---|------|------|------|------|
| **L1** | `FMCPLogCapture` 扩展 | `MCPLogCapture.h/.cpp` | ring buffer 上限升级为 10000 行 / 5MB；新增 seq、UTC 时间戳、recent-data 检测、`GetSince()` 增量读取。 | ✅ |
| **L2** | `FGetUnrealLogsAction` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp` | 新增 `get_unreal_logs` 命令：支持 `cursor=live:<seq>`、过滤、`tail_lines`、`max_bytes`、2 秒 recent 判定。 | ✅ |
| **L3** | 独立 MCP server | `Python/ue_editor_mcp/server_unreal_logs.py` | 新增 `ue-editor-mcp-logs` 工具组：`unreal.logs.get` 与 `unreal.asset_thumbnail.get`；日志能力 `auto` 优先 live，失败回退 saved/offline_saved。 | ✅ |
| **L4** | 工作区注册 | `.vscode/mcp.json` | 新增 `ue-editor-mcp-logs` server 条目，和 `ue-editor-mcp` 并行运行。 | ✅ |
| **L5** | 文档与测试 | `README.md`, `tests/test_unreal_logs_server.py` | 增加工具用法、cursor 增量建议；补充 saved tail / cursor delta / filter 基础测试。 | ✅ |
| **L6** | 资产缩略图工具接入 | `server_unreal_logs.py`, `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py`, `README.md` | 新增 `unreal.asset_thumbnail.get`（调用 C++ `get_selected_asset_thumbnail`），支持 `assetPath/assetPaths/assetIds/ids` 输入、返回 `thumbnails[]`，并限制缩略图尺寸 `<=256`。 | ✅ |
| **L7** | 批量资产重命名 + Redirector Fixup | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py`, `README.md` | 新增 `editor.rename_assets`，支持单个字段或 `items[]` 批量重命名；默认自动执行 `IAssetTools::FixupReferencers`，可选 `fixup_mode=delete|leave|prompt`。 | ✅ |
| **L8** | Diff Against Depot | `Actions/EditorDiffActions.h/.cpp`, `MCPBridge.cpp`, `Build.cs`, `server_unreal_logs.py`, `README.md` | 新增 `diff_against_depot` C++ Action（基于 `FGraphDiffControl::DiffGraphs` + `DiffUtils::CompareUnrelatedObjects`），在 `ue-editor-mcp-logs` 注册 `unreal.asset_diff.get` 工具；返回结构化 JSON diff 数据（节点/引脚/属性级别）。Build.cs 新增 `SourceControl` 依赖。 | ✅ |

### Phase 2: C++ 增强 — 图描述 / 日志 / 批量 / 崩溃保护

> **目标**：补齐 AI 自主编排蓝图所需的四项 C++ 基础能力。  
> **前置**：Phase 1 已完成；C++ 侧 Action 基类管线不变（数量随迭代增加，当前与状态快照一致）。  
> **优先级**：P2.1 → P2.2 → P2.3 → P2.4（图拓扑数据是后续 Patch 系统的前提）。

| # | 任务 | 文件 | 说明 | 状态 |
|---|------|------|------|------|
| **P2.1** | `FDescribeGraphAction` | `Actions/NodeActions.h/.cpp` | 一次性转储完整图拓扑：所有节点（GUID / 类型 / Title / 位置）+ 所有 Pin（名称 / 方向 / 类别 / LinkedTo / DefaultValue）+ 所有连接边。返回 JSON 结构体。与 `get_blueprint_summary` 互补：后者宏观概要，前者面向 AI 的精确图谱。 | ✅ |
| **P2.2** | SEH 崩溃保护 | `EditorAction.cpp`, `MCPBridge.cpp` | 实装 `ExecuteWithCrashProtection()` 的 `__try/__except` 包裹（仅 MSVC / Win64）；`ExecuteCommandSafe()` 补充 `try/catch` 顶层兜底。崩溃时返回结构化错误而非进程退出。 | ✅ |
| **P2.3** | `FGetEditorLogsAction` | `Actions/EditorActions.h/.cpp`, `MCPLogCapture.h/.cpp` | 注册为 `get_editor_logs` 命令。核心：自定义 `FOutputDevice` 子类（`FMCPLogCapture`）挂载到 `GLog`，维护 ring buffer（最近 500 条），按 Category/Verbosity 过滤；返回 `{lines: [{timestamp, category, verbosity, message}], total, filtered}`。 | ✅ |
| **P2.4** | `FBatchExecuteAction` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp` | 注册为 `batch_execute` 命令。接收 `{commands: [{type, params}, ...], stop_on_error: bool}`。单次 TCP 请求 → GameThread 上串行调用 `MCPBridge::ExecuteCommand`；支持 `stop_on_error` 控制是否中途停止。**非原子**（不回滚已成功命令），但大幅减少 TCP 往返。 | ✅ |
| **P2.5** | Python 侧 Action 注册 + Log 桥接 | `registry/actions.py`, `server_unified.py` | 新增 `graph.describe` / `editor.get_logs` / `batch.execute` 三条 ActionDef。`ue_logs_tail` 增加 `source` 参数（`python` / `editor` / `both`），当 `source=editor` 时转发到 C++ `get_editor_logs`。 | ✅ |
| **P2.6** | 自定义 LogCategory | 全局头 + 各 Action 文件 | 用 `DECLARE_LOG_CATEGORY_EXTERN(LogMCP, Log, All)` 替代 `LogTemp`，所有 MCP 相关日志输出走统一 Category，便于 `FGetLogsAction` 过滤。 | ✅ |
| **P2.7** | `FGetSelectedNodesAction` | `Actions/NodeActions.h/.cpp` | `graph.get_selected_nodes` — 只读动作，读取当前编辑器中选中的蓝图节点（GUID / class / title / position / pins / edges）。支持无参数调用（自动回退到焦点编辑器）或指定 `blueprint_name`/`graph_name`。复用 `FDescribeGraphAction::SerializePinToJson` 进行 pin 序列化。 | ✅ |
| **P2.8** | `FCollapseSelectionToFunctionAction` | `Actions/NodeActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 新增 `graph.collapse_selection_to_function` 写动作：复用引擎原生命令 `Collapse Selection To Function`，基于当前蓝图编辑器选区执行折叠，返回新建函数名列表并触发自动保存链路。 | ✅ |
| **P2.8b** | `FCollapseSelectionToMacroAction` | `Actions/NodeActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 新增 `graph.collapse_selection_to_macro` 写动作：复用引擎原生命令 `CollapseSelectionToMacro`，将当前蓝图编辑器选区折叠为新宏（Macro），返回新建宏名列表及 before/after 宏数量。宏支持 Latent 节点（Delay 等）；不适用于 AnimGraph。 | ✅ |
| **P2.9** | `FRenameBlueprintFunctionAction` | `Actions/NodeActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 新增 `function.rename` 写动作：通过 `FBlueprintEditorUtils::RenameGraph()` 实现蓝图自定义函数重命名，自动更新所有 `K2Node_CallFunction` 调用点引用、`FunctionEntry`/`FunctionResult` 节点引用及本地变量作用域。返回 `old_name`/`new_name`/`exact_match`。 | ✅ |
| **P2.10** | `FRenameBlueprintMacroAction` | `Actions/NodeActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 新增 `macro.rename` 写动作：通过 `FBlueprintEditorUtils::RenameGraph()` 实现蓝图自定义宏（Macro Graph）重命名，自动更新所有宏实例节点引用。遍历 `Blueprint->MacroGraphs` 查找目标宏，支持同名碰撞检测和 `already_named` 短路返回。返回 `old_name`/`new_name`/`exact_match`。 | ✅ |

#### P2 详细设计

<details>
<summary>P2.1 FDescribeGraphAction — 输入/输出 Schema</summary>

**C++ 命令**: `describe_graph`  
**Action ID**: `graph.describe`

**输入 Schema**:
```json
{
  "type": "object",
  "properties": {
    "blueprint_name": { "type": "string", "description": "蓝图包名或资产路径" },
    "graph_name": { "type": "string", "default": "EventGraph", "description": "目标图名（EventGraph / 函数名 / 宏名）" },
    "include_hidden_pins": { "type": "boolean", "default": false },
    "include_orphan_pins": { "type": "boolean", "default": false }
  },
  "required": ["blueprint_name"]
}
```

**输出 Schema**:
```json
{
  "success": true,
  "graph_name": "EventGraph",
  "node_count": 12,
  "edge_count": 8,
  "nodes": [
    {
      "node_id": "A1B2C3D4...",
      "node_class": "K2Node_Event",
      "node_title": "Event BeginPlay",
      "pos_x": 100, "pos_y": 200,
      "comment": "",
      "pins": [
        {
          "pin_name": "then",
          "direction": "output",
          "category": "exec",
          "sub_category": "",
          "is_connected": true,
          "linked_to": [{"node_id": "E5F6...", "pin_name": "execute"}],
          "default_value": ""
        }
      ]
    }
  ],
  "edges": [
    {
      "from_node": "A1B2C3D4...", "from_pin": "then",
      "to_node": "E5F6...", "to_pin": "execute"
    }
  ]
}
```

**实现要点**:
- 复用 `FBlueprintNodeAction` 基类的 `GetTargetGraph()`
- 遍历 `Graph->Nodes` + 每节点 `Pins[]`，序列化为 JSON 数组
- `edges[]` 从各 Pin 的 `LinkedTo` 去重构建（每条边只出现一次）
- Pin 序列化逻辑从 `FGetNodePinsAction` 提取为共用 helper `SerializePinToJson()`

</details>

<details>
<summary>P2.2 SEH 崩溃保护 — 实现方式</summary>

**`EditorAction.cpp` — `ExecuteWithCrashProtection()`**:
```cpp
TSharedPtr<FJsonObject> FEditorAction::ExecuteWithCrashProtection(const TSharedPtr<FJsonObject>& Params)
{
#if PLATFORM_WINDOWS
    __try
    {
        return ExecuteInternal(Params);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        auto Err = MakeShared<FJsonObject>();
        Err->SetBoolField(TEXT("success"), false);
        Err->SetStringField(TEXT("error"),
            FString::Printf(TEXT("SEH exception 0x%08X in action '%s'"), GetExceptionCode(), *CommandType));
        return Err;
    }
#else
    return ExecuteInternal(Params);
#endif
}
```

**`MCPBridge.cpp` — `ExecuteCommandSafe()`**:
```cpp
TSharedPtr<FJsonObject> UMCPBridge::ExecuteCommandSafe(const FString& Type, const TSharedPtr<FJsonObject>& Params)
{
    try
    {
        return ExecuteCommand(Type, Params);
    }
    catch (const std::exception& e)
    {
        auto Err = MakeShared<FJsonObject>();
        Err->SetBoolField(TEXT("success"), false);
        Err->SetStringField(TEXT("error"), FString::Printf(TEXT("C++ exception: %hs"), e.what()));
        return Err;
    }
    catch (...)
    {
        auto Err = MakeShared<FJsonObject>();
        Err->SetBoolField(TEXT("success"), false);
        Err->SetStringField(TEXT("error"), TEXT("Unknown C++ exception"));
        return Err;
    }
}
```

</details>

<details>
<summary>P2.3 FGetLogsAction — 日志捕获架构</summary>

**新增类**: `FMCPLogCapture : public FOutputDevice`  
**文件**: `Source/Public/MCPLogCapture.h` / `Source/Private/MCPLogCapture.cpp`

```cpp
class UEEDITORMCP_API FMCPLogCapture : public FOutputDevice
{
public:
    static FMCPLogCapture& Get();  // 单例
    void Start();                  // GLog->AddOutputDevice(this)
    void Stop();                   // GLog->RemoveOutputDevice(this)

    struct FLogEntry
    {
        double      Timestamp;
        FName       Category;
        ELogVerbosity::Type Verbosity;
        FString     Message;
    };

    TArray<FLogEntry> GetRecent(int32 Count = 100, const FString& CategoryFilter = TEXT("")) const;
    void Clear();

protected:
    void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;

private:
    mutable FCriticalSection Lock;
    TArray<FLogEntry> RingBuffer;  // 固定 500 条
    int32 WriteIndex = 0;
    bool bFull = false;
};
```

- `FGetLogsAction` 命令 `get_editor_logs`：调用 `FMCPLogCapture::Get().GetRecent(count, category)`
- 插件 `StartupModule()` 中调用 `FMCPLogCapture::Get().Start()`
- 插件 `ShutdownModule()` 中调用 `Stop()`

</details>

<details>
<summary>P2.4 FBatchExecuteAction — 批量执行协议</summary>

**C++ 命令**: `batch_execute`

**输入**:
```json
{
  "commands": [
    {"type": "create_blueprint", "params": {"name": "BP_Test", "parent_class": "Actor"}},
    {"type": "add_blueprint_component", "params": {"blueprint_name": "BP_Test", "component_class": "StaticMeshComponent", "component_name": "Mesh"}},
    {"type": "compile_blueprint", "params": {"blueprint_name": "BP_Test"}}
  ],
  "stop_on_error": true
}
```

**输出**:
```json
{
  "success": true,
  "total": 3,
  "executed": 3,
  "succeeded": 3,
  "failed": 0,
  "results": [
    {"index": 0, "type": "create_blueprint", "success": true, ...},
    {"index": 1, "type": "add_blueprint_component", "success": true, ...},
    {"index": 2, "type": "compile_blueprint", "success": true, ...}
  ]
}
```

**实现要点**:
- 在 GameThread 上同步串行调用 `MCPBridge::ExecuteCommand()` / `ExecuteCommandSafe()`
- 结果按索引收集，`stop_on_error=true` 时遇第一个失败即停止
- 最大 50 条命令（C++ 侧限制，Python 侧 `ue_batch` 保持 20 条软限制）
- `success` 字段：全部成功 → `true`，有失败 → `false`

</details>

### Phase 3: Patch 系统 — 声明式图编辑

> **目标**：提供高层抽象，让 AI 用一个 JSON patch 文档描述蓝图图的目标状态变更，而非逐条调用底层 Action。  
> **前置**：P2.1（`graph.describe`）必须完成，P2.4（batch）推荐完成。  
> **风险评级**：moderate — 涉及 Pin 解析、类型匹配、节点工厂调用，需充分测试。

| # | 任务 | 文件 | 说明 | 状态 |
|---|------|------|------|------|
| **P3.1** | `FGraphDescribeEnhancedAction` | `Actions/GraphActions.h/.cpp` | `graph.describe_enhanced` — 在 P2.1 基础上增加：变量引用追踪、函数签名内联展开、Pin 类型的完整 `FEdGraphPinType` 序列化（含 `PinSubCategoryObject` 的类路径）、节点 metadata（BreakpointEnabled / EnabledState）。 | ✅ |
| **P3.2** | Patch Ops 定义 | `Actions/GraphActions.h` | 定义 `EPatchOpType` 枚举和 `FPatchOp` 结构体。支持的 op 类型：`add_node` / `remove_node` / `set_node_property` / `connect` / `disconnect` / `add_variable` / `set_variable_default` / `set_pin_default`。每个 op 有独立的参数 schema。 | ✅ |
| **P3.3** | `FApplyPatchAction` | `Actions/GraphActions.cpp` | `graph.apply_patch` — 接收 `{blueprint_name, graph_name, ops: [...]}` 的 patch 文档。解释器逐条执行 op：安全模式下先全部 validate，再逐条 execute。单个 op 失败时可 `continue_on_error` 或停止。成功后自动编译蓝图。 | ✅ |
| **P3.4** | `FValidatePatchAction` | `Actions/GraphActions.cpp` | `graph.validate_patch` — dryRun 模式，只执行 validate 阶段，不实际修改图。返回每个 op 的验证结果（合法 / 警告 / 错误），让 AI 预检 patch 合法性。 | ✅ |
| **P3.5** | Python 侧 Patch ActionDef | `registry/actions.py` | 新增 `graph.describe_enhanced` / `graph.apply_patch` / `graph.validate_patch` 三条 ActionDef，含完整 input_schema 和 examples。 | ✅ |
| **P3.6** | Patch 规范文档更新 | `resources/patch_spec.md` | 更新 patch_spec.md，加入完整的 op 类型参考、约束规则、示例 patch 文档（从空图创建 BeginPlay → PrintString 的完整流程）。 | ✅ |

#### P3 详细设计

<details>
<summary>P3 Patch 文档格式示例</summary>

```json
{
  "blueprint_name": "BP_MyActor",
  "graph_name": "EventGraph",
  "continue_on_error": false,
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
      }
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

**节点引用规则**:
- `"id": "new_xxx"` — patch 内新增节点的临时 ID（同一 patch 内引用）
- `"node": "A1B2C3D4..."` — 已存在节点的 NodeGuid（从 `graph.describe` 获取）
- `"node": "$last_node"` — 上下文别名（复用现有 Context 机制）

</details>

<details>
<summary>P3 Patch 执行流程</summary>

```
graph.validate_patch(patch)          ← dryRun: 校验所有 op
    │  ↓ 全部 OK
graph.apply_patch(patch)             ← 真正执行
    │
    ├── Phase A: Validate All Ops    ← 在图上做只读检查
    │   ├── 检查节点类型是否可创建
    │   ├── 检查 Pin 名/方向/类型是否兼容
    │   └── 检查引用的 node ID 是否存在（或为 patch 内新 ID）
    │
    ├── Phase B: Execute Ops         ← 逐条执行修改
    │   ├── add_node → 复用现有 FAdd*NodeAction 的核心逻辑
    │   ├── connect → 复用 FConnectBlueprintNodesAction 的核心逻辑
    │   └── ... 每个 op 结果记录到 results[]
    │
    └── Phase C: Post-Execute
        ├── MarkBlueprintModified()
        ├── CompileBlueprint()
        └── 返回完整结果 {success, results: [{op_index, op_type, success, ...}]}
```

</details>

<details>
<summary>P3 Op 参数参考</summary>

| Op | 必选参数 | 可选参数 | 说明 |
|---|---|---|---|
| `add_node` | `id`, `node_type` | `event_name` / `function_name` / `macro_name` / `class_name` / `defaults` / `pos_x` / `pos_y` | 创建节点并记录临时 ID 映射 |
| `remove_node` | `node` | — | 删除节点（自动断开所有连接） |
| `set_node_property` | `node`, `property`, `value` | — | 设置节点属性（Comment / EnabledState 等） |
| `connect` | `from.node`, `from.pin`, `to.node`, `to.pin` | — | 连接两个 Pin |
| `disconnect` | `node`, `pin` | `target_node`, `target_pin` | 断开 Pin 的连接（可指定目标或断开全部） |
| `add_variable` | `name`, `type` | `default_value`, `category`, `is_instance_editable` | 向蓝图添加变量 |
| `set_variable_default` | `name`, `value` | — | 设置变量默认值 |
| `set_pin_default` | `node`, `pin`, `value` | — | 设置 Pin 的默认字面量 |

</details>

### Phase 4: 材质系统增强 — 图查看 / 布局 / 注释 / 参数扩展（2026-02-22）

> **目标**：将现有 10 个材质 Action 从"可用"提升到"好用"——补齐图查看、自动布局、自动注释三大开发体验能力，同时扩展常用参数节点类型。  
> **前置**：Phase 1-3 均已完成；材质基础 Action（`material.create` / `add_expression` / `connect_*` / `compile` / `create_instance`）已可用。  
> **架构基础**：`UMaterialGraph : UEdGraph`、`UMaterialGraphNode : UMaterialGraphNode_Base : UEdGraphNode`，与蓝图图共享同一基类体系（`NodePosX/Y`、`UEdGraphPin`、`UEdGraphNode_Comment`），布局/注释核心算法可从蓝图侧复用。  
> **风险评级**：low-moderate — 材质图与蓝图图共享 90% 基础设施，主要适配点在数据流差异和注释节点创建路径。

#### 现有材质 Action 盘点（已完成，1563 行 C++）

| # | Action ID | C++ Command | 功能 | 状态 |
|---|-----------|-------------|------|------|
| 1 | `material.create` | `create_material` | 创建材质（domain / blend_mode / blendable_location） | ✅ |
| 2 | `material.add_expression` | `add_material_expression` | 添加表达式节点（~50 种类型，含 Custom HLSL） | ✅ |
| 3 | `material.connect_expressions` | `connect_material_expressions` | 节点间连线（~20 种目标类型硬编码） | ✅ |
| 4 | `material.connect_to_output` | `connect_to_material_output` | 连接到材质主输出（11 个属性） | ✅ |
| 5 | `material.set_expression_property` | `set_material_expression_property` | 通过反射设置节点属性 | ✅ |
| 6 | `material.set_property` | `set_material_property` | 设置材质级属性（ShadingModel 等） | ✅ |
| 7 | `material.compile` | `compile_material` | 编译材质 | ✅ |
| 8 | `material.create_instance` | `create_material_instance` | 创建 MaterialInstanceConstant（scalar/vector 覆写） | ✅ |
| 9 | `material.create_post_process_volume` | `create_post_process_volume` | 创建后处理体积 Actor | ✅ |
| 10 | `blueprint.create_colored_material` | `create_colored_material` | 快捷纯色材质（遗留） | ✅ |

#### 已支持的表达式类型（ExpressionClassMap，约 50 种）

| 分类 | 类型 |
|------|------|
| **Custom HLSL** | `Custom`（Code / Description / OutputType / 动态 Inputs） |
| **参数** | `ScalarParameter`（ParameterName / DefaultValue）、`VectorParameter` |
| **常量** | `Constant`、`Constant2/3/4Vector` |
| **数学** | Add / Subtract / Multiply / Divide / Power / Sqrt / Abs / Min / Max / Clamp / Saturate / Floor / Ceil / Frac / OneMinus / Step / SmoothStep |
| **三角** | Sin、Cos |
| **向量** | DotProduct / CrossProduct / Normalize / AppendVector / ComponentMask |
| **程序化** | Noise / Time / Panner |
| **场景** | SceneTexture / SceneDepth / ScreenPosition / TextureCoordinate / TextureSample / PixelDepth / WorldPosition / CameraPosition |
| **控制** | If / Lerp |
| **导数** | DDX / DDY |

#### P4 任务列表

| # | 任务 | 文件 | 说明 | 优先级 | 状态 |
|---|------|------|------|--------|------|
| **P4.1** | `material.get_summary` | `Actions/MaterialActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 只读 Action，一次性返回材质图完整结构：所有节点（名称 / 类型 / 位置 / 属性）+ 所有连接 + 材质级属性（Domain / BlendMode / ShadingModel）。用于调试验证和 AI 自省材质图状态。遍历 `Material->GetExpressionCollection().Expressions` + `Context.MaterialNodeMap`。 | P0 | ✅ |
| **P4.2** | 通用连线方案 | `Actions/MaterialActions.cpp` | 将 `ConnectToExpressionInput()` 从 ~20 种类型的硬编码 `Cast<>` 重构为通用 `GetInput(index)` 方案（`UMaterialExpression::GetInput(int32)` 返回 `FExpressionInput*`，按名称匹配 `GetInputName(index)`）。新增表达式类型无需再改连线代码。 | P1 | ✅ |
| **P4.3** | `TextureParameter` + Texture 采样设置 | `Actions/MaterialActions.cpp`, `registry/actions.py` | 在 `ExpressionClassMap` 中新增 `TextureParameter`（`UMaterialExpressionTextureObjectParameter`）和 `TextureSampleParameter2D`。支持通过资产路径设置 `Texture` 属性（`UObject*` 引用赋值，需 `LoadObject<UTexture>`）。同步在 `create_instance` 中支持 `texture_parameters` 覆写。 | P1 | ✅ |
| **P4.4** | `material.auto_layout` | `Actions/MaterialActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 材质图自动布局。核心策略：基于数据流拓扑排序的分层布局（从材质输出逆向沿 Input 做最长路径分层）。布局完成后调用 `MaterialGraph->RebuildGraph()` 同步位置到 `MaterialExpressionEditorX/Y`。 | P1 | ✅ |
| **P4.5** | `material.auto_comment` | `Actions/MaterialActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 材质图自动注释。包围盒计算和碰撞避让逻辑（5 次迭代，60px 间距）。创建路径：`NewObject<UMaterialExpressionComment>(Material)` → 设置 `Text` / `SizeX` / `SizeY` / `CommentColor` → `Material->GetExpressionCollection().AddExpression()` → `MaterialGraph->RebuildGraph()`。 | P1 | ✅ |
| **P4.6** | `material.remove_expression` | `Actions/MaterialActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 按 `node_name` 从 Context 查找表达式，断开所有连接（含材质输出），从 `ExpressionCollection` 移除，取消 Context 注册。支持单个或批量删除。 | P2 | ✅ |
| **P4.7** | `StaticSwitchParameter` + 实例 Switch 覆写 | `Actions/MaterialActions.cpp`, `registry/actions.py` | 在 `ExpressionClassMap` 中新增 `StaticSwitchParameter`（`UMaterialExpressionStaticSwitchParameter`）和 `StaticComponentMaskParameter`（`UMaterialExpressionStaticComponentMaskParameter`）。在 `create_instance` 中新增 `static_switch_parameters` 覆写支持（`SetStaticSwitchParameterValueEditorOnly`）。 | P2 | ✅ |
| **P4.8** | `MaterialFunction` 引用 | `Actions/MaterialActions.h/.cpp`, `registry/actions.py` | 在 `ExpressionClassMap` 中新增 `MaterialFunctionCall`（`UMaterialExpressionMaterialFunctionCall`），支持通过资产路径加载 `UMaterialFunction`，调用 `SetMaterialFunction()` 绑定。 | P3 | ✅ |

#### P4 详细设计

<details>
<summary>P4.1 material.get_summary — 输入/输出 Schema</summary>

**C++ 命令**: `get_material_summary`  
**Action ID**: `material.get_summary`

**输入 Schema**:
```json
{
  "type": "object",
  "properties": {
    "material_name": { "type": "string", "description": "材质名称或资产路径" }
  },
  "required": ["material_name"]
}
```

**输出 Schema**:
```json
{
  "success": true,
  "name": "M_PostProcess_CRT",
  "path": "/Game/Materials/M_PostProcess_CRT",
  "domain": "PostProcess",
  "blend_mode": "Opaque",
  "shading_model": "Unlit",
  "two_sided": false,
  "expression_count": 8,
  "expressions": [
    {
      "node_name": "time_node",
      "class": "Time",
      "pos_x": -600, "pos_y": 0,
      "properties": {}
    },
    {
      "node_name": "custom_hlsl",
      "class": "Custom",
      "pos_x": -200, "pos_y": 0,
      "properties": {"Code": "return sin(Time);", "OutputType": "CMOT_Float1"}
    }
  ],
  "connections": [
    {"source": "time_node", "source_output": 0, "target": "custom_hlsl", "target_input": "Time"},
    {"source": "custom_hlsl", "source_output": 0, "target": "$output", "target_input": "EmissiveColor"}
  ]
}
```

**实现要点**:
- 遍历 `Material->GetExpressionCollection().Expressions` 收集所有表达式
- 使用 `Context.MaterialNodeMap` 反向查找 `node_name`（无名称的表达式用 `$expr_N` 临时命名）
- 连接信息从各表达式的输入引脚遍历 `FExpressionInput::Expression` 指针反查
- 材质主输出连接通过 `UMaterialEditorOnlyData` 的 11 个属性检测

</details>

<details>
<summary>P4.2 通用连线重构 — 设计方案</summary>

**现状问题**：`ConnectToExpressionInput()` 硬编码 ~20 种 `Cast<UMaterialExpressionXxx>` 分支，每新增一种表达式类型需手动添加连线支持。

**目标方案**：使用 `UMaterialExpression::GetInputs()` 通用接口：
```cpp
bool ConnectToExpressionInput(UMaterialExpression* SourceExpr, int32 OutputIndex,
    UMaterialExpression* TargetExpr, const FString& InputName, FString& OutError) const
{
    // 1. 获取所有输入
    const TArray<FExpressionInput*> Inputs = TargetExpr->GetInputs();
    
    // 2. 按名称匹配
    for (FExpressionInput* Input : Inputs)
    {
        if (Input && TargetExpr->GetInputName(InputIndex).ToString().Equals(InputName, ESearchCase::IgnoreCase))
        {
            Input->Expression = SourceExpr;
            Input->OutputIndex = OutputIndex;
            return true;
        }
    }
    
    // 3. Custom 节点特殊处理：动态添加输入
    if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(TargetExpr))
    {
        FCustomInput NewInput;
        NewInput.InputName = FName(*InputName);
        NewInput.Input.Expression = SourceExpr;
        NewInput.Input.OutputIndex = OutputIndex;
        Custom->Inputs.Add(NewInput);
        return true;
    }
    
    OutError = FString::Printf(TEXT("Input '%s' not found on expression '%s'"), *InputName, *TargetExpr->GetClass()->GetName());
    return false;
}
```

**向后兼容**：保留现有硬编码分支作为 fallback（部分旧版本引擎 `GetInputs()` 可能不完整），优先走通用路径。

</details>

<details>
<summary>P4.4 material.auto_layout — 设计方案</summary>

**C++ 命令**: `auto_layout_material`  
**Action ID**: `material.auto_layout`

**输入 Schema**:
```json
{
  "type": "object",
  "properties": {
    "material_name": { "type": "string", "description": "材质名称" },
    "layer_spacing": { "type": "number", "default": 0, "description": ">0=固定像素, 0=自动" },
    "row_spacing": { "type": "number", "default": 0, "description": ">0=固定像素, 0=自动" }
  },
  "required": ["material_name"]
}
```

**架构基础**（UE 材质图 = UEdGraph 子类）：
```
UEdGraph
  └── UMaterialGraph                ← 材质图

UEdGraphNode
  └── UMaterialGraphNode_Base       ← 材质图节点基类
        ├── UMaterialGraphNode      ← 材质表达式节点
        └── UMaterialGraphNode_Root ← 材质根节点（输出属性）

UEdGraphNode_Comment
  └── UMaterialGraphNode_Comment    ← 材质注释节点
```

**位置双重存储 + 同步**：
- `NodePosX/Y`（UEdGraphNode 继承）← 布局直接修改
- `MaterialExpressionEditorX/Y`（UMaterialExpression）← 由 `NotifyGraphChanged()` 自动同步

**与蓝图布局的关键差异**：
| 维度 | 蓝图图 | 材质图 | 适配方案 |
|------|--------|--------|---------|
| 流类型 | Exec + Data（双流） | 纯 Data（单流） | 无 Exec/Pure 区分，全部节点平等分层 |
| 分层基准 | Exec 链从左到右 | 数据流从左到右 | 从 Root 逆向沿 Input 做拓扑分层 |
| 注释节点创建 | `UEdGraphNode_Comment` 直接添加 | 需通过 `UMaterialExpressionComment` + `AddComment()` | 专用创建路径 |

**算法步骤**（复用 + 新增）：

| 阶段 | 来源 | 说明 |
|------|------|------|
| 1. 获取材质图 | 新增 | `Material->MaterialGraph`（若为 null 则通过 `RebuildGraph()` 构建） |
| 2. 节点分类 | 新增 | 遍历 `Graph->Nodes`，区分 Root / Expression / Comment |
| 3. 数据流拓扑分层 | 新增 | 从 Root 沿 Input Pin 的 `LinkedTo` 反向 BFS，最长路径法分配 layer |
| 4. 行排序 | 复用 `FBlueprintAutoLayout` | Barycenter 交叉优化 |
| 5. 坐标计算 | 复用 `FBlueprintAutoLayout` | 宽度/高度感知的坐标分配 |
| 6. 碰撞检测 | 复用 `FBlueprintAutoLayout` | 节点重叠避让 |
| 7. 注释 resize | 复用 `FBlueprintAutoLayout` | 注释框自动包裹子节点 |
| 8. 位置写回 | 新增 | `Graph->NotifyGraphChanged()` 同步到 Expression |

</details>

<details>
<summary>P4.5 material.auto_comment — 设计方案</summary>

**C++ 命令**: `auto_comment_material`  
**Action ID**: `material.auto_comment`

**输入 Schema**:
```json
{
  "type": "object",
  "properties": {
    "material_name": { "type": "string", "description": "材质名称" },
    "comment_text": { "type": "string", "description": "注释文本" },
    "node_names": { "type": "array", "items": { "type": "string" }, "description": "要包裹的节点名称列表（从 Context.MaterialNodeMap 查找）。省略则包裹所有非注释节点。" },
    "color": { "type": "array", "items": { "type": "number" }, "minItems": 4, "maxItems": 4, "description": "RGBA 颜色 (0-1)" },
    "padding": { "type": "number", "default": 40 }
  },
  "required": ["material_name", "comment_text"]
}
```

**创建流程**（与蓝图注释的差异）：
```cpp
// 蓝图：直接创建 UEdGraphNode_Comment
UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
Graph->AddNode(CommentNode);

// 材质：需通过 UMaterialExpressionComment + MaterialGraph->AddComment()
UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Material);
Comment->Text = CommentText;
Comment->MaterialExpressionEditorX = CommentX;
Comment->MaterialExpressionEditorY = CommentY;
Comment->SizeX = CommentWidth;
Comment->SizeY = CommentHeight;
Comment->MaterialExpressionGuid = FGuid::NewGuid();
Material->GetExpressionCollection().AddExpression(Comment);
MaterialGraph->AddComment(Comment);
MaterialGraph->NotifyGraphChanged();
```

**可复用部分**（从 `FAutoCommentAction`）：
- 包围盒计算（遍历目标节点的 `NodePosX/Y` + `GetNodeSize()`）
- 碰撞避让（与已有注释框的 AABB 检测，60px 间距保持）
- 最小宽度保证（标题文本宽度估算，CJK 字符支持）

</details>

<details>
<summary>P4.3 TextureParameter — 输入 Schema 示例</summary>

**在 `material.add_expression` 中使用**：
```json
{
  "material_name": "M_Example",
  "expression_class": "TextureParameter",
  "node_name": "diffuse_tex",
  "properties": {
    "ParameterName": "DiffuseTexture",
    "Texture": "/Game/Textures/T_Default_D"
  }
}
```

**在 `material.create_instance` 中覆写**：
```json
{
  "instance_name": "MI_Example_01",
  "parent_material": "M_Example",
  "texture_parameters": {
    "DiffuseTexture": "/Game/Textures/T_Brick_D"
  }
}
```

**实现要点**：
- `SetExpressionProperties()` 中检测 `FObjectProperty` 类型，通过 `LoadObject<UTexture>()` 加载资产引用
- `create_instance` 新增 `texture_parameters` 处理块：`NewInstance->SetTextureParameterValueEditorOnly(FName, UTexture*)`

</details>

### Phase 5: 材质管线增强 — 编译诊断 / 材质应用 / 编辑器菜单（2026-02-22 调研）

> **目标**：补齐材质工作流的三大缺口——编译错误可观测性、运行时/编辑器材质应用能力、材质编辑器 Auto Layout 菜单命令。  
> **前置**：Phase 4 已完成（材质图查看 / 布局 / 注释 / 编译）。  
> **风险评级**：low — 全部基于已有引擎 API，无需自建底层能力。

#### 调研结果

<details>
<summary>调研 1：材质编译错误获取 API（源码级详细调研 2026-02-22）</summary>

**现状问题**：当前 `material.compile`（`FCompileMaterialAction`）调用 `PreEditChange` → `PostEditChange` → `ForceRecompileForRendering` 后，`error_count` / `warning_count` **始终返回 0**——因为没有实际读取编译结果。

---

##### 1. CompileErrors / ErrorExpressions 声明

**文件**: `Engine/Source/Runtime/Engine/Public/MaterialShared.h`

成员变量（`class FMaterial` 内，`#if WITH_EDITOR` 块）：
```cpp
// MaterialShared.h L2917-2920
TArray<FString> CompileErrors;
/** List of material expressions which generated a compiler error during the last compile. */
TArray<TObjectPtr<UMaterialExpression>> ErrorExpressions;
```

访问器（同一个 `#if WITH_EDITOR` 块）：
```cpp
// MaterialShared.h L2561-2563
const TArray<FString>& GetCompileErrors() const { return CompileErrors; }
void SetCompileErrors(const TArray<FString>& InCompileErrors) { CompileErrors = InCompileErrors; }
const TArray<UMaterialExpression*>& GetErrorExpressions() const { return ObjectPtrDecay(ErrorExpressions); }
```

> `CompileErrors[i]` 与 `ErrorExpressions[i]` 是**平行数组**——第 i 条错误文本由第 i 个表达式节点产生。

---

##### 2. 错误写入流程

**2a. 编译入口：清空旧错误**

| 路径 | 文件 & 行号 | 代码 |
|------|------------|------|
| 老翻译器 | `HLSLMaterialTranslator.cpp L1402-1403` | `Material->CompileErrors.Empty(); Material->ErrorExpressions.Empty();` |
| 新翻译器 | `MaterialShared.cpp L3454-3455` | `CompileErrors.Empty(); ErrorExpressions.Empty();` |
| CacheShaders 快路径 | `MaterialShared.cpp L3103` | `CompileErrors.Empty();` |

**2b. 核心错误写入：`FHLSLMaterialTranslator::Error()`**

`Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp L4769-4852`

```cpp
int32 FHLSLMaterialTranslator::Error(const TCHAR* Text)
{
    // 支持错误代理（预编译阶段临时收错）
    bool bUsingErrorProxy = (CompileErrorsSink && CompileErrorExpressionsSink);
    TArray<FString>& CompileErrors = bUsingErrorProxy ? *CompileErrorsSink : Material->CompileErrors;
    TArray<TObjectPtr<UMaterialExpression>>& ErrorExpressions = bUsingErrorProxy
        ? *CompileErrorExpressionsSink : Material->ErrorExpressions;

    FString ErrorString;
    UMaterialExpression* ExpressionToError = nullptr;

    // 构建函数调用栈前缀 "(Function Foo|Bar) "
    if (CurrentFunctionStack.Num() > 1) { ... }

    // 追加节点类名前缀 "(Node TextureSample) "
    if (CurrentFunctionStack.Last()->ExpressionStack.Num() > 0) { ... }

    ErrorString += Text;

    // 去重：同一节点 + 同样错误文本 → 跳过
    if (ExpressionToError) {
        for (int32 i = 0; i < ErrorExpressions.Num(); i++) {
            if (ErrorExpressions[i] == ExpressionToError && CompileErrors[i] == ErrorString)
                return INDEX_NONE;
        }
    }

    CompileErrors.Add(ErrorString);
    ErrorExpressions.Add(ExpressionToError);
    return INDEX_NONE;
}
```

**2c. 格式化包装器 `Errorf()`**（`MaterialShared.cpp L313-318`）：
```cpp
int32 FMaterialCompiler::Errorf(const TCHAR* Format,...) {
    TCHAR ErrorText[2048];
    GET_TYPED_VARARGS(...);
    return Error(ErrorText);
}
```

**2d. 直接追加 `AppendExpressionError()`**（`HLSLMaterialTranslator.cpp L4855-4864`）：
```cpp
void FHLSLMaterialTranslator::AppendExpressionError(UMaterialExpression* Expression, const TCHAR* Text) {
    Material->ErrorExpressions.Add(Expression);
    Expression->LastErrorText = ErrorText;
    Material->CompileErrors.Add(ErrorText);
}
```

**2e. 新翻译器路径**（`MaterialShared.cpp L3472-3480`）：
```cpp
if (!Builder.Build(&Module)) {
    for (const FMaterialIRModule::FError& Error : Module.GetErrors()) {
        ErrorExpressions.Push(Error.Expression);
        CompileErrors.Push(Error.Message);
    }
    return false;
}
```

---

##### 3. 编译完成检测与同步等待

| 方法 | 阻塞? | 粒度 | 源码位置 | 签名 |
|------|--------|------|---------|------|
| `IsCompilationFinished()` | 否 | 单材质 | `MaterialShared.cpp L842-856` | `bool FMaterial::IsCompilationFinished() const` |
| `IsGameThreadShaderMapComplete()` | 否 | 单材质 | `MaterialShared.h L2669-2672` | `inline bool ... const`（原子操作） |
| `FinishCompilation()` | **是** | 单材质 | `MaterialShared.cpp L880-892` | `void FMaterial::FinishCompilation()` |
| `FinishAllCompilation()` | **是** | 全局 | `ShaderCompiler.cpp L2727-2757` | `void FShaderCompilingManager::FinishAllCompilation()` |

**推荐实现方案**：使用 `FMaterialResource::FinishCompilation()` 阻塞等待**单个材质**编译完成（比全局 `FinishAllCompilation()` 更精准，不会阻塞无关的 shader 编译）。

**`IsCompilationFinished()` 实现摘要**：
```cpp
bool FMaterial::IsCompilationFinished() const {
    if (CacheShadersPending.IsValid() && !CacheShadersPending->IsReady())
        return false;
    FinishCacheShaders();
    if (GameThreadCompilingShaderMapId != 0u)
        return !GShaderCompilingManager->IsCompilingShaderMap(GameThreadCompilingShaderMapId);
    return true;
}
```

**`FinishCompilation()` 实现摘要**：
```cpp
void FMaterial::FinishCompilation() {
    FinishCacheShaders();
    TArray<int32> ShaderMapIdsToFinish;
    AddShaderMapIDsWithUnfinishedCompilation(ShaderMapIdsToFinish);
    if (ShaderMapIdsToFinish.Num() > 0)
        GShaderCompilingManager->FinishCompilation(*GetFriendlyName(), ShaderMapIdsToFinish);
}
```

---

##### 4. `ForceRecompileForRendering()` — 触发异步编译

**文件**: `Engine/Source/Runtime/Engine/Private/Materials/Material.cpp L7210-7213`

```cpp
void UMaterial::ForceRecompileForRendering(EMaterialShaderPrecompileMode CompileMode) {
    UpdateCachedExpressionData();
    CacheResourceShadersForRendering(false, CompileMode);  // 异步！不阻塞！
}
```

> **关键**：`ForceRecompileForRendering()` 本身**不阻塞**，提交编译任务后立即返回。因此调用后必须通过 `FinishCompilation()` 等待完成再读取 `GetCompileErrors()`。

---

##### 5. `UMaterial::GetMaterialResource()` — 获取 FMaterialResource

**文件**: `Engine/Source/Runtime/Engine/Private/Materials/Material.cpp L3036-3053`

```cpp
FMaterialResource* UMaterial::GetMaterialResource(EShaderPlatform InShaderPlatform,
    EMaterialQualityLevel::Type QualityLevel = EMaterialQualityLevel::Num)
{
    if (QualityLevel == EMaterialQualityLevel::Num)
        QualityLevel = GetCachedScalabilityCVars().MaterialQualityLevel;
    return FindMaterialResource(MaterialResources, InShaderPlatform, QualityLevel, true);
}
```

> **注意**（UE 5.7 破坏性变更）：`GetMaterialResource(ERHIFeatureLevel::Type)` 已标记 `UE_DEPRECATED(5.7)`，必须使用 `EShaderPlatform` 版本。获取当前平台时使用 `GMaxRHIShaderPlatform`。

---

##### 6. 引擎内部如何读取编译错误（材质编辑器参考）

**文件**: `Engine/Source/Editor/MaterialEditor/Private/MaterialEditor.cpp`

```cpp
// L2661-2665 — Tick 中检查当前平台编译错误
FMaterialResource* CurrentResource = Material->GetMaterialResource(ShaderPlatform);
if (CurrentResource && CurrentResource->GetCompileErrors().Num() > 0) {
    bBaseMaterialFailsToCompile = true;
}

// L2950-2953 — Stats 面板读取错误
CompileErrors = MaterialResource->GetCompileErrors();
FailingExpression = MaterialResource->GetErrorExpressions();

// L3248-3259 — 构建错误消息列表
for (int32 ErrorIndex = 0; ErrorIndex < CompileErrors.Num(); ErrorIndex++) {
    FString ErrorString = FString::Printf(TEXT("[%s] %s"), *FeatureLevelName, *CompileErrors[ErrorIndex]);
    TSharedRef<FTokenizedMessage> Line = FTokenizedMessage::Create(EMessageSeverity::Error);
    Line->AddToken(FTextToken::Create(FText::FromString(ErrorString)));
    Messages.Add(Line);
}
```

---

##### 7. P5.1 推荐实现伪代码

```cpp
// FCompileMaterialAction::ExecuteInternal 改进
Material->PreEditChange(nullptr);
Material->PostEditChange();
Material->ForceRecompileForRendering();

// 阻塞等待该材质编译完成
FMaterialResource* MatResource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
if (MatResource) {
    MatResource->FinishCompilation();  // 阻塞，仅等这一个材质

    const TArray<FString>& Errors = MatResource->GetCompileErrors();
    const TArray<UMaterialExpression*>& ErrExprs = MatResource->GetErrorExpressions();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField("error_count", Errors.Num());

    TArray<TSharedPtr<FJsonValue>> ErrorArray;
    for (int32 i = 0; i < Errors.Num(); i++) {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField("message", Errors[i]);
        if (ErrExprs.IsValidIndex(i) && ErrExprs[i]) {
            Entry->SetStringField("node_name", ErrExprs[i]->GetName());
            Entry->SetStringField("node_class", ErrExprs[i]->GetClass()->GetName());
        }
        ErrorArray.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetArrayField("errors", ErrorArray);
}
```

</details>

<details>
<summary>调研 2：材质应用到 Actor/组件</summary>

**引擎基础 API**：

```cpp
// UPrimitiveComponent（基类虚方法）
void SetMaterial(int32 ElementIndex, UMaterialInterface* Material);
void SetMaterialByName(FName MaterialSlotName, UMaterialInterface* Material);

// UMeshComponent（override）
void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
```

**当前 MCP 能力评估**：
- `editor.set_actor_property` 无法直接设置材质引用（`FObjectProperty` 路径解析不到组件子属性）。
- `component.set_property` 理论上可以设置 `OverrideMaterials` 数组，但需要知道正确的属性路径和 UObject 引用格式。
- **结论**：需要专门的 `material.apply_to_component` Action，简化为 `actor_name + component_name + slot_index + material_path` 的输入接口。

**额外能力（可选）**：
- `material.apply_to_actor`：遍历 Actor 的所有 `UPrimitiveComponent` 并设置 slot 0 的材质（快捷方式）。
- 支持 `UMaterialInstance` 引用（`UMaterialInterface` 基类兼容）。

</details>

<details>
<summary>调研 3：材质编辑器 Auto Layout 菜单注册</summary>

**关键发现**：`IMaterialEditorModule` 同时继承了 `IHasMenuExtensibility` 和 `IHasToolBarExtensibility`。

```cpp
class IMaterialEditorModule : public IModuleInterface,
    public IHasMenuExtensibility, public IHasToolBarExtensibility
```

**ExtensibilityManager 方法**：
| 方法 | 说明 |
|------|------|
| `GetMenuExtensibilityManager()` | 返回 `TSharedPtr<FExtensibilityManager>`，可添加菜单 extender |
| `GetToolBarExtensibilityManager()` | 返回 `TSharedPtr<FExtensibilityManager>`，可添加工具栏 extender |

**模块访问**：`IMaterialEditorModule::Get()` 或 `FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor")`

**实现方案**：与当前蓝图 Auto Layout 菜单注册模式**完全一致**——创建 `FExtender` → `AddMenuExtension("EditSearch", ...)` → `AddExtender()`。

**关键差异**（材质 vs 蓝图编辑器）：

| 维度 | 蓝图编辑器 | 材质编辑器 |
|------|-----------|----------|
| 模块名 | `Kismet`（`FBlueprintEditorModule`） | `MaterialEditor`（`IMaterialEditorModule`） |
| 菜单挂载点 | `EditSearch`（Edit 菜单之后） | 需调研具体挂载点（`EditSearch` 或按需选择） |
| 图类型 | `UEdGraph`（蓝图图） | `UMaterialGraph : UEdGraph` |
| 编辑器类 | `FBlueprintEditor` | `FMaterialEditor`（私有，通过 `IMaterialEditorModule` 间接访问） |
| 选区获取 | `BPEditor->GetSelectedNodes()` | `MatEditor->GetSelectedNodes()`（同基类方法） |
| SharedCommands | `GetsSharedBlueprintEditorCommands()` | 无对应公开方法，需通过 extender 注册 |

**FMaterialEditor 访问问题**：`FMaterialEditor` 声明在 Private/ 下，外部模块无法直接访问其成员。但菜单回调可通过：
1. 运行时遍历 `GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetAllEditedAssets()` 找到正在编辑的材质
2. 通过 `FSlateApplication` 查找焦点的材质图 Tab/Widget
3. 或者直接通过 `UMaterial::MaterialGraph` 获取图，然后用现有的 `FAutoLayoutMaterialAction` 布局逻辑

**Build.cs 依赖**：需新增 `MaterialEditor` 模块依赖。

</details>

#### P5 任务列表

| # | 任务 | 文件 | 说明 | 优先级 | 状态 |
|---|------|------|------|--------|------|
| **P5.1** | `material.compile` 增强 — 真实错误/警告返回 | `Actions/MaterialActions.cpp`, `registry/actions.py` | 编译后通过 `FMaterialResource::GetCompileErrors()` + `GetErrorExpressions()` 读取真实编译错误，返回 `errors[]`（含错误文本 + 关联节点名）和准确的 `error_count` / `warning_count`。需调用 `GShaderCompilingManager->FinishAllCompilation()` 确保同步编译完成后再读取。 | P0 | ✅ |
| **P5.2** | `material.apply_to_component` | `Actions/MaterialActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 新增 Action：按 `actor_name` + `component_name`（可选）+ `slot_index`（默认 0）+ `material_path` 将材质应用到关卡 Actor 的组件上。内部使用 `UPrimitiveComponent::SetMaterial()` + `LoadObject<UMaterialInterface>()`。 | P1 | ✅ |
| **P5.3** | 材质编辑器 Auto Layout 菜单 | `UEEditorMCPModule.h/.cpp`, `Build.cs` | 在材质编辑器的 Edit 菜单中注册 Auto Layout 命令（与蓝图编辑器一致）。通过 `IMaterialEditorModule::GetMenuExtensibilityManager()` 注册 extender。回调中获取当前材质的 `UMaterialGraph`，调用现有的材质布局核心逻辑。Build.cs 新增 `MaterialEditor` 模块依赖。 | P1 | ✅ |
| **P5.4** | `material.apply_to_actor` 快捷方式（可选） | `Actions/MaterialActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 遍历 Actor 的所有 `UPrimitiveComponent`，将 slot 0 统一设置为指定材质。适用于快速替换整个 Actor 的材质。 | P3 | ✅ |

#### P5 详细设计

<details>
<summary>P5.1 material.compile 增强 — 实现方案</summary>

**现状代码**（`FCompileMaterialAction::ExecuteInternal`）：
```cpp
// 当前：error_count / warning_count 始终为 0
bool bSuccess = true;
int32 ErrorCount = 0;
int32 WarningCount = 0;
Material->ForceRecompileForRendering();
```

**目标代码**：
```cpp
// 1. 触发编译
Material->PreEditChange(nullptr);
Material->PostEditChange();
Material->ForceRecompileForRendering();

// 2. 等待 Shader 编译完成（同步）
if (GShaderCompilingManager)
{
    GShaderCompilingManager->FinishAllCompilation();
}

// 3. 读取编译结果
TArray<FString> AllErrors;
TArray<UMaterialExpression*> AllErrorExprs;

FMaterialResource* MatResource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
if (MatResource)
{
    AllErrors = MatResource->GetCompileErrors();
    AllErrorExprs = MatResource->GetErrorExpressions();
}

// 4. 构建响应
int32 ErrorCount = AllErrors.Num();
TSharedPtr<FJsonArray> ErrorsArray = MakeShared<FJsonArray>();
for (int32 i = 0; i < AllErrors.Num(); ++i)
{
    TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
    ErrorObj->SetStringField(TEXT("message"), AllErrors[i]);
    if (i < AllErrorExprs.Num() && AllErrorExprs[i])
    {
        // 从 Context.MaterialNodeMap 反查节点名
        ErrorObj->SetStringField(TEXT("expression_class"), AllErrorExprs[i]->GetClass()->GetName());
    }
    ErrorsArray->Add(MakeShared<FJsonValueObject>(ErrorObj));
}
Result->SetArrayField(TEXT("errors"), *ErrorsArray);
```

**输出 Schema 变更**：
```json
{
  "name": "M_Example",
  "error_count": 2,
  "warning_count": 0,
  "errors": [
    { "message": "Failed to compile Custom expression", "expression_class": "MaterialExpressionCustom" },
    { "message": "TextureSample has no texture assigned", "expression_class": "MaterialExpressionTextureSample" }
  ]
}
```

**需要的 include**：`ShaderCompiler.h`（for `GShaderCompilingManager`）

</details>

<details>
<summary>P5.2 material.apply_to_component — 输入/输出 Schema</summary>

**C++ 命令**: `apply_material_to_component`  
**Action ID**: `material.apply_to_component`

**输入 Schema**:
```json
{
  "type": "object",
  "properties": {
    "actor_name": { "type": "string", "description": "关卡中的 Actor 名称" },
    "component_name": { "type": "string", "description": "组件名称（省略则使用 Root 组件或第一个 PrimitiveComponent）" },
    "slot_index": { "type": "integer", "default": 0, "description": "材质槽索引" },
    "material_path": { "type": "string", "description": "材质资产路径（如 /Game/Materials/M_Example）" }
  },
  "required": ["actor_name", "material_path"]
}
```

**实现要点**：
- 通过 `editor.find_actors` 相同逻辑查找 Actor
- `LoadObject<UMaterialInterface>()` 加载材质（支持 Material 和 MaterialInstance）
- 查找目标组件：优先 `component_name` 匹配 → 回退 Root Component → 回退第一个 `UPrimitiveComponent`
- `Component->SetMaterial(SlotIndex, LoadedMaterial)`
- 返回 `actor_name` / `component_name` / `slot_index` / `material_path` / `previous_material`

</details>

<details>
<summary>P5.3 材质编辑器 Auto Layout 菜单 — 实现方案</summary>

**模式**：完全复用蓝图编辑器菜单注册模式。

```cpp
// UEEditorMCPModule.cpp — RegisterAutoLayoutCommands() 中新增

// ---- 材质编辑器菜单注册 ----
MaterialMenuExtender = MakeShared<FExtender>();
MaterialMenuExtender->AddMenuExtension(
    "EditSearch",  // 需确认材质编辑器的实际挂载点
    EExtensionHook::After,
    AutoLayoutCommandList,  // 复用同一命令列表
    FMenuExtensionDelegate::CreateLambda([](FMenuBuilder& MenuBuilder)
    {
        MenuBuilder.BeginSection("MaterialAutoLayout", LOCTEXT("MaterialAutoLayoutSection", "Auto Layout"));
        {
            MenuBuilder.AddMenuEntry(
                LOCTEXT("MaterialAutoLayout", "Auto Layout"),
                LOCTEXT("MaterialAutoLayoutTooltip", "Auto-layout material graph nodes"),
                FSlateIcon(),
                FUIAction(FExecuteAction::CreateStatic(&ExecuteMaterialAutoLayout)));
        }
        MenuBuilder.EndSection();
    }));

if (FModuleManager::Get().IsModuleLoaded("MaterialEditor"))
{
    IMaterialEditorModule& MatEdModule = IMaterialEditorModule::Get();
    MatEdModule.GetMenuExtensibilityManager()->AddExtender(MaterialMenuExtender);
}
```

**回调实现**：
```cpp
static void ExecuteMaterialAutoLayout()
{
    // 1. 遍历已打开的资产编辑器，找到当前聚焦的材质
    UAssetEditorSubsystem* AssetEditorSS = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    TArray<UObject*> EditedAssets = AssetEditorSS->GetAllEditedAssets();
    
    UMaterial* FocusedMaterial = nullptr;
    for (UObject* Asset : EditedAssets)
    {
        if (UMaterial* Mat = Cast<UMaterial>(Asset))
        {
            FocusedMaterial = Mat;
            break; // 取第一个（后续可按焦点细化）
        }
    }
    if (!FocusedMaterial || !FocusedMaterial->MaterialGraph) return;
    
    // 2. 调用现有材质布局逻辑
    UMaterialGraph* MatGraph = FocusedMaterial->MaterialGraph;
    // 复用 FAutoLayoutMaterialAction 的核心算法
}
```

**Build.cs 新增依赖**：`"MaterialEditor"`

**与蓝图布局的差异**：
- 材质布局使用数据流拓扑排序（非 Exec 链），已在 P4.4 `material.auto_layout` 中实现
- 菜单回调直接复用 P4.4 的布局核心，只是入口从 MCP 变成菜单按钮

</details>

### Phase 6: PIE 控制 / 日志增强 / 断言验证 / Outliner 管理（2026-02-23 完成）

> **目标**：补齐自动化测试闭环（PIE + 日志断言）和 World Outliner 编辑器管理能力，使 AI 具备"启动游戏 → 观察日志 → 判定 pass/fail → 管理场景层级"的端到端自动化能力。  
> **前置**：Phase 1-5 均已完成；日志基础设施（`FMCPLogCapture`、`GetSince` 增量读取）已就绪。  
> **风险评级**：moderate — PIE 控制涉及编辑器 Play 会话状态机，需处理异步启动和 World 切换；Outliner 操作需适配 World Partition。

#### 现有能力盘点

| 能力 | 状态 | 说明 |
|------|------|------|
| 日志增量读取（游标） | ✅ 已有 | `FMCPLogCapture::GetSince(seq)` + `live:<seq>` 游标协议 |
| 日志环形缓冲区 | ✅ 已有 | 10000 行 / 5MB，seq 单调递增 |
| 日志 `Clear()` 方法 | ⚠️ C++ 存在未暴露 | `FMCPLogCapture::Clear()` 已实现，但无对应 MCP Action |
| PIE 控制 | ❌ 完全缺失 | 无 start/stop/query |
| 断言验证 | ❌ 完全缺失 | 无 log assert / pass-fail |
| Actor 重命名（Label） | ❌ 缺独立 Action | `SetActorLabel` 仅在 spawn 时使用 |
| Actor 文件夹管理 | ❌ 完全缺失 | 无 `SetFolderPath` / 文件夹创建 |
| Outliner Actor 选择 | ❌ 完全缺失 | 无 `GEditor->SelectActor` 暴露 |
| Outliner 层级查询 | ❌ 完全缺失 | 无按文件夹/层级返回 Actor 树 |

#### P6 任务列表

| # | 任务 | 文件 | 说明 | 优先级 | 状态 |
|---|------|------|------|--------|------|
| **P6.1** | `editor.start_pie` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | PIE 启动。通过 `GEditor->RequestPlaySession()` 启动 PIE，支持 `mode`（`SelectedViewport` / `NewWindow` / `Simulate`）和 `player_count`（多人测试）参数。返回 PIE 会话信息（World 名称、模式）。需处理异步启动：启动后轮询 `GEditor->IsPlaySessionInProgress()` 或监听 `FEditorDelegates::BeginPIE` 回调确认就绪。 | P0 | ✅ |
| **P6.2** | `editor.stop_pie` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | PIE 停止。通过 `GEditor->RequestEndPlayMap()` 停止 PIE，仅停止游戏会话不关闭编辑器。返回停止前的会话持续时间。需检查 `IsPlaySessionInProgress()` 避免重复停止。 | P0 | ✅ |
| **P6.3** | `editor.get_pie_state` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | PIE 状态查询。返回 `{ state: "Running"/"Stopped", world_name, start_time, duration_seconds, player_count, mode }`。通过 `GEditor->PlayWorld` 非空判定 Running；`FApp::GetCurrentTime()` 计算持续时间。只读 Action。 | P0 | ✅ |
| **P6.4** | `editor.clear_logs` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 日志清空 / 会话分段。暴露已有的 `FMCPLogCapture::Clear()` 为 MCP Action；返回清空前的 `entry_count` 和新的 `cursor`（清空后 seq 重置，使后续增量读取从干净状态开始）。可选 `tag` 参数，在清空前插入一条 `[SESSION] <tag>` 标记行用于会话分段。 | P1 | ✅ |
| **P6.5** | `editor.assert_log` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | 断言型日志验证。输入 `assertions[]`（每条含 `keyword`、`expected_count`、`comparison`（`==` / `>=` / `<=` / `>`）、`category`（可选过滤）），在当前日志缓冲区中按关键字匹配计数，逐条判定 pass/fail。返回 `{ overall: "pass"/"fail", results: [{ keyword, expected, actual, comparison, passed }] }`。支持 `since_cursor` 参数限定检查范围（仅检查指定游标之后的日志）。 | P1 | ✅ |
| **P6.6** | `editor.rename_actor_label` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | Actor 标签重命名。通过 `AActor::SetActorLabel()` 修改 Outliner 中显示的名称。输入 `actor_name`（当前名）+ `new_label`。返回 `old_label` / `new_label`。支持 `items[]` 批量重命名。 | P1 | ✅ |
| **P6.7** | `editor.set_actor_folder` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | Actor 文件夹管理。通过 `AActor::SetFolderPath()` 将 Actor 移入 Outliner 文件夹（不存在则自动创建）。输入 `actor_name` + `folder_path`（如 `"Enemies/Flying"`）。支持 `items[]` 批量操作。返回每个 Actor 的 `old_folder` / `new_folder`。 | P1 | ✅ |
| **P6.8** | `editor.select_actors` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | Outliner Actor 选择。通过 `GEditor->SelectActor()` 在编辑器中选中/取消选中 Actor。输入 `actor_names[]` + `mode`（`set` 替换选择 / `add` 追加 / `remove` 移除 / `toggle` 切换）。返回选择操作后的 `selected_count`。 | P2 | ✅ |
| **P6.9** | `editor.get_outliner_tree` | `Actions/EditorActions.h/.cpp`, `MCPBridge.cpp`, `registry/actions.py` | Outliner 层级查询。返回按文件夹组织的 Actor 树结构 `{ folders: [{ path, actors: [{ name, class, label }] }], unfoldered_actors: [...] }`。通过遍历 `UWorld::GetLevel()->Actors` 并按 `GetFolderPath()` 分组生成。支持 `class_filter`（按类过滤）和 `folder_filter`（按文件夹前缀过滤）。 | P2 | ✅ |

#### P6 详细设计

<details>
<summary>P6.1 editor.start_pie — 输入/输出 Schema</summary>

**C++ 命令**: `start_pie`  
**Action ID**: `editor.start_pie`

**输入 Schema**:
```json
{
  "type": "object",
  "properties": {
    "mode": {
      "type": "string",
      "enum": ["SelectedViewport", "NewWindow", "Simulate"],
      "default": "SelectedViewport",
      "description": "PIE 启动模式"
    },
    "player_count": {
      "type": "integer",
      "default": 1,
      "minimum": 1,
      "maximum": 4,
      "description": "玩家数量（多人测试）"
    }
  }
}
```

**输出 Schema**:
```json
{
  "success": true,
  "mode": "SelectedViewport",
  "world_name": "UEDPIE_0_Lvl_SideScrolling",
  "message": "PIE session started"
}
```

**实现要点**:
- 使用 `FRequestPlaySessionParams` 配置启动参数
- `Params.WorldType = EPlaySessionWorldType::PlayInEditor`
- `Params.DestinationSlateViewport` / `Params.EditorPlaySettings->NewWindowPosition` 控制模式
- `GEditor->RequestPlaySession(Params)` 提交请求
- PIE 启动是异步的，Action 返回后 PIE 可能仍在初始化中，配合 `editor.get_pie_state` 轮询确认
- 需在 `#if WITH_EDITOR` 保护下使用

</details>

<details>
<summary>P6.2 editor.stop_pie — 实现方案</summary>

**C++ 命令**: `stop_pie`  
**Action ID**: `editor.stop_pie`

```cpp
TSharedPtr<FJsonObject> FStopPIEAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params)
{
    auto Result = MakeShared<FJsonObject>();

    if (!GEditor->PlayWorld)
    {
        Result->SetBoolField(TEXT("success"), true);
        Result->SetStringField(TEXT("state"), TEXT("already_stopped"));
        return Result;
    }

    GEditor->RequestEndPlayMap();

    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("message"), TEXT("PIE stop requested"));
    return Result;
}
```

</details>

<details>
<summary>P6.3 editor.get_pie_state — 输出 Schema</summary>

**C++ 命令**: `get_pie_state`  
**Action ID**: `editor.get_pie_state`

**输出 Schema**:
```json
{
  "success": true,
  "state": "Running",
  "world_name": "UEDPIE_0_Lvl_SideScrolling",
  "duration_seconds": 12.5,
  "is_paused": false,
  "is_simulating": false
}
```

```json
{
  "success": true,
  "state": "Stopped",
  "world_name": null,
  "duration_seconds": 0
}
```

**实现要点**:
- `GEditor->PlayWorld != nullptr` → Running
- `GEditor->PlayWorld->GetName()` → world_name
- `GEditor->PlayWorld->IsPaused()` → is_paused
- `GEditor->bIsSimulatingInEditor` → is_simulating

</details>

<details>
<summary>P6.5 editor.assert_log — 输入/输出 Schema</summary>

**C++ 命令**: `assert_log`  
**Action ID**: `editor.assert_log`

**输入 Schema**:
```json
{
  "type": "object",
  "properties": {
    "assertions": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "keyword": { "type": "string", "description": "要搜索的关键字（子串匹配）" },
          "expected_count": { "type": "integer", "description": "期望出现次数" },
          "comparison": {
            "type": "string",
            "enum": ["==", ">=", "<=", ">", "<"],
            "default": ">=",
            "description": "比较运算符"
          },
          "category": { "type": "string", "description": "可选日志分类过滤" }
        },
        "required": ["keyword", "expected_count"]
      }
    },
    "since_cursor": {
      "type": "string",
      "description": "仅检查此游标之后的日志（live:<seq> 格式）"
    }
  },
  "required": ["assertions"]
}
```

**输出 Schema**:
```json
{
  "success": true,
  "overall": "pass",
  "total_assertions": 2,
  "passed": 2,
  "failed": 0,
  "results": [
    { "keyword": "HelloWorld", "expected": 1, "actual": 1, "comparison": ">=", "passed": true },
    { "keyword": "ForLoop", "expected": 3, "actual": 3, "comparison": "==", "passed": true }
  ],
  "log_range": { "from_seq": 100, "to_seq": 250, "lines_scanned": 150 }
}
```

**实现要点**:
- 复用 `FMCPLogCapture::GetSince()` 获取日志范围
- 对每条 assertion，遍历日志行做子串匹配计数
- 按 `comparison` 运算符判定 pass/fail
- `overall` = 全部 pass 则 "pass"，否则 "fail"

</details>

<details>
<summary>P6.7 editor.set_actor_folder — 实现方案</summary>

**C++ 命令**: `set_actor_folder`  
**Action ID**: `editor.set_actor_folder`

**输入 Schema**:
```json
{
  "type": "object",
  "properties": {
    "actor_name": { "type": "string", "description": "Actor 名称" },
    "folder_path": { "type": "string", "description": "目标文件夹路径（如 Enemies/Flying）" },
    "items": {
      "type": "array",
      "description": "批量操作（与 actor_name/folder_path 二选一）",
      "items": {
        "type": "object",
        "properties": {
          "actor_name": { "type": "string" },
          "folder_path": { "type": "string" }
        },
        "required": ["actor_name", "folder_path"]
      }
    }
  }
}
```

**实现要点**:
- `AActor::SetFolderPath(FName(*FolderPath))` — Outliner 文件夹自动创建
- 需在事务中执行以支持 Undo：`FScopedTransaction`
- World Partition 兼容：WP 模式下文件夹行为与传统关卡一致，`SetFolderPath` 同样适用

</details>

<details>
<summary>P6.9 editor.get_outliner_tree — 输出 Schema</summary>

**C++ 命令**: `get_outliner_tree`  
**Action ID**: `editor.get_outliner_tree`

**输出 Schema**:
```json
{
  "success": true,
  "total_actors": 42,
  "folders": [
    {
      "path": "Enemies",
      "actors": [
        { "name": "BP_SideScrolling_NPC_C_0", "class": "BP_SideScrolling_NPC_C", "label": "Scrap Rat 01" }
      ]
    },
    {
      "path": "Enemies/Flying",
      "actors": [
        { "name": "BP_Fly_C_0", "class": "BP_Fly_C", "label": "Flying Enemy 01" }
      ]
    }
  ],
  "unfoldered_actors": [
    { "name": "PlayerStart0", "class": "PlayerStart", "label": "PlayerStart" }
  ]
}
```

**实现要点**:
- 遍历 `EditorWorld->GetCurrentLevel()->Actors`
- 按 `Actor->GetFolderPath().ToString()` 分组
- 空 FolderPath → `unfoldered_actors`
- 支持 `class_filter` 和 `folder_filter` 前缀过滤

</details>

---

## 四、文件结构

### 新增文件

```
Python/ue_editor_mcp/
    registry/
        __init__.py         # ActionRegistry 类 + 搜索引擎
        actions.py          # 全部 115 个 action 注册
    resources/
        __init__.py         # 资源索引
        patch_spec.md       # Patch 规范文档
        error_codes.md      # 错误码解释
        conventions.md      # 项目约定
    server_unified.py       # 新的统一 7-tool server
```

### 保留文件（兼容回退）

```
server_editor.py            # 旧 6-server 架构 (通过 mcp.json 切换)
server_blueprint_management.py
server_blueprint_nodes.py
server_blueprint_graph.py
server_materials.py
server_umg_widget.py
server_factory.py
tools/*.py                  # 旧工具定义
```

### 无需修改

```
C++ 侧全部代码            # 零改动
connection.py              # TCP 连接复用
```

---

## 五、向后兼容

- 旧 `server_*.py` 保留不删，通过 `mcp.json` 切换即可回退
- C++ 侧 `MCPBridge::RegisterActions()` 和所有 `FEditorAction` 完全不动
- TCP 协议格式不变
- `connection.py` 的 `PersistentUnrealConnection` 完全复用

---

## 六、历史记录

<details>
<summary>v1 架构 (6 MCP Servers, 98 tools) — 已替代</summary>

### v1 架构概览

```
C++ 层 (UE 插件)
  EditorAction 基类 → 子类实现具体命令
  MCPBridge::RegisterActions() 注册所有 Action → TCP 分发

Python 层 (MCP SDK)
  server_factory.py       → 共享工厂函数
  server_editor.py        → 编辑器服务           18 tools
  server_blueprint_management.py → 蓝图管理服务   18 tools
  server_blueprint_nodes.py      → 节点创建服务   21 tools
  server_blueprint_graph.py      → 图表操作服务   18 tools
  server_materials.py     → 材质/项目服务        15 tools
  server_umg_widget.py    → UMG 控件服务         18 tools
```

6 个 MCP 服务（每个 ≤21 tools），C++ 侧已注册约 115 个 Action。

### v1 问题

- 工具数膨胀：每新增一个 C++ Action 就要加一个 MCP Tool
- 隐藏工具组：VS Code Copilot Chat 的 `activate_*` 分组导致工具不可见
- 多服务管理：6 个 server 进程 = 6 个 TCP 连接 = 管理复杂
- 上下文噪声：AI 需要在 100 个工具名中"盲选"

</details>

<details>
<summary>Bug 修复记录 (2026-02-13)</summary>

| # | 问题 | 根因 | 修复 | 涉及文件 |
|---|------|------|------|----------|
| B1 | `add_blueprint_function_node` 无法找到常用函数 | UE 使用 `K2_` 前缀 | 自动追加 K2_ 前缀重试 | `NodeActions.cpp` |
| B2 | `add_blueprint_self_reference` 忽略 graph_name | 硬编码 EventGraph | 改用 `GetTargetGraph()` | `NodeActions.cpp` |
| B3 | `create_blueprint_function` 类型解析不全 | 只支持 5 种类型 | 新增 9 种类型分支 | `NodeActions.cpp` |

</details>

<details>
<summary>Bug 修复记录 (2026-02-17)</summary>

| # | 问题 | 根因 | 修复 | 涉及文件 |
|---|------|------|------|----------|
| B4 | `graph.get_selected_nodes` / `graph.collapse_selection_to_function` 在多蓝图窗口打开时始终操作第一个蓝图编辑器，而非当前聚焦的编辑器 | `NodeActions.cpp` 的 `GetActiveBlueprintEditorForAction()` 遍历 `GetAllEditedAssets()` 后直接返回第一个匹配项，完全没有检查活跃/前台状态 | 将三处独立的蓝图编辑器解析函数统一为 `FMCPCommonUtils::GetActiveBlueprintEditor()`，使用三层启发式判断（`IsActive()` → `IsForeground()` → `HasFocusedDescendants()` → 回退并警告日志） | `MCPCommonUtils.h/cpp`（新增公共方法）、`NodeActions.cpp`（删除有 bug 的 static 版本）、`UEEditorMCPModule.cpp`（删除仅 IsForeground 的 static 版本）、`LayoutActions.cpp`（删除本地 static 版本，改用公共方法） |

</details>
