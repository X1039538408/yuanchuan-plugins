# XFX 中文控制台安装、升级和卸载

适用 Windows x64 / After Effects。此包为 1.0.17 测试版，宿主验收边界见 [VERIFICATION.md](VERIFICATION.md)。

## 一键安装

1. 保存所有工程并完全退出 AE。**此版安装器会强制结束仍在运行的 After Effects 进程，未保存工程会丢失。**
2. 双击本目录的 `XFXNativeConsoleInstaller-1.0.17.exe`，接受 Windows 管理员权限提示。
3. 安装完成后查看实际安装目录，重新打开 AE，按默认的 `Ctrl+Space` 或从“窗口”菜单打开 XFX 中文控制台。原配置中自定义的快捷键保留。

安装器从 Adobe 的 HKLM/HKCU 注册表和常见 Program Files 目录发现所有有效 AE 目录，每个目标需已有 AfterFX.exe（或 AfterFXBeta.exe）及 Plug-ins 文件夹。会尝试安装到全部发现的有效目录；发现目录不表示该版本已通过兼容性测试。便携或未登记目录可显式指定。

## 命令行

在已下载文件的目录打开 PowerShell：

```powershell
# 仅查看目标目录，不安装
.\XFXNativeConsoleInstaller-1.0.17.exe --list

# 安装到指定 Support Files
.\XFXNativeConsoleInstaller-1.0.17.exe --target 'D:\Adobe\Adobe After Effects 2026\Support Files'

# 等待完成并取得退出码；静默模式仍要求管理员权限
$process = Start-Process -FilePath '.\XFXNativeConsoleInstaller-1.0.17.exe' -ArgumentList '--silent' -Wait -PassThru
$process.ExitCode
```

`--help` 查看参数，`/quiet` 等同于 `--silent`；退出码 0 为成功、1 为失败。默认日志在系统“文档”目录 `LoYoi\XFXNative\install.log`；`--log` 可指定位置。EXE 使用编译时内置资源，无需下载额外脚本运行时。

## 升级与备份

安装器写入四个文件：

- `Plug-ins\XFXNativeConsole.aex`
- `Plug-ins\XFXNativeConsole.aliases.default.tsv`
- `Plug-ins\XFXNativeConsole.THIRD-PARTY-NOTICES.txt`
- `Scripts\ScriptUI Panels\XFX 中文控制台.jsx`

多目标安装使用同一文件事务；失败时尝试逆序恢复旧文件。成功后的旧文件保留为 `.backup-时间戳`。断电或进程崩溃不保证自动恢复，可按日志和备份手动恢复。

已有 `Plug-ins\XFXNativeConsole.aliases.tsv`、文档目录下用户词库和个人设置保留。旧 XFXEnhancerBridge 脚本改名为 `.disabled-时间戳` 备份；其他厂商插件和脚本保持原样。

1.0.17 已移除旧快照图库、固定截图位置和清缓存入口；旧图片保留，新快照保存到另存为对话框选择的位置。

## 卸载和回滚

先保存工程并退出 AE。在安装成功窗口列出的每个 Support Files 目录中删除 `Plug-ins\XFXNativeConsole.aex` 即可停用；可选移除默认词库、第三方声明及 `Scripts\ScriptUI Panels\XFX 中文控制台.jsx`。保留个人词库、文档目录下的 LoYoi\XFXNative 和备份文件，以便恢复。

回滚时移走当前文件，将对应 `.backup-时间戳` 恢复为原名。工具栏 JSX 和 AEX 应配套恢复。不要删除其他厂商文件。
