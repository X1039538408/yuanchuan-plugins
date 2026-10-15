"""Stable local MCP facade; new preparations select a verified release, jobs pin it."""
from __future__ import annotations

import argparse
import asyncio
import json
import os
from pathlib import Path
import re
import sys
import time

import mcp.types as types
from mcp.server.lowlevel import Server
from mcp.server.stdio import stdio_server
from local_client import call_local

PANEL_URI = "ui://framefit/panel.html"
STATE = Path(os.getenv("FRAMEFIT_STATE_DIR") or Path(os.getenv("LOCALAPPDATA", str(Path.home()))) / "FrameFitCodex")
PLUGIN_ROOT = Path(sys.executable).resolve().parents[1] if getattr(sys, 'frozen', False) else Path(__file__).resolve().parents[1]

CONFIG = {"type": "object", "additionalProperties": False, "properties": {
    "direction": {"type": "string", "enum": ["landscape", "portrait", "custom"]},
    "templateId": {"type": ["string", "null"], "enum": ["landscape", "portrait", None]},
    "template": {"type": ["string", "null"], "description": "Local PNG/JPG/BMP/WebP template path. Required for custom direction."},
    "detection": {"type": "string", "enum": ["auto", "black", "white", "transparent"]},
    "rect": {"type": ["array", "null"], "items": {"type": "integer"}, "minItems": 4, "maxItems": 4},
    "fit": {"type": "string", "enum": ["cover", "contain", "stretch", "zoom"]},
    "zoom": {"type": "number", "minimum": 10, "maximum": 500},
    "resolution": {"type": "string", "enum": ["720p", "1080p", "4k", "original"]},
    "outputSize": {"type": ["array","null"], "items":{"type":"integer","minimum":2,"maximum":16384,"multipleOf":2},"minItems":2,"maxItems":2},
    "quality": {"type": "string", "enum": ["high", "standard"]},
    "backend": {"type": "string", "enum": ["auto", "nvenc", "cpu"]},
    "outputDir": {"type": "string"}, "preset": {"type": "string"},
    "language": {"type": "string", "enum": ["auto", "简中", "英", "日", "韩", "other", "none"]},
    "otherLanguage": {"type": "string"},
    "nameMode": {"type": "string", "enum": ["compact", "full"]},
    "outputGroup": {"type": "string", "enum": ["folder", "project", "template"]},
    "borderPolicy": {"type": "string", "enum": ["ask", "keep", "remove"]},
    "templateConfirmed": {"type": "boolean", "description": "Only true after the user chooses a template or confirms the preselected previous template."},
}}
ITEM_CONFIG = {**CONFIG, 'properties': {**CONFIG['properties'], 'outputName': {'type': 'string'}, 'outputKey':{'type':'string','maxLength':100}}}
ITEMS = {'type': 'array', 'items': ITEM_CONFIG, 'description': 'Optional per-video settings, one object per deduplicated video. Use to confirm a project candidate, decide border handling or edit outputName.'}
SID = {"type": "string", "pattern": "^[a-zA-Z0-9_-]{1,100}$"}


def tool(name, title, description, properties, required=(), readonly=True, panel=False):
    return types.Tool(name=name, title=title, description=description,
        inputSchema={"type": "object", "properties": properties, "required": list(required), "additionalProperties": False},
        outputSchema={"type": "object"},
        annotations=types.ToolAnnotations(readOnlyHint=readonly, destructiveHint=False, openWorldHint=False),
        **({"_meta": {"ui": {"resourceUri": PANEL_URI}, "openai/outputTemplate": PANEL_URI}} if panel else {}))


TOOLS = [
    tool("prepare_config", "准备视频套版", "Prepare a new session through the running FrameFit desktop software. The desktop app is the source of local settings and checks for updates when it starts.",
         {"videos": {"type": "array", "items": {"type": "string"}}, "config": CONFIG, "sessionId": SID, "itemConfigs": ITEMS,
          "outputTemplates":{"type":"array","minItems":1,"maxItems":100,"items":CONFIG,"description":"Create one output per input video and template configuration."}}, readonly=False),
    tool("update_config", "修改套版参数", "Update the session settings shared by conversation, desktop software and panel. No video export starts.",
         {"sessionId": SID, "config": CONFIG, "itemConfigs": ITEMS}, ("sessionId", "config"), False),
    tool("preview_frame", "预览合成画面", "Create a static preview at a timestamp using the same video-above-template composition as export.",
         {"sessionId": SID, "videoIndex": {"type": "integer", "minimum": 0}, "seconds": {"type": "number", "minimum": 0}}, ("sessionId",), False),
    tool("thumbnail", "生成时间轴缩略图", "Return a small local video frame for a draggable desktop or Codex timeline.",
         {"sessionId": SID, "videoIndex": {"type": "integer", "minimum": 0}, "seconds": {"type": "number", "minimum": 0}}, ("sessionId",)),
    tool("analyze_borders", "分析视频黑白边", "Scan source frames once, report black or white edge segments, then compare beginning/middle/end composites against the source.",
         {"sessionId": SID, "videoIndex": {"type": "integer", "minimum": 0}}, ("sessionId",)),
    tool("match_project_templates", "匹配本机项目套版", "Resolve a project template using this computer's private schedule/template paths and mappings.",
         {"filename": {"type": "string", "minLength": 1},
          "direction": {'type':'string', 'enum':['landscape','portrait','custom']},
          "outputSize": {'type':'array','items':{'type':'integer','minimum':2},'minItems':2,'maxItems':2}}, ("filename",)),
    tool("manage_recipes", "管理本机批量配方", "List, save or delete recipes on this computer; recipe paths and settings are not uploaded.",
         {"action": {"type": "string", "enum": ["list", "save", "delete"]}, "name": {"type": "string"}, "sessionId": SID}, ("action",), False),
    tool("render_panel", "打开视频套版面板", "Show the interactive template, fitting, zoom, preview and batch-export panel for a prepared session.", {"sessionId": SID}, ("sessionId",), panel=True),
    tool("export_videos", "开始批量导出", "Export the prepared videos locally. Use a unique requestId for a new export and retain it on retries. Explicit user instructions suffice; no mandatory extra confirmation. Jobs pin their processing version.",
         {"sessionId": SID, "requestId": {"type": "string", "minLength": 1, "maxLength": 200}}, ("sessionId", "requestId"), False),
    tool("get_task", "查询套版任务", "Read the original task's progress, encoder, output paths and errors; never submits another task.", {"taskId": SID}, ("taskId",)),
    tool("cancel_task", "取消套版任务", "Cancel the original task, preserving completed videos and removing incomplete output.", {"taskId": SID}, ("taskId",), False),
    tool("resume_task", "继续未完成视频", "Resume failed/cancelled/interrupted items using the task's original version. Completed items are skipped.", {"taskId": SID}, ("taskId",), False),
    tool("manage_presets", "管理套版预设", "List shared and personal presets, or save/delete a personal preset. This does not publish local paths or templates to GitHub.",
         {"action": {"type": "string", "enum": ["list", "save", "delete"]}, "name": {"type": "string"}, "sessionId": SID}, ("action",), False),
    tool("open_output", "打开导出结果", "Open an exported video in the local default player, or its output folder when videoIndex is omitted.",
         {"taskId": SID, "videoIndex": {"type": "integer", "minimum": 0}}, ("taskId",), False),
]


