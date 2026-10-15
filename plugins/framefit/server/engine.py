"""FrameFit local worker. JSON requests on stdin; durable, version-pinned export jobs."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import ctypes
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
import uuid

from PIL import Image
import core

ID = re.compile(r"^[a-zA-Z0-9_-]{1,100}$")
DEFAULTS = dict(direction="landscape", templateId="landscape", detection="auto", rect=None,
                fit="cover", zoom=100, resolution="1080p", quality="high", backend="auto")
CHOICES = dict(direction=("landscape", "portrait", "custom"), detection=("auto", "black", "white", "transparent"),
               fit=("cover", "contain", "stretch", "zoom"), resolution=("720p", "1080p", "4k", "original"),
               quality=("high", "standard"), backend=("auto", "nvenc", "cpu"))
FIT_LABELS = {"等比填满（推荐）": "cover", "完整显示": "contain", "完整显示（可能留黑边）": "contain",
              "拉伸铺满": "stretch", "自由缩放": "zoom"}


def read_json(path, default=None):
    deadline = time.monotonic() + 5
    while True:
        try:
            return json.loads(Path(path).read_text(encoding="utf-8-sig"))
        except FileNotFoundError:
            return default
        except PermissionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(.01)


def atomic_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        tmp.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        # On Windows a reader can briefly hold the destination without FILE_SHARE_DELETE.
        # Preserve the completed job update rather than crashing after an otherwise successful export.
        deadline = time.monotonic() + 5
        while True:
            try:
                os.replace(tmp, path)
                break
            except PermissionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.01)
    finally:
        tmp.unlink(missing_ok=True)


@contextmanager
def file_lock(path):
    """OS-owned advisory lock is automatically released if a worker crashes."""
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a+b") as handle:
        handle.seek(0)
        if os.name == "nt":
            import msvcrt
            if Path(path).stat().st_size == 0:
                handle.write(b"0")
                handle.flush()
            handle.seek(0)
            # Retry with a deadline; msvcrt's blocking mode has only ten retries.
            deadline = time.monotonic() + 30
            while True:
                try:
                    msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    if time.monotonic() > deadline:
                        raise RuntimeError("另一个操作仍在进行，请稍后重试。")
                    time.sleep(.05)
            try:
                yield
            finally:
                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


def valid_id(value):
    if not isinstance(value, str) or not ID.fullmatch(value):
        raise ValueError("无效的任务或会话编号。")
    return value


def pid_alive(pid):
    if not isinstance(pid, int) or pid <= 0:
        return False
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.OpenProcess(0x100000, False, pid)
        if not handle:
            return False
        try:
            return kernel.WaitForSingleObject(handle, 0) == 258
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


class Engine:
    def __init__(self, runtime_root, state_dir):
        self.root = Path(runtime_root).resolve()
        self.state = Path(state_dir).resolve()
        self.version = read_json(self.root / "runtime.json", {"version": "dev"})["version"]
        # GUI's _MEIPASS points inside _internal; portable release stores media at package root.
        core.ROOT = self.root
        self._media = os.getenv("FRAMEFIT_BIN_DIR")
        if self._media:
            original = core.binary
            core.binary = lambda name: str(Path(self._media) / (name + (".exe" if os.name == "nt" else ""))) if (Path(self._media) / (name + (".exe" if os.name == "nt" else ""))).is_file() else original(name)
        self.state.mkdir(parents=True, exist_ok=True)

    def session_path(self, session_id):
        return self.state / "sessions" / (valid_id(session_id) + ".json")

    def task_path(self, task_id):
        return self.state / "tasks" / (valid_id(task_id) + ".json")

    def load_session(self, session_id):
        data = read_json(self.session_path(session_id))
        if data is None:
            raise ValueError("会话不存在，请重新提供视频。")
        return data

    def templates(self):
        result = []
        for key, filename, label in (("landscape", "default.png", "仙界横版套框"), ("portrait", "default_portrait.png", "仙界竖版套框")):
            path = self.root / "assets" / filename
            with Image.open(path) as image:
                result.append(dict(id=key, name=label, path=str(path), width=image.width, height=image.height))
        return result

    def presets(self):
        shared = read_json(self.root / "shared-presets.json", [])
        personal = read_json(self.state / "presets.json", {})
        if not isinstance(shared, list) or not isinstance(personal, dict):
            raise ValueError("预设文件格式错误。")
        return [dict(p, scope="shared") for p in shared] + [dict(name=k, config=v, scope="local") for k, v in personal.items()]

    def migrate(self, direction):
        marker = self.state / "migration.json"
        if not marker.exists():
            legacy = Path(os.getenv("LOCALAPPDATA", str(Path.home()))) / "FrameFit" / "settings.json"
            try:
                raw = read_json(legacy, {})
                profiles = raw.get("profiles", {})
                converted = {}
                for key in ("landscape", "portrait", "custom"):
                    old = profiles.get(key, raw if key == "landscape" else {})
                    if not old:
                        continue
                    cfg = dict(DEFAULTS, direction=key, templateId="portrait" if key == "portrait" else "landscape")
                    if old.get("template") and Path(old["template"]).is_file():
                        cfg["template"] = old["template"]
                        cfg["rect"] = old.get("rect")
                    cfg["fit"] = FIT_LABELS.get(old.get("mode"), "cover")
                    cfg["zoom"] = old.get("zoom", 100)
                    size = old.get("size", "")
                    cfg["resolution"] = "original" if key == "custom" or "原始" in size else "4k" if "4K" in size else "720p" if "720P" in size else "1080p"
                    converted[key] = cfg
                # Only this user's local state receives legacy absolute paths.
                atomic_json(marker, {"profiles": converted, "outputDir": raw.get("output")})
            except (OSError, ValueError, TypeError):
                atomic_json(marker, {"profiles": {}})
        data = read_json(marker, {})
        cfg = data.get("profiles", {}).get(direction, {})
        if data.get("outputDir"):
            cfg = dict(cfg, outputDir=data["outputDir"])
        return cfg

    def validate(self, config):
        unknown = set(config) - set(DEFAULTS) - {"template", "outputDir", "preset"}
        if unknown:
            raise ValueError("未知参数：" + ", ".join(sorted(unknown)))
        cfg = dict(DEFAULTS, **config)
        for key, choices in CHOICES.items():
            if cfg[key] not in choices:
                raise ValueError(f"{key} 参数无效。")
        cfg["zoom"] = core.validate_zoom(cfg["zoom"])
        template = cfg.get("template")
        if not template:
            template_id = cfg.get("templateId", cfg["direction"])
            filename = {"landscape": "default.png", "portrait": "default_portrait.png"}.get(template_id)
            if not filename:
                raise ValueError("请选择有效的模板图片。")
            if cfg["direction"] == "custom":
                raise ValueError("自定义比例需要提供模板图片，或选择已保存的自定义预设。")
            template = str(self.root / "assets" / filename)
        template = Path(template).expanduser().resolve(strict=True)
        if template.suffix.lower() not in (".png", ".jpg", ".jpeg", ".bmp", ".webp"):
            raise ValueError("模板支持 PNG、JPG、BMP、WebP。")
        with Image.open(template) as image:
            if min(image.size) < 2:
                raise ValueError("模板尺寸至少为 2×2。")
            if cfg["direction"] == "landscape" and image.width <= image.height:
                raise ValueError("竖转横需要横版模板。")
            if cfg["direction"] == "portrait" and image.height <= image.width:
                raise ValueError("横转竖需要竖版模板。")
            raw_rect = cfg.get("rect")
            if raw_rect is not None and (not isinstance(raw_rect, list) or len(raw_rect) != 4 or any(isinstance(v, bool) or not isinstance(v, int) for v in raw_rect)):
                raise ValueError("窗口坐标需要四个整数 [X,Y,宽,高]。")
            rect = core.validate_rect(raw_rect, image.size) if raw_rect is not None else core.detect_window(image, cfg["detection"])
            if cfg["direction"] == "custom" or cfg["resolution"] == "original":
                size = tuple(v // 2 * 2 for v in image.size)
                if cfg["direction"] == "custom":
                    cfg["resolution"] = "original"
            else:
                size = {"720p": (1280, 720), "1080p": (1920, 1080), "4k": (3840, 2160)}[cfg["resolution"]]
                if cfg["direction"] == "portrait":
                    size = size[::-1]
        cfg["template"] = str(template)
        cfg["rect"] = list(rect)
        cfg["outputDir"] = str(Path(cfg.get("outputDir") or Path.home() / "Videos" / "套版导出").expanduser().resolve())
        return cfg, list(size)

    def prepare_config(self, videos=None, config=None, sessionId=None):
        with file_lock(self.state / "sessions.lock"):
            previous = self.load_session(sessionId) if sessionId else None
            incoming = dict(config or {})
            preset_config = {}
            if "preset" in incoming:
                name = incoming.pop("preset")
                preset = next((p for p in self.presets() if p["name"] == name), None)
                if preset is None:
                    raise ValueError("未找到该预设。")
                preset_config = preset["config"]
            direction = incoming.get("direction", preset_config.get("direction", previous["config"]["direction"] if previous else "landscape"))
            if not previous or direction != previous["config"]["direction"]:
                saved_profiles = read_json(self.state / "profiles.json", {})
                cfg = dict(DEFAULTS, **saved_profiles.get(direction, self.migrate(direction)))
                cfg.update(direction=direction, templateId="portrait" if direction == "portrait" else "landscape")
            else:
                cfg = dict(previous["config"])
                if any(cfg.get("template") == t["path"] for t in previous["templates"]):
                    cfg.pop("template", None)
            incoming = dict(preset_config, **incoming)
            if ("template" in incoming or "templateId" in incoming or "detection" in incoming) and "rect" not in incoming:
                cfg["rect"] = None
            if "templateId" in incoming and "template" not in incoming:
                cfg.pop("template", None)
            cfg.update(incoming)
            rect_source = ("auto" if incoming["rect"] is None else "manual") if "rect" in incoming else (
                "auto" if cfg.get("rect") is None else previous.get("rectSource", "manual") if previous else "manual")
            if rect_source == "auto":
                cfg["rect"] = None
            cfg, size = self.validate(cfg)
            if videos is None:
                videos = [v["path"] for v in previous["videos"]] if previous else []
            if not isinstance(videos, list) or any(not isinstance(v, str) or not v.strip() for v in videos):
                raise ValueError("videos 必须是视频路径数组。")
            expanded = []
            extensions = {".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".ts", ".wmv"}
            for value in videos:
                path = Path(value).expanduser().resolve()
                expanded.extend(sorted(p for p in path.iterdir() if p.is_file() and p.suffix.lower() in extensions) if path.is_dir() else [path])
            items, seen = [], set()
            for path in expanded:
                identity = os.path.normcase(str(path))
                if identity in seen:
                    continue
                seen.add(identity)
                item = dict(path=str(path), name=path.name)
                try:
                    if not path.is_file():
                        raise ValueError("视频不存在或当前不可访问。")
                    item["info"] = core.probe(path)
                except Exception as exc:
                    item["error"] = str(exc)
                items.append(item)
            session = dict(sessionId=sessionId or uuid.uuid4().hex, version=self.version, config=cfg,
                           videos=items, outputSize=size, rect=cfg["rect"], rectSource=rect_source, templates=self.templates(), presets=self.presets())
            atomic_json(self.session_path(session["sessionId"]), session)
            saved = dict(cfg)
            if any(saved["template"] == t["path"] for t in session["templates"]):
                saved.pop("template", None)
            if rect_source == "auto":
                saved["rect"] = None
            profiles = read_json(self.state / "profiles.json", {})
            profiles[direction] = saved
            atomic_json(self.state / "profiles.json", profiles)
            return session

    def update_config(self, sessionId, config):
        return self.prepare_config(None, config, sessionId)

    def preview_frame(self, sessionId, videoIndex=0, seconds=1):
        import base64
        session = self.load_session(sessionId)
        if not isinstance(videoIndex, int) or isinstance(videoIndex, bool) or not 0 <= videoIndex < len(session["videos"]):
            raise ValueError("请选择队列中的视频。")
        item = session["videos"][videoIndex]
        if item.get("error"):
            raise ValueError(item["error"])
        seconds = float(seconds)
        if not math.isfinite(seconds) or seconds < 0:
            raise ValueError("预览时间需要大于或等于 0。")
        info = item["info"]
        # Seeking after the last frame's PTS can yield no image, especially at low FPS.
        last_frame = max(0, (math.ceil(info["duration"] * info["fps"]) - 1) / info["fps"])
        seconds = min(seconds, math.floor(last_frame * 1e6) / 1e6)
        cfg = session["config"]
        image = core.preview(item["path"], cfg["template"], cfg["rect"], session["outputSize"], cfg["fit"], seconds, cfg["zoom"])
        image.thumbnail((1200, 1200))
        stream = io.BytesIO()
        image.save(stream, format="JPEG", quality=88)
        data = stream.getvalue()
        preview_path = self.state / "previews" / (sessionId + ".jpg")
        preview_path.parent.mkdir(parents=True, exist_ok=True)
        preview_path.write_bytes(data)
        return dict(sessionId=sessionId, version=session["version"], seconds=seconds, previewPath=str(preview_path),
                    previewDataUrl="data:image/jpeg;base64," + base64.b64encode(data).decode())

    def _command(self, job_id):
        if getattr(sys, "frozen", False):
            command = [sys.executable]
        else:
            command = [sys.executable, str(Path(__file__).resolve())]
        return command + ["--runtime-root", str(self.root), "--state-dir", str(self.state), "--job", job_id]

    def _start(self, task):
        atomic_json(self.task_path(task["taskId"]), task)
        flags = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
        log = self.state / "tasks" / (task["taskId"] + ".stderr.log")
        with open(log, "ab") as errors:
            proc = subprocess.Popen(self._command(task["taskId"]), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                    stderr=errors, creationflags=flags, start_new_session=os.name != "nt")
        # Worker owns later state; do not overwrite progress after spawning.
        return dict(task, pid=proc.pid)

    def export_videos(self, sessionId, requestId):
        if not isinstance(requestId, str) or not 1 <= len(requestId) <= 200:
            raise ValueError("导出需要稳定的 requestId；重试时沿用同一个编号。")
        task_id = hashlib.sha256((sessionId + "\0" + requestId).encode()).hexdigest()[:32]
        with file_lock(self.state / "submit.lock"):
            existing = read_json(self.task_path(task_id))
            if existing:
                return self.get_task(task_id)
            session = self.load_session(sessionId)
            if not session["videos"]:
                raise ValueError("请先添加视频。")
            # Revalidate files and freeze settings before starting a long-running process.
            cfg, size = self.validate(session["config"])
            # A custom source image may be edited or removed during a long batch.
            # Each task owns its immutable template snapshot as well as frozen parameters.
            task_assets = self.state / "tasks" / task_id
            task_assets.mkdir(parents=True, exist_ok=True)
            template_snapshot = task_assets / ("template" + Path(cfg["template"]).suffix.lower())
            shutil.copyfile(cfg["template"], template_snapshot)
            cfg["template"] = str(template_snapshot)
            task = dict(taskId=task_id, sessionId=sessionId, version=self.version, runtimeRoot=str(self.root),
                        status="queued", progress=0, config=cfg, outputSize=size, createdAt=time.time(),
                        items=[dict(path=v["path"], name=v["name"], status="failed" if v.get("error") else "queued",
                                    progress=0, **({"error": v["error"]} if v.get("error") else {})) for v in session["videos"]])
            return self._start(task)

    def get_task(self, taskId):
        task = read_json(self.task_path(taskId))
        if task is None:
            raise ValueError("任务不存在。")
        stale = task["status"] in ("running", "queued") and (
            (task.get("pid") and not pid_alive(task["pid"])) or
            (not task.get("pid") and time.time() - task["createdAt"] > 30))
        if stale:
            task = dict(task, status="interrupted")
            # Read-only query reports interruption; resume is the explicit state mutation.
        return task

    def cancel_task(self, taskId):
        task = self.get_task(taskId)
        if task["status"] in ("running", "queued"):
            self.task_path(taskId).with_suffix(".cancel").touch()
            return dict(task, cancelRequested=True)
        return task

    def resume_task(self, taskId):
        with file_lock(self.state / "submit.lock"):
            task = self.get_task(taskId)
            if task["status"] in ("running", "queued", "completed"):
                return task
            if Path(task["runtimeRoot"]).resolve() != self.root:
                raise ValueError("继续任务必须使用任务原来的运行版本。")
            self.task_path(taskId).with_suffix(".cancel").unlink(missing_ok=True)
            for item in task["items"]:
                if item["status"] != "completed":
                    item.update(status="queued", progress=0)
                    item.pop("error", None)
            task.update(status="queued", createdAt=time.time())
            task.pop("pid", None)
            return self._start(task)

    def manage_presets(self, action, name=None, sessionId=None):
        if action == "list":
            return {"presets": self.presets()}
        if action not in ("save", "delete") or not isinstance(name, str) or not 1 <= len(name.strip()) <= 100:
            raise ValueError("请选择 save/delete 并填写预设名称。")
        if any(p["name"] == name and p["scope"] == "shared" for p in self.presets()):
            raise ValueError("共享预设随版本发布，请为个人预设使用不同名称。")
        with file_lock(self.state / "presets.lock"):
            path = self.state / "presets.json"
            personal = read_json(path, {})
            if action == "save":
                session = self.load_session(sessionId)
                cfg = dict(session["config"])
                if session.get("rectSource") == "auto":
                    cfg["rect"] = None
                # Built-in template path resolves against the current release at next use.
                if any(cfg.get("template") == t["path"] for t in self.templates()):
                    cfg.pop("template", None)
                personal[name] = cfg
            else:
                personal.pop(name, None)
            atomic_json(path, personal)
        return {"presets": self.presets()}

    def open_output(self, taskId, videoIndex=None):
        task = self.get_task(taskId)
        if videoIndex is None:
            destination = Path(task["config"]["outputDir"])
        else:
            if not isinstance(videoIndex, int) or not 0 <= videoIndex < len(task["items"]):
                raise ValueError("视频序号无效。")
            destination = Path(task["items"][videoIndex].get("output", ""))
        if not destination.exists() or (videoIndex is not None and not destination.is_file()):
            raise ValueError("输出尚未生成。")
        if os.name == "nt":
            os.startfile(str(destination))
        return {"path": str(destination), "opened": os.name == "nt"}

    def run_job(self, taskId):
        task_path = self.task_path(taskId)
        with file_lock(task_path.with_suffix(".lock")):
            task = read_json(task_path)
            if task["status"] == "completed":
                return
            task.update(pid=os.getpid(), status="running")
            atomic_json(task_path, task)
            cancel = threading.Event()
            done = threading.Event()
            def watch():
                while not done.wait(.15):
                    if task_path.with_suffix(".cancel").exists():
                        cancel.set()
            watcher = threading.Thread(target=watch, daemon=True)
            watcher.start()
            cfg = task["config"]
            last_save = [0.0]
            def save(force=False):
                now = time.monotonic()
                if force or now - last_save[0] > .25:
                    task["progress"] = sum(i["progress"] for i in task["items"]) / len(task["items"])
                    atomic_json(task_path, task)
                    last_save[0] = now
            try:
                for item in task["items"]:
                    if item["status"] in ("completed", "failed"):
                        continue
                    if cancel.is_set() or task_path.with_suffix(".cancel").exists():
                        cancel.set()
                        break
                    item.update(status="running", progress=0)
                    save(True)
                    def progress(value):
                        item["progress"] = value
                        save()
                    def backend(value, note):
                        item.update(backend=value, backendNote=note)
                        save(True)
                    try:
                        output = core.export(item["path"], cfg["template"], cfg["rect"], task["outputSize"], cfg["fit"],
                                             cfg["outputDir"], 18 if cfg["quality"] == "high" else 23,
                                             cancel=cancel, progress=progress, backend=cfg["backend"], on_backend=backend,
                                             zoom=cfg["zoom"])
                        item.update(status="completed", progress=1, output=str(output))
                    except core.Cancelled:
                        item.update(status="cancelled")
                        break
                    except Exception as exc:
                        item.update(status="failed", error=str(exc), progress=1)
                    save(True)
                if cancel.is_set():
                    task["status"] = "cancelled"
                else:
                    completed = sum(i["status"] == "completed" for i in task["items"])
                    task["status"] = "completed" if completed == len(task["items"]) else "partial" if completed else "failed"
                save(True)
            finally:
                done.set()
                watcher.join(timeout=1)


def main():
    for stream in (sys.stdin, sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime-root", required=True)
    parser.add_argument("--state-dir", required=True)
    parser.add_argument("--job")
    args = parser.parse_args()
    engine = Engine(args.runtime_root, args.state_dir)
    if args.job:
        engine.run_job(args.job)
        return
    try:
        request = json.load(sys.stdin)
        allowed = ("prepare_config", "update_config", "preview_frame", "export_videos", "get_task", "cancel_task",
                   "resume_task", "manage_presets", "open_output")
        if request.get("action") not in allowed:
            raise ValueError("未知处理操作。")
        result = getattr(engine, request["action"])(**request.get("arguments", {}))
        print(json.dumps({"ok": True, "data": result}, ensure_ascii=False))
    except Exception as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, ensure_ascii=False))
        sys.exit(1)


if __name__ == "__main__":
    main()
