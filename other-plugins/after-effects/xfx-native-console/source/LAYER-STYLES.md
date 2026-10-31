# 1.0.22 独立图层样式按钮排

按用户最终要求：**不用搜索**，在原快捷栏/结果区域下方增加一排图层样式按钮。原1–9快捷槽、效果与预设搜索、收藏、配置键及筛选选项保持不变。

| 完整样式 | 窄按钮文字 | AE样式组matchName | 原生命令 |
| --- | --- | --- | --- |
| 投影 | 投影 | `dropShadow/enabled` | 9000 |
| 内阴影 | 内影 | `innerShadow/enabled` | 9001 |
| 外发光 | 外光 | `outerGlow/enabled` | 9002 |
| 内发光 | 内光 | `innerGlow/enabled` | 9003 |
| 斜面和浮雕 | 浮雕 | `bevelEmboss/enabled` | 9004 |
| 光泽 | 光泽 | `chromeFX/enabled` | 9005 |
| 颜色叠加 | 颜色 | `solidFill/enabled` | 9006 |
| 渐变叠加 | 渐变 | `gradientFill/enabled` | 9007 |
| 描边 | 描边 | `frameFX/enabled` | 9008 |

悬停显示完整中文/英文名称和操作提示。点击按钮向当前合成中所选图层添加或启用样式；不占用1–9数字快捷键。样式不是普通效果，不使用`ADBE Effect Parade.addProperty`，也不重新暴露之前屏蔽的Photoshop兼容效果。

## 应用与安全

- 已存在且启用：不执行菜单切换、不重设参数；已存在但关闭：只启用对应样式组。
- AE未创建的样式组可能已经能被property读取，但`canSetEnabled=false`；此时使用经过核实的原生图层样式菜单命令。
- 菜单每次只选择一个目标图层，确认目标合成及Composition Viewer确已激活后执行；随后重新取得属性引用并检查目标样式可启用且已启用，不把静默失败报成功。
- 锁定图层、没有图层样式组的图层、非AV图层（如摄像机/灯光）返回具体错误；其他图层可继续，报告部分完成数量。
- 如果“图层样式”父组总开关关闭，拒绝并提示用户先在AE中启用，不为了一个样式同时打开兄弟样式。
- 一个点击对应一个平衡的Undo组。层选择按原对象引用恢复，并尝试恢复原查看器；不自动执行Undo回滚、不删除已有样式。修改过的属性选择状态不在保证范围。
- 点击通过延迟消息处理，先隐藏控制台再执行AE操作，成功关闭、失败恢复显示；原生按钮事件不会被同步销毁打断。

## 证据与验证范围

- 独立九组命令ID期望表防止测试跟随错误生产映射自证；脚本VM目前84项通过，覆盖新增样式、已有参数、关闭后重启、多层选择、锁定/非AV/父组关闭、未知ID、菜单静默失败、查看器失败、撤销组和原效果/预设功能。
- Native UI验证独立九按钮、至少28px高度、横排间距、结果/状态不重叠、延迟执行链路、搜索目录不出现样式、原快捷槽不变；沿用现有主题/字体/hover/tooltip。最小高度增加样式行空间。
- 2026-10-30真实AE **25.4x86** 用户手动运行实际C++生成脚本的验收：9种全部通过，每种对2个测试图层初次添加、重复启用、关闭再启用均`OK\t2`；未选中的测试层不变，选层恢复。若有opacity属性，将其设37后核对重复操作未重设。
- 首次空白工程保护拒绝后，用户明确授权在当前工程使用临时合成。最终报告显示测试合成已删除、项目项 **14→14**；只清理本次新增且未被其他合成引用的纯色素材与本次空文件夹，原有素材/合成不删除。创建删除会留下Undo/dirty状态，报告不是无痕测试声明。
- 实际报告位于忽略目录`artifacts/layer-styles-20261029/ae-layer-style-validation.json`。未验收所有AE版本/渲染器、真实宿主Undo恢复及3D/视频/文字/形状组合渲染；不承诺命令ID跨全部版本永远不变。

## 维护与复测

先运行`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-ae-script-tests.ps1`导出实际脚本，再经用户授权在AE运行`scripts/test-layer-styles-in-ae.jsx`。它不打开/保存/替换工程，只创建专用测试合成并清理自有资源；会改变Undo/dirty状态，不应未经用户授权在未保存工程运行。

参考：
- [Layer Styles Match Names](https://ae-scripting.docsforadobe.dev/matchnames/layer/layerstyles/)
- [Adobe社区：Add a Layer Style (Stroke)](https://community.adobe.com/questions-529/add-a-layer-style-stroke-72770?lang=en)
- [独立命令参考表](https://github.com/hyperbrew/after-effects-command-ids/blob/master/json/manual.json)

官方指南/社区正文抓取受网络限制，匹配名和行为资料为检索摘要；独立命令JSON正文已下载核对，并由本机AE25.4x86九项实际添加结果交叉验证。不能把社区映射当成Adobe跨版本保证。