def read_state(kind, identifier):
    if not isinstance(identifier, str) or not re.fullmatch(r"[a-zA-Z0-9_-]{1,100}", identifier):
        raise ValueError("无效的会话或任务编号。")
    path = STATE / kind / (identifier + ".json")
    if not path.is_file():
        raise ValueError("会话或任务不存在，请重新准备视频。")
    deadline = time.monotonic() + 5
    while True:
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except PermissionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(.01)


async def invoke(name, arguments):
    if name == "render_panel":
        data = read_state("sessions", arguments["sessionId"])
        await asyncio.to_thread(call_local, "manage_presets", {"action": "list"})
    else:
        data = await asyncio.to_thread(call_local, name, arguments)
    return data, None


async def on_list_tools(ctx, params):
    return types.ListToolsResult(tools=TOOLS)


async def on_call_tool(ctx, params):
    name, arguments = params.name, params.arguments or {}
    definition = next((t for t in TOOLS if t.name == name), None)
    try:
        if definition is None:
            raise ValueError("未知工具。")
        import jsonschema
        jsonschema.validate(arguments, definition.input_schema)
        data, root = await invoke(name, arguments)
        content = [types.TextContent(type="text", text=json.dumps({k: v for k, v in data.items() if k != "previewDataUrl"}, ensure_ascii=False))]
        if name in ("preview_frame", "thumbnail"):
            key = "previewDataUrl" if name == "preview_frame" else "thumbnailDataUrl"
            content.append(types.ImageContent(type="image", mimeType="image/jpeg", data=data[key].split(",", 1)[1]))
        meta = None
        if name == "render_panel":
            # Resource handler serves the release pinned to this panel/session.
            meta = {"ui": {"resourceUri": PANEL_URI}, "openai/outputTemplate": PANEL_URI}
        return types.CallToolResult(content=content, structuredContent=data, **({"_meta": meta} if meta else {}))
    except Exception as exc:
        return types.CallToolResult(content=[types.TextContent(type="text", text=str(exc))], structuredContent={"error": str(exc)}, isError=True)


async def on_list_resources(ctx, params):
    return types.ListResourcesResult(resources=[types.Resource(uri=PANEL_URI, name="视频套版 FrameFit", mimeType="text/html;profile=mcp-app")])


async def on_read_resource(ctx, params):
    if str(params.uri) != PANEL_URI:
        raise ValueError("未知面板资源。")
    panel = PLUGIN_ROOT / "web" / "panel.html"
    text = panel.read_text(encoding="utf-8")
    return types.ReadResourceResult(contents=[types.TextResourceContents(uri=PANEL_URI, mimeType="text/html;profile=mcp-app", text=text,
        **{"_meta": {"ui": {"csp": {"connectDomains": [], "resourceDomains": ["data:"]}}}})])


async def main():
    server = Server("framefit", version="1.1.0", instructions="Local Windows video framing. The running desktop software owns settings, project matching and processing. Start FrameFit before calling tools. Query original task IDs; do not resubmit on status requests.",
        on_list_tools=on_list_tools, on_call_tool=on_call_tool, on_list_resources=on_list_resources, on_read_resource=on_read_resource,
        get_tool_input_schema=lambda name: next((t.input_schema for t in TOOLS if t.name == name), None))
    async with stdio_server() as (reader, writer):
        await server.run(reader, writer, server.create_initialization_options())


if __name__ == "__main__":
    for stream in (sys.stdin, sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="FrameFit local MCP host")
    parser.add_argument("--state-dir", help="Local state directory")
    options = parser.parse_args()
    if options.state_dir:
        STATE = Path(options.state_dir).resolve()
    asyncio.run(main())
