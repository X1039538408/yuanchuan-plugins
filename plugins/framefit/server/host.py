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
from update import ensure_runtime, verify_runtime

PANEL_URI = "ui://framefit/panel.html"
STATE = Path(os.getenv("FRAMEFIT_STATE_DIR") or Path(os.getenv("LOCALAPPDATA", str(Path.home()))) / "FrameFitCodex")
DEV_ROOT = os.getenv("FRAMEFIT_DEV_RUNTIME")
verified_roots = {}

CONFIG = {"type": "object", "additionalProperties": False, "properties": {
    "direction": {"type": "string", "enum": ["landscape", "portrait", "custom"]},
    "templateId": {"type": ["string", "null"], "enum": ["landscape", "portrait", None]},
    "template": {"type": ["string", "null"], "description": "Local PNG/JPG/BMP/WebP template path. Required for custom direction."},
    "detection": {"type": "string", "enum": ["auto", "black", "white", "transparent"]},
    "rect": {"type": ["array", "null"], "items": {"type": "integer"}, "minItems": 4, "maxItems": 4},
    "fit": {"type": "string", "enum": ["cover", "contain", "stretch", "zoom"]},
    "zoom": {"type": "number", "minimum": 10, "maximum": 500},
    "resolution": {"type": "string", "enum": ["720p", "1080p", "4k", "original"]},
    "quality": {"type": "string", "enum": ["high", "standard"]},
    "backend": {"type": "string", "enum": ["auto", "nvenc", "cpu"]},
    "outputDir": {"type": "string"}, "preset": {"type": "string"},
}}
SID = {"type": "string", "pattern": "^[a-zA-Z0-9_-]{1,100}$"}


def tool(name, title, description, properties, required=(), readonly=True, panel=False):
    return types.Tool(name=name, title=title, description=description,
        inputSchema={"type": "object", "properties": properties, "required": list(required), "additionalProperties": False},
        outputSchema={"type": "object"},
        annotations=types.ToolAnnotations(readOnlyHint=readonly, destructiveHint=False, openWorldHint=False),
        **({"_meta": {"ui": {"resourceUri": PANEL_URI}, "openai/outputTemplate": PANEL_URI}} if panel else {}))


