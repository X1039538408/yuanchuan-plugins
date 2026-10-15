# Seedance 创意导演

根据参考图片与简略脚本，共创可复制的中文视频提示词。创意边界开放；关键矛盾、人物动机和前因后果缺口通过对话解决。技能型插件，无需自行配置模型密钥或部署服务器，实际看图能力取决于使用它的宿主模型。

## 使用

在插件页添加插件后，在聊天中选择它并提供图片和描述。也可以使用宿主支持的技能调用 `$seedance-director`。

- “让图里的像素少年拔剑，紧张一点，镜头和动作你来补。”
- “检查剧情里人物为什么会突然背叛；先和我讨论，再写提示词。”
- “他已经把玉佩扔了，下一幕却用它开门，帮我理顺这段。”
- “按刚确认的动机，只修改结尾运镜，重新给完整提示词。”

默认固定生成两个中文成品版本：LibTV 版和即梦网页版。两版独立代码块，正文一致，只替换素材引用格式。LibTV 的图片、视频和音频按提供顺序统一引用为 `{{Mixed 1}}`、`{{Mixed 2}}`、`{{Mixed 3}}` 等。讨论阶段会直接提问，不受成品模板限制；用户明确只要单版时才输出单版。

宫格分镜模式默认关闭。对于镜头过多、动作链复杂或连续性约束细的任务，插件可以推荐四宫格或九宫格，但必须等用户明确确认后才启用。确认后先询问画幅比例和分辨率，再通过已授权的 `@LibTV` MCP 插件使用 `Lib Image 2.5 Pro` 普通图片生成模型制作宫格图；不调用 LibTV 自带的九宫格或多机位预设。LibTV 失败后，只有用户确认才回退到 ChatGPT 内置生图，生成后等待用户验收，再生成视频双版本。MCP 插件授权与本地 CLI 登录相互独立。

## 即梦双 CLI 视频生成

提示词完成后可以选择一个视频平台：LibTV 或即梦 CLI。即梦 CLI 分成两条独立路径：

- **即梦画布**：`dreamina-canvas.exe`，按账号选择最近 6 个画布项目，读取节点、模型 schema，并用 `operationRef` 恢复；
- **即梦对话**：`dreamina.exe`，按账号选择最近 6 个对话会话，读取剩余积分和当前子命令帮助，并用 `submit_id` 恢复。

账号路由使用本机 `%USERPROFILE%\bin\dreamina-accounts.ps1`。画布 profile 与对话 CLI 的独立用户目录分别保存，画布登录不代表对话登录。第一版一次只提交一个平台；最终确认前不提交付费任务。

新增流程控制器为 `tools/dreamina_video_flow.ps1`，状态保存在 `%USERPROFILE%\.seedance-director\dreamina-runs`。

插件同时提供 `mcp.json` 和 `server/` 右侧视频生成控制面板。面板会批量读取实时能力，让用户在一屏内选择平台、账号、项目/会话、模型、模式、比例、分辨率和时长；未点击“确认并生成”前不会提交付费任务。宿主不支持 MCP App 时，仍回退到原有文字流程。

## 开发

入口是 [SKILL.md](skills/seedance-director/SKILL.md)，模块登记在 [modules.md](skills/seedance-director/references/modules.md)。

- 创意文件：动作、镜头、氛围、节奏、剧情。
- 约束文件：意图继承、素材用途、状态、空间镜头、声音。
- 共创：维护 [dialogue.md](skills/seedance-director/references/dialogue.md)。
- 默认值：维护 [preferences.md](skills/seedance-director/references/preferences.md)。
- 平台引用：只修改 adapters/platforms.md，避免在创意模块复制语法。
- 宫格分镜：维护 creative/storyboard-grid.md、constraints/storyboard.md 和 adapters/image-generation.md，保持推荐、确认、生图、验收和视频交付边界清楚。
- 视频生成流程：维护 flow/video-generation-controller.md、flow/video-run-state.md 和 tools/libtv_video_flow.ps1；账号、项目、节点、模型 schema、参数、预检、提交和恢复由控制器处理。
- 新模块登记唯一 ID，停用修改状态，删除同时清理引用。规则和术语只保留一个维护位置。

开发校验器需要 Python 与 PyYAML（`python -m pip install PyYAML`）；插件在聊天中使用时不需要这些依赖。在插件根目录执行 `python tools/validate_package.py` 检查结构、模块索引、链接、展示字段和 UTF-8 文件。包根目录名称必须与 plugin.json 的 name 一致。压缩时保留这一层目录，ZIP 放在目录外。

更新已保存的插件时使用原插件 ID 和当前 release ID，不能用“创建”重复生成新插件。递增版本，保留用户当前私有范围。

右侧面板的本地 MCP 服务位于 `server/`，使用 Node.js 内置模块，无需安装额外依赖。验证时运行 `node server/tests/mcp_contract.test.mjs`。

## 验证边界

本地检查验证包结构和可访问性，不能证明模型每次都正确理解故事。见 [评估场景](evals/scenarios.md)：其中列出预期行为，须在目标宿主中实际运行并记录结果，不能把存在场景文件当作行为测试通过。

实际图片理解、安装后的技能自动触发、连续多轮互动和视频生成效果需分别验收。插件提供提示词和沟通流程，没有内置媒体生成 API；不会仅因写好提示词就声称已生成视频。

## 来源

基于指定上游和用户提供的六份资料整理，详见 [来源说明](SOURCES.md)、[来源指纹](source-provenance.json) 与 [上游 MIT 许可](LICENSE)。
