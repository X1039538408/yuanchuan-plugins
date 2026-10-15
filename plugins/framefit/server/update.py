"""Verified public release updates. No account or third party Python package needed."""
from __future__ import annotations

import contextlib
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import tempfile
import time
from urllib.parse import urlparse
from urllib.request import HTTPRedirectHandler, build_opener
import zipfile

CHANNEL = "https://raw.githubusercontent.com/X1039538408/yuanchuan-plugins/main/plugins/framefit/channels/stable.json"
API_VERSION = 1
MAX_ARCHIVE = 2 * 1024**3
MAX_EXPANDED = 4 * 1024**3
VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$")
SHA = re.compile(r"^[a-f0-9]{64}$")


def _safe_path(name: str) -> Path:
    normalized = name.replace("\\", "/")
    p = PurePosixPath(normalized)
    if not name or p.is_absolute() or any(x in ("", ".", "..") or ":" in x for x in normalized.split("/")):
        raise ValueError("Unsafe archive path")
    # Windows aliases, ADS, device files and trailing dots/spaces are rejected on all OSes.
    for x in p.parts:
        if x.endswith((".", " ")) or re.match(r"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)", x, re.I):
            raise ValueError("Unsafe Windows path")
    return Path(*p.parts)


def _allowed_release(url: str, test: bool) -> bool:
    p = urlparse(url)
    if test and p.scheme == "file":
        return True
    return bool(p.scheme == "https" and p.netloc == "github.com" and not p.query and not p.fragment
                and re.fullmatch(r"/X1039538408/yuanchuan-plugins/releases/download/framefit-v[0-9A-Za-z.-]+/framefit-[0-9A-Za-z.-]+-windows-x64\.zip", p.path))


class _Redirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        p = urlparse(newurl)
        if p.scheme != "https" or p.hostname not in {"github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com", "raw.githubusercontent.com"}:
            raise ValueError("Untrusted download redirect")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def _download(url: str, target: Path, maximum: int) -> None:
    with build_opener(_Redirect()).open(url, timeout=30) as response, target.open("wb") as out:
        size = 0
        while chunk := response.read(1024 * 1024):
            size += len(chunk)
            if size > maximum:
                raise ValueError("Download exceeds size limit")
            out.write(chunk)


