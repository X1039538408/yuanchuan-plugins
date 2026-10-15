# 即梦双 CLI 视频流程控制器

控制器脚本为 `tools/dreamina_video_flow.ps1`，支持 `canvas` 和 `conversation` 两种适配器。

状态顺序：

```text
prompt_ready → platform_selected → adapter_selected → profile_selected → container_selected
→ assets_resolved → mode_selected → model_selected → parameters_selected → plan_validated
→ awaiting_final_confirmation → submitted → running → completed/failed/recoverable
```

固定入口：双版本提示词完成后先让用户选择 LibTV 或即梦 CLI；选择即梦后再选画布或对话。第一版一次只提交一个平台。

控制器动作：

| 动作 | 画布 | 对话 |
|---|---|---|
| `discover-profiles` | 读取画布与对话账号状态 | 读取画布与对话账号状态 |
| `list-containers` | 最近 6 个画布项目 | 最近 6 个对话会话 |
| `resolve-assets` | 映射真实 node/resource | 校验本地文件路径 |
| `list-models` | 实时视频模型目录 | 实时读取子命令帮助 |
| `validate-plan` | schema/素材/参数预检 | 帮助约束/素材/参数预检 |
| `submit` | 一次 `node create video --run` | 一次异步生成命令 |
| `status/resume` | 原 operationRef | 原 submit_id |

未最终确认不能提交。提交状态未知时只能查询原任务。状态记录只保存账号、容器、模型、参数摘要和任务身份，不保存凭据、提示词全文或授权材料。
