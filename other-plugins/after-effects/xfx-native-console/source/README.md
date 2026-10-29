# 效果控制台 1.0.19 源码

这是已发布 Windows x64 原生插件、安装器、卸载器和测试的可审查源码。技术文件名 XFXNativeConsole 与个人配置目录保持兼容，用户看到的名称为“效果控制台”。

## 构建依赖

- Windows x64、Windows PowerShell 5.1 或 PowerShell 7；本次使用 5.1 验证。
- Visual Studio 2022 Build Tools：MSVC v143（本轮 14.44.35207）及 Windows SDK 10.0.26100.0。脚本固定该已验证版本的工具路径；若使用其他版本需调整脚本中的路径。
- 自行从 Adobe 获取 After Effects SDK（本轮使用 25.6），解压到 vendor/ae-sdk，使 Examples/Headers/AEConfig.h 存在。SDK 和参考软件安装器不随源码分发。

源码 ZIP 和 Git 源文件统一使用 LF 换行，保留 UTF-8 BOM（适用的 PowerShell 脚本）；`SOURCE-SHA256SUMS.txt` 不依赖 Windows 的 CRLF 检出设置。打包脚本从 `VERSION` 读取版本号，并拒绝覆盖既有导出目录或压缩包。

## 编译

在本目录打开 PowerShell：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-effect-console-release.ps1
```

输出 artifacts/native-plugin/AEGP/XFXNativeConsole.aex、installer/XFXNativeConsoleInstaller.exe 和 installer/EffectConsoleUninstaller.exe。构建过程只编译、打包和校验，不安装到真实 AE。

## 测试

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test-native-installer-cpp.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test-native-installer.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test-effect-console-lifecycle.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-native-core-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-console-support-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run-native-ui-tests.ps1
node scripts/scriptui-toolbar-tests.cjs
node scripts/effect-inventory-tests.cjs
node scripts/audit-effect-translations-tests.cjs
```

Node 仅用于工具栏测试，最终用户不需要。生命周期测试使用临时目录和复制的 PING 伪 AE，不结束真实 AE；测试入口 EXE 仅将 UAC 清单改为 asInvoker，并写入 artifacts/installer-entry-tests。发行 EXE 始终要求管理员权限。

## 译名覆盖核对

在 AE 中通过“文件 → 脚本 → 运行脚本文件”运行 `scripts/export-effect-inventory.jsx`，只读导出已注册效果到 `artifacts/effect-inventory/ae-effects.json`，不修改工程。脚本写入权限未开启时会报告错误，不自动更改首选项。

```powershell
node scripts/audit-effect-translations.cjs artifacts/effect-inventory/ae-effects.json installer/XFXNativeConsole.aliases.tsv
```

报告仅统计随包词典覆盖，不等于用户覆盖或真实宿主行为验证；厂商分类为启发式，未知效果单独保留。外部效果全量翻译仍在进行中，不宣称已全译。

## 目录

- native/plugin 与 native/src：AE 入口、Win32 界面、搜索、设置、图片和桥接实现。
- native/tests、installer/tests：测试源码。
- installer：安装/卸载共用 C++ 源码、资源、清单、词库和 JSX 工具栏。内部 JSX 源文件保留旧名，安装目标为 效果控制台.jsx。
- scripts：构建、测试、源码打包与安全安装脚本。
- vendor/zlib-inflate、native/src/data：随附第三方来源与许可。vendor/ae-sdk 仅包含获取说明。

本仓库未为自有代码声明开源许可证；上传源码不表示额外授予许可。第三方内容遵循其随附许可。测试版宿主与 UI 验收限制见发布目录的 VERIFICATION.md。