def _hash(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def verify_runtime(root: Path, version: str) -> dict:
    manifest = json.loads((root / "runtime.json").read_text(encoding="utf-8-sig"))
    if manifest.get("version") != version or manifest.get("apiVersion") != API_VERSION:
        raise ValueError("Runtime version/API mismatch")
    files = manifest.get("files")
    required = {"host/FrameFitHost.exe", "worker/FrameFitWorker.exe", "bin/ffmpeg.exe", "bin/ffprobe.exe", "panel.html", "shared-presets.json"}
    if not isinstance(files, dict) or not required <= files.keys():
        raise ValueError("Runtime file manifest is incomplete")
    actual = set()
    for path in root.rglob("*"):
        if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            raise ValueError("Runtime contains symlink")
        if path.is_file() and path.name != "runtime.json":
            actual.add(path.relative_to(root).as_posix())
    if actual != set(files):
        raise ValueError("Runtime file inventory mismatch")
    for name, digest in files.items():
        target = root / _safe_path(name)
        if not SHA.fullmatch(str(digest)) or _hash(target) != digest:
            raise ValueError(f"Runtime file verification failed: {name}")
    return manifest


@contextlib.contextmanager
def _lock(state: Path):
    lock = state / "update.lock"
    deadline = time.monotonic() + 120
    while True:
        try:
            lock.mkdir()
            break
        except FileExistsError:
            # Both bootstrap and host use this directory lock; long-dead owners recover.
            if time.time() - lock.stat().st_mtime > 1800:
                shutil.rmtree(lock, ignore_errors=True)
                continue
            if time.monotonic() >= deadline:
                raise TimeoutError("Another FrameFit update is still running")
            time.sleep(.2)
    try:
        yield
    finally:
        shutil.rmtree(lock, ignore_errors=True)


def _cached(state: Path) -> dict | None:
    try:
        active = json.loads((state / "active.json").read_text(encoding="utf-8-sig"))
        version = active["version"]
        if not VERSION.fullmatch(version):
            return None
        root = state / "releases" / version
        verify_runtime(root, version)
        return {"root": str(root), "version": version, "warning": None}
    except (OSError, ValueError, KeyError, TypeError):
        return None


def _extract(archive: Path, dest: Path) -> None:
    with zipfile.ZipFile(archive) as z:
        seen = set()
        total = 0
        for entry in z.infolist():
            relative = _safe_path(entry.filename.rstrip("/"))
            key = relative.as_posix().casefold()
            if key in seen or stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError("Duplicate path or symlink in release")
            seen.add(key)
            total += entry.file_size
            if total > MAX_EXPANDED:
                raise ValueError("Expanded release exceeds size limit")
            target = dest / relative
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with z.open(entry) as source, target.open("wb") as out:
                    shutil.copyfileobj(source, out)


def ensure_runtime(state_dir: Path, channel_url: str | None = None, allow_file_for_tests: bool = False) -> dict:
    state = Path(state_dir)
    state.mkdir(parents=True, exist_ok=True)
    channel = channel_url or CHANNEL
    if channel != CHANNEL and not (allow_file_for_tests and urlparse(channel).scheme == "file"):
        raise ValueError("Only the official stable channel is accepted")
    with _lock(state):
        cached = _cached(state)
        try:
            with tempfile.TemporaryDirectory(prefix="download-", dir=state) as tmp:
                tmp = Path(tmp)
                _download(channel, tmp / "channel.json", 65536)
                release = json.loads((tmp / "channel.json").read_text(encoding="utf-8-sig"))
                version = release.get("version", "")
                if not isinstance(version, str) or not VERSION.fullmatch(version) or release.get("apiVersion") != API_VERSION:
                    raise ValueError("Stable release is not compatible with this plugin")
                digest = release.get("sha256", "")
                if not SHA.fullmatch(str(digest)) or not _allowed_release(release.get("url", ""), allow_file_for_tests):
                    raise ValueError("Invalid release URL or checksum")
                if not allow_file_for_tests and f"/framefit-v{version}/framefit-{version}-windows-x64.zip" not in release["url"]:
                    raise ValueError("Release URL version does not match manifest")
                if cached and cached["version"] == version:
                    active = json.loads((state / "active.json").read_text(encoding="utf-8-sig"))
                    if active.get("sha256") != digest:
                        raise ValueError("Published versions are immutable; checksum changed")
                    return cached
                _download(release["url"], tmp / "release.zip", MAX_ARCHIVE)
                if _hash(tmp / "release.zip") != digest:
                    raise ValueError("Release SHA-256 does not match")
                stage = tmp / "runtime"
                stage.mkdir()
                _extract(tmp / "release.zip", stage)
                verify_runtime(stage, version)
                dest = state / "releases" / version
                dest.parent.mkdir(exist_ok=True)
                if dest.exists():
                    existing = verify_runtime(dest, version)
                    downloaded = verify_runtime(stage, version)
                    if existing != downloaded:
                        raise ValueError("Published version differs from an existing pinned runtime")
                else:
                    os.replace(stage, dest)
                active = {"version": version, "sha256": digest, "apiVersion": API_VERSION}
                pending = state / "active.pending.json"
                pending.write_text(json.dumps(active), encoding="utf-8")
                os.replace(pending, state / "active.json")
                return {"root": str(dest), "version": version, "warning": None}
        except Exception as error:
            if cached:
                cached["warning"] = f"Update unavailable; using verified {cached['version']}: {error}"
                return cached
            raise RuntimeError(f"FrameFit first download failed. Check network then retry: {error}") from error
