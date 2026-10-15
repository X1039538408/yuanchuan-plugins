import hashlib
import json
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))
import host


def test_progress_poll_reuses_verification_and_detects_file_changes(tmp_path, monkeypatch):
    root = tmp_path / "releases" / "1.0.0"
    files = {}
    for name in ("host/FrameFitHost.exe", "worker/FrameFitWorker.exe", "bin/ffmpeg.exe", "bin/ffprobe.exe", "panel.html", "shared-presets.json"):
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(b"original")
        files[name] = hashlib.sha256(b"original").hexdigest()
    (root / "runtime.json").write_text(json.dumps({"version":"1.0.0", "apiVersion":1, "files":files}))
    monkeypatch.setattr(host, "STATE", tmp_path)
    monkeypatch.setattr(host, "DEV_ROOT", None)
    host.verified_roots.clear()
    actual = host.verify_runtime
    calls = []
    def verified(*args):
        calls.append(args)
        return actual(*args)
    monkeypatch.setattr(host, "verify_runtime", verified)
    for _ in range(20):
        assert host.pinned_runtime({"version":"1.0.0"}) == root
    assert len(calls) == 1
    (root / "worker/FrameFitWorker.exe").write_bytes(b"tampered")
    with pytest.raises(ValueError, match="verification failed"):
        host.pinned_runtime({"version":"1.0.0"})
    assert len(calls) == 2
