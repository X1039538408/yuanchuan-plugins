import asyncio
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import sys
import threading

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

PLUGIN = Path(__file__).resolve().parents[1]


def test_official_sdk_stdio_tools_resources_and_validation(tmp_path):
    async def run():
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args): pass
            def do_POST(self):
                if self.headers.get("Authorization") != "Bearer integration-test-token-0123456789":
                    self.send_error(401); return
                request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                action = request["action"]
                args = request["arguments"]
                if action == "prepare_config":
                    data = {"sessionId": "session-stdio-123", "version": "desktop-test", "config": {}, "videos": []}
                    sessions = tmp_path / "sessions"
                    sessions.mkdir(exist_ok=True)
                    (sessions / "session-stdio-123.json").write_text(json.dumps(data))
                elif action == "manage_presets":
                    data = {"presets": []}
                elif action == "update_config" and args["config"].get("zoom", 0) > 500:
                    result = {"ok": False, "error": "缩放比例必须在 10%～500% 之间。"}
                    body = json.dumps(result, ensure_ascii=False).encode()
                    self.send_response(400); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body); return
                else:
                    data = {"sessionId": args.get("sessionId"), "config": args.get("config", {})}
                body = json.dumps({"ok": True, "data": data}, ensure_ascii=False).encode()
                self.send_response(200); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        descriptor = tmp_path / "bridge.json"
        descriptor.write_text(json.dumps({"port": server.server_address[1], "token": "integration-test-token-0123456789"}))
        parameters = StdioServerParameters(command=sys.executable, args=[str(PLUGIN / "server" / "host.py")],
            env={**os.environ, "FRAMEFIT_BRIDGE_FILE": str(descriptor), "FRAMEFIT_STATE_DIR": str(tmp_path), "PYTHONUTF8":"1"})
        try:
            async with stdio_client(parameters) as (reader, writer):
                async with ClientSession(reader, writer) as client:
                    initialized = await client.initialize()
                    assert initialized.server_info.name == "framefit"
                    listed = await client.list_tools()
                    assert len(listed.tools) == 14
                    result = await client.call_tool("prepare_config", {"videos":[], "config":{"direction":"landscape", "templateId":"landscape"}})
                    assert not result.is_error, result
                    session = result.structured_content
                    panel = await client.call_tool("render_panel", {"sessionId": session["sessionId"]})
                    assert not panel.is_error and panel.meta["ui"]["resourceUri"] == "ui://framefit/panel.html"
                    resource = await client.read_resource("ui://framefit/panel.html")
                    assert "ui/initialize" in resource.contents[0].text
                    invalid = await client.call_tool("update_config", {"sessionId":session["sessionId"], "config":{"zoom":999}})
                    assert invalid.is_error
        finally:
            server.shutdown(); server.server_close(); thread.join(timeout=1)
    asyncio.run(run())
