import hashlib
import io
import json
import zipfile

import pytest

from plugins.framefit.desktop.source import updater


def package(version="1.2.0", entries=None):
    entries = entries if entries is not None else {name: b'verified placeholder' for name in (
        'FrameFitApp/FrameFitStudio.exe', 'worker/FrameFitWorker.exe', 'host/FrameFitHost.exe',
        'bin/ffmpeg.exe', 'bin/ffprobe.exe', 'assets/default.png', 'assets/default_portrait.png', 'shared-presets.json')}
    if entries and 'FrameFitApp/FrameFitStudio.exe' in entries:
        entries['runtime.json'] = json.dumps({'version':version,'apiVersion':1}).encode()
    files = {name: hashlib.sha256(value).hexdigest() for name, value in entries.items()}
    metadata = json.dumps({"version": version, "apiVersion": 1, "files": files}).encode()
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, value in entries.items():
            archive.writestr(name, value)
        archive.writestr("desktop-release.json", metadata)
    return stream.getvalue()


def test_stage_verifies_archive_and_inventory_before_returning_version(monkeypatch, tmp_path):
    payload = package()
    monkeypatch.setattr(updater, "_read_url", lambda *_args, **_kwargs: payload)
    target = updater.stage_update({"version": "1.2.0", "url": "https://github.com/X1039538408/yuanchuan-plugins/releases/download/framefit-desktop-v1.2.0/pkg.zip",
                                   "sha256": hashlib.sha256(payload).hexdigest()}, tmp_path)
    assert (target / 'FrameFitApp/FrameFitStudio.exe').read_bytes() == b'verified placeholder'
    assert not (tmp_path / "current.json").exists()


def test_stage_rejects_bad_checksum_and_zip_path_escape(monkeypatch, tmp_path):
    payload = package()
    monkeypatch.setattr(updater, "_read_url", lambda *_args, **_kwargs: payload)
    with pytest.raises(ValueError, match="SHA-256"):
        updater.stage_update({"version": "1.2.0", "url": "https://github.com/X1039538408/yuanchuan-plugins/releases/download/framefit-desktop-v1.2.0/pkg.zip", "sha256": "0" * 64}, tmp_path)
    assert not (tmp_path / "versions" / "1.2.0").exists()

    malicious = package(entries={"../escape.txt": b"no"})
    monkeypatch.setattr(updater, "_read_url", lambda *_args, **_kwargs: malicious)
    with pytest.raises(ValueError, match="不安全"):
        updater.stage_update({"version": "1.2.0", "url": "https://github.com/X1039538408/yuanchuan-plugins/releases/download/framefit-desktop-v1.2.0/pkg.zip",
                              "sha256": hashlib.sha256(malicious).hexdigest()}, tmp_path)
    assert not (tmp_path / "escape.txt").exists()


def test_activate_requires_version_inside_install_root(tmp_path):
    install = tmp_path / "FrameFit"
    version = install / "versions" / "1.2.0"
    version.mkdir(parents=True)
    with zipfile.ZipFile(io.BytesIO(package())) as archive:
        archive.extractall(version)
    updater.activate_update(version, install)
    assert json.loads((install / "current.json").read_text()) == {"versionDirectory": "versions\\1.2.0"}
    with pytest.raises(ValueError, match="必须位于"):
        updater.activate_update(tmp_path / "outside", install)


def test_incompatible_api_and_modified_cached_files_never_replace_current(monkeypatch, tmp_path):
    manifest = {'version': '1.2.0', 'apiVersion': 2, 'url': 'unused', 'sha256': '0' * 64}
    with pytest.raises(ValueError, match='不兼容'):
        updater.stage_update(manifest, tmp_path)
    payload = package()
    monkeypatch.setattr(updater, '_read_url', lambda *_args, **_kwargs: payload)
    manifest.update(apiVersion=1, url='https://github.com/X1039538408/yuanchuan-plugins/releases/download/v/pkg.zip', sha256=hashlib.sha256(payload).hexdigest())
    target = updater.stage_update(manifest, tmp_path)
    (target / 'FrameFitApp/FrameFitStudio.exe').write_bytes(b'changed')
    with pytest.raises(ValueError, match='校验失败'):
        updater.stage_update(manifest, tmp_path)
    assert not (tmp_path / 'current.json').exists()