TOOLS = [
    tool("prepare_config", "准备视频套版", "Prepare a new local video framing session; checks for a published runtime/template update first. Accept accessible local attachment paths, video files or folders. Clear instructions may export immediately after preparation.",
         {"videos": {"type": "array", "items": {"type": "string"}}, "config": CONFIG, "sessionId": SID}, readonly=False),
    tool("update_config", "修改套版参数", "Update the session settings shared by conversation and panel. Changing template or detection without rect re-detects its opening. No video export starts.",
         {"sessionId": SID, "config": CONFIG}, ("sessionId", "config"), False),
    tool("preview_frame", "预览合成画面", "Create a static preview at a timestamp using the same video-above-template composition as export.",
         {"sessionId": SID, "videoIndex": {"type": "integer", "minimum": 0}, "seconds": {"type": "number", "minimum": 0}}, ("sessionId",), False),
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


async def latest_runtime():
    if DEV_ROOT:
        root = Path(DEV_ROOT).resolve()
        return {"root": str(root), "version": json.loads((root / "runtime.json").read_text())["version"], "warning": "开发源码模式"}
    return await asyncio.to_thread(ensure_runtime, STATE)


def pinned_runtime(data):
    if DEV_ROOT:
        # Development override is explicit; production accepts only verified managed cache paths.
        return Path(DEV_ROOT).resolve()
    version = data["version"]
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", version):
        raise ValueError("任务版本号无效。")
    root = STATE / "releases" / version
    # Hash once per immutable release in this host; a 1-second progress poll
    # must not repeatedly hash hundreds of MB while FFmpeg is exporting.
    # Inventory/stat changes always trigger full verification again.
    fingerprint = []
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError("运行包包含不允许的符号链接。")
        if path.is_file():
            stat = path.stat()
            fingerprint.append((str(path.relative_to(root)), stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns))
    fingerprint = tuple(fingerprint)
    if verified_roots.get(str(root)) != fingerprint:
        verify_runtime(root, version)
        verified_roots[str(root)] = fingerprint
    return root


async def run_worker(root, action, arguments):
    if DEV_ROOT:
        command = [sys.executable, str(Path(__file__).with_name("engine.py"))]
    else:
        command = [str(root / "worker" / "FrameFitWorker.exe")]
    command += ["--runtime-root", str(root), "--state-dir", str(STATE)]
    proc = await asyncio.create_subprocess_exec(*command, stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        **({"creationflags": 0x08000000} if os.name == "nt" else {}))
    try:
        stdout, stderr = await asyncio.wait_for(proc.communicate(json.dumps({"action": action, "arguments": arguments}, ensure_ascii=False).encode("utf-8")), 180)
    except (TimeoutError, asyncio.CancelledError):
        proc.kill()
        await proc.wait()
        raise
    try:
        response = json.loads(stdout.decode("utf-8-sig"))
    except (ValueError, UnicodeError):
        raise RuntimeError("本地处理程序没有返回有效结果：" + stderr.decode("utf-8", "replace")[-1200:])
    if not response.get("ok"):
        raise ValueError(response.get("error", "处理失败。"))
    return response["data"]


async def invoke(name, arguments):
    if name == "prepare_config" or (name == "manage_presets" and not arguments.get("sessionId")):
        runtime = await latest_runtime()
        root = Path(runtime["root"])
        warning = runtime.get("warning")
    elif "taskId" in arguments:
        root = await asyncio.to_thread(pinned_runtime, read_state("tasks", arguments["taskId"]))
        warning = None
    else:
        root = await asyncio.to_thread(pinned_runtime, read_state("sessions", arguments["sessionId"]))
        warning = None
    if name == "render_panel":
        data = read_state("sessions", arguments["sessionId"])
    else:
        data = await run_worker(root, name, arguments)
    if warning:
        data["warning"] = warning
    return data, root


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
        if name == "preview_frame":
            content.append(types.ImageContent(type="image", mimeType="image/jpeg", data=data["previewDataUrl"].split(",", 1)[1]))
        meta = None
        if name == "render_panel":
            # Resource handler serves the release pinned to this panel/session.
            panel_roots[data["sessionId"]] = root
            global current_panel_root
            current_panel_root = root
            meta = {"ui": {"resourceUri": PANEL_URI}, "openai/outputTemplate": PANEL_URI}
        return types.CallToolResult(content=content, structuredContent=data, **({"_meta": meta} if meta else {}))
    except Exception as exc:
        return types.CallToolResult(content=[types.TextContent(type="text", text=str(exc))], structuredContent={"error": str(exc)}, isError=True)


panel_roots = {}
current_panel_root = None


async def on_list_resources(ctx, params):
    return types.ListResourcesResult(resources=[types.Resource(uri=PANEL_URI, name="视频套版 FrameFit", mimeType="text/html;profile=mcp-app")])


async def on_read_resource(ctx, params):
    if str(params.uri) != PANEL_URI:
        raise ValueError("未知面板资源。")
    root = current_panel_root
    if root is None:
        runtime = await latest_runtime()
        root = Path(runtime["root"])
    panel = root / "panel.html"
    if DEV_ROOT:
        panel = root / "web" / "panel.html"
    text = panel.read_text(encoding="utf-8")
    return types.ReadResourceResult(contents=[types.TextResourceContents(uri=PANEL_URI, mimeType="text/html;profile=mcp-app", text=text,
        **{"_meta": {"ui": {"csp": {"connectDomains": [], "resourceDomains": ["data:"]}}}})])


async def main():
    server = Server("framefit", version="1.0.0", instructions="Local Windows video framing. Prepare with accessible local paths; explicit clear user requests may export directly. Only prepare checks latest runtime. Query original task IDs; do not resubmit on status requests.",
        on_list_tools=on_list_tools, on_call_tool=on_call_tool, on_list_resources=on_list_resources, on_read_resource=on_read_resource,
        get_tool_input_schema=lambda name: next((t.input_schema for t in TOOLS if t.name == name), None))
    async with stdio_server() as (reader, writer):
        await server.run(reader, writer, server.create_initialization_options())


if __name__ == "__main__":
    for stream in (sys.stdin, sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="FrameFit local MCP host")
    parser.add_argument("--runtime-root", help="Verified runtime selected by bootstrap")
    parser.add_argument("--state-dir", help="Local state directory")
    options = parser.parse_args()
    if options.state_dir:
        STATE = Path(options.state_dir).resolve()
    if options.runtime_root:
        current_panel_root = Path(options.runtime_root).resolve()
    asyncio.run(main())
