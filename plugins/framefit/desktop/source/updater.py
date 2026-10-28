"""Verified, opt-in update staging for the Windows FrameFit launcher."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import re
import urllib.request
import zipfile
import tempfile
import time

CHANNEL_URL = "https://raw.githubusercontent.com/X1039538408/yuanchuan-plugins/main/plugins/framefit/desktop/channels/stable.json"
MAX_ARCHIVE_BYTES = 2 * 1024 * 1024 * 1024


def _read_url(url, timeout=15):
    request = urllib.request.Request(url, headers={"User-Agent": "FrameFit-Updater/1"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        chunks, total = [], 0
        while chunk := response.read(1024 * 1024):
            total += len(chunk)
            if total > MAX_ARCHIVE_BYTES:
                raise ValueError('更新下载超过允许大小。')
            chunks.append(chunk)
        return b''.join(chunks)


def check_update(current_version, channel_url=CHANNEL_URL):
    manifest = json.loads(_read_url(channel_url).decode("utf-8"))
    if manifest.get('status') == 'unpublished':
        return None
    version = manifest.get("version")
    if not isinstance(version, str) or not manifest.get("url") or not manifest.get("sha256"):
        raise ValueError("FrameFit 稳定版本清单格式无效。")
    if manifest.get('apiVersion') != 1:
        raise ValueError('更新接口版本不兼容，请从 Release 下载新安装包。')
    def version_key(value):
        if not isinstance(value, str) or not re.fullmatch(r'\d+\.\d+\.\d+', value):
            raise ValueError('稳定版本号无效。')
        return tuple(map(int, value.split('.')))
    return manifest if version_key(version) > version_key(current_version) else None


def _safe_member(name):
    path = PurePosixPath(name)
    reserved = {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}
    return (bool(name) and not path.is_absolute() and not any(c in name for c in ':\\<>"|?*') and
            not any(ord(c) < 32 for c in name) and
            all(part not in ("", ".", "..") and not part.endswith((".", " ")) and
                part.split(".", 1)[0].upper() not in reserved for part in path.parts))


def verify_inventory(payload, version):
    release = json.loads((payload / 'desktop-release.json').read_text(encoding='utf-8'))
    if release.get('version') != version or release.get('apiVersion') != 1 or not isinstance(release.get('files'), dict):
        raise ValueError('更新包版本、接口版本或文件清单无效。')
    actual = {p.relative_to(payload).as_posix() for p in payload.rglob('*') if p.is_file()}
    if actual != set(release['files']) | {'desktop-release.json'}:
        raise ValueError('更新包文件清单与实际文件不一致。')
    required = {'FrameFitApp/FrameFitStudio.exe', 'worker/FrameFitWorker.exe', 'host/FrameFitHost.exe',
                'bin/ffmpeg.exe', 'bin/ffprobe.exe', 'runtime.json',
                'assets/default.png', 'assets/default_portrait.png', 'shared-presets.json'}
    if not required.issubset(actual):
        raise ValueError('更新包缺少可运行的程序、媒体工具或默认资源。')
    runtime = json.loads((payload / 'runtime.json').read_text(encoding='utf-8'))
    if runtime.get('version') != version or runtime.get('apiVersion') != 1:
        raise ValueError('更新包运行版本不兼容。')
    for name, expected in release['files'].items():
        if not _safe_member(name) or not isinstance(expected, str) or not re.fullmatch('[0-9a-fA-F]{64}', expected):
            raise ValueError('更新包文件清单无效。')
        file = payload / name
        if file.is_symlink() or payload.resolve() not in file.resolve().parents:
            raise ValueError('更新包路径越界。')
        if hashlib.sha256(file.read_bytes()).hexdigest() != expected.lower():
            raise ValueError(f'更新文件校验失败：{name}')
    return release


def stage_update(manifest, install_root):
    """Download and verify a release into a versioned directory without touching current."""
    root = Path(install_root).resolve()
    if manifest.get('apiVersion', 1) != 1:
        raise ValueError('更新接口版本不兼容。')
    version = manifest.get("version")
    if not isinstance(version, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", version):
        raise ValueError("版本号无效。")
    digest = manifest.get("sha256", "")
    if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest.lower()):
        raise ValueError("SHA-256 格式无效。")
    root.mkdir(parents=True, exist_ok=True)
    versions = root / "versions"
    versions.mkdir(exist_ok=True)
    target = versions / version
    if target.is_symlink() or versions.resolve() not in target.resolve().parents:
        raise ValueError('更新版本路径越界。')
    if target.exists():
        verify_inventory(target, version)
        return target
    stage = Path(tempfile.mkdtemp(prefix=version + '.stage-', dir=versions))
    try:
        if not manifest["url"].startswith("https://github.com/X1039538408/yuanchuan-plugins/releases/download/"):
            raise ValueError("更新下载地址不是已配置的公开 GitHub Release。")
        data = _read_url(manifest["url"], timeout=60)
        if len(data) > MAX_ARCHIVE_BYTES:
            raise ValueError("更新包超过允许大小。")
        actual = hashlib.sha256(data).hexdigest()
        if actual.lower() != digest.lower():
            raise ValueError("更新包 SHA-256 校验失败。")
        temp_zip = stage / "package.zip"
        temp_zip.write_bytes(data)
        with zipfile.ZipFile(temp_zip) as archive:
            entries = archive.infolist()
            names = [entry.filename for entry in entries]
            if len(names) != len({name.rstrip('/').casefold() for name in names}) or any(not _safe_member(name) for name in names):
                raise ValueError("更新包包含重复或不安全的路径。")
            if sum(entry.file_size for entry in entries) > MAX_ARCHIVE_BYTES * 2:
                raise ValueError("更新包解压后超过允许大小。")
            if any((entry.external_attr >> 16) & 0o170000 == 0o120000 for entry in entries):
                raise ValueError("更新包不允许包含符号链接。")
            unpack = stage / "payload"
            unpack.mkdir()
            for entry in entries:
                destination = (unpack / Path(*PurePosixPath(entry.filename).parts)).resolve()
                if unpack.resolve() not in destination.parents and destination != unpack.resolve():
                    raise ValueError("更新包路径越界。")
            archive.extractall(unpack)
        runtime_manifest_path = stage / "payload" / "desktop-release.json"
        if not runtime_manifest_path.is_file():
            raise ValueError("更新包缺少 desktop-release.json 文件清单。")
        payload = stage / "payload"
        verify_inventory(payload, version)
        os.replace(payload, target)
        shutil.rmtree(stage, ignore_errors=True)
        return target
    except Exception:
        shutil.rmtree(stage, ignore_errors=True)
        raise


def _wait_ready(selected, process, bridge, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError('新版启动失败，已恢复旧版入口。')
        try:
            data = json.loads(bridge.read_text(encoding='utf-8'))
            if data.get('ready') and data.get('pid') == process.pid and Path(data.get('runtimeRoot', '')).resolve() == selected:
                return
        except (OSError, ValueError, TypeError):
            pass
        time.sleep(.1)
    raise RuntimeError('新版未在 30 秒内完成启动，已恢复旧版入口，请重试或检查本机启动日志。')


def activate_update(version_dir, install_root, launcher=None):
    root = Path(install_root).resolve()
    current = root / "current.json"
    temporary = current.with_suffix(".tmp")
    selected = Path(version_dir).resolve()
    if (root / 'versions').resolve() not in selected.parents or not selected.is_dir():
        raise ValueError('更新版本必须位于软件的版本目录中。')
    try:
        selected_value = str(selected.relative_to(root))
    except ValueError as exc:
        raise ValueError("更新版本必须位于软件的版本目录中。") from exc
    verify_inventory(selected, selected.name)
    previous = current.read_bytes() if current.exists() else None
    bridge = Path(os.getenv('LOCALAPPDATA', str(Path.home()))) / 'FrameFitCodex' / 'bridge.json'
    previous_bridge = bridge.read_bytes() if bridge.exists() else None
    process = None
    if launcher and not Path(launcher).is_file():
        raise ValueError('更新启动器缺失，请重新解压完整安装包。')
    try:
        temporary.write_text(json.dumps({"versionDirectory": selected_value}, ensure_ascii=False), encoding="utf-8")
        os.replace(temporary, current)
        if launcher:
            # Own the new GUI process so failed startup can be terminated without touching the old app.
            process = subprocess.Popen([str(selected / 'FrameFitApp' / 'FrameFitStudio.exe')], cwd=str(selected), close_fds=True,
                env=dict(os.environ, PYINSTALLER_RESET_ENVIRONMENT='1', FRAMEFIT_INSTALL_ROOT=str(root), FRAMEFIT_BIN_DIR=str(selected / 'bin')))
            _wait_ready(selected, process, bridge)
    except Exception:
        if process and process.poll() is None:
            try:
                process.terminate()
                process.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                pass
        if previous is None:
            current.unlink(missing_ok=True)
        else:
            temporary.write_bytes(previous)
            os.replace(temporary, current)
        if previous_bridge is not None:
            bridge.parent.mkdir(parents=True, exist_ok=True)
            bridge_tmp = bridge.with_suffix('.rollback')
            bridge_tmp.write_bytes(previous_bridge)
            os.replace(bridge_tmp, bridge)
        elif bridge.exists():
            bridge.unlink()
        raise
