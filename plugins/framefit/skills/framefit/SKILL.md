---
name: framefit
description: 使用视频套版 FrameFit 在 Windows 本机批量转换横竖视频、合成图片模板、调整窗口与缩放、预览并导出，或管理套版任务和预设。用于现有视频套版，不用于生成新视频或发布素材。
---

# 视频套版 FrameFit

使用插件的 FrameFit MCP 工具执行任务。视频在当前 Windows 电脑处理；本机 FrameFit 桌面软件必须保持打开，它是模板、设置和处理任务的权威来源。若本机服务不可用，先提示用户启动 FrameFit，不要创建第二份设置或改用云端处理。

## 对话工作流

1. 从用户附件获取真实、可访问的本机路径，或使用其提供的视频／文件夹路径。不要猜测路径，也不要把远程附件 URL 当本机路径；附件无法落盘访问时，说明情况并请用户提供本机完整路径。
2. 默认按本机项目规则匹配，每个输入可有自己的模板。`prepare_config` 返回每条视频的 `match`、`requiresTemplateReview`、实际 `config` 和 `plannedOutput`；也可用 `match_project_templates` 查询候选，并传 `direction` 和可选的 `outputSize`（宽、高），优先按用户要求的目标方向与尺寸筛选，不能用旧文件名尺寸覆盖转换要求。多个候选、套版更新或尺寸未匹配时展示图片、尺寸和原因，请用户选择。无匹配时预选桌面当前模板并等用户确认。用户明确提供模板路径时可视为已选择，传 `templateConfirmed: true`。不要把项目路径发到仓库或远端。
3. 调用 `prepare_config`，传入 `videos` 和已明确的 `config`。本机桌面设置作为默认值，返回 `sessionId` 和固定处理版本；运行中的任务保持该版本。已有会话修改配置时用 `update_config`，增删素材用带 `sessionId` 的 `prepare_config`。
4. 用户要求预览时调用 `preview_frame`；拖动时间轴时用 `thumbnail`。用户要求面板时调用 `render_panel`。每批导出前调用 `analyze_borders` 检查每个输入的原片边缘与套版开中尾预览；区分原片已有黑／白边与套版窗口新增边。存在新增边时列出文件和时间，询问用户保留还是消除；确认后通过 `update_config` 传 `borderPolicy: keep` 或 `remove`。remove 会等比例放大并重新检查，不能保证消除时会停止并报告。
5. 用户要求明确且素材、模板有效时，直接 `export_videos`。每次用户导出意图创建唯一 `requestId`；超时或重试同一意图复用这个 ID，避免重复导出。需要保存本机输出模板时调用 `manage_recipes`。正在运行的任务使用冻结配置和版本。
6. 用 `get_task` 查询进度，报告实际输出和逐条失败原因；保留 `taskId` 供后续查询。工具调用结束不代表导出完成；只有任务完成且对应条目有输出才交付成片。使用绝对路径链接。用户要求打开结果时调用 `open_output`。

`cancel_task` 停止任务并保留已完成结果。`resume_task` 继续失败、取消或中断任务的未完成项目，仍使用任务原版本。需要保存个人预设时用 `manage_presets`，输出模板使用 `manage_recipes`；共享预设只能通过仓库稳定版本发布。不把个人路径、素材和日志推送到仓库。

## 参数与判断

`config` 支持：

| 字段 | 值 |
| --- | --- |
| `direction` | `landscape` 竖转横、`portrait` 横转竖、`custom` 自定义 |
| `templateId` / `template` | 内置 `landscape`、`portrait`，或模板图片本机路径 |
| `detection` | `auto`、`transparent`、`black`、`white` |
| `rect` | 自动为 `null`，手动为模板原始像素 `[x,y,width,height]` |
| `fit` / `zoom` | `cover` 填满、`contain` 完整显示、`stretch` 拉伸、`zoom` 自由缩放；缩放 10～500% |
| `outputSize` | 可选 `[宽,高]`，2～16384 的正偶数；优先于 resolution，适用于各类比例 |
| `resolution` | `720p`、`1080p`、`4k`、`original` |
| `quality` / `backend` | `high`、`standard`；`auto`、`nvenc`、`cpu` |
| `outputDir` | 本机输出文件夹，默认由桌面软件提供 |
| `language` / `otherLanguage` | `auto`、`简中`、`英`、`日`、`韩`、`other`、`none`；其他语言填写标签，仅修改命名，不翻译字幕 |
| `nameMode` / `outputGroup` | `compact` / `full`；`folder` / `project` / `template` |
| `templateConfirmed` / `borderPolicy` | 用户已选模板为 `true`；新增边缘处理 `ask` / `keep` / `remove` |

`prepare_config` 与 `update_config` 支持 `itemConfigs` 数组，顺序和去重后的队列一致。给某条视频确认模板时传该条 `template`、必要的 `direction` 与 `rect`；其他条传 `{}`。可用 `outputName` 修改该条建议文件名（必须为普通 `.mp4` 文件名，不能含路径）。导出前说明总数量、实际尺寸、各条 `plannedOutput` 与需要确认的模板；不要重复询问已明确的参数。项目自动匹配优先于上次模板，无匹配仍须确认。

`prepare_config` 可传 `outputTemplates` 数组，每项是一份模板、窗口、适配、outputSize 配置，按输入视频×套版生成独立输出。不能与 itemConfigs 同传。返回条目的 outputKey 区分同视频的不同输出；不要按路径把它们合并。用户要求“按软件的输出模板处理”时沿用软件中保存的组合；可用 config.preset 指定一个保存的输出模板名。导出前报告的是输出项数及每项尺寸。

未指定项沿用桌面软件返回的本机配置／共享默认配置；自然语言明确要求覆盖对应项。切换方向时同步选择对应内置模板并清除旧手动窗口；使用自选模板时清除内置模板选择。自定义模式缺少有效模板、自动识别失败或参数矛盾时，只询问影响执行的缺项。

视频位于模板上方，放大后能覆盖模板装饰，仅由成片边界裁切。不要改成把模板覆盖在视频上，也不要把识别窗口当最终裁切框。保留原音频，无声输入保持无声；同名输出自动编号。

`auto` 优先 NVIDIA，编码不可用时回退 CPU；`nvenc` 是强制 GPU，失败需明确报告，不能悄悄回退。预览用于检查布局，不代表编码或整批任务已经成功。

## 更新与故障

桌面软件启动时读取公开稳定版本清单；下载和安装必须等用户点击“更新”。失败时继续当前可用版，向用户说明实际版本和错误。插件技能、名称或工具接口变更需要刷新市场，必要时重启 Codex／新建聊天。

不要为修复插件自动修改用户系统执行策略、安装全局 Python／Node 或关闭安全软件。首先报告工具给出的具体错误，并采用已提供的本机配置和重试入口。
