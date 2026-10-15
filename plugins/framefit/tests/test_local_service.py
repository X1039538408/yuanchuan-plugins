import json
from pathlib import Path
import sys
import urllib.error
import urllib.request

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "desktop" / "source"))
from local_service import FrameFitLocalService


class FakeEngine:
    def prepare_config(self, videos=None, config=None):
        return {"videos": videos or [], "config": config or {}}

    def get_task(self, taskId):
        return {"taskId": taskId, "status": "completed"}


def request(service, action, arguments, token=None):
    body = json.dumps({"action": action, "arguments": arguments}).encode()
    req = urllib.request.Request(f"http://127.0.0.1:{service.port}/rpc", data=body,
        headers={"Content-Type": "application/json", **({"Authorization": "Bearer " + token} if token else {})}, method="POST")
    return urllib.request.urlopen(req, timeout=3)


def test_authenticated_loopback_bridge_dispatches_engine_and_cleans_descriptor(tmp_path):
    service = FrameFitLocalService(FakeEngine(), str(tmp_path)).start()
    try:
        descriptor = json.loads((tmp_path / "bridge.json").read_text())
        assert descriptor["port"] == service.port and len(descriptor["token"]) >= 32
        with pytest.raises(urllib.error.HTTPError) as unauthorized:
            request(service, "prepare_config", {"videos": []})
        assert unauthorized.value.code == 401
        with request(service, "prepare_config", {"videos": ["local.mp4"]}, service.token) as response:
            result = json.loads(response.read())
        assert result["ok"] and result["data"]["videos"] == ["local.mp4"]
        with pytest.raises(urllib.error.HTTPError) as invalid:
            request(service, "shutdown", {}, service.token)
        assert invalid.value.code == 400
    finally:
        service.close()
    assert not (tmp_path / "bridge.json").exists()
