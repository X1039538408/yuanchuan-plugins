import asyncio
import json
import os
from pathlib import Path
import sys

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

PLUGIN = Path(__file__).resolve().parents[1]


def test_official_sdk_stdio_tools_resources_and_validation(tmp_path):
    async def run():
        parameters = StdioServerParameters(command=sys.executable, args=[str(PLUGIN / "server" / "host.py")],
            env={**os.environ, "FRAMEFIT_DEV_RUNTIME": str(PLUGIN), "FRAMEFIT_STATE_DIR": str(tmp_path), "PYTHONUTF8":"1"})
        async with stdio_client(parameters) as (reader, writer):
            async with ClientSession(reader, writer) as client:
                initialized = await client.initialize()
                assert initialized.server_info.name == "framefit"
                listed = await client.list_tools()
                assert len(listed.tools) == 10
                result = await client.call_tool("prepare_config", {"videos":[], "config":{"direction":"landscape", "templateId":"landscape"}})
                assert not result.is_error, result
                session = result.structured_content
                panel = await client.call_tool("render_panel", {"sessionId": session["sessionId"]})
                assert not panel.is_error and panel.meta["ui"]["resourceUri"] == "ui://framefit/panel.html"
                resource = await client.read_resource("ui://framefit/panel.html")
                assert "ui/initialize" in resource.contents[0].text
                invalid = await client.call_tool("update_config", {"sessionId":session["sessionId"], "config":{"zoom":999}})
                assert invalid.is_error
    asyncio.run(run())
