# 效果控制台（After Effects）

原 XFX 中文控制台，现名 **效果控制台**。Windows x64 原生 AEX 插件，当前为 **1.0.19 测试版**。

- [下载一键安装器 EffectConsoleInstaller-1.0.19.exe](https://github.com/X1039538408/yuanchuan-plugins/raw/refs/heads/main/other-plugins/after-effects/xfx-native-console/EffectConsoleInstaller-1.0.19.exe)
- [下载一键卸载器 EffectConsoleUninstaller-1.0.19.exe](https://github.com/X1039538408/yuanchuan-plugins/raw/refs/heads/main/other-plugins/after-effects/xfx-native-console/EffectConsoleUninstaller-1.0.19.exe)
- [下载、安装与使用图文教程](tutorial/README.md)
- [一键卸载、手动删除与个人数据清理教程](UNINSTALL.md)
- [浏览完整原生源码](source/README.md) · [下载源码 ZIP](EffectConsole-1.0.19-source.zip)
- [安装与回滚](INSTALL.md) · [使用说明](USAGE.md) · [构建验证和已知限制](VERIFICATION.md) · [第三方许可](THIRD-PARTY-NOTICES.txt) · [SHA-256](SHA256SUMS.txt)

## 1.0.19 更新与边界

- 修复搜索输入时控件禁绘后遗漏恢复、浮窗重开后被旧关闭消息销毁等问题；缓存会话目录与规范化搜索字段。
- 补充截图中的 Sapphire/BCC+ 擦除效果译名，兼容 `BCC+` 后有无分隔空格的名称；原始效果标识与用户译名保留。
- 提供只读效果清单导出和词典覆盖审计脚本。**外部插件全量翻译尚未完成**：当前词典 660 行不等于 660 个外部效果，不代表覆盖全部已安装效果；未匹配项目保留原名。
- 源码、安装器、卸载器与源码 ZIP 同步发布；验证范围见 [验证说明](VERIFICATION.md)。

## 安装或卸载前

请先切回 AE 保存所有工程，再自行退出所有 AE。检测到 AE 正在运行时，安装器和卸载器都会停止并提示先保存、退出后重新运行，**不会结束进程，也不会向 AE 发送关闭请求**。静默模式仍会阻止操作，返回 1 并记录原因。

安装器会尝试处理全部发现的有效 AE 目录；自定义路径可用 --target 指定。卸载器运行后会列出目标，确认后仅移除本插件的活动文件。个人设置、用户词库、快照与备份保留。无需 Python、Node 或 .NET。

## 功能与兼容

中文、全拼、首字母和英文搜索；收藏、置顶、九个快捷槽；合成帧快照与导出；主题与用户译名词库。默认 Ctrl+空格，旧配置中自定义按键保留。

技术文件名 XFXNativeConsole.aex、个人数据目录 LoYoi/XFXNative 和仓库目录名保持兼容。升级时旧“XFX 中文控制台.jsx”备份后停用，新工具栏为“效果控制台.jsx”。

本仓库源码需自行准备 Adobe AE SDK，SDK 和参考软件不分发。自有代码尚未声明开源许可；第三方内容保留其许可。当前版本尚未完成最新真实 AE 宿主、全部第三方效果及全部 UI 验收，见 VERIFICATION.md。

此为 AE 插件，不能通过 Codex 市场直接安装。旧 1.0.17 安装器已从当前主线移除，请使用 1.0.19；历史提交中的旧 EXE 不再作为下载入口。
