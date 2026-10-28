"""FrameFit local worker. JSON requests on stdin; durable, version-pinned export jobs."""
from __future__ import annotations

import argparse
import base64
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
import framefit_workflow as workflow
import naming

ID = re.compile(r"^[a-zA-Z0-9_-]{1,100}$")
DEFAULTS = dict(direction="landscape", templateId="landscape", detection="auto", rect=None,
                fit="cover", zoom=100, resolution="1080p", quality="high", backend="auto",
                language="auto", otherLanguage="", nameMode="compact", outputGroup="folder",
                borderPolicy="ask", templateConfirmed=False, outputSize=None,
                namePattern=naming.DEFAULT_PATTERN,nameDate='',nameProject='',nameGame='')
CHOICES = dict(direction=("landscape", "portrait", "custom"), detection=("auto", "black", "white", "transparent"),
               fit=("cover", "contain", "stretch", "zoom"), resolution=("720p", "1080p", "4k", "original"),
               quality=("high", "standard"), backend=("auto", "nvenc", "cpu"),
               language=("auto", "简中", "繁中", "英", "日", "韩", "other", "none"),
               nameMode=("compact", "full", "custom"), outputGroup=("folder", "project", "template"),
               borderPolicy=("ask", "keep", "remove"))
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
        self.state.mkdir(parents=True, exist_ok=True)

    def session_path(self, session_id):
        return self.state / "sessions" / (valid_id(session_id) + ".json")

    def task_path(self, task_id):
        return self.state / "tasks" / (valid_id(task_id) + ".json")

    def desktop_settings_path(self):
        return Path(os.getenv("LOCALAPPDATA", str(Path.home()))) / "FrameFit" / "settings.json"

    def desktop_settings(self):
        value = read_json(self.desktop_settings_path(), {})
        return value if isinstance(value, dict) else {}

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
        if cfg['nameMode']=='custom':
            naming.validate_pattern(cfg['namePattern'])
        from datetime import date
        cfg['nameDate']=cfg.get('nameDate') or date.today().isoformat()
        try:
            date.fromisoformat(cfg['nameDate'])
        except (ValueError,TypeError):
            raise ValueError('命名日期需要 YYYY-MM-DD 格式。') from None
        requested_size = cfg.get('outputSize')
        if requested_size is not None and (not isinstance(requested_size, list) or len(requested_size)!=2 or
                any(isinstance(value,bool) or not isinstance(value,int) or value<2 or value>16384 or value%2 for value in requested_size)):
            raise ValueError('outputSize 需要两个 2～16384 的正偶数。')
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
        if requested_size:
            size = tuple(requested_size)
        cfg["template"] = str(template)
        cfg["rect"] = list(rect)
        cfg["outputDir"] = str(Path(cfg.get("outputDir") or Path.home() / "Videos" / "套版导出").expanduser().resolve())
        return cfg, list(size)

    def prepare_config(self, videos=None, config=None, sessionId=None, itemConfigs=None, outputTemplates=None):
        with file_lock(self.state / "sessions.lock"):
            previous = self.load_session(sessionId) if sessionId else None
            incoming = dict(config or {})
            desktop_variants=None
            preset_config = {}
            if "preset" in incoming:
                name = incoming.pop("preset")
                preset = next((p for p in self.presets() if p["name"] == name), None)
                if preset is None:
                    desktop_recipe = self.desktop_settings().get("recipes", {}).get(name)
                    if isinstance(desktop_recipe, dict):
                        preset = {"config": desktop_recipe.get("engineConfig") or desktop_recipe.get("config")}
                        if outputTemplates is None and desktop_recipe.get('outputTemplates') and desktop_recipe.get('exportMode','auto') in ('multi','auto'):
                            outputTemplates = desktop_recipe['outputTemplates']
                if preset is None:
                    raise ValueError("未找到该预设。")
                preset_config = preset["config"]
                preset_config = {k: v for k, v in preset_config.items() if k not in ('templateConfirmed', 'borderPolicy')}
            if outputTemplates is None and itemConfigs is None and previous is None and not incoming.get('template'):
                settings = self.desktop_settings()
                if settings.get('exportMode', 'multi' if settings.get('outputTemplates') else 'single') in ('multi','auto'):
                    desktop_variants = settings.get('outputTemplates') or None
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
                if previous and itemConfigs is None:
                    itemConfigs = [dict(item.get('overrides',{}), outputKey=item.get('outputKey','')) for item in previous['videos']]
            if itemConfigs is not None and (not isinstance(itemConfigs,list) or any(not isinstance(v,dict) for v in itemConfigs)):
                raise ValueError('itemConfigs 需要配置对象数组。')
            if not isinstance(videos, list) or any(not isinstance(v, str) or not v.strip() for v in videos):
                raise ValueError("videos 必须是视频路径数组。")
            if outputTemplates is not None:
                if not isinstance(outputTemplates,list) or not 1<=len(outputTemplates)<=100 or any(not isinstance(v,dict) for v in outputTemplates):
                    raise ValueError('outputTemplates 需要 1～100 个套版设置。')
                if itemConfigs is not None:
                    raise ValueError('outputTemplates 与 itemConfigs 不能同时提供。')
                source_paths = []
                for value in videos:
                    path = Path(value).expanduser().resolve()
                    source_paths.extend(sorted(p for p in path.iterdir() if p.is_file() and p.suffix.lower() in {'.mp4','.mov','.mkv','.avi','.webm','.m4v','.ts','.wmv'}) if path.is_dir() else [path])
                sources = list(dict.fromkeys(str(path) for path in source_paths))
                videos = [path for path in sources for variant in outputTemplates]
                itemConfigs = [dict(variant, outputKey=str(index), templateConfirmed=True)
                               for path in sources for index,variant in enumerate(outputTemplates)]
            expanded = []
            extensions = {".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v", ".ts", ".wmv"}
            for value in videos:
                path = Path(value).expanduser().resolve()
                expanded.extend(sorted(p for p in path.iterdir() if p.is_file() and p.suffix.lower() in extensions) if path.is_dir() else [path])
            items, seen = [], set()
            retained_overrides = []
            for position,path in enumerate(expanded):
                overrides = itemConfigs[position] if itemConfigs is not None and len(itemConfigs)==len(expanded) else {}
                output_key = overrides.get('outputKey', '')
                if not isinstance(output_key,str) or len(output_key)>100:
                    raise ValueError('outputKey 需要最多 100 字符的字符串。')
                identity = (os.path.normcase(str(path)),output_key)
                if identity in seen:
                    continue
                seen.add(identity)
                item = dict(path=str(path), name=path.name, outputKey=output_key)
                retained_overrides.append(overrides)
                try:
                    if not path.is_file():
                        raise ValueError("视频不存在或当前不可访问。")
                    item["info"] = core.probe(path)
                except Exception as exc:
                    item["error"] = str(exc)
                items.append(item)
            if itemConfigs is not None and (not isinstance(itemConfigs, list) or len(itemConfigs) not in (len(items),len(expanded)) or any(not isinstance(v, dict) for v in itemConfigs)):
                raise ValueError('itemConfigs 需要与去重后的视频队列一一对应。')
            if itemConfigs is not None and len(itemConfigs)==len(expanded):
                itemConfigs = retained_overrides
            desktop_workflow = bool(self.desktop_settings().get('machine'))
            if previous is None and itemConfigs is None:
                prepared=[]
                prepared_overrides=[]
                for item in items:
                    match=None
                    variants=desktop_variants
                    if desktop_workflow and self.desktop_settings().get('machine',{}).get('matchPaths',True) and not cfg.get('templateConfirmed') and not item.get('error'):
                        requested=cfg.get('outputSize') or (size if cfg['resolution']!='original' and cfg['direction']!='custom' else None)
                        match=self.match_project_templates(item['name'],direction=cfg['direction'],outputSize=requested)
                        if match['status']=='matched':
                            variants=[{'template':candidate['path'],
                                       'rect':cfg['rect'] if incoming.get('rect') is not None else None,
                                       'direction':cfg['direction'],'resolution':cfg['resolution'],
                                       'outputSize':requested or [candidate['width']//2*2,candidate['height']//2*2]}
                                      for candidate in match['candidates']]
                    for number,variant in enumerate(variants or [{}]):
                        clone=dict(item)
                        override=dict(variant)
                        if variants:
                            clone['outputKey']=str(number)
                            override.update(outputKey=str(number),templateConfirmed=True)
                        if match is not None:
                            clone['match']=match
                            clone['requiresTemplateReview']=match['status']!='matched'
                            override.update(nameProject=match.get('projectFolder',''),nameGame=match.get('game',''))
                        prepared.append(clone)
                        prepared_overrides.append(override)
                if any(prepared_overrides):
                    items=prepared
                    itemConfigs=prepared_overrides
            previous_items = {(item['path'],item.get('outputKey','')): item for item in previous.get('videos', [])} if previous else {}
            for index, item in enumerate(items):
                item['sequence']=index+1
                overrides = dict(itemConfigs[index]) if itemConfigs is not None else (
                    dict(previous_items.get((item['path'],item.get('outputKey','')), {}).get('overrides', {})))
                item['overrides'] = dict(overrides)
                if item.get('error'):
                    continue
                item_cfg = dict(cfg)
                item_rect_source = rect_source
                output_name = overrides.pop('outputName', None)
                overrides.pop('outputKey', None)
                if output_name:
                    item['outputName'] = self.safe_filename(output_name)
                if 'template' in overrides and 'rect' not in overrides:
                    item_cfg['rect'] = None
                item_cfg.update(overrides)
                if 'rect' in overrides:
                    item_rect_source = 'auto' if overrides['rect'] is None else 'manual'
                if desktop_workflow and 'match' not in item and self.desktop_settings().get('machine',{}).get('matchPaths',True) and not (item_cfg.get('templateConfirmed') or 'template' in overrides):
                    query_size = None
                    if item_cfg['resolution'] != 'original' and item_cfg['direction'] != 'custom':
                        query_size = list({'720p':(1280,720), '1080p':(1920,1080), '4k':(3840,2160)}[item_cfg['resolution']])
                        if item_cfg['direction'] == 'portrait':
                            query_size.reverse()
                    match = self.match_project_templates(item['name'], direction=item_cfg['direction'],
                        outputSize=query_size)
                    item['match'] = match
                    if match['status'] == 'matched':
                        chosen = match['candidates'][0]
                        chosen_identity = os.path.normcase(str(Path(chosen['path']).resolve()))
                        same_template = os.path.normcase(str(Path(item_cfg.get('template', '')).resolve())) == chosen_identity
                        prior = previous_items.get((item['path'],item.get('outputKey','')), {})
                        prior_cfg = prior.get('config', {})
                        carry_manual = (prior.get('rectSource') == 'manual' and
                            os.path.normcase(str(Path(prior_cfg.get('template', '')).resolve())) == chosen_identity and
                            not any(key in incoming for key in ('rect', 'template', 'templateId', 'detection')) and 'rect' not in overrides)
                        if carry_manual:
                            item_cfg['rect'] = prior_cfg['rect']
                        explicit_rect = incoming.get('rect') is not None or overrides.get('rect') is not None or (
                            same_template and item_cfg.get('rect') is not None and rect_source == 'manual') or carry_manual
                        item_cfg.update(template=chosen['path'], rect=item_cfg['rect'] if explicit_rect else None)
                        item_rect_source = 'manual' if explicit_rect else 'auto'
                    else:
                        item['requiresTemplateReview'] = True
                item['config'], item['outputSize'] = self.validate(item_cfg)
                item['rectSource'] = item_rect_source
                item['plannedOutput'] = self.planned_output(item)
            session = dict(sessionId=sessionId or uuid.uuid4().hex, version=self.version, config=cfg,
                           videos=items, workflowEnabled=desktop_workflow, outputSize=size, rect=cfg["rect"], rectSource=rect_source, templates=self.templates(), presets=self.presets())
            atomic_json(self.session_path(session["sessionId"]), session)
            saved = dict(cfg)
            saved['templateConfirmed'] = False
            saved['borderPolicy'] = 'ask'
            if any(saved["template"] == t["path"] for t in session["templates"]):
                saved.pop("template", None)
            if rect_source == "auto":
                saved["rect"] = None
            profiles = read_json(self.state / "profiles.json", {})
            profiles[direction] = saved
            atomic_json(self.state / "profiles.json", profiles)
            return session

    def update_config(self, sessionId, config, itemConfigs=None):
        return self.prepare_config(None, config, sessionId, itemConfigs)

    @staticmethod
    def safe_filename(value):
        if not isinstance(value, str) or not value.strip() or value.endswith((' ', '.')) or re.search(r'[<>:"/\\|?*\x00-\x1f]', value):
            raise ValueError('名称不可包含路径或 Windows 文件名禁用字符。')
        if value.split('.')[0].upper() in {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1,10)), *(f'LPT{i}' for i in range(1,10))}:
            raise ValueError('名称不能使用 Windows 保留名称。')
        return value

    def planned_output(self, item):
        cfg, size = item['config'], item['outputSize']
        parsed = workflow.parse_video_name(item['name'])
        language = workflow.output_language(item['name'], cfg.get('template', '')) if cfg['language'] == 'auto' else cfg['otherLanguage'] if cfg['language'] == 'other' else '' if cfg['language'] == 'none' else cfg['language']
        name = item.get('outputName') or workflow.output_filename(item['name'], size, language)
        project = item.get('match', {}).get('projectFolder') or cfg.get('nameProject') or parsed['projectFolder']
        if cfg['nameMode']=='custom' and not item.get('outputName'):
            name=naming.render_name(cfg['namePattern'],item['name'],size,language=language,
                template=cfg.get('template',''),project=project,game=item.get('match',{}).get('game') or cfg.get('nameGame',''),
                date=cfg.get('nameDate',''),index=item.get('sequence',1))
        if cfg['nameMode'] == 'full' and not item.get('outputName'):
            ratio = math.gcd(*size)
            name = f"{parsed['baseName']}_{project}_{language or '无语言'}_{size[0]//ratio}-{size[1]//ratio}.mp4"
        self.safe_filename(name)
        if not name.lower().endswith('.mp4'):
            raise ValueError('建议文件名必须以 .mp4 结尾。')
        directory = Path(cfg['outputDir'])
        if cfg['outputGroup'] == 'project':
            directory /= self.safe_filename(project)
        elif cfg['outputGroup'] == 'template':
            directory /= self.safe_filename(Path(cfg['template']).stem)
        return str(directory / name)

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
        cfg = item.get('config', session['config'])
        image = core.preview(item["path"], cfg["template"], cfg["rect"], item.get('outputSize', session['outputSize']), cfg["fit"], seconds, cfg["zoom"])
        image.thumbnail((1200, 1200))
        stream = io.BytesIO()
        image.save(stream, format="JPEG", quality=88)
        data = stream.getvalue()
        preview_path = self.state / "previews" / (sessionId + ".jpg")
        preview_path.parent.mkdir(parents=True, exist_ok=True)
        preview_path.write_bytes(data)
        return dict(sessionId=sessionId, version=session["version"], seconds=seconds, previewPath=str(preview_path),
                    previewDataUrl="data:image/jpeg;base64," + base64.b64encode(data).decode())

    def thumbnail(self, sessionId, videoIndex=0, seconds=0):
        session = self.load_session(sessionId)
        if not isinstance(videoIndex, int) or isinstance(videoIndex, bool) or not 0 <= videoIndex < len(session["videos"]):
            raise ValueError("请选择队列中的视频。")
        item = session["videos"][videoIndex]
        if item.get("error"):
            raise ValueError(item["error"])
        seconds = float(seconds)
        if not math.isfinite(seconds) or seconds < 0:
            raise ValueError('缩略图时间需要是非负有限数字。')
        seconds = min(seconds, max(0.0, item["info"]["duration"] - .15))
        image = core.thumbnail(item["path"], seconds)
        stream = io.BytesIO()
        image.save(stream, format="JPEG", quality=76)
        return dict(sessionId=sessionId, version=session["version"], seconds=seconds,
                    thumbnailDataUrl="data:image/jpeg;base64," + base64.b64encode(stream.getvalue()).decode())

    def source_borders(self, video, on_progress=None):
        path = Path(video)
        stat = path.stat()
        key = hashlib.sha256(f"edges-v3\0{path.resolve()}\0{stat.st_size}\0{stat.st_mtime_ns}".encode()).hexdigest()
        cache = self.state / "analysis" / (key + ".json")
        with file_lock(cache.with_suffix('.lock')):
            source = read_json(cache)
            if source is None:
                source = core.analyze_borders(path, on_progress=on_progress) if on_progress else core.analyze_borders(path)
                atomic_json(cache, source)
        if on_progress:
            on_progress(1.0)
        return source

    def analyze_borders(self, sessionId, videoIndex=0):
        session = self.load_session(sessionId)
        if not isinstance(videoIndex, int) or isinstance(videoIndex, bool) or not 0 <= videoIndex < len(session['videos']):
            raise ValueError('请选择队列中的视频。')
        item = session['videos'][videoIndex]
        if item.get('error'):
            raise ValueError(item['error'])
        path = Path(item['path'])
        source = self.source_borders(path)
        cfg = item.get('config', session['config'])
        composite = core.analyze_composite_borders(path, cfg["template"], cfg["rect"],
                                                   item.get('outputSize', session['outputSize']), cfg["fit"], cfg["zoom"],
                                                   source.get("segments", []))
        return dict(sessionId=sessionId, version=session["version"], source=source,
                    composite=composite, newBorders=composite["newBorders"])

    def match_project_templates(self, filename, outputSize=None, direction=None):
        if not isinstance(filename, str) or not filename.strip():
            raise ValueError("请提供视频文件名。")
        if direction not in (None, 'landscape', 'portrait', 'custom'):
            raise ValueError('输出方向无效。')
        if outputSize is not None and (not isinstance(outputSize, list) or len(outputSize) != 2 or
                any(isinstance(value, bool) or not isinstance(value, int) or value < 2 for value in outputSize)):
            raise ValueError('输出尺寸需要两个正整数。')
        legacy = Path(os.getenv("LOCALAPPDATA", str(Path.home()))) / "FrameFit" / "settings.json"
        config = read_json(legacy, {}) or {}
        machine = config.get("machine", {})
        schedule = machine.get("scheduleRoot", "")
        template = machine.get("templateRoot", "")
        mapping = dict(machine, projects=machine.get("projects", {}))
        return workflow.find_project_candidates(filename, schedule, template, mapping, output_size=outputSize, direction=direction)

    def manage_recipes(self, action, name=None, sessionId=None):
        desktop_path = self.desktop_settings_path()
        if action == "list":
            local = self.desktop_settings().get("recipes", {})
            return {"recipes": [dict(name=k, config=v)
                                for k, v in local.items() if isinstance(v, dict)]}
        if action not in ("save", "delete") or not isinstance(name, str) or not 1 <= len(name.strip()) <= 100:
            raise ValueError("请选择 list/save/delete 并填写配方名称。")
        with file_lock(self.state / "recipes.lock"):
            settings = self.desktop_settings()
            recipes = settings.get("recipes", {}) if isinstance(settings.get("recipes"), dict) else {}
            if action == "save":
                if not sessionId:
                    raise ValueError("保存配方需要 sessionId。")
                session = self.load_session(sessionId)
                direction = session["config"]["direction"]
                profile = settings.get("profiles", {}).get(direction, {})
                recipe = recipes.get(name.strip(), {})
                recipe.update(direction=direction, profile=profile,
                              engineConfig=dict(session["config"], resolution=("original" if session["config"]["resolution"] == "original" else session["config"]["resolution"])),
                              quality="高画质" if session["config"]["quality"] == "high" else "标准画质（文件更小）",
                              backend={"auto": "自动（优先 GPU）", "nvenc": "NVIDIA GPU · NVENC", "cpu": "CPU · 兼容模式"}[session["config"]["backend"]],
                              output=session["config"]["outputDir"], language=settings.get("language", "自动识别"),
                              otherLanguage=settings.get("otherLanguage", ""), nameMode=settings.get("nameMode", "精简命名"),
                              namePattern=session['config'].get('namePattern',naming.DEFAULT_PATTERN),
                              exportMode='auto',outputTemplates=list({item.get('outputKey',''):item['config'] for item in session['videos'] if item.get('outputKey') and item.get('config')}.values()),
                              outputGroup=settings.get("outputGroup", "统一文件夹"))
                recipes[name.strip()] = recipe
            else:
                recipes.pop(name.strip(), None)
            settings["recipes"] = recipes
            atomic_json(desktop_path, settings)
        return {"recipes": [dict(name=k, config=v) for k, v in recipes.items()]}

    def _command(self, job_id):
        if getattr(sys, "frozen", False):
            worker = self.root / "worker" / ("FrameFitWorker.exe" if os.name == "nt" else "FrameFitWorker")
            if not worker.is_file():
                raise RuntimeError("本机 FrameFit Worker 未安装，请重新安装软件包。")
            command = [str(worker)]
        else:
            command = [sys.executable, str(Path(__file__).resolve())]
        return command + ["--runtime-root", str(self.root), "--state-dir", str(self.state), "--job", job_id]

    def _start(self, task):
        atomic_json(self.task_path(task["taskId"]), task)
        flags = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
        log = self.state / "tasks" / (task["taskId"] + ".stderr.log")
        with open(log, "ab") as errors:
            environment = dict(os.environ)
            environment['PYINSTALLER_RESET_ENVIRONMENT'] = '1'
            if (self.root / 'bin').is_dir():
                environment['FRAMEFIT_BIN_DIR'] = str(self.root / 'bin')
            proc = subprocess.Popen(self._command(task["taskId"]), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                    stderr=errors, env=environment, creationflags=flags, start_new_session=os.name != "nt")
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
            shared_snapshot = task_assets / ('template' + Path(cfg['template']).suffix.lower())
            shutil.copyfile(cfg['template'], shared_snapshot)
            frozen_items = []
            for index, video in enumerate(session['videos']):
                item = dict(video, status='failed' if video.get('error') else 'queued', progress=0)
                if not video.get('error'):
                    if video.get('requiresTemplateReview'):
                        raise ValueError(f"{video['name']}：{video.get('match', {}).get('message') or '项目套版需要确认'}；请选择候选或确认使用上次模板。")
                    item_cfg, item_size = self.validate(video.get('config', cfg))
                    if session.get('workflowEnabled') and item_cfg['borderPolicy'] != 'keep':
                        report = self.analyze_borders(sessionId, index)
                        if report['newBorders']:
                            if item_cfg['borderPolicy'] == 'ask':
                                raise ValueError(f"{video['name']}：检测到 {len(report['newBorders'])} 处套版新增黑白边；请查看 analyze_borders 并选择 borderPolicy=keep 或 remove。")
                            if item_cfg['borderPolicy'] == 'remove':
                                correction = core.remove_composite_borders(video['path'], item_cfg['template'], item_cfg['rect'], item_size,
                                                                          item_cfg['fit'], item_cfg['zoom'], report['source']['segments'])
                                item_cfg.update(fit=correction['fit'], zoom=correction['zoom'])
                        item['borderReport'] = report
                    snapshot = task_assets / (f'template-{index}' + Path(item_cfg['template']).suffix.lower())
                    shutil.copyfile(item_cfg['template'], snapshot)
                    item.update(config=item_cfg, outputSize=item_size, sourceTemplate=item_cfg['template'])
                    item['plannedOutput'] = self.planned_output(item)
                    item_cfg['template'] = str(snapshot)
                frozen_items.append(item)
            cfg['template'] = str(shared_snapshot)
            task = dict(taskId=task_id, sessionId=sessionId, version=self.version, runtimeRoot=str(self.root),
                        status="queued", progress=0, config=cfg, outputSize=size, createdAt=time.time(),
                        items=frozen_items)
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

    def resume_task(self, taskId, excludeIndices=None):
        with file_lock(self.state / "submit.lock"):
            task = self.get_task(taskId)
            if task["status"] in ("running", "queued", "completed"):
                return task
            if excludeIndices:
                if not isinstance(excludeIndices,list) or any(type(i) is not int or not 0 <= i < len(task['items']) for i in excludeIndices):
                    raise ValueError('移除项目的编号无效。')
                remaining=[dict(item) for index,item in enumerate(task['items']) if index not in excludeIndices]
                if not remaining or all(item['status']=='completed' for item in remaining):
                    raise ValueError('没有可继续的未完成项目。')
                task=dict(task,taskId=uuid.uuid4().hex,resumedFrom=taskId,items=remaining)
            pinned_root = Path(task["runtimeRoot"]).resolve()
            if pinned_root != self.root:
                if not (pinned_root / "runtime.json").is_file():
                    raise ValueError("原任务版本已不可用，无法继续该任务。")
                original_root = core.ROOT
                pinned = Engine(pinned_root, self.state)
                self.task_path(task['taskId']).with_suffix(".cancel").unlink(missing_ok=True)
                for item in task["items"]:
                    if item["status"] != "completed":
                        item.update(status="queued", progress=0)
                        item.pop("error", None)
                task.update(status="queued", createdAt=time.time())
                task.pop("pid", None)
                try:
                    return pinned._start(task)
                finally:
                    core.ROOT = original_root
            self.task_path(task['taskId']).with_suffix(".cancel").unlink(missing_ok=True)
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
                cfg['templateConfirmed'] = False
                cfg['borderPolicy'] = 'ask'
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
                        item_cfg = item.get('config', cfg)
                        destination = Path(item.get('plannedOutput') or item_cfg['outputDir'])
                        output = core.export(item["path"], item_cfg["template"], item_cfg["rect"], item.get('outputSize', task['outputSize']), item_cfg["fit"],
                                             destination.parent if item.get('plannedOutput') else destination,
                                             18 if item_cfg["quality"] == "high" else 23,
                                             cancel=cancel, progress=progress, backend=item_cfg["backend"], on_backend=backend,
                                             zoom=item_cfg["zoom"], output_name=destination.name if item.get('plannedOutput') else None)
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
        allowed = ("prepare_config", "update_config", "preview_frame", "thumbnail", "analyze_borders",
                   "match_project_templates", "manage_recipes", "export_videos", "get_task", "cancel_task",
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
