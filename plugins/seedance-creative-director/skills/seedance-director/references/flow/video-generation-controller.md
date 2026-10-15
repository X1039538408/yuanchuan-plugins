# 视频生成流程控制器

## 职责

本文件只规定提示词完成后的 LibTV 视频生成固定流程，不负责创意、剧情、人物、动作、镜头或约束内容。

控制器脚本：

```text
tools/libtv_video_flow.ps1
```

控制器使用本机 LibTV CLI，并通过 `LIBTV_CONFIG_DIR` 绑定用户选中的独立 profile。

## 状态机

```text
prompt_ready
profile_selected
project_selected
assets_resolved
model_selected
parameters_selected
plan_validated
awaiting_final_confirmation
submitted
running
completed
failed
recoverable
```

状态只能按合法顺序推进。模型不能绕过控制器直接调用生成命令。

## 控制器动作

| 动作 | 作用 |
| --- | --- |
| `discover-profiles` | 并行或兼容回退地检查本机 profile 登录状态 |
| `list-projects` | 按确认的 DH folderId 筛选并返回最近 6 个项目 |
| `resolve-assets` | 根据 `{{Mixed N}}` 和素材清单映射真实节点 |
| `list-models` | 动态读取当前 profile 可用视频模型 |
| `get-model-schema` | 只读取用户选中模型的最新 schema |
| `validate-plan` | 校验 modeType、参数、素材数量和 schema 必填字段 |
| `submit` | 用户最终确认后创建唯一视频节点并只触发一次 `--run` |
| `status` | 查询已有运行节点状态 |
| `resume` | 根据 run 状态恢复查询，不创建替代任务 |

## 账号与项目

- profile 目录为 `%USERPROFILE%\\.libtv-accounts\\<profile-id>`。
- 所有 CLI 命令必须设置对应的 `LIBTV_CONFIG_DIR`。
- 不使用远程 MCP OAuth 假装切换独立账号。
- 没有 DH folderId 时返回 `folder_required`，由用户确认后保存。
- 使用缓存的 folderId 但返回 0 个项目时返回 `folder_mapping_invalid`，要求重新确认映射；用户本次明确提供 folderId 但没有匹配项目时返回 `no_projects`，允许改正或明确查看全部项目。
- 项目必须按精确 UUID 选择，不自动选择、创建或合并项目。
- 项目缓存有效期为 60 秒。

## 动态模型

- 不硬编码模型名称。
- 先运行 `libtv model search -t video`。
- 用户选中模型后再运行 `libtv model <modelName>`。
- 保存 `modelName`、`modelKey`、`schemaHash` 和 `schemaFetchedAt`。
- schema 标准字段动态生成选择项。
- 无法解析的必填字段严格拦截，不猜默认值、不自动换模型。
- 模型目录和 schema 缓存有效期为 10 分钟。
- 在最终确认和断点恢复前重新读取已选模型 schema；`schemaHash` 变化时暂停提交，返回 `schema_changed`，要求按新字段重新确认参数。

## LibTV CLI 提交

CLI 的生成入口是视频节点：

```text
libtv node create <unique-node-name> -p <projectUuid> -t video --prompt <LibTV prompt> -s <key=value> --left <assetNode> -r
```

控制器用 `runId` 生成唯一节点名，并把 run 状态保存到本机。CLI 没有独立的幂等键参数，因此本地幂等由以下三项共同保证：

- `runId`；
- 唯一视频节点名；
- 状态文件中的 `taskId` / `nodeKey`。

如果提交返回错误但状态不明确，必须先查询原节点；不得直接创建第二个节点。
节点查询会把 `processing/running`、`succeeded/completed`、`failed/error` 等返回状态归类为 `running`、`completed` 或 `failed`，未知状态保留原阶段。

## 安全边界

- 不读取或输出 token、Cookie、验证码和完整凭据。
- 不把凭据写入项目包、缓存或日志。
- 不在预检前创建视频节点。
- 不在用户最终确认前触发 `--run`。
- 不把 `{{Mixed N}}`、文件名或宫格格号当作真实节点 ID。
