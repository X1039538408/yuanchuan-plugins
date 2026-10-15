"""Offline, deterministic update tests; no GitHub credentials or network required."""
from __future__ import annotations
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))
import update


def release(base: Path, version="1.0.0", api=1, extra=None):
    files = {name: f"{version}:{name}".encode() for name in (
        "host/FrameFitHost.exe", "worker/FrameFitWorker.exe", "bin/ffmpeg.exe",
        "bin/ffprobe.exe", "panel.html", "shared-presets.json", "assets/default.png")}
    manifest = {"version": version, "apiVersion": api, "files": {n: hashlib.sha256(b).hexdigest() for n, b in files.items()}}
    archive = base / f"{version}.zip"
    with zipfile.ZipFile(archive, "w") as z:
        for name, content in files.items():
            z.writestr(name, content)
        z.writestr("runtime.json", json.dumps(manifest))
        for name, content in (extra or {}).items():
            z.writestr(name, content)
    channel = base / "channel.json"
    channel.write_text(json.dumps({"version": version, "apiVersion": api, "url": archive.as_uri(), "sha256": update._hash(archive)}))
    return channel


def install(state, channel):
    return update.ensure_runtime(state, channel.as_uri(), allow_file_for_tests=True)


def test_two_independent_installs_update_and_pin_inflight_runtime(tmp_path):
    channel = release(tmp_path)
    a, b = tmp_path / "computer-a", tmp_path / "computer-b"
    old = install(a, channel)
    assert install(b, channel)["version"] == "1.0.0"
    channel = release(tmp_path, "1.1.0")
    assert install(a, channel)["version"] == "1.1.0"
    assert install(b, channel)["version"] == "1.1.0"
    assert old["version"] == "1.0.0"
    assert Path(old["root"], "worker/FrameFitWorker.exe").read_text().startswith("1.0.0:")
    assert install(a, channel)["warning"] is None


def test_offline_uses_verified_cache(tmp_path):
    channel = release(tmp_path)
    state = tmp_path / "state"
    install(state, channel)
    channel.unlink()
    result = install(state, channel)
    assert result["version"] == "1.0.0" and "using verified" in result["warning"]


def test_first_offline_download_reports_retry(tmp_path):
    with pytest.raises(RuntimeError, match="first download failed"):
        install(tmp_path / "state", tmp_path / "missing.json")


def test_bad_checksum_falls_back_without_switching(tmp_path):
    channel = release(tmp_path)
    state = tmp_path / "state"
    install(state, channel)
    release(tmp_path, "1.1.0")
    data = json.loads(channel.read_text())
    data["sha256"] = "0" * 64
    channel.write_text(json.dumps(data))
    result = install(state, channel)
    assert result["version"] == "1.0.0" and "SHA-256" in result["warning"]
    assert json.loads((state / "active.json").read_text())["version"] == "1.0.0"


def test_incompatible_api_falls_back(tmp_path):
    channel = release(tmp_path)
    state = tmp_path / "state"
    install(state, channel)
    release(tmp_path, "2.0.0", api=2)
    assert install(state, channel)["version"] == "1.0.0"


@pytest.mark.parametrize("evil", ["../escape.exe", "/absolute.exe", "C:/escape.exe", "CON.txt", "worker/file:stream", "worker/../escape.exe", "worker/FrameFitWorker.exe.", "HOST/framefithost.exe", "worker/./alias", "worker//alias"])
def test_unsafe_archives_never_activate(tmp_path, evil):
    channel = release(tmp_path, extra={evil: b"bad"})
    with pytest.raises(RuntimeError):
        install(tmp_path / "state", channel)
    assert not (tmp_path / "state/active.json").exists()
    assert not (tmp_path / "escape.exe").exists()


def test_symlink_archive_rejected(tmp_path):
    channel = release(tmp_path)
    archive = tmp_path / "1.0.0.zip"
    with zipfile.ZipFile(archive, "a") as z:
        link = zipfile.ZipInfo("link")
        link.create_system = 3
        link.external_attr = 0o120777 << 16
        z.writestr(link, "../outside")
    data = json.loads(channel.read_text())
    data["sha256"] = update._hash(archive)
    channel.write_text(json.dumps(data))
    with pytest.raises(RuntimeError, match="symlink"):
        install(tmp_path / "state", channel)


def test_tampered_cache_never_runs_offline(tmp_path):
    channel = release(tmp_path)
    result = install(tmp_path / "state", channel)
    Path(result["root"], "worker/FrameFitWorker.exe").write_text("tampered")
    channel.unlink()
    with pytest.raises(RuntimeError, match="first download failed"):
        install(tmp_path / "state", channel)


