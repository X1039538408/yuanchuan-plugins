# XFX 中文控制台

XFX 中文控制台是面向 Windows x64 / After Effects 的原生 `.aex` 插件。它把 FX Console 风格的效果查找流程改成中文、全拼、首字母和英文都能使用的搜索浮窗，并提供快捷槽、收藏、译名词库和当前帧导出。

## 下载与安装

从 [插件目录](../other-plugins/after-effects/xfx-native-console/) 下载 `XFXNativeConsoleInstaller-1.0.17.exe`。关闭所有 After Effects 后运行安装器，接受 Windows 管理员权限提示，重新打开 AE，按 `Ctrl+Space` 或从“窗口”菜单打开控制台。

安装器会从 Adobe 注册表和常见 Program Files 目录发现有效 AE 安装，并把 AEX、默认词库、第三方声明和中文 ScriptUI 工具栏安装到各自的 Support Files。自定义安装目录可用 `--target` 指定。

## 主要功能

- 中文、拼音全拼、首字母、英文和内部标识搜索。
- Enter 或鼠标左键应用到所选图层；Tab 切换全部、效果和预设。
- 收藏、最近使用、置顶排序、搜索别名和 1–9 九个快捷槽。
- 保存当前合成帧为 PNG；导出入口支持透明 PNG、白底 JPG 和剪贴板。
- 中文工具栏、深浅主题、用户译名词库、配置导入导出。

## 验收边界

版本 1.0.17 为测试版。未知第三方效果保留原名，插件不翻译第三方参数面板。完整第三方效果覆盖、物理输入法组合、原版逐像素对照以及 ScriptUI 面板拖入 AE 分组后的停靠仍需人工验收。安装器保留用户词库和旧文件备份。

这是非 Codex 的 Windows/After Effects 插件，按市场仓库贡献指南放在 `other-plugins/`，不会通过 Codex 市场安装入口直接安装。
