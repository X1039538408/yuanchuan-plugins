"""Build a self-contained Windows release using the current build venv.

python tools/build_release.py --version 1.0.0 --ffmpeg-dir C:/media --output dist
The media directory must contain ffmpeg.exe, ffprobe.exe and its licensing files.
"""
from __future__ import annotations
import argparse
import hashlib
from importlib import metadata
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

PLUGIN = Path(__file__).resolve().parents[1]

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def build(version: str, media: Path, output: Path) -> Path:
    if sys.platform != "win32":
        raise RuntimeError("Build on Windows x64; cross compiling PyInstaller is unsupported")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", version):
        raise ValueError("Invalid release version")
    if not media.is_dir():
        raise ValueError("FFmpeg directory does not exist")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="framefit-build-") as temporary:
        temp = Path(temporary)
        runtime = temp / "runtime"
        runtime.mkdir()
        for name, source, destination in (("FrameFitHost", "host.py", "host"), ("FrameFitWorker", "engine.py", "worker")):
            command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir", "--console",
                       "--name", name, "--distpath", str(temp / "dist"), "--workpath", str(temp / "work" / name),
                       "--specpath", str(temp / "spec"), "--paths", str(PLUGIN / "server")]
            if name == "FrameFitHost":
                # Do not collect mcp.cli: importing its optional CLI dependencies
                # exits the analysis process when the runtime-only SDK is used.
                command += ["--copy-metadata", "mcp", "--copy-metadata", "mcp-types"]
            command.append(str(PLUGIN / "server" / source))
            subprocess.run(command, check=True)
            shutil.copytree(temp / "dist" / name, runtime / destination)
        bin_dir = runtime / "bin"
        bin_dir.mkdir()
        for binary in ("ffmpeg.exe", "ffprobe.exe"):
            candidates = list(media.rglob(binary))
            if len(candidates) != 1:
                raise ValueError(f"Expected exactly one {binary} beneath --ffmpeg-dir")
            shutil.copy2(candidates[0], bin_dir / binary)
            subprocess.run([str(bin_dir / binary), "-version"], check=True, stdout=subprocess.DEVNULL)
        shutil.copytree(PLUGIN / "assets", runtime / "assets")
        shutil.copy2(PLUGIN / "web" / "panel.html", runtime / "panel.html")
        shutil.copy2(PLUGIN / "shared-presets.json", runtime / "shared-presets.json")
        licenses = runtime / "licenses"
        shutil.copytree(PLUGIN / "licenses", licenses)
        shutil.copy2(PLUGIN / "LICENSE", licenses / "FrameFit-LICENSE")
        if (PLUGIN / "SOURCES.md").is_file():
            shutil.copy2(PLUGIN / "SOURCES.md", licenses / "FrameFit-SOURCES.md")
        versions = []
        for binary in ("ffmpeg.exe", "ffprobe.exe"):
            info = subprocess.run([str(bin_dir / binary), "-version"], check=True, capture_output=True, text=True, encoding="utf-8", errors="replace")
            versions.append(info.stdout)
        (licenses / "media-versions.txt").write_text("\n".join(versions), encoding="utf-8")
        # Preserve distribution license/readme metadata supplied with the selected binaries.
        for path in media.rglob("*"):
            if path.is_file() and ("license" in path.name.lower() or path.suffix.lower() in {".txt", ".md"}):
                target = licenses / "ffmpeg-distribution" / path.relative_to(media)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
        with (licenses / "build-environment.txt").open("w", encoding="utf-8") as environment:
            subprocess.run([sys.executable, "-m", "pip", "freeze"], check=True, stdout=environment)
        # Frozen dependencies remain redistributed software; retain their actual
        # installed license texts and package metadata rather than only a pip list.
        for distribution in metadata.distributions():
            package = re.sub(r"[^A-Za-z0-9_.-]", "_", distribution.metadata["Name"])
            for relative in distribution.files or []:
                name = str(relative).replace("\\", "/")
                if ".dist-info/" not in name:
                    continue
                leaf = relative.name.lower()
                if "license" not in name.lower() and leaf not in {"metadata", "copying", "notice", "authors"}:
                    continue
                source = Path(distribution.locate_file(relative))
                if source.is_file():
                    target = licenses / "python-packages" / package / Path(*name.split(".dist-info/", 1)[1].split("/"))
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, target)
        python_license = Path(sys.base_prefix) / "LICENSE.txt"
        if python_license.is_file():
            shutil.copy2(python_license, licenses / "Python-runtime-LICENSE.txt")
        manifest = {"version": version, "apiVersion": 1, "files": {p.relative_to(runtime).as_posix(): sha256(p) for p in sorted(runtime.rglob("*")) if p.is_file()}}
        (runtime / "runtime.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        archive = output / f"framefit-{version}-windows-x64.zip"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for path in sorted(runtime.rglob("*")):
                if path.is_file():
                    z.write(path, path.relative_to(runtime).as_posix())
        digest = sha256(archive)
        (output / (archive.name + ".sha256")).write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
        release = {"version": version, "apiVersion": 1, "url": f"https://github.com/X1039538408/yuanchuan-plugins/releases/download/framefit-v{version}/{archive.name}", "sha256": digest}
        (output / "stable.json").write_text(json.dumps(release, indent=2) + "\n", encoding="utf-8")
        shutil.copy2(runtime / "runtime.json", output / "runtime.json")
        return archive

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--ffmpeg-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(build(args.version, args.ffmpeg_dir.resolve(), args.output.resolve()))