def test_immutable_version_replacement_does_not_switch(tmp_path):
    channel = release(tmp_path)
    state = tmp_path / "state"
    install(state, channel)
    data = json.loads(channel.read_text())
    data["sha256"] = "0" * 64
    channel.write_text(json.dumps(data))
    assert "immutable" in install(state, channel)["warning"]


def test_concurrent_installers_share_atomic_activation(tmp_path):
    channel = release(tmp_path)
    with ThreadPoolExecutor(max_workers=2) as executor:
        results = list(executor.map(lambda _: install(tmp_path / "state", channel), [0, 1]))
    assert results[0] == results[1]
    assert not (tmp_path / "state/update.lock").exists()
    assert not list((tmp_path / "state").glob("download-*"))


@pytest.mark.parametrize("url", ["http://github.com/X1039538408/yuanchuan-plugins/releases/download/framefit-v1.0.0/framefit-1.0.0-windows-x64.zip", "https://evil.example/release.zip", "https://github.com/other/repo/releases/download/framefit-v1.0.0/framefit-1.0.0-windows-x64.zip", "file:///C:/release.zip"])
def test_consumer_rejects_untrusted_release_urls(url):
    assert not update._allowed_release(url, False)


def test_alternate_channel_forbidden_outside_tests(tmp_path):
    with pytest.raises(ValueError, match="official stable"):
        update.ensure_runtime(tmp_path / "state", "https://evil.example/stable.json")


def test_interrupted_download_preserves_active_release_and_cleans_staging(tmp_path, monkeypatch):
    channel = release(tmp_path)
    state = tmp_path / "state"
    install(state, channel)
    release(tmp_path, "1.1.0")
    download = update._download
    def interrupted(url, target, maximum):
        if str(url).endswith(".zip"):
            target.write_bytes(b"incomplete")
            raise OSError("Simulated connection interrupted")
        return download(url, target, maximum)
    monkeypatch.setattr(update, "_download", interrupted)
    result = install(state, channel)
    assert result["version"] == "1.0.0" and "interrupted" in result["warning"]
    assert not list(state.glob("download-*"))


def test_archive_runtime_api_must_match_channel_api(tmp_path):
    channel = release(tmp_path, api=2)
    data = json.loads(channel.read_text())
    data["apiVersion"] = 1
    channel.write_text(json.dumps(data))
    with pytest.raises(RuntimeError, match="version/API mismatch"):
        install(tmp_path / "state", channel)


def test_unlisted_file_invalidates_cached_runtime(tmp_path):
    channel = release(tmp_path)
    result = install(tmp_path / "state", channel)
    Path(result["root"], "host/extra.dll").write_bytes(b"unexpected code")
    channel.unlink()
    with pytest.raises(RuntimeError, match="first download failed"):
        install(tmp_path / "state", channel)


def test_windows_powershell51_bootstrap_helpers_share_cache_and_reject_traversal(tmp_path):
    powershell = shutil.which("powershell.exe")
    if powershell is None:
        pytest.skip("Windows PowerShell 5.1 is only available on Windows")
    channel = release(tmp_path)
    state = tmp_path / "state"
    install(state, channel)
    evil = tmp_path / "evil.zip"
    with zipfile.ZipFile(evil, "w") as z:
        z.writestr("../outside.exe", b"unsafe")
    bootstrap = (Path(__file__).resolve().parents[1] / "bootstrap.ps1").read_text(encoding="utf-8")
    helper_code = bootstrap.split("$lock = Join-Path $StateDir 'update.lock'", 1)[0]
    helper_code = helper_code.replace("} catch { return $null }", "} catch { throw }")
    # Test the production helpers in the actual no-profile 5.1 interpreter. Do
    # not execute bootstrap's network or process launch segment in a unit test.
    quote = lambda path: "'" + str(path).replace("'", "''") + "'"
    script = tmp_path / "bootstrap-check.ps1"
    script.write_text(helper_code + f"""
$cached = Get-Cached
if (-not $cached -or $cached.version -ne '1.0.0') {{ throw 'Python/PowerShell cache contract mismatch' }}
$rejected = $false
try {{ Expand-Safe {quote(evil)} {quote(tmp_path / 'extract')} }} catch {{ $rejected = $true }}
if (-not $rejected) {{ throw 'PowerShell accepted archive traversal' }}
$rejected = $false
try {{ Remove-StateDirectory {quote(tmp_path)} }} catch {{ $rejected = $true }}
if (-not $rejected) {{ throw 'PowerShell cleanup escaped its state directory' }}
Write-Output 'bootstrap-helpers-ok'
""", encoding="utf-8-sig")
    result = subprocess.run([powershell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script), "-StateDir", str(state)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    assert "bootstrap-helpers-ok" in result.stdout
