"""Per-computer project matching, naming and border analysis helpers."""
from __future__ import annotations

import re
import os
from pathlib import Path

DIMENSIONS = re.compile(r"(?<!\d)(\d{2,5})\s*[xX×]\s*(\d{2,5})(?!\d)")
VERSION = re.compile(r"[\s._-]+(?:v|ver|version)\s*\d+(?:\.\d+)*(?=$|[\s._-])", re.I)
LANGUAGES = {"简中": "简中", "中文": "简中", "cn": "简中", "zh": "简中",
             "英": "英", "英语": "英", "en": "英", "日": "日", "日语": "日", "ja": "日",
             "韩": "韩", "韩语": "韩", "ko": "韩"}


def parse_video_name(filename: str) -> dict:
    """Parse dimension/version/language markers while preserving the base title."""
    stem = Path(filename).stem
    dims = list(DIMENSIONS.finditer(stem))
    source_size = tuple(map(int, dims[-1].groups())) if dims else None
    cleaned = DIMENSIONS.sub(" ", stem)
    cleaned = VERSION.sub(" ", cleaned)
    language = None
    trailing = re.search(r"(?:^|[\s._-])([^\s._-]+)$", cleaned.strip())
    if trailing and trailing.group(1).casefold() in LANGUAGES:
        language = LANGUAGES[trailing.group(1).casefold()]
        cleaned = cleaned[:trailing.start()]
    base = re.sub(r"\s+", " ", cleaned).strip(" ._- ") or stem
    project_folder = Path(base).name
    return {"baseName": base, "sourceSize": source_size, "language": language,
            "projectFolder": project_folder,
            "hadDimensions": bool(dims), "hadVersion": bool(VERSION.search(stem))}


def output_filename(filename: str, size: tuple[int, int], language: str = "") -> str:
    info = parse_video_name(filename)
    base = info["baseName"]
    language = (LANGUAGES.get(language.casefold(), language) if language else "")
    suffix = f" {language}" if language and language not in ("不添加", "无", "none") else ""
    return f"{base} {size[0]}X{size[1]}{suffix}.mp4"


def _normal(value: str) -> str:
    return re.sub(r"[^0-9a-z\u3400-\u9fff]+", "", value.casefold())


