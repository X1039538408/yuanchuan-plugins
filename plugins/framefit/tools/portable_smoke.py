"""Exercise an extracted release without an installed Python/Node runtime.

The test driver uses only Python's standard library. Every application/media
subprocess comes from the release archive and receives a Windows-only PATH.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import zipfile
import zlib


def require(condition, detail):
    if not condition:
        raise AssertionError(detail)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inventory(root):
    manifest = json.loads((root / "runtime.json").read_text(encoding="utf-8-sig"))
    files = {path.relative_to(root).as_posix(): sha256(path)
             for path in root.rglob("*") if path.is_file() and path != root / "runtime.json"}
    require(manifest["apiVersion"] == 1, manifest)
    require(files == manifest["files"], "Runtime files differ from the immutable manifest")
    return manifest


def template(path):
    # Small lossless fixture, avoiding Pillow or other test-driver dependencies.
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = []
    for y in range(90):
        rows.append(b"\0" + b"".join(b"\0\0\0" if 40 <= x < 120 and 10 <= y < 80 else b"\x37\x66\x59" for x in range(160)))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 160, 90, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))


def smoke(archive, work):
    require(os.name == "nt", "The Windows portable smoke test must run on Windows")
    root = work / "portable 便携运行包"
    state = work / "local 用户状态"
    root.mkdir(parents=True)
    with zipfile.ZipFile(archive) as release:
        release.extractall(root)
    manifest = inventory(root)
    manifest_hash = sha256(root / "runtime.json")
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(("PYTHON", "FRAMEFIT", "NODE", "VIRTUAL_ENV", "CONDA", "_PYI"))}
    windows = Path(env["SystemRoot"])
    env.update(PATH=str(windows / "System32") + os.pathsep + str(windows),
               FRAMEFIT_STATE_DIR=str(state), LOCALAPPDATA=str(work / "local app data"))

    def run(command, payload=None, timeout=40):
        result = subprocess.run([str(arg) for arg in command], input=payload, capture_output=True,
                                text=True, encoding="utf-8", errors="replace", env=env, cwd=work, timeout=timeout)
        require(result.returncode == 0, {"command": str(command[0]), "code": result.returncode,
                                       "stdout": result.stdout[-5000:], "stderr": result.stderr[-5000:]})
        return result.stdout

    host = root / "host" / "FrameFitHost.exe"
    worker = root / "worker" / "FrameFitWorker.exe"
    requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "portable-smoke", "version": "1"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
    ]
    replies = [json.loads(line) for line in run([host, "--runtime-root", root, "--state-dir", state],
               "".join(json.dumps(request) + "\n" for request in requests)).splitlines()]
    require(any(reply.get("id") == 1 and "result" in reply for reply in replies), replies)
    listing = next(reply for reply in replies if reply.get("id") == 2)
    names = {tool["name"] for tool in listing["result"]["tools"]}
    require({"prepare_config", "update_config", "preview_frame", "render_panel", "export_videos", "get_task",
             "cancel_task", "resume_task", "manage_presets", "open_output"} <= names, listing)

    def call(action, **arguments):
        response = json.loads(run([worker, "--runtime-root", root, "--state-dir", state],
                                  json.dumps({"action": action, "arguments": arguments}, ensure_ascii=False)))
        require(response.get("ok"), response)
        return response["data"]

    source = work / "中文 sample ; $ &.mp4"
    frame = work / "套框 fixture.png"
    output_dir = work / "导出 output"
    template(frame)
    run([root / "bin" / "ffmpeg.exe", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
         "testsrc2=s=96x64:r=12:d=1", "-f", "lavfi", "-i", "sine=frequency=440:duration=1", "-c:v",
         "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", source])
    session = call("prepare_config", videos=[str(source)], config={"direction": "custom", "template": str(frame),
                   "resolution": "original", "backend": "cpu", "fit": "contain", "outputDir": str(output_dir)})
    require(session["version"] == manifest["version"] and session["outputSize"] == [160, 90], session)
    require(len(session["videos"]) == 1 and not session["videos"][0].get("error"), session["videos"])
    require(session["videos"][0]["info"]["audio"], session["videos"])
    preview = call("preview_frame", sessionId=session["sessionId"], seconds=100)
    prefix = "data:image/jpeg;base64,"
    require(preview["previewDataUrl"].startswith(prefix), "Preview has no JPEG data URL")
    image = Path(preview["previewPath"])
    require(image.read_bytes() == base64.b64decode(preview["previewDataUrl"][len(prefix):]) and image.stat().st_size > 100,
            "Preview file does not match returned image")
    task = call("export_videos", sessionId=session["sessionId"], requestId="portable-smoke-export")
    task_id = task["taskId"]
    require(call("export_videos", sessionId=session["sessionId"], requestId="portable-smoke-export")["taskId"] == task_id,
            "Export retry created another task")
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        task = call("get_task", taskId=task_id)
        if task["status"] not in {"queued", "running"}:
            break
        time.sleep(.15)
    require(task["status"] == "completed" and task["progress"] == 1, task)
    item = task["items"][0]
    require(item["status"] == "completed" and item["backend"] == "cpu", item)
    require(Path(task["runtimeRoot"]) == root and task["version"] == manifest["version"], task)
    exported = Path(item["output"])
    require(exported.is_file() and exported.parent == output_dir and exported.stat().st_size > 100, item)
    info = json.loads(run([root / "bin" / "ffprobe.exe", "-v", "error", "-show_streams", "-show_format", "-of", "json", exported]))
    video = next(stream for stream in info["streams"] if stream["codec_type"] == "video")
    require([video["width"], video["height"]] == session["outputSize"], video)
    require(any(stream["codec_type"] == "audio" for stream in info["streams"]), "Export lost audio")
    require(abs(float(info["format"]["duration"]) - 1) < .15, info["format"])
    rate_a, rate_b = map(int, video["avg_frame_rate"].split("/"))
    require(abs(rate_a / rate_b - 12) < .1, video)
    require(source.exists() and len(list(output_dir.glob("*.mp4"))) == 1, "Input changed or duplicate export created")
    require(inventory(root) == manifest and sha256(root / "runtime.json") == manifest_hash, "Application modified runtime")
    print(json.dumps({"version": manifest["version"], "archiveSha256": sha256(archive), "archiveBytes": archive.stat().st_size,
                      "tools": len(names), "status": task["status"], "outputSize": session["outputSize"], "audio": True,
                      "backend": item["backend"], "immutableRuntimeFiles": len(manifest["files"]), "workDir": str(work)}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, help="Keep extracted runtime and evidence in a new empty directory")
    args = parser.parse_args()
    if args.work_dir:
        destination = args.work_dir.resolve()
        destination.mkdir(parents=True, exist_ok=True)
        require(not any(destination.iterdir()), "--work-dir must be empty")
        smoke(args.archive.resolve(), destination)
    else:
        with tempfile.TemporaryDirectory(prefix="framefit smoke 中文 ") as directory:
            smoke(args.archive.resolve(), Path(directory))
