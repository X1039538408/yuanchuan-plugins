"""MCP stdio host client for the authenticated FrameFit desktop service."""
from __future__ import annotations

import json
import os
from pathlib import Path
import urllib.error
import urllib.request


def descriptor_path() -> Path:
    override = os.getenv("FRAMEFIT_BRIDGE_FILE")
    if override:
        return Path(override)
    root = Path(os.getenv("LOCALAPPDATA", str(Path.home())))
    return root / "FrameFitCodex" / "bridge.json"


def call_local(action: str, arguments: dict) -> dict:
    path = descriptor_path()
    try:
        descriptor = json.loads(path.read_text(encoding="utf-8"))
        port, token = descriptor["port"], descriptor["token"]
    except (OSError, ValueError, KeyError, TypeError):
        raise RuntimeError("请先打开本机 FrameFit 桌面软件，再通过 @视频套版 操作。") from None
    if not isinstance(port, int) or not 1 <= port <= 65535 or not isinstance(token, str) or len(token) < 32:
        raise RuntimeError("FrameFit 桌面连接信息无效，请重启桌面软件。")
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/rpc",
        data=json.dumps({"action": action, "arguments": arguments}, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json", "Authorization": "Bearer " + token}, method="POST")
    try:
        with urllib.request.urlopen(request, timeout=180) as response:
            result = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        try:
            detail = json.loads(exc.read().decode('utf-8')).get('error', str(exc))
        except (ValueError, OSError):
            detail = str(exc)
        raise RuntimeError(detail) from None
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        raise RuntimeError(f"FrameFit 桌面软件未响应，请检查软件是否正在运行：{exc}") from None
    if not result.get("ok"):
        raise RuntimeError(result.get("error", "FrameFit 桌面操作失败。"))
    return result["data"]
