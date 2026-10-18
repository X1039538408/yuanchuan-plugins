# XFX 中文控制台原生插件安装

双击 `XFXNativeConsoleInstaller.exe`（或同目录的 `Install-XFXNativeConsole.cmd`）。Windows 会要求管理员权限；如果 AE 正在运行，安装器会直接强制结束所有 After Effects 进程后继续安装，未保存工程会丢失。安装完成后显示实际安装目录；重新打开 AE 后按 `Ctrl+空格` 呼出，也可以在设置中按下新的组合键自定义。窗口失焦会自动关闭，设置中不再提供固定窗口或自动关闭开关。

安装器从 Adobe 的 HKLM/HKCU 注册表及 `Program Files\Adobe\Adobe After Effects*` 目录发现所有版本，安装到全部有效目录。每个目标必须已有 `AfterFX.exe`（或 `AfterFXBeta.exe`）及 `Plug-ins` 文件夹。非默认盘符的安装通常可由注册表发现；没有登记的便携/自定义安装请显式指定目标。发现安装目录不代表该 AE 版本已经通过运行兼容性测试，参见 `docs/compatibility.md`。

## 命令行

```powershell
# 安装到指定版本，路径指向 Support Files
.\installer\XFXNativeConsoleInstaller.exe --target 'D:\Adobe\Adobe After Effects 2026\Support Files'

# 不弹安装结果窗口；管理员权限要求仍由 Windows 处理
.\installer\XFXNativeConsoleInstaller.exe --target 'D:\Adobe\Adobe After Effects 2026\Support Files' --silent --log 'D:\Logs\xfx-install.log'

# 仅查看检测到的目标目录，不安装
.\installer\XFXNativeConsoleInstaller.exe --list
```

`--help` 显示参数；`/quiet` 等同 `--silent`。退出码 `0` 表示成功，`1` 表示失败。GUI 程序在 PowerShell 中需要可靠等待退出时可使用 `Start-Process -Wait -PassThru`；`--list --silent` 的结果也记录在日志中。

默认 UTF-8 日志：系统“文档”目录下的 `LoYoi\XFXNative\install.log`。异常窗口会给出日志位置；日志包含目标与失败/回滚信息。`--log` 可改位置。安装前会确认日志可写。

源码安装脚本不会自动提权；安装到 Program Files 时从管理员 PowerShell 执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-native-plugin.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-native-plugin.ps1 -AeRoot 'D:\Adobe\Adobe After Effects 2026\Support Files' -Silent
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-native-plugin.ps1 -List
```

脚本的 `-Target` 是 `-AeRoot` 别名；`-LogPath` 指定日志，`-ProjectRoot` 指定源码/打包根目录。脚本优先使用该目录下最新编译的 `artifacts\native-plugin\AEGP\XFXNativeConsole.aex`，没有时使用 `installer\XFXNativeConsole.aex`。EXE 始终使用编译时内置的资源。

## 升级与中文词库

1.0.17 移除快照图库、固定截图位置和清缓存入口。相机按钮改为打开 PNG 保存对话框，默认当前合成名，允许修改名称与位置，确认后才渲染保存；取消不生成文件。导出使用临时图片源并在结束后清理。升级保留旧版 `LoYoi\XFXNative\Capture` 目录及已有图片。

先把新 AEX、默认词库、许可文件和中文工具栏脚本写入每个目标目录的临时文件，再备份并替换已安装文件。所有选中版本共用一个文件事务；任一步失败会尝试逆序恢复全部旧文件。正常成功后旧文件保留为 `.backup-时间戳`。进程崩溃或断电不保证自动恢复；此时可根据日志和备份手动恢复。

- 安装/更新：`Plug-ins\XFXNativeConsole.aex`、`Plug-ins\XFXNativeConsole.aliases.default.tsv`、`Plug-ins\XFXNativeConsole.THIRD-PARTY-NOTICES.txt`、`Scripts\ScriptUI Panels\XFX 中文控制台.jsx`。面板可从“窗口”菜单的脚本面板入口打开；真实宿主内停靠/绘制仍待验收。
- 保留：已有 `Plug-ins\XFXNativeConsole.aliases.tsv`，其中的个人翻译不会被覆盖。
- 用户词库：系统“文档”目录下 `LoYoi\XFXNative\aliases.user.tsv`；安装器不会修改它。插件启动时读取默认词库、兼容旧词库、用户词库，后加载的同名规则优先。默认更新后若某条翻译仍旧，请检查旧词库或用户词库中的覆盖项。
- 原有 `XFXEnhancerBridge.jsx` / `.jsxbin` 会改名为 `.disabled-时间戳` 保留原内容，防止旧桥接与原生版本同时运行。其他 JSX、桥接备份和第三方插件保持原样。

## 卸载或恢复旧版

安装器会检测 `AfterFX.exe`、`AfterFX.com`、`AfterFXBeta.exe` 和 `AfterFXBeta.com`，并直接强制结束它们后替换文件；未保存工程会丢失，无法结束时安装会停止并写入日志。安装完成后，进入安装成功窗口所列出的每个 `Support Files\\Plug-ins` 目录，删除 **`XFXNativeConsole.aex`** 即可停用插件；可选删除 `XFXNativeConsole.aliases.default.tsv`。保留 `XFXNativeConsole.aliases.tsv`、系统“文档”目录下的 `LoYoi\XFXNative`、`.backup-*` 文件和 `.disabled-*` 桥接备份，以便重新安装或恢复。无需删除其他厂商的插件。

同时移除 `Support Files\Scripts\ScriptUI Panels\XFX 中文控制台.jsx`，以移除工具栏菜单项。需要恢复旧版工具栏时可还原同名 `.backup-*` 文件；请配合对应版本 AEX 使用。

需要回到本插件旧版时，先移走当前 AEX，再把对应的 `XFXNativeConsole.aex.backup-*` 恢复为 `XFXNativeConsole.aex`；默认词库可同步恢复对应备份。确需启用旧桥接时，把选定的 `.jsx.disabled-*` 或 `.jsxbin.disabled-*` 恢复成原文件名，并先停用本原生 AEX。

## 安装流程测试

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-native-installer.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-native-installer-cpp.ps1
```

测试只创建系统临时目录，使用虚构的 AE 文件结构验证新装、升级、保留自定义、桥接备份、真实文件锁触发的回滚、拒绝非 AE 目录；C++ 测试另验证多版本目标和参数解析。C++ harness 使用同一安装事务函数、控制台入口和 `/MANIFESTUAC:NO`，不运行需提权的发行安装器，不写入真实 Program Files。编译 harness 需要与项目构建脚本相同的 MSVC/Windows SDK。这些测试不代表插件已在所有 AE 版本中实测。
