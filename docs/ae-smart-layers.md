# AE 智能分层 · After Effects

Windows / AE25 自包含 JSX，内核 portable14，安装发布包装1.0.0测试版。**它是AE工具，不是Codex插件或Skill。**

- 原生点文字逐字输出真正可编辑文字层，保留受支持样式与位置；复杂塑形、Animator、路径/文本框等明确拒绝，不静默栅格化。
- 图片和预合成当前帧按画面分出文字及花瓣、图标等可分离非文字区域，输出蒙版图像；不展开预合成内部图层、不跟踪动画、不自动冻帧，粘连/遮挡不保证逐实例分开。
- 1–20源混选，真实阶段进度、取消与批次回滚；保留隐藏原层、锚点居中、输出全选；每源左到右、下到上排序。

## 下载、安装与卸载

[完整包目录与EXE下载](<../other-plugins/after-effects/ae-smart-layers/README.md>) · [图文教程](<../other-plugins/after-effects/ae-smart-layers/tutorial/README.md>) · [源码](<../other-plugins/after-effects/ae-smart-layers/source/README.md>)

安装器与独立卸载器均使用系统.NET Framework 4.x；只向已验证AE目录的`Support Files/Scripts`放置本插件，不写Plug-ins/ScriptUI Panels。同名不同内容不覆盖；卸载只删除原始hash相符JSX，不删其他脚本、工程或AE目录。不会关闭/重启AE、修改安全偏好或保存工程。EXE未数字签名，请核对SHA-256并遵循组织安全策略。

## 验证范围

[验证与限制](<../other-plugins/after-effects/ae-smart-layers/VERIFICATION.md>)区分公开合成worker、AE宿主mock、临时模拟AE安装/卸载与真正AE宿主。历史101图像案例中的私人参考图不分发，公开源码可重跑96个人工合成案例；**没有AE25真机排版/ScriptUI全场景验收**。

[使用边界](<../other-plugins/after-effects/ae-smart-layers/USAGE.md>) · [卸载与手动恢复](<../other-plugins/after-effects/ae-smart-layers/UNINSTALL.md>) · [许可与来源](<../other-plugins/after-effects/ae-smart-layers/LICENSE-NOTICE.md>)
