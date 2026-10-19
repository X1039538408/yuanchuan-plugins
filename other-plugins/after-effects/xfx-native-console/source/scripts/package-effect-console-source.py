"""Copy an explicit source-only allowlist; never distribute Adobe SDK or reference binaries."""
import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--destination', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
dest = Path(args.destination).resolve()
if dest.exists():
    raise SystemExit('Preserve existing source export; choose a new destination.')
dest.mkdir(parents=True)
files = []
for directory in ['native', 'vendor/zlib-inflate', 'assets/catalog']:
    for p in (root / directory).rglob('*'):
        rel = p.relative_to(root)
        if not p.is_file() or any(x in ['obj', 'build', 'bin', '__pycache__'] for x in rel.parts):
            continue
        if p.suffix.lower() not in ['.cpp', '.c', '.h', '.r', '.vcxproj', '.filters', '.json', '.md', '.txt', '.py']:
            continue
        files.append(rel)
for p in (root / 'installer').rglob('*'):
    rel = p.relative_to(root)
    if p.is_file() and 'obj-installer' not in rel.parts and p.suffix.lower() in ['.cpp', '.rc', '.manifest', '.jsx', '.tsv', '.txt', '.cmd', '.md']:
        files.append(rel)
script_names = ['build-native-plugin.ps1', 'build-installer-exe.ps1', 'build-effect-console-release.ps1',
    'check-ae-sdk.ps1', 'install-native-plugin.ps1', 'test-native-installer.ps1', 'test-native-installer-cpp.ps1',
    'test-effect-console-lifecycle.ps1', 'test-installer-exe.ps1', 'test-installer-payloads.ps1',
    'test-ae-process-detection.ps1', 'scriptui-toolbar-tests.cjs', 'package-effect-console-source.py']
files += [Path('scripts') / name for name in script_names]
files += [p.relative_to(root) for p in (root / 'scripts').glob('run-*-tests.ps1') if p.name != 'run-core-tests.ps1']
files += [Path('Directory.Build.props'), Path('vendor/ae-sdk/README.txt')]
for rel in sorted(set(files)):
    output = dest / rel
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / rel, output)
readme = '''# 效果控制台 1.0.18 源码

这是已发布 Windows x64 原生插件、安装器、卸载器和测试的可审查源码。技术文件名 XFXNativeConsole 与个人配置目录保持兼容，用户看到的名称为“效果控制台”。

## 构建依赖

- Windows x64、PowerShell 7。
- Visual Studio 2022 Build Tools：MSVC v143（本轮 14.44.35207）及 Windows SDK 10.0.26100.0。脚本固定该已验证版本的工具路径；若使用其他版本需调整脚本中的路径。
- 自行从 Adobe 获取 After Effects SDK（本轮使用 25.6），解压到 vendor/ae-sdk，使 Examples/Headers/AEConfig.h 存在。SDK 和参考软件安装器不随源码分发。

## 编译

在本目录打开 PowerShell：

```powershell
pwsh -NoProfile -File scripts/build-effect-console-release.ps1
```

输出 artifacts/native-plugin/AEGP/XFXNativeConsole.aex、installer/XFXNativeConsoleInstaller.exe 和 installer/EffectConsoleUninstaller.exe。构建过程只编译、打包和校验，不安装到真实 AE。

## 测试

```powershell
pwsh -NoProfile -File scripts/test-native-installer-cpp.ps1
pwsh -NoProfile -File scripts/test-native-installer.ps1
pwsh -NoProfile -File scripts/test-effect-console-lifecycle.ps1
node scripts/scriptui-toolbar-tests.cjs
```

Node 仅用于工具栏测试，最终用户不需要。生命周期测试使用临时目录和复制的 PING 伪 AE，不结束真实 AE；测试入口 EXE 仅将 UAC 清单改为 asInvoker，并写入 artifacts/installer-entry-tests。发行 EXE 始终要求管理员权限。

## 目录

- native/plugin 与 native/src：AE 入口、Win32 界面、搜索、设置、图片和桥接实现。
- native/tests、installer/tests：测试源码。
- installer：安装/卸载共用 C++ 源码、资源、清单、词库和 JSX 工具栏。内部 JSX 源文件保留旧名，安装目标为 效果控制台.jsx。
- scripts：构建、测试、源码打包与安全安装脚本。
- vendor/zlib-inflate、native/src/data：随附第三方来源与许可。vendor/ae-sdk 仅包含获取说明。

本仓库未为自有代码声明开源许可证；上传源码不表示额外授予许可。第三方内容遵循其随附许可。测试版宿主与 UI 验收限制见发布目录的 VERIFICATION.md。
'''
(dest / 'README.md').write_text(readme, encoding='utf-8')
(dest / 'VERSION').write_text('1.0.18\n', encoding='ascii')
(dest / '.gitignore').write_text('artifacts/\nbin/\nobj/\n*.obj\n*.pdb\nnative/build/\nnative/plugin/*.rc\ninstaller/obj-installer/\ninstaller/*.exe\ninstaller/*.aex\nvendor/ae-sdk/*\n!vendor/ae-sdk/README.txt\n', encoding='utf-8')
manifest = []
for p in sorted(dest.rglob('*')):
    if p.is_file():
        manifest.append(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(dest).as_posix())
(dest / 'SOURCE-SHA256SUMS.txt').write_text('\n'.join(manifest) + '\n', encoding='utf-8')
archive = dest.parent / 'EffectConsole-1.0.18-source.zip'
if archive.exists():
    raise SystemExit('Source archive already exists; preserve it.')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for p in sorted(dest.rglob('*')):
        if p.is_file():
            z.write(p, 'source/' + p.relative_to(dest).as_posix())
print(json.dumps({'files': len(manifest), 'source': str(dest), 'archive': str(archive), 'bytes': archive.stat().st_size}, ensure_ascii=False))