def test_windows_case_collisions_and_backslash_traversal_are_rejected(monkeypatch, tmp_path):
    for entries in ({'../escape.exe': b'bad'}, {'..\\escape.exe': b'bad'}, {'App.bin': b'a', 'app.bin': b'b'}):
        payload = package(entries=entries)
        monkeypatch.setattr(updater, '_read_url', lambda *_args, **_kwargs: payload)
        with pytest.raises(ValueError, match='不安全'):
            updater.stage_update({'version': '1.2.0', 'url': 'https://github.com/X1039538408/yuanchuan-plugins/releases/download/v/pkg.zip',
                                 'sha256': hashlib.sha256(payload).hexdigest()}, tmp_path)


def test_failed_restart_rolls_back_current_and_preserves_settings(monkeypatch, tmp_path):
    payload = package()
    monkeypatch.setattr(updater, '_read_url', lambda *_args, **_kwargs: payload)
    target = updater.stage_update({'version': '1.2.0', 'url': 'https://github.com/X1039538408/yuanchuan-plugins/releases/download/v/pkg.zip',
                                   'sha256': hashlib.sha256(payload).hexdigest()}, tmp_path)
    current = tmp_path / 'current.json'
    current.write_text('{"versionDirectory":"versions/1.1.0"}')
    settings = tmp_path / 'settings.json'
    settings.write_text('{"private":"local"}')
    launcher = tmp_path / 'FrameFit.exe'
    launcher.write_bytes(b'test')
    def fail(*_args, **_kwargs):
        raise OSError('restart failed')
    monkeypatch.setattr(updater.subprocess, 'Popen', fail)
    with pytest.raises(OSError, match='restart failed'):
        updater.activate_update(target, tmp_path, launcher)
    assert json.loads(current.read_text())['versionDirectory'] == 'versions/1.1.0'
    assert json.loads(settings.read_text()) == {'private':'local'}


def test_missing_entrypoints_and_failed_new_process_restore_previous_pointer(monkeypatch, tmp_path):
    payload = package(entries={})
    monkeypatch.setattr(updater, '_read_url', lambda *_args, **_kwargs: payload)
    manifest = {'version':'1.2.0','url':'https://github.com/X1039538408/yuanchuan-plugins/releases/download/v/pkg.zip',
                'sha256':hashlib.sha256(payload).hexdigest()}
    with pytest.raises(ValueError, match='缺少可运行'):
        updater.stage_update(manifest, tmp_path)
    payload = package()
    manifest['sha256'] = hashlib.sha256(payload).hexdigest()
    selected = updater.stage_update(manifest, tmp_path)
    current = tmp_path / 'current.json'
    current.write_text('{"versionDirectory":"versions/1.1.0"}')
    monkeypatch.setenv('LOCALAPPDATA', str(tmp_path / 'local'))
    bridge = tmp_path / 'local/FrameFitCodex/bridge.json'
    bridge.parent.mkdir(parents=True)
    bridge.write_text('{"token":"old-desktop"}')
    launcher = tmp_path / 'FrameFit.exe'
    launcher.touch()
    class FailedProcess:
        pid = 12345
        def poll(self): return 1
    monkeypatch.setattr(updater.subprocess, 'Popen', lambda *_args, **_kwargs: FailedProcess())
    with pytest.raises(RuntimeError, match='新版启动失败'):
        updater.activate_update(selected, tmp_path, launcher)
    assert json.loads(current.read_text())['versionDirectory'] == 'versions/1.1.0'
    assert json.loads(bridge.read_text())['token'] == 'old-desktop'
