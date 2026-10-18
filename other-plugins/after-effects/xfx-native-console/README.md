# XFX 中文控制台（After Effects）

Windows x64 的 After Effects 原生插件安装器。安装器会自动发现已登记的 After Effects 安装目录，写入 XFXNativeConsole.aex、中文默认词库、第三方声明和中文工具栏脚本。

## 一键安装

1. 完全关闭 After Effects，并保存工程。
2. [下载 XFXNativeConsoleInstaller-1.0.17.exe](https://github.com/X1039538408/yuanchuan-plugins/raw/refs/heads/main/other-plugins/after-effects/xfx-native-console/XFXNativeConsoleInstaller-1.0.17.exe) 并运行。
3. 在 Windows 管理员权限提示中选择“是”。安装器会显示实际写入的 AE 目录。
4. 重新打开 AE，按 **Ctrl+Space**，或从“窗口”菜单打开 XFX 中文控制台。

安装器支持命令行：

```powershell
.\XFXNativeConsoleInstaller-1.0.17.exe --list
.\XFXNativeConsoleInstaller-1.0.17.exe --target "D:\Adobe\Adobe After Effects 2026\Support Files"
.\XFXNativeConsoleInstaller-1.0.17.exe --target "D:\Adobe\Adobe After Effects 2026\Support Files" --silent --log "D:\Logs\xfx-install.log"
```

## 功能

- 中文、全拼、首字母和英文搜索效果及预设。
- 收藏、最近使用、置顶排序和九个快捷槽。
- 当前合成帧 PNG 快照、透明 PNG、白底 JPG 和剪贴板导出。
- 中文 ScriptUI 工具栏、深浅主题和用户译名词库。

## 版本边界

这是 1.0.17 测试版。未知第三方效果保留原名；完整第三方效果覆盖、物理输入法组合、原版逐像素对照和 ScriptUI 拖入 AE 分组后的停靠仍需人工验收。安装器会保留用户词库和旧文件备份。

**请先保存工程并退出 AE。此版安装器会强制结束仍在运行的 AE 进程，未保存工程会丢失。** 安装时会尝试更新所有发现的有效 AE 目录；这不表示所有 AE 版本已经实测兼容。

[安装、卸载和回滚说明](INSTALL.md) · [使用说明](USAGE.md) · [本次构建验证](VERIFICATION.md) · [第三方许可](THIRD-PARTY-NOTICES.txt)

本次仅分发安装器和说明，不分发 Adobe SDK 或参考软件安装包。本仓库未为 XFX 自有代码声明开源许可；第三方内容按随包声明保留许可。无需额外登录，无需 Python、Node 或 .NET。

SHA-256：

```text
B5B922563E34B3E89B8063EA82CBC25569989976D4DE14889CBE8F93EC860C45  XFXNativeConsoleInstaller-1.0.17.exe
```

本目录按市场仓库贡献指南作为非 Codex、Windows/After Effects 插件收录，不登记到 Codex marketplace.json。
