# UEEditorMCP

> **基于 [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP) 修改扩展。**  
> 原项目采用 **MIT 协议**，本插件在其基础上进行了大量重构与功能增强，同样遵循 MIT 协议发布。  
> 感谢 [@lilklon](https://github.com/lilklon) 提供的架构基础。

---

一个面向 AI 辅助开发的 MCP 插件，用于 Unreal Engine 5.7+ 蓝图操作。  
对外暴露 **7 个固定 MCP 工具**，由后端 **动作注册表（Action Registry）** 驱动，支持 141 个动作，提供持久 TCP 连接、多客户端支持与自动保存。

同时包含一个可选的**独立日志 MCP 服务器**（`ue-editor-mcp-logs`），暴露 `unreal.logs.get`、`unreal.asset_thumbnail.get` 和 `unreal.asset_diff.get` 三个工具。

> **仅限编辑器** — C++ 模块类型为 `Editor`，在所有游戏打包构建（Shipping、Development Game 等）中完全排除，不会对运行时性能或打包产物产生任何影响。

## 功能特性

- **7 个固定 MCP 工具** — 单一 `ue-editor-mcp` 服务器，工具接口永远不变
- **141 个动作** — 蓝图、图节点、材质、UMG 控件、MVVM、增强输入、组件事件绑定、PIE 启停/状态查询、日志断言、Outliner 管理、资产缩略图提取、批量资产重命名及重定向修复、与版本控制仓库的差异对比 — 均可通过动作注册表动态发现
- **搜索 → 模式 → 执行** — AI 动态发现动作，无需记忆命令名
- **批量执行** — `ue_batch` 通过 C++ `batch_execute` 在**单次 TCP 往返**中执行最多 50 个动作
- **多客户端 TCP** — 端口 55558，最多支持 8 路并发连接（每连接独立线程）
- **持久连接** — Socket 在命令间保持开启，支持心跳检测与自动重连
- **自动保存** — 每次成功操作后自动保存脏包
- **崩溃保护** — 动作执行管道启用了 SEH + C++ 异常防护
- **字符串解析** — 接受蓝图名称、资产路径或引擎类名
- **嵌入式文档** — `ue_resources_read` 将约定规范、错误码和补丁规范暴露给 AI

## 架构

```
VS Code / MCP 客户端（GitHub Copilot 等）
        │
        │  7 个 MCP 工具（stdio）
        ▼
  server_unified.py（动作注册表 + 分发器）
        │
        │  TCP/JSON（端口 55558，持久连接，长度前缀帧）
        ▼
  C++ 插件（FMCPServer → 每连接一个 FMCPClientHandler）
        │
        │  游戏线程分发
        ▼
  FEditorAction 子类（~150 个处理器）→ 校验 → 执行 → 自动保存
        │
        ▼
  Unreal Editor
```

单一 Python 服务器进程与 C++ 服务器建立持久 TCP 连接。C++ 侧为每个连接派生独立的 `FMCPClientHandler` 线程，所有编辑器变更均被序列化到游戏线程以保证线程安全。

对于日志上下文场景，第二个轻量服务器（`server_unreal_logs.py`）可并行运行，提供：
- `unreal.logs.get`（即使 UE 未运行也可通过 `Saved/Logs` 离线读取）
- `unreal.asset_thumbnail.get`（需要 UE 编辑器连接，以 PNG base64 格式返回资产缩略图）
- `unreal.asset_diff.get`（需要 UE 编辑器 + 源码控制连接，返回资产与仓库版本的结构化差异）

## MCP 工具（7 个固定）

| # | 工具 | 用途 |
|---|------|------|
| 1 | `ue_ping` | 测试与 Unreal Engine 的连接，存活时返回 `{pong: true}` |
| 2 | `ue_actions_search` | 按关键字或标签搜索动作，返回排序后的动作 ID 列表及描述 |
| 3 | `ue_actions_schema` | 获取指定动作的完整输入模式、示例和元数据 |
| 4 | `ue_actions_run` | 执行单个带参数的动作 |
| 5 | `ue_batch` | 通过 C++ `batch_execute` 在**单次 TCP 往返**中执行多个动作，最多 50 个 |
| 6 | `ue_resources_read` | 读取嵌入式文档：`conventions.md`、`error_codes.md`、`patch_spec.md` |
| 7 | `ue_logs_tail` | 追踪近期日志（Python 命令日志 / 编辑器环形缓冲区 / 两者），支持分类和详细级别过滤 |

### AI 工作流（快速路径 — 1 次往返）

```
# 当动作 ID 及参数已知时，直接跳过搜索/模式步骤：
ue_batch(actions=[
  {action_id: "blueprint.create", params: {name: "BP_Player", parent_class: "Character"}},
  {action_id: "variable.create", params: {blueprint_name: "BP_Player", variable_name: "Speed", variable_type: "Float"}},
  {action_id: "blueprint.compile", params: {blueprint_name: "BP_Player"}}
])
→ 所有动作在 1 次 TCP 往返中完成
```

### AI 工作流（发现路径 — 3 次往返）

```
第 1 步：ue_actions_search(query="create blueprint")
  → [{id: "blueprint.create", desc: "..."}, ...]

第 2 步：ue_actions_schema(action_id="blueprint.create")
  → {input_schema: {required: ["name","parent_class"], ...}, examples: [...]}

第 3 步：ue_actions_run(action_id="blueprint.create", params={name: "BP_Player", parent_class: "Character"})
  → {success: true, blueprint_name: "BP_Player", path: "/Game/Blueprints/BP_Player"}
```

### 动作域（核心）

| 域 | 数量 | 说明 | 示例 ID |
|----|------|------|--------|
| `blueprint.*` | 11 | 蓝图增删改查、组件、接口、完整快照 | `blueprint.create`、`blueprint.compile`、`blueprint.add_component`、`blueprint.describe_full` |
| `batch.*` | 1 | 批量命令执行 | `batch.execute` |
| `component.*` | 4 | 组件属性、网格体、物理、事件绑定 | `component.set_property`、`component.set_static_mesh`、`component.bind_event` |
| `editor.*` | 18 | 关卡 Actor、视口、资产、日志、生命周期、源码控制差异 | `editor.spawn_actor`、`editor.list_assets`、`editor.get_logs`、`editor.is_ready`、`editor.request_shutdown` |
| `layout.*` | 3 | 节点自动布局 | `layout.auto_selected`、`layout.auto_subtree`、`layout.auto_blueprint` |
| `node.*` | 19 | 蓝图图节点创建 | `node.add_event`、`node.add_function_call`、`node.add_branch` |
| `variable.*` | 8 | 变量增删改查、默认值、元数据 | `variable.create`、`variable.add_getter`、`variable.set_default` |
| `function.*` | 4 | 函数创建、管理与重构 | `function.create`、`function.call`、`function.delete`、`function.rename` |
| `dispatcher.*` | 4 | 事件派发器管理 | `dispatcher.create`、`dispatcher.call`、`dispatcher.bind` |
| `graph.*` | 18 | 图连线、检视、注释、补丁、折叠重构 | `graph.connect_nodes`、`graph.describe`、`graph.get_selected_nodes`、`graph.collapse_selection_to_function`、`graph.auto_comment`、`graph.apply_patch` |
| `macro.*` | 1 | 宏管理 | `macro.rename` |
| `material.*` | 16 | 材质创建、编辑、编译诊断、图检视与布局、关卡应用、编辑器刷新 | `material.create`、`material.add_expression`、`material.compile`、`material.get_summary`、`material.auto_layout`、`material.auto_comment`、`material.remove_expression`、`material.apply_to_component`、`material.apply_to_actor`、`material.refresh_editor` |
| `widget.*` | 21 | UMG 控件蓝图（24 种类型）+ MVVM | `widget.create`、`widget.add_component`、`widget.mvvm_add_viewmodel`、`widget.mvvm_remove_viewmodel` |
| `input.*` | 4 | 增强输入系统 | `input.create_action`、`input.create_mapping_context` |

### 编译诊断（重要）

- `blueprint.compile` 返回状态及汇总计数（`status`、`error_count`、`warning_count`），但对某些 UMG/MVVM 编译失败可能不包含完整编译器文本。
- 如需详细诊断，请调用 `editor.get_logs`（或带 `source="editor"` 的 `ue_logs_tail`），并按 `category="LogBlueprint"` + `min_verbosity="Error"` 过滤。
- 推荐排查流程：
  1. `blueprint.compile`
  2. `editor.get_logs(count=200, category="LogBlueprint", min_verbosity="Error")`
  3. 如需进一步扩展，可将过滤类别改为 `category="LogOutputDevice"` 获取 Ensure/Fatal 上下文

### 材质编译诊断（Phase 5）

- `material.compile` 现已同步等待材质着色器编译完成，并返回真实编译诊断。
- 响应中包含 `error_count`、`warning_count` 以及 `errors[]` 列表。
- `errors[]` 的每一项包含：`message`、`expression_name`、`expression_class`、`node_name`（如可解析）。
- 推荐排查流程：
  1. `material.compile`
  2. 若有需要，再结合 `ue_logs_tail(source="editor", category="LogMaterial", min_verbosity="Error")` 查看上下文日志

## 蓝图编辑器自动布局命令

插件在蓝图编辑器菜单（`编辑` → `Auto Layout`）中直接注册了自动布局命令，并提供默认快捷键：

- `Auto Layout`：`Ctrl+Alt+L`

行为：
- 若有节点被选中，对当前选中节点执行布局
- 若无选中，对整个焦点图执行布局

这些是编辑器命令，可在 Unreal Editor 快捷键设置中重映射。

## 材质编辑器自动布局菜单（Phase 5）

插件在材质编辑器菜单（`Edit`）中注册了 `Auto Layout` 菜单项。

行为：
- 对当前焦点材质的 `UMaterialGraph` 执行自动布局
- 与 `material.auto_layout` 动作配套，便于在编辑器内直接整理材质图

## 安装与配置

### 第 1 步：编译 C++ 插件

插件已位于 `Plugins/UEEditorMCP/`，编译编辑器目标：

```
Engine\Build\BatchFiles\Build.bat p110_2Editor Win64 Development ${workspaceFolder:p110_2}\p110_2.uproject -waitmutex
```

或使用 VS Code 任务：**p110_2Editor Win64 Development Build**。

### 第 2 步：一键 Python 环境配置（使用 UE 引擎内置 Python）

**无需安装外部 Python。** 配置脚本会自动找到 Unreal Engine 内置的 Python 3.11，创建虚拟环境并安装 MCP 包。

**PowerShell（推荐）：**
```powershell
cd Plugins/UEEditorMCP
.\setup_mcp.ps1
# 或显式指定引擎路径：
.\setup_mcp.ps1 -EngineRoot "${workspaceFolder:UE5}"
```

**命令提示符：**
```cmd
cd Plugins\UEEditorMCP
setup_mcp.bat
```

脚本将会：
1. 自动检测 UE 引擎内置 Python（`Engine/Binaries/ThirdParty/Python3/Win64/python.exe`）
2. 在 `Python/.venv` 创建虚拟环境
3. 安装 `mcp` 包
4. 在项目根目录生成 `.vscode/mcp.json`，并填入正确路径

<details>
<summary><strong>手动配置（可选）</strong></summary>

如需手动配置或使用自定义 Python：

```bash
cd Plugins/UEEditorMCP/Python
python -m venv .venv
.venv\Scripts\activate        # Windows
pip install -r requirements.txt
```

然后在项目根目录创建 `.vscode/mcp.json`：

```jsonc
{
  "servers": {
    "ue-editor-mcp": {
      "command": "./Plugins/UEEditorMCP/Python/.venv/Scripts/python.exe",
      "args": ["-m", "ue_editor_mcp.server_unified"],
      "env": {
        "PYTHONPATH": "./Plugins/UEEditorMCP/Python"
      }
    },
    "ue-editor-mcp-logs": {
      "command": "./Plugins/UEEditorMCP/Python/.venv/Scripts/python.exe",
      "args": ["-m", "ue_editor_mcp.server_unreal_logs"],
      "env": {
        "PYTHONPATH": "./Plugins/UEEditorMCP/Python"
      }
    }
  }
}
```

</details>

### 日志上下文工具（`ue-editor-mcp-logs`）

`ue-editor-mcp-logs` 暴露三个工具：

**`unreal.logs.get`**
- `mode`：`auto|live|saved`（默认 `auto`）
- `tailLines`：默认 `200`（范围 `20..2000`）
- `maxBytes`：默认 `65536`（范围 `8192..1048576`）
- `cursor`：增量游标（`live:<seq>` 或 `file:<hash>:<offset>:<mtime_ns>:<size>`）
- `workspaceRoot`：UE 不可达时的离线读取必填项

推荐用法：
1. 首次调用：`mode=auto, tailLines=200, maxBytes=65536`
2. 保存返回的 `cursor`
3. 后续调用传入 `cursor`，仅获取增量日志，避免 token 爆炸

行为说明：
- `mode=auto`：若日志在 2 秒内更新则优先读取实时环形缓冲区，否则回退到 `Saved/Logs`
- `mode=live`：仅读取内存中的实时环形缓冲区，无文件 IO
- `mode=saved`：使用反向块读取方式追踪最新 `Saved/Logs` 文件
- UE 不可达时：若提供 `workspaceRoot` 则从磁盘返回 `offline_saved`

**`unreal.asset_thumbnail.get`**
- `assetPath`：可选，单个资产完整路径
- `assetPaths`：可选，多个完整路径
- `assetIds` / `ids`：可选，路径数组别名（批量）
- `size`：可选，缩略图尺寸（默认 `256`，上限 `1..256`）
- 返回 `thumbnails[]`（每个输入对应一项），并保留首项兼容字段 `image_base64`
- 需要 UE 编辑器连接

**`unreal.asset_diff.get`**
- `assetPath`（必填）：完整资产路径（如 `/Game/P110_2/Blueprints/BP_Foo`）
- `revision`（可选）：指定版本号（默认最新版本）
- 返回结构化差异数据：`hasDifferences`、`summary`（新增/删除/修改/次要变更计数）、`diffs[]`（类型、分类、显示字符串、所属图、节点/引脚名称）
- 对于蓝图：逐图节点级别变更（NODE_ADDED、NODE_REMOVED、PIN_DEFAULT_VALUE、NODE_MOVED 等）
- 对于通用资产：属性级别差异（PropertyValueChanged、PropertyAddedToA/B）
- 包含 `revisionInfo`（版本号、日期、用户名、描述）
- 需要 UE 编辑器运行且源码控制（SVN/Perforce/Git）已连接

### 第 3 步：启动

1. 在编辑器中打开 Unreal 项目（插件自动在端口 55558 启动 TCP 服务器）
2. 打开 VS Code — `ue-editor-mcp` 服务器通过 stdio 启动并连接到端口 55558
3. 使用 GitHub Copilot Chat 或任意兼容 MCP 的客户端发出命令

## 新增动作

若要为插件扩展新能力：

### 第 1 步：C++ 侧 — 实现动作处理器

```cpp
// Source/Public/Actions/MyActions.h
class FMyNewAction : public FBlueprintNodeAction
{
public:
    FMyNewAction() : FBlueprintNodeAction(TEXT("my_new_action")) {}
    FString Validate(const TSharedPtr<FJsonObject>& Params) override;
    TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params) override;
};
```

在 `MCPBridge.cpp` 中注册：
```cpp
ActionHandlers.Add(TEXT("my_new_action"), MakeShared<FMyNewAction>());
```

### 第 2 步：Python 侧 — 添加 ActionDef

在 `registry/actions.py` 中添加动作定义：
```python
ActionDef(
    id="domain.my_new_action",
    command="my_new_action",
    description="这个动作的用途说明",
    tags=["domain", "keyword1", "keyword2"],
    input_schema={
        "type": "object",
        "properties": {
            "param1": {"type": "string", "description": "..."},
        },
        "required": ["param1"],
    },
    examples=[{"param1": "value"}],
)
```

### 第 3 步：编译与测试

```
Engine\Build\BatchFiles\Build.bat p110_2Editor Win64 Development ...
```

无需修改任何服务器代码 — `ue_actions_search` / `ue_actions_run` 将自动识别新动作。

## 编辑器专属安全保障

本插件通过三重机制**保证永远不出现在打包构建中**：

| 层级 | 机制 | 效果 |
|------|------|------|
| `.uplugin` | `"Type": "Editor"` | UBT 对所有非编辑器目标跳过此模块 |
| `.Build.cs` | 依赖 `UnrealEd`、`BlueprintGraph`、`Kismet`、`UMGEditor` 等 | 无法链接到游戏目标（这些模块在游戏构建中不存在） |
| `.uplugin` | `"PlatformAllowList": ["Win64", "Mac", "Linux"]` | 仅限桌面编辑器平台 |

TCP 服务器、所有 MCP 工具以及所有蓝图操作代码**仅存在于编辑器 DLL 中**。

## 技术细节

### C++ 服务器（`FMCPServer`）

- 监听 `127.0.0.1:55558`（仅限本地，不对网络暴露）
- Accept 循环为每个连接派生独立的 `FMCPClientHandler`（每个独立线程）
- `ping` 和 `close` 直接在客户端线程处理（无需游戏线程）
- 所有其他命令通过 `AsyncTask` + `FEvent` 同步分发到游戏线程
- 客户端超时：120 秒无活动后断开
- 启用 `SO_REUSEADDR`，避免编辑器重启时端口冲突

### Python 服务器（`server_unified.py`）

- 对外暴露 7 个固定工具的单一 MCP 服务器
- 动作注册表含 141 个 ActionDef 条目，支持关键字搜索和模式自省
- UMG 控件类型通过内部组件类型映射分发（支持 24 种控件类型）
- `ue_batch` 批量执行（每次最多 50 个动作）
- 命令日志环形缓冲区，供 `ue_logs_tail` 使用
- 通过 `ue_resources_read` 暴露嵌入式资源文档

### 通信协议格式

```
[4 字节：消息长度（大端序）] [UTF-8 JSON 载荷]
```

请求：
```json
{"type": "create_blueprint", "params": {"name": "BP_MyActor", "parent_class": "Actor"}}
```

响应：
```json
{"success": true, "blueprint_name": "BP_MyActor", "path": "/Game/Blueprints/BP_MyActor"}
```

### 关键文件

| 文件 | 用途 |
|------|------|
| `Python/ue_editor_mcp/server_unified.py` | 单一 MCP 服务器，7 个工具，动作分发 |
| `Python/ue_editor_mcp/registry/__init__.py` | ActionRegistry 类，关键字搜索引擎 |
| `Python/ue_editor_mcp/registry/actions.py` | 141 个带模式/标签/示例的 ActionDef 条目 |
| `Python/ue_editor_mcp/resources/*.md` | 供 `ue_resources_read` 使用的嵌入式文档 |
| `Python/ue_editor_mcp/connection.py` | `PersistentUnrealConnection`（TCP、心跳、自动重连） |
| `Source/Private/MCPServer.cpp` | TCP Accept 循环 + 每客户端处理线程 |
| `Source/Private/MCPBridge.cpp` | 动作处理器注册表（~150 条命令） |
| `Source/Private/Actions/*.cpp` | `FEditorAction` 子类具体实现 |

## 环境要求

- Unreal Engine 5.7+（内置 Python 3.11，无需另行安装）
- Visual Studio 2022（插件编译所需）
- VS Code + GitHub Copilot（或任意兼容 MCP 的客户端）

## 开发路线

开发计划详见 [DEVPLAN.md](DEVPLAN.md)。当前状态：

### 阶段一：统一服务器 + 动作注册表 ✅

7 工具统一 MCP 服务器，动作注册表（141 个动作）、关键字搜索、批量执行、嵌入式文档和结构化日志——**全部完成**。

### Phase 6: PIE / 日志断言 / Outliner ✅

| # | Feature | Description | Status |
|---|---------|-------------|--------|
| P6.1 | `editor.start_pie` / `editor.stop_pie` / `editor.get_pie_state` | PIE 启动、停止与状态查询，支持自动化运行闭环 | ✅ |
| P6.2 | `editor.clear_logs` / `editor.assert_log` | 日志会话分段与断言验证，支持基于游标的自动化检查 | ✅ |
| P6.3 | Outliner 管理动作 | 支持 Actor 重命名、文件夹归类、选择与层级查询 | ✅ |

### Phase 5: 材质系统增强（续）✅

| # | Feature | Description | Status |
|---|---------|-------------|--------|
| P5.1 | `material.compile` 诊断增强 | 同步等待编译完成，返回真实 `errors[]`（错误文本 + 关联表达式信息）与准确计数 | ✅ |
| P5.2 | `material.apply_to_component` | 将材质应用到关卡 Actor 指定组件（支持 `component_name`、`slot_index`） | ✅ |
| P5.3 | 材质编辑器 `Auto Layout` 菜单 | 在材质编辑器 `Edit` 菜单注册布局命令，直接整理当前材质图 | ✅ |
| P5.4 | `material.apply_to_actor` | 将材质批量应用到 Actor 全部 `UPrimitiveComponent` | ✅ |

### 阶段二：C++ 增强 ✅

| # | 功能 | 说明 | 状态 |
|---|------|------|------|
| P2.1 | `graph.describe` | 完整图拓扑转储（所有节点、引脚、连接、位置） | ✅ |
| P2.2 | SEH 崩溃保护 | `ExecuteWithCrashProtection()` 中的 `__try/__except` 封装 | ✅ |
| P2.3 | `editor.get_logs` | 通过自定义 `FOutputDevice` 环形缓冲区进行结构化日志捕获 | ✅ |
| P2.4 | `batch.execute`（C++） | 单次 TCP 请求 → 串行游戏线程执行多条命令 | ✅ |
| P2.5 | Python 集成 | 新增 ActionDef + `ue_logs_tail` 编辑器日志桥接 | ✅ |
| P2.6 | 自定义日志分类 | 全动作范围内以 `LogMCP` 替换 `LogTemp` | ✅ |

### 阶段三：补丁系统 ✅

| # | 功能 | 说明 | 状态 |
|---|------|------|------|
| P3.1 | `graph.describe_enhanced` | 扩展拓扑信息（变量引用、完整 PinType、节点元数据）；支持 `compact` 模式降低输出量 | ✅ |
| P3.2 | 补丁操作定义 | `EPatchOpType` 枚举 + `FPatchOp` 结构体（8 种操作类型） | ✅ |
| P3.3 | `graph.apply_patch` | 通过 JSON 补丁文档进行声明式图编辑 | ✅ |
| P3.4 | `graph.validate_patch` | 试运行校验 — 验证所有操作而不修改图 | ✅ |
| P3.5 | Python 补丁 ActionDef | 补丁动作的注册表条目、模式与示例 | ✅ |
| P3.6 | 补丁规范文档 | 更新 `patch_spec.md`，含完整操作参考 | ✅ |

### Layout V2: Enhanced Auto-Layout ✅

- `layout.auto_selected` / `layout.auto_subtree` / `layout.auto_blueprint` 已完成 Enhanced Sugiyama 改造：最长路径分层、Barycenter 交叉优化、宽高感知间距、Pure Pin 对齐、周围节点避让、Comment 框调整。
- `layer_spacing` / `row_spacing` 语义：`>0` 固定间距，`<=0` 自动间距（默认自动）。
- 新参数（按动作范围）：`horizontal_gap`、`vertical_gap`、`crossing_passes`、`pin_align_pure`、`avoid_surrounding`、`include_pure_deps`、`surrounding_margin`、`preserve_comments`。

### DevPlan 路径规则

- 插件开发计划唯一来源：`Plugins/UEEditorMCP/DEVPLAN.md`。
- 其他临时计划文档在需求完成后应合并回该文件并移除，避免多份计划口径冲突。

### Phase 4: 材质系统增强 ✅

| # | Feature | Description | Status |
|---|---------|-------------|--------|
| P4.1 | `material.get_summary` | 只读 Action，一次性返回材质图完整结构（表达式/连接/属性/注释） | ✅ |
| P4.2 | 通用连线方案 | 将 `ConnectToExpressionInput()` 从硬编码 Cast<> 重构为通用 `GetInput(index)` 方案 | ✅ |
| P4.3 | TextureParameter | ExpressionClassMap 新增 TextureParameter/TextureObjectParameter/TextureSampleParameter2D + `create_instance` 纹理覆写 | ✅ |
| P4.4 | `material.auto_layout` | 数据流拓扑排序分层布局 | ✅ |
| P4.5 | `material.auto_comment` | 包围盒注释 + 碰撞避让（60px gap, 5 iterations） | ✅ |
| P4.6 | `material.remove_expression` | 安全删除（断连 + ExpressionCollection 移除 + Context 反注册） | ✅ |
| P4.7 | StaticSwitchParameter | ExpressionClassMap 新增 StaticSwitchParameter/StaticComponentMaskParameter + `create_instance` switch 覆写 | ✅ |
| P4.8 | MaterialFunction 引用 | ExpressionClassMap 新增 MaterialFunctionCall + `SetMaterialFunction()` 绑定 | ✅ |

## 文档

- **[DEVPLAN.md](DEVPLAN.md)** — 详细开发计划（各阶段设计文档）

---

## 许可证

MIT

本项目基于 [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP)（MIT 许可证）修改扩展。  
原始代码版权归 [@lilklon](https://github.com/lilklon) 所有。
