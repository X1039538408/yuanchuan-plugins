"""Authenticated loopback bridge owned by the running desktop application."""
from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import json
import os
from pathlib import Path
import secrets
import threading


class FrameFitLocalService:
    ACTIONS = {"prepare_config", "update_config", "preview_frame", "export_videos",
               "get_task", "cancel_task", "resume_task", "manage_presets", "open_output",
               "thumbnail", "analyze_borders", "match_project_templates", "manage_recipes"}

    def __init__(self, engine, state_dir: str, on_config=None, before_call=None, on_task=None):
        self.engine = engine
        self.state = Path(state_dir)
        self.state.mkdir(parents=True, exist_ok=True)
        self.descriptor = self.state / "bridge.json"
        self.token = secrets.token_urlsafe(32)
        self.on_config = on_config
        self.before_call = before_call
        self.on_task = on_task
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                return

            def do_POST(self):
                if self.path != "/rpc" or self.client_address[0] not in ("127.0.0.1", "::1"):
                    self.send_error(404)
                    return
                auth = self.headers.get("Authorization", "")
                if not hmac.compare_digest(auth, "Bearer " + owner.token):
                    self.send_error(401)
                    return
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                    if length < 2 or length > 4 * 1024 * 1024:
                        raise ValueError("请求大小无效。")
                    request = json.loads(self.rfile.read(length).decode("utf-8"))
                    action = request.get("action")
                    arguments = request.get("arguments") or {}
                    if action not in owner.ACTIONS:
                        raise ValueError("未知 FrameFit 操作。")
                    if owner.before_call:
                        owner.before_call(action)
                    result = getattr(owner.engine, action)(**arguments)
                    if action in ("prepare_config", "update_config", "manage_recipes") and owner.on_config:
                        owner.on_config(result)
                    if action in ('export_videos', 'resume_task') and owner.on_task:
                        owner.on_task(result)
                    body = json.dumps({"ok": True, "data": result}, ensure_ascii=False).encode("utf-8")
                    self.send_response(200)
                except Exception as exc:
                    body = json.dumps({"ok": False, "error": str(exc)}, ensure_ascii=False).encode("utf-8")
                    self.send_response(400)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, name="FrameFitLocalService", daemon=True)

    def start(self):
        tmp = self.descriptor.with_suffix(".tmp")
        descriptor = {'port': self.port, 'token': self.token, 'ready': False,
                      'pid': os.getpid(), 'version': getattr(self.engine, 'version', 'dev')}
        if getattr(self.engine, 'root', None) and os.getenv('FRAMEFIT_INSTALL_ROOT'):
            descriptor.update(runtimeRoot=str(self.engine.root), installRoot=os.getenv('FRAMEFIT_INSTALL_ROOT'))
        tmp.write_text(json.dumps(descriptor, ensure_ascii=False), encoding="utf-8")
        os.replace(tmp, self.descriptor)
        self.thread.start()
        return self

    def mark_ready(self):
        data = json.loads(self.descriptor.read_text(encoding='utf-8'))
        if hmac.compare_digest(data.get('token', ''), self.token):
            data['ready'] = True
            tmp = self.descriptor.with_suffix('.tmp')
            tmp.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
            os.replace(tmp, self.descriptor)

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        if self.descriptor.exists():
            try:
                data = json.loads(self.descriptor.read_text(encoding="utf-8"))
                if hmac.compare_digest(data.get("token", ""), self.token):
                    self.descriptor.unlink()
            except (OSError, ValueError):
                pass
