# 模块索引

这是模块启用和条件加载的唯一索引。必读项提供基本流程；“条件”表示先判断任务是否需要，再读取文件。停用创意模块不影响提问、事实检查和完整输出。

| ID | 类型 | 状态 | 文件 | 加载条件 |
| --- | --- | --- | --- | --- |
| dialogue | 共创 | 必读 | [dialogue.md](dialogue.md) | 每轮的澄清判断与已决问题继承 |
| preferences | 配置 | 必读 | [preferences.md](preferences.md) | 默认创意、声音和交付偏好 |
| observation | 输入 | 条件 | [observation.md](observation.md) | 有图片或其他参考素材 |
| intent | 约束 | 必读 | [intent.md](constraints/intent.md) | 用户要求、冲突和局部修改 |
| reference | 约束 | 条件 | [reference.md](constraints/reference.md) | 有图片、视频、音频或首尾帧 |
| state | 约束 | 条件 | [state.md](constraints/state.md) | 接触、拿放、数量、变形或连续多镜头 |
| camera-space | 约束 | 条件 | [camera-space.md](constraints/camera-space.md) | 机位、镜头变化、视角或转场 |
| storyboard | 约束 | 条件 | [storyboard.md](constraints/storyboard.md) | 用户确认宫格模式后的格间连续性、画幅和状态继承 |
| audio | 约束 | 条件 | [audio.md](constraints/audio.md) | 台词、声音、音乐或明确静音 |
| action | 创意 | 条件 | [action.md](creative/action.md) | 动作、情绪、表演需要展开 |
| camera | 创意 | 条件 | [camera.md](creative/camera.md) | 需要选择镜头语言 |
| atmosphere | 创意 | 条件 | [atmosphere.md](creative/atmosphere.md) | 光影、色彩、质感和环境表达 |
| rhythm | 创意 | 条件 | [rhythm.md](creative/rhythm.md) | 多动作节奏、分镜、剪辑和停顿 |
| story | 创意 | 条件 | [story.md](creative/story.md) | 扩写剧情、动机或因果衔接 |
| storyboard-grid | 创意 | 条件 | [storyboard-grid.md](creative/storyboard-grid.md) | 用户确认宫格模式后设计四宫格或九宫格连续分镜 |
| terminology | 知识 | 条件 | [terminology.md](knowledge/terminology.md) | 术语理解或精确转译 |
| film-style | 知识 | 条件 | [film-style.md](knowledge/film-style.md) | 用户提出影片、摄影风格或审美方向 |
| output | 交付 | 必读 | [output.md](output.md) | 区分讨论与可复制成品 |
| platforms | 适配 | 必读 | [platforms.md](adapters/platforms.md) | 固定生成 LibTV 和即梦两个交付版本；平台引用、双版或参数问题 |
| image-generation | 适配 | 条件 | [image-generation.md](adapters/image-generation.md) | 用户确认宫格模式后调用 ChatGPT 内置生图工具 |
| libtv-image-generation | 适配 | 条件 | [libtv-image-generation.md](adapters/libtv-image-generation.md) | 用户确认宫格模式并选择比例、分辨率后使用 LibTV 图片模型 |
| libtv-video-generation | 适配 | 条件 | [libtv-video-generation.md](adapters/libtv-video-generation.md) | 双版本提示词完成且用户确认使用 LibTV 生成视频 |
| libtv-account-profiles | 适配 | 条件 | [libtv-account-profiles.md](adapters/libtv-account-profiles.md) | 需要在本机保存或选择多个独立 LibTV 登录账号 |
| dreamina-account-profiles | 适配 | 条件 | [dreamina-account-profiles.md](adapters/dreamina-account-profiles.md) | 需要选择即梦画布或即梦对话账号 |
| dreamina-canvas-video-generation | 适配 | 条件 | [dreamina-canvas-video-generation.md](adapters/dreamina-canvas-video-generation.md) | 选择即梦画布生成视频 |
| dreamina-conversation-video-generation | 适配 | 条件 | [dreamina-conversation-video-generation.md](adapters/dreamina-conversation-video-generation.md) | 选择即梦对话生成视频 |
| video-flow-controller | 流程 | 条件 | [video-generation-controller.md](flow/video-generation-controller.md) | 用户确认使用 LibTV 生成视频后的固定状态机、缓存、预检、提交和恢复 |
| dreamina-video-flow-controller | 流程 | 条件 | [dreamina-video-generation-controller.md](flow/dreamina-video-generation-controller.md) | 用户选择即梦 CLI 后的双适配器状态机 |
| dreamina-run-state | 流程 | 条件 | [dreamina-run-state.md](flow/dreamina-run-state.md) | 即梦画布 operationRef 或对话 submit_id 的恢复 |
| video-config-panel | 流程/UI | 条件 | [video-config-panel.md](flow/video-config-panel.md) | 宿主支持右侧 MCP App 时的一次性配置、预检、确认和恢复 |
| quality | 检查 | 必读 | [quality.md](quality.md) | 每次交付完整提示词前 |

## 增删改

新增模块时：创建一个文件，写清职责、适用条件、规则、输出和例子，再在本表登记唯一 ID。不要把同一规则复制进其他模块。

停用时将状态改成“停用”；入口不再加载该项。删除文件时同步清理索引和其他引用。修改规则时顺带检查 [评估场景](../../../evals/scenarios.md) 中受影响的案例。

各模块使用同一份当前任务信息：实际观察、用户目标、已确认决定、暂定补全、待决问题、当前动作与镜头安排。它们是内部工作信息，不要求用户填写表格，也不要求展示推理过程。

冲突处理依据 [intent](constraints/intent.md)；素材语法只在 [platforms](adapters/platforms.md) 维护；术语含义只在 [terminology](knowledge/terminology.md) 维护。