def find_project_candidates(filename: str, schedule_root: str, template_root: str,
                            local_mapping: dict | None = None, output_size=None, direction=None) -> dict:
    """Resolve an exact task-folder name to local template files; never silently pick ties."""
    info = parse_video_name(filename)
    project = info["projectFolder"]
    root = Path(schedule_root).expanduser()
    if not root.is_dir():
        return {"status": "root_unavailable", "projectFolder": project,
                "message": f"任务排期目录不可访问：{schedule_root}", "candidates": []}
    key = _normal(project)
    matches: list[Path] = []
    try:
        visited = 0
        for parent in root.iterdir():
            if not parent.is_dir():
                continue
            for current, dirs, _ in os.walk(parent):
                visited += 1
                if visited > 50000:
                    break
                dirs[:] = sorted(dirs, key=str.casefold)
                path = Path(current)
                if _normal(path.name) == key:
                    matches.append(path)
                if len(matches) >= 100:
                    break
            if len(matches) >= 100:
                break
    except OSError as exc:
        return {"status": "root_unavailable", "projectFolder": project,
                "message": str(exc), "candidates": []}
    if not matches:
        return {"status": "project_missing", "projectFolder": project,
                "message": f"排期目录内没有找到项目文件夹“{project}”。", "candidates": []}
    mapping = local_mapping or {}
    aliases = mapping.get("gameKeywords", [])
    output_keyword = _normal(mapping.get("outputKeyword", "输出"))
    local_root = Path(template_root).expanduser()
    candidate_dirs = []
    # Local rules are explicit per computer: each project can point to one local output root.
    project_map = mapping.get("projects", {})
    explicit = project_map.get(key) or next((value for name, value in project_map.items()
                                             if _normal(name) == key), None)
    selected_path = Path(explicit.get('path', '')) if isinstance(explicit, dict) else Path(explicit) if explicit else None
    if selected_path:
        candidate_dirs = [selected_path if selected_path.is_dir() else selected_path.parent]
    elif local_root.is_dir():
        # Match the actual schedule game's directory, not every project to the default game.
        game_tokens = [token for token in re.split(r'[-＋+_\s]+', matches[0].parent.name) if _normal(token)]
        # A shared studio keyword cannot establish a game's identity. Different local
        # aliases require the user's explicit project mapping above, rather than
        # replacing the actual schedule game with this computer's default keywords.
        wanted = [_normal(token) for token in (game_tokens or aliases)]
        try:
            visited = 0
            for current, dirs, _ in os.walk(local_root):
                visited += 1
                if visited > 50000:
                    break
                current_path = Path(current)
                dirs[:] = sorted(dirs, key=str.casefold)
                if output_keyword in _normal(current_path.name) and all(token in _normal(str(current_path)) for token in wanted):
                    candidate_dirs.append(current_path)
                    dirs[:] = []
        except OSError:
            candidate_dirs = []
    if not candidate_dirs:
        return {"status": "template_missing", "projectFolder": project,
                "projectPaths": [str(p) for p in matches], "message": "找到了排期项目，但本机没有匹配的套版输出目录。", "candidates": []}
    files = []
    extensions = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}
    for folder in candidate_dirs:
        try:
            visited = 0
            for current, dirs, names in os.walk(folder):
                visited += 1
                if visited > 10000:
                    break
                for name in names:
                    path = Path(current) / name
                    if path.suffix.casefold() not in extensions:
                        continue
                    try:
                        from PIL import Image
                        with Image.open(path) as image:
                            files.append({"path": str(path), "name": path.name,
                                          "width": image.width, "height": image.height,
                                          "projectPath": str(matches[0]), "modifiedAt": path.stat().st_mtime})
                    except (OSError, ValueError):
                        continue
        except OSError:
            continue
    if direction in ('portrait', 'landscape'):
        oriented = [item for item in files if (item['height'] > item['width'] if direction == 'portrait' else item['width'] > item['height'])]
        if not oriented:
            return {'status':'orientation_missing', 'projectFolder':project, 'projectPaths':[str(p) for p in matches],
                    'message':'本机项目没有当前输出方向的套版，请选择其他模板或调整输出方向。', 'candidates':files}
        files = oriented
    requested_size = tuple(output_size) if output_size is not None else info['sourceSize'] if direction is None else None
    if requested_size:
        matching_size = [item for item in files if (item["width"], item["height"]) == requested_size]
        if not matching_size:
            return {"status": "size_missing", "projectFolder": project,
                    "projectPaths": [str(p) for p in matches], "requestedSize": list(requested_size),
                    "message": f"本机项目目录中没有 {requested_size[0]}×{requested_size[1]} 套版。",
                    "candidates": files}
        files = matching_size
    if not files:
        return {"status": "template_missing", "projectFolder": project,
                "projectPaths": [str(p) for p in matches], "message": "本机输出目录中没有可用模板图片。", "candidates": []}
    if selected_path and not selected_path.exists():
        return {'status': 'template_updated', 'projectFolder': project, 'candidates': files,
                'message': '上次选择的套版已不存在，请确认新的候选，不能自动替换。'}
    if selected_path and selected_path.is_file():
        chosen = next((item for item in files if Path(item['path']).resolve() == selected_path.resolve()), None)
        if chosen and isinstance(explicit, dict):
            changed = chosen['modifiedAt'] != explicit.get('modifiedAt')
            newer = any(item['modifiedAt'] > explicit.get('selectedAt', 0) and item['path'] != chosen['path'] for item in files)
            if explicit.get('knownPaths'):
                newer = newer or any(item['path'] not in explicit['knownPaths'] for item in files)
            if changed or newer:
                return {'status': 'template_updated', 'projectFolder': project, 'candidates': files,
                        'message': '已选套版被修改或目录中出现更新的套版，请重新确认。', 'preselected': chosen['path']}
        if chosen:
            files = [chosen]
    # Files are candidates until their aspect/size and project scope are selected by the user.
    return {"status": "matched" if len(matches) == 1 and len(files) == 1 else "ambiguous",
            "projectFolder": project, "projectPaths": [str(p) for p in matches],
            "candidates": files, "message": "" if len(matches) == 1 and len(files) == 1 else "找到多个项目目录或套版，请选择。"}


def _edge_band(image, color: str, threshold: int = 20, fraction: float = .985):
    """Return solid near-black/white letterbox bands touching opposite canvas edges."""
    rgb = image.convert("RGB")
    rgb.thumbnail((640, 640))
    pixels = rgb.load()
    w, h = rgb.size
    if color == "black":
        matches = lambda p: max(p) <= threshold
    else:
        matches = lambda p: min(p) >= 255 - threshold

    def band_at(row_or_col, vertical=True):
        length = h if vertical else w
        span = w if vertical else h
        required = max(1, int(span * fraction))
        flags = []
        for pos in range(length):
            count = 0
            for off in range(span):
                px = pixels[off, pos] if vertical else pixels[pos, off]
                count += matches(px)
            flags.append(count >= required)
        start = 0
        while start < length and flags[start]:
            start += 1
        end = length
        while end > start and flags[end - 1]:
            end -= 1
        # A letterbox must have matching bars on both opposing edges, with active content between.
        return (start, length - end) if start >= 2 and length - end >= 2 and end > start else (0, 0)

    top, bottom = band_at(0, True)
    left, right = band_at(0, False)
    if top or bottom or left or right:
        return {"color": color, "top": top, "bottom": bottom, "left": left, "right": right,
                "width": w, "height": h}
    return None


def detect_borders(image):
    return [result for color in ("black", "white")
            if (result := _edge_band(image, color)) is not None]


def group_border_samples(samples: list[dict], gap_seconds: float = 1.1) -> list[dict]:
    """Group same-color detections into clickable time ranges."""
    ordered = sorted(samples, key=lambda item: (item["color"], item["seconds"]))
    groups = []
    for item in ordered:
        if groups and groups[-1]["color"] == item["color"] and item["seconds"] - groups[-1]["end"] <= gap_seconds:
            groups[-1]["end"] = item["seconds"]
            groups[-1]["count"] += 1
        else:
            groups.append({"color": item["color"], "start": item["seconds"],
                           "end": item["seconds"], "count": 1})
    return groups
