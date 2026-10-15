"""Build the Windows x64 FrameFit desktop app and its stable launcher."""
from __future__ import annotations

import argparse
import hashlib
import json
import importlib.metadata
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import re
import struct
import zipfile

PLUGIN = Path(__file__).resolve().parents[1]
SOURCE = PLUGIN / "desktop" / "source"


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def zip_tree(root, output):
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for item in sorted(root.rglob("*")):
            if item.is_file():
                archive.write(item, item.relative_to(root).as_posix())


def build(version, media_dir, output):
    if sys.platform != "win32" or struct.calcsize('P') != 8:
        raise RuntimeError("FrameFit desktop packages must be built on Windows x64.")
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Use a stable major.minor.patch version.')
    if not media_dir.is_dir() or any(not list(media_dir.rglob(name)) for name in ("ffmpeg.exe", "ffprobe.exe")):
        raise ValueError("Provide a media directory containing ffmpeg.exe and ffprobe.exe.")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="framefit-desktop-") as temporary:
        temp = Path(temporary)
        dist, work = temp / "dist", temp / "work"
        version_file = temp / "version.json"
        version_file.write_text(json.dumps({"version": version, "apiVersion": 1}), encoding="utf-8")
        def pyinstaller(name, entry, windowed, paths=(), add_data=(), onefile=False, metadata=()):
            command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean",
                       "--name", name, "--distpath", str(dist), "--workpath", str(work / name),
                       "--specpath", str(temp / "spec")]
            command.append("--onefile" if onefile else "--onedir")
            command.append("--windowed" if windowed else "--console")
            for path in paths:
                command += ["--paths", str(path)]
            for source, target in add_data:
                command += ["--add-data", f"{source};{target}"]
            for item in metadata:
                command += ["--copy-metadata", item]
            command.append(str(entry))
            subprocess.run(command, check=True)
        pyinstaller("FrameFitApp", SOURCE / "app.py", True,
                    paths=(SOURCE, PLUGIN / "server"),
                    add_data=((version_file, "."),))
        pyinstaller("FrameFitWorker", PLUGIN / "server" / "engine.py", False,
                    paths=(PLUGIN / "server",))
        pyinstaller("FrameFitHost", PLUGIN / "server" / "host.py", False,
                    paths=(PLUGIN / "server",), metadata=("mcp", "mcp-types"))
        pyinstaller("FrameFit", SOURCE / "launcher.py", True, onefile=True)
        package = temp / "package"
        version_root = package / "versions" / version
        version_root.mkdir(parents=True)
        shutil.copytree(dist / "FrameFitApp", version_root / "FrameFitApp")
        shutil.copytree(dist / "FrameFitWorker", version_root / "worker")
        shutil.copytree(dist / "FrameFitHost", version_root / "host")
        shutil.copytree(PLUGIN / "server", version_root / "server",
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "host.py", "local_client.py"))
        shutil.copytree(PLUGIN / "assets", version_root / "assets")
        shutil.copytree(PLUGIN / "web", version_root / "web")
        shutil.copytree(PLUGIN / "licenses", version_root / "licenses")
        shutil.copy2(PLUGIN / 'LICENSE', version_root / 'licenses' / 'FrameFit-LICENSE')
        notices = []
        for distribution in importlib.metadata.distributions():
            name = distribution.metadata['Name']
            notices.append(f'{name}=={distribution.version}')
            for entry in distribution.files or []:
                if any(token in entry.name.lower() for token in ('license', 'copying')):
                    source = Path(distribution.locate_file(entry))
                    if source.is_file() and source.stat().st_size < 4 * 1024 * 1024:
                        destination = version_root / 'licenses' / 'python-packages' / name / str(entry)
                        if '..' not in entry.parts:
                            destination.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copy2(source, destination)
        import tkinter
        tcl_root = Path(tkinter.Tcl().eval('info library'))
        for library in (tcl_root, tcl_root.with_name('tk' + tkinter.TkVersion.__str__())):
            for filename in ('license.terms', 'license.txt'):
                if (library / filename).is_file():
                    shutil.copy2(library / filename, version_root / 'licenses' / (library.name + '-' + filename))
        (version_root / 'licenses' / 'packaged-python-dependencies.txt').write_text('\n'.join(sorted(notices)), encoding='utf-8')
        shutil.copy2(PLUGIN / "shared-presets.json", version_root / "shared-presets.json")
        (version_root / "bin").mkdir()
        for name in ("ffmpeg.exe", "ffprobe.exe"):
            matches = list(media_dir.rglob(name))
            if len(matches) != 1:
                raise ValueError(f"Expected exactly one {name} under media directory.")
            shutil.copy2(matches[0], version_root / "bin" / name)
        (version_root / "runtime.json").write_text(json.dumps({"version": version, "apiVersion": 1}, indent=2), encoding="utf-8")
        files = {path.relative_to(version_root).as_posix(): digest(path)
                 for path in sorted(version_root.rglob("*")) if path.is_file()}
        release_manifest = {"version": version, "apiVersion": 1, "files": files}
        (version_root / "desktop-release.json").write_text(json.dumps(release_manifest, ensure_ascii=False, indent=2), encoding="utf-8")
        update_zip = output / f"framefit-desktop-{version}-windows-x64.zip"
        zip_tree(version_root, update_zip)
        launcher_root = package
        launcher_exe = dist / "FrameFit.exe"
        shutil.copy2(launcher_exe, launcher_root / "FrameFit.exe")
        (launcher_root / 'INSTALL.txt').write_text(
            'FrameFit Windows x64\n\n将本压缩包完整解压到 %LOCALAPPDATA%\\FrameFit，运行 FrameFit.exe。\n'
            '保持软件打开后使用 Codex 视频套版插件。个人设置仅保存在本机。\n'
            '启动只检查更新；点击更新按钮后才下载、校验并重启。\n'
            '图文教程：https://github.com/X1039538408/yuanchuan-plugins/blob/main/docs/framefit.md\n', encoding='utf-8-sig')
        (launcher_root / "current.json").write_text(json.dumps({"versionDirectory": str(Path("versions") / version)}), encoding="utf-8")
        full_zip = output / f"framefit-install-{version}-windows-x64.zip"
        zip_tree(package, full_zip)
        stable = {"version": version, "apiVersion": 1,
                  "url": f"https://github.com/X1039538408/yuanchuan-plugins/releases/download/framefit-desktop-v{version}/{update_zip.name}",
                  "sha256": digest(update_zip)}
        (output / "stable.json").write_text(json.dumps(stable, indent=2) + "\n", encoding="utf-8")
        return full_zip, update_zip


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--media-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(*build(args.version, args.media_dir.resolve(), args.output.resolve()), sep="\n")
