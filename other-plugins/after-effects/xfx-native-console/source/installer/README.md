# 效果控制台 1.0.19 安装与卸载

请先保存工程并自行退出所有 AE，再运行 XFXNativeConsoleInstaller.exe（源码构建产物名）。发行包名称为 EffectConsoleInstaller-1.0.19.exe。需要 Windows 管理员权限。

AE 正在运行时安装器会停止并提示先保存、退出后重试；不会结束或发送关闭请求给 AE。静默模式返回 1 并记录原因。安装或卸载均有相同保护。

## 安装

默认检测 Adobe 注册表和常见 Program Files 中的有效 AE，安装到所有发现的目录；可用 --target 指定一个 Support Files。发现目录不表示宿主兼容性已经实测。

```powershell
.\EffectConsoleInstaller-1.0.19.exe --list
.\EffectConsoleInstaller-1.0.19.exe --target 'D:\Adobe\Adobe After Effects 2026\Support Files'
```

写入 Plug-ins 下的 XFXNativeConsole.aex、XFXNativeConsole.aliases.default.tsv、XFXNativeConsole.THIRD-PARTY-NOTICES.txt 及 Scripts/ScriptUI Panels/效果控制台.jsx。旧 XFX 中文控制台.jsx 备份后移除活动入口；用户词库和个人设置保留。新装默认 Ctrl+Space，已有自定义快捷键保留。

## 一键卸载

双击 EffectConsoleUninstaller-1.0.19.exe（源码构建输出 installer/EffectConsoleUninstaller.exe），允许管理员权限，核对目录并确认。安装器也支持 --uninstall。未指定目标时处理全部检测到的有效 AE 目录。

卸载只移除上述四个文件以及旧 XFX 中文控制台.jsx；用户词库、settings.ini、旧快照、备份和其他插件保持原样。不删除整个目录。目录联接和符号链接路径会被拒绝。

## 日志、回滚和数据

默认日志为系统“文档”目录 LoYoi/XFXNative/install.log，--log 指定其他位置；--silent 不弹确认或结果框，需要调用者自行等待进程。退出码 0 成功，1 未完成。

安装事务跨多个目标；失败时尝试恢复已移动的旧文件。成功后 .backup-* 和 .disabled-* 备份保留。卸载先暂存所有固定文件，暂存失败时恢复；删除暂存文件失败会返回 1 并列出残留文件。断电或崩溃不保证自动恢复。

手动卸载时先保存工程并退出 AE，再仅移除本插件的 AEX 和工具栏等固定文件。可选个人数据清理与详细图文步骤见发布目录 UNINSTALL.md；不要删除其他插件、整个 Plug-ins 或用户文档目录。

## 源码与构建

在源码根目录运行 powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-effect-console-release.ps1。Adobe SDK 需自行获取；不会随源码分发。最终用户无需 Python、Node 或 .NET。构建不等于最新 AE 宿主全部验收通过，当前仍为测试版。
