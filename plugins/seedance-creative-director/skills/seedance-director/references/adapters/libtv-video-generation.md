# LibTV 视频生成适配

## 职责

把已经定稿的 LibTV 版提示词交给本机视频生成流程控制器。该模块只在用户明确选择“使用 LibTV 生成视频”后加载。

固定流程由[视频生成流程控制器](../flow/video-generation-controller.md)执行；本文件负责输入契约和交付边界。

## 输入契约

控制器必须收到：

- 已交付的 LibTV 版 prompt；
- 用户确认的素材用途；
- 本机 profile 选择；
- 项目 UUID；
- 用户确认的模型、模式、比例、分辨率和时长；
- 可映射的真实 LibTV 节点。

`{{Mixed N}}` 只用于提示词交付，不直接传给 LibTV CLI 作为节点 ID。

## 两阶段交互

### 阶段一：账号与项目

1. 调用 `discover-profiles`。
2. 用户选择 profile。
3. 复用已确认的 DH folderId；没有时询问一次。
4. 调用 `list-projects`。
5. 展示最近 6 个项目和精确 UUID。
6. 用户选择项目。

### 阶段二：模型与参数

1. 调用 `resolve-assets`。
2. 调用 `list-models`。
3. 用户选择模型和模式。
4. 调用 `get-model-schema`。
5. 根据 schema 动态生成比例、分辨率、时长和其他必填字段。
6. 用户完成参数选择。
7. 调用 `validate-plan`。
8. 展示最终摘要并等待最终确认。
9. 用户确认后调用 `submit`。

## 动态模型规则

- 每次按当前 profile 查询模型目录。
- 不硬编码 Seedance 2.5 或其他模型名称。
- 新模型自动展示。
- 新字段可以动态生成选项。
- 无法解析的必填字段严格拦截。
- 不自动替换模型、模式、比例、分辨率或时长。
- 最终确认和恢复前重新读取 schema；`schemaHash` 变化时返回 `schema_changed`，暂停提交并要求重新确认受影响参数。

## 提交规则

- 生成使用 LibTV 版 prompt。
- 使用真实 image、video、audio 节点。
- 控制器创建唯一视频节点并只触发一次 CLI `--run`。
- 预检失败不创建节点。
- 未最终确认不触发 `--run`。
- 提交状态未知时查询原节点，不创建第二个节点。

## 断点恢复

- 状态保存在 `%USERPROFILE%\\.seedance-director\\video-runs`，缓存保存在 `%USERPROFILE%\\.seedance-director\\video-flow\\cache`。
- “继续”只调用 `status` 或 `resume`。
- 已提交任务不重复提交。
- 只有用户明确要求重新生成并重新确认参数时，才创建新的 runId。

## 失败处理

- profile 401：标记该 profile 失效并停止该 profile 流程。
- 没有 folderId：返回 `folder_required` 并暂停。
- 缓存 folderId 无项目：返回 `folder_mapping_invalid` 并要求重新确认；用户刚提供的 folderId 无项目：返回 `no_projects`，允许改正映射。
- 没有项目：允许修改 folderId 或明确查看全部项目。
- 素材无法映射：暂停并让用户选择真实节点。
- schema 不支持参数：严格拦截并展示允许值。
- CLI 提交错误：保留错误摘要和 runId，不假装生成成功。
