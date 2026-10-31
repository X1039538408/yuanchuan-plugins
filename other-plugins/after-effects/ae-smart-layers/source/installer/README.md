# AE 智能分层 Windows 一键安装器

仅分发经过审核的原始 JSX，不修改脚本。版本：安装器 1.0.0 / portable14；脚本 SHA-256：

```text
2EB5FA596DFF774F50AA4029AAAAE8B3907F540475F6642E9825B52BBAAE279D
```

## 发布文件

- `dist/AE 智能分层.exe`：原生中文 WinForms 安装与卸载界面；内嵌 JSX 和独立卸载程序。
- `dist/uninstaller.exe`：独立原生中文卸载界面；无需旁边放置脚本。
- 不依赖下载。运行和构建需要 Windows 自带 .NET Framework 4；安装器没有代码签名，Windows 可能显示未知发布者/SmartScreen 提示，不应声称已签名。

## 使用

打开安装器；唯一检测到的有效 AE 自动选中，多版本明确选择，也可浏览 AE 根目录或 `Support Files`。必须存在 `Support Files/AfterFX.exe`，不会猜测不存在的 AE 路径。检测只读 Program Files 的 Adobe 子目录、Adobe After Effects 与卸载注册表的32/64位视图，不写注册表。

点击“安装”，只放置 `Support Files/Scripts/AE 智能分层.jsx`，不是 ScriptUI Panels。安装后可以从 **文件 > 脚本** 菜单或 **运行脚本文件** 执行；菜单发现可能等下次 AE 启动，但不强制重启。不会关闭/杀死 AE、修改安全首选项或保存工程。权限不足时明确报错，用户需自行右键“以管理员身份运行”，不自动提权。

卸载可在安装器中点击“卸载”，或运行独立 `uninstaller.exe`。安装时还会在当前用户 `%LOCALAPPDATA%/AE Smart Layer Split/` 保存卸载程序与 `manifest.xml`。卸载默认候选来自此记录和有效 AE 检测；多个目标不会批量猜测，逐个选择。全部卸载后为管理其他版本保留卸载程序与空记录；如不再需要，可自行仅删除该目录内的 `uninstaller.exe` 与 `manifest.xml`，本程序不递归删目录、不尝试自删正在运行的程序。

同名原始哈希文件可重复安装；同名不同内容拒绝覆盖。卸载仅删除实际内容与内置原始哈希一致的固定名称 JSX，修改内容保留且操作明确失败，记录保留。其他脚本与目录不删除。

### 卸载记录丢失或异常

- 记录丢失时可浏览并明确选择有效 AE 目录卸载：仍要求 AfterFX.exe 存在，并核对内置原始哈希，不靠记录授权删除。
- 记录损坏时停止操作，不悄悄替换记录或回报成功。备份当前用户的 `manifest.xml` 后移走损坏记录，对每个已装版本重新安装以重建记录；不同内容脚本仍拒绝覆盖。
- 手动方法：在确认的 AE `Support Files/Scripts` 中只检查 `AE 智能分层.jsx`，先备份自己修改过的内容，再自行删除该文件。不要删除 Scripts、ScriptUI Panels 或其他插件目录。

## 静默命令

生产静默命令必须同时显式指定操作和绝对目标目录；不会按检测结果自动执行：

```powershell
& '.\AE 智能分层.exe' --install --target 'C:\Program Files\Adobe\Adobe After Effects 2025' --quiet
& '.\uninstaller.exe' --uninstall --target 'C:\Program Files\Adobe\Adobe After Effects 2025' --quiet
```

成功退出码0，任何参数、验证、冲突、权限、事务或卸载保护错误均退出1。EXE 为 WinExe，建议自动化使用 `Start-Process -Wait -PassThru` 读取退出码；不会弹出静默错误对话框。

`--state-dir` 仅允许与明确的 `--test-mode` 配合；测试模式必须提供状态目录。此开关仅为隔离状态与屏蔽真实检测的测试便利，**不是权限边界或安全沙箱**：仍需调用者保证 `--target` 指向自己创建的测试夹具。它不让任意目标合法，仍完整验证固定名称、AfterFX.exe、祖先 reparse 与原始哈希。

## 从源码离线重建

在 Windows PowerShell 5.1 或 PowerShell 7 执行：

```powershell
& .\build.ps1 -JsxPath 'C:\完整路径\AE 智能分层.jsx' -OutputDirectory '.\dist'
& .\tests\test.ps1 -BinaryDirectory '.\dist' -ReportPath '.\tests\test-report.json'
```

只调用系统 `C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe`，无NuGet、联网下载或第三方编译工具。先编译卸载器，再将卸载器与原始 JSX 作为资源编译进安装器。构建前后都核对 JSX SHA-256；不接受不同内容。PowerShell 脚本应保留 **UTF-8 BOM**，避免 Windows PowerShell 5.1 将中文源路径按系统 ANSI 解码。

## 状态和安全约束

- XML记录仅存规范化的 `Support Files` 目标路径与内置固定哈希；禁止DTD、未知字段、重复目标、过大记录。记录仅作发现提示，实际操作始终重新验证目标及当前文件。
- 严格只触及目标下 `Scripts/AE 智能分层.jsx`，要求 AfterFX.exe 仍存在，目标、Scripts、固定文件、状态路径的所有现存祖先均拒绝 reparse/junction/symlink。
- 同用户状态由命名Mutex串行化。记录/卸载器使用同目录新建临时文件、Flush(true)、原子 File.Replace（新文件用 Move）；失败恢复之前的记录和卸载器，并回滚本次创建的脚本。若回滚本身失败，明确显示恢复失败，不回报成功。
- 安装不覆盖既有不同内容；新脚本用 CreateNew 防止一般同名抢占。卸载先原子更新记录，再重新核对原始哈希并删除固定脚本；删除失败回滚记录。不会递归删除任何目录。此实现不是对能并发修改同一用户目录的恶意本地进程的内核级安全边界。

## 验证范围

测试仅在 `tests/temp-fixture-<GUID>/` 创建空 AfterFX.exe 与假 AE 目录，不启动 AE、不触真实安装、不改注册表、不改真实 Program Files ACL。JSON报告和夹具保留供审计；无需递归清理才能运行。测试包括普通安装/卸载、幂等、修改文件与同名冲突、其他文件、多目标、无效目标、AfterFX丢失、记录异常/DTD/任意路径/伪哈希、junction、参数以及三类回滚。

UI测试从相同源码编译隔离 harness，离屏创建原生 WinForms，检查默认与最小窗口裁切、执行安装/修改保护/卸载按钮，生成可检查的截图。尚未验证真实AE菜单发现、JSX在AE中的行为、真实Program Files权限提示、跨机器高DPI与辅助技术行为；不把夹具通过宣称为AE真机通过。
