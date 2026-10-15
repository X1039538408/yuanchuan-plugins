# 即梦画布视频生成适配

该适配器只处理 `dreamina-canvas.exe` 的画布项目和节点。

1. 检查选定 canvas profile 的 `auth status`。
2. 使用 `canvas ls` 按更新时间倒序列出最近 6 个“即梦画布项目”，显示项目名、UUID、更新时间和链接。
3. 用户选择精确 `projectId` 后，使用 `node find --project-id` 解析需要的真实节点。
4. `{{Mixed N}}` 是提示词交付占位符，不能直接传给 CLI；素材无法唯一映射时暂停询问。
5. 使用 `model list --type video`，用户选中模型后再读取该模型 schema。
6. 根据 schema 选择 mode、比例、分辨率、时长和其他必填字段；新增模型和枚举自动出现，未知必填字段严格拦截。
7. 预检成功后展示项目、模型、参数、素材节点和 LibTV 版 prompt，等待最终确认。
8. 确认后只提交一个视频节点，保存 `projectId`、`nodeId`、`submitId/operationRef`。

网络中断或“继续”只使用原 `operationRef` 查询，不创建第二个节点。schema 指纹变化时暂停并重新确认受影响参数。
