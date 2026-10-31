"""Export a source-only allowlist; never distribute Adobe SDK or build outputs."""
import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--destination', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
version = (root / 'VERSION').read_text(encoding='ascii').strip()
if not re.fullmatch(r'\d+\.\d+\.\d+', version):
    raise SystemExit('VERSION must contain major.minor.patch')
dest = Path(args.destination).resolve()
archive = dest.parent / f'EffectConsole-{version}-source.zip'
if dest.exists() or archive.exists():
    raise SystemExit('Preserve existing source exports; choose a new destination directory.')
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
    'test-ae-process-detection.ps1', 'scriptui-toolbar-tests.cjs', 'package-effect-console-source.py',
    'audit-effect-translations.cjs', 'audit-effect-translations-tests.cjs',
    'export-effect-inventory.jsx', 'effect-inventory-tests.cjs',
    'generate-effect-translations.cjs', 'generate-effect-translations-tests.cjs', 'display-label-tests.cjs',
    'audit-effect-catalog.cjs', 'effect-catalog-tests.cjs',
    'export-effect-availability.jsx', 'effect-availability-tests.cjs', 'test-layer-styles-in-ae.jsx']
files += [Path('scripts') / name for name in script_names]
files += [p.relative_to(root) for p in (root / 'scripts').glob('run-*-tests.ps1') if p.name != 'run-core-tests.ps1']
files += [Path('Directory.Build.props'), Path('vendor/ae-sdk/README.txt'), Path('README.md'), Path('LOCALIZATION.md'), Path('CATALOG.md'), Path('LAYER-STYLES.md'), Path('INPUT-FIX.md'), Path('MENU-LABELS.md'), Path('PAINT-FIX.md'), Path('VERSION'), Path('.gitignore'), Path('.gitattributes')]
# Validate the entire allowlist before creating an incomplete export.
for rel in sorted(set(files)):
    if not (root / rel).is_file():
        raise SystemExit(f'Missing source input: {rel}')
dest.mkdir(parents=True)
for rel in sorted(set(files)):
    output = dest / rel
    output.parent.mkdir(parents=True, exist_ok=True)
    # All allowed files are text. Match Git's source/.gitattributes LF policy,
    # rather than hashing platform-dependent checkout CRLF conversions.
    output.write_bytes((root / rel).read_bytes().replace(b'\r\n', b'\n'))
manifest = []
for p in sorted(dest.rglob('*')):
    if p.is_file():
        manifest.append(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(dest).as_posix())
(dest / 'SOURCE-SHA256SUMS.txt').write_text('\n'.join(manifest) + '\n', encoding='utf-8')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for p in sorted(dest.rglob('*')):
        if p.is_file():
            z.write(p, 'source/' + p.relative_to(dest).as_posix())
print(json.dumps({'version': version, 'files': len(manifest), 'source': str(dest), 'archive': str(archive), 'bytes': archive.stat().st_size}, ensure_ascii=False))
