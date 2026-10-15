import asyncio
import json
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))
import host


def test_prepare_and_task_calls_are_forwarded_to_desktop(tmp_path, monkeypatch):
    monkeypatch.setattr(host, "call_local", lambda action, arguments: {"action": action, "arguments": arguments})
    result, _ = asyncio.run(host.invoke("prepare_config", {"videos": ["local.mp4"]}))
    assert result == {"action": "prepare_config", "arguments": {"videos": ["local.mp4"]}}
    result, _ = asyncio.run(host.invoke("get_task", {"taskId": "task-123"}))
    assert result["action"] == "get_task"


def test_panel_reads_session_from_desktop_owned_state(tmp_path, monkeypatch):
    monkeypatch.setattr(host, "STATE", tmp_path)
    (tmp_path / "sessions").mkdir()
    (tmp_path / "sessions" / "session-123.json").write_text(json.dumps({"sessionId": "session-123"}))
    monkeypatch.setattr(host, "call_local", lambda *_args: {"presets": []})
    data, _ = asyncio.run(host.invoke("render_panel", {"sessionId": "session-123"}))
    assert data["sessionId"] == "session-123"


def test_missing_desktop_session_is_actionable(tmp_path, monkeypatch):
    monkeypatch.setattr(host, "STATE", tmp_path)
    with pytest.raises(ValueError, match="重新准备视频"):
        host.read_state("sessions", "missing")
