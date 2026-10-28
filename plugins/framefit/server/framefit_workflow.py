"""Per-computer project matching, naming and border analysis helpers."""
from __future__ import annotations

import re
import os
from pathlib import Path

DIMENSIONS = re.compile(r"(?<!\d)(\d{2,5})\s*[xX×]\s*(\d{2,5})(?!\d)")
VERSION = re.compile(r"[\s._-]+(?:v|ver|version)\s*\d+(?:\.\d+)*(?=$|[\s._-])", re.I)
LANGUAGES = {"简中": "简中", "中文": "简中", "cn": "简中", "zh": "简中", "chs": "简中", "zh-cn": "简中",
             "繁中": "繁中", "繁体": "繁中", "cht": "繁中", "tw": "繁中", "zh-tw": "繁中",
             "英": "英", "英语": "英", "en": "英", "eng": "英", "english": "英",
             "日": "日", "日语": "日", "ja": "日", "jp": "日", "jpn": "日",
             "韩": "韩", "韩语": "韩", "ko": "韩", "kr": "韩", "kor": "韩"}


def template_language(filename: str) -> str | None:
    """Read explicit language suffixes, including the studio's 1-45EN files."""
    stem = Path(filename).stem
    for marker in sorted(LANGUAGES, key=len, reverse=True):
        if re.search(r'(?:^|[\d\s._+()\[\]\-])' + re.escape(marker) + r'[)\]\s]*$', stem, re.I):
            return LANGUAGES[marker]
    return None


def output_language(filename: str, template: str = '') -> str:
    # Each selected template describes its own output language; a source marker
    # is the fallback when the template has no language marker.
    return template_language(template) or parse_video_name(filename)['language'] or ''


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
    if (local_mapping or {}).get('matchPaths') is False:
        return {'status':'disabled','projectFolder':project,'message':'关键词匹配路径已关闭，使用手动套版。','candidates':[]}
    if not str(schedule_root or '').strip() or not str(template_root or '').strip():
        return {'status':'root_unavailable','projectFolder':project,
                'message':'请先在设置中选择本机任务排期目录和套版根目录。','candidates':[]}
    root = Path(schedule_root).expanduser()
    if not root.is_dir():
        return {"status": "root_unavailable", "projectFolder": project,
                "message": f"任务排期目录不可访问：{schedule_root}", "candidates": []}
    key = _normal(project)
    matches: list[Path] = []
    try:
        # Schedule structure is year/month/company-game/project. Do not crawl
        # footage and production subfolders inside every unrelated project.
        pending=[(root,0)]
        visited=0
        while pending and visited<50000 and len(matches)<100:
            parent,depth=pending.pop(0)
            visited+=1
            with os.scandir(parent) as entries:
                for entry in entries:
                    if not entry.is_dir(follow_symlinks=False):
                        continue
                    path=Path(entry.path)
                    if _normal(entry.name)==key:
                        matches.append(path)
                    elif depth<2:
                        pending.append((path,depth+1))
    except OSError as exc:
        return {"status": "root_unavailable", "projectFolder": project,
                "message": str(exc), "candidates": []}
    if not matches:
        return {"status": "project_missing", "projectFolder": project,
                "message": f"排期目录内没有找到项目文件夹“{project}”。", "candidates": []}
    reference={'projectFolder':project,'projectPaths':[str(p) for p in matches],
               'game':matches[0].parent.name,'templateRoots':[]}
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
    selected_paths=[Path(p) for p in explicit.get('paths',[])] if isinstance(explicit,dict) else []
    if selected_paths:
        candidate_dirs=[Path(p) for p in explicit.get('templateRoots',[])] or list(dict.fromkeys(p if p.is_dir() else p.parent for p in selected_paths))
    elif selected_path:
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
    reference['templateRoots']=[str(path) for path in candidate_dirs]
    if not candidate_dirs:
        return {"status": "template_missing", "projectFolder": project,
                **reference, "message": "找到了排期项目，但本机没有匹配的套版输出目录。", "candidates": []}
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
                                          "projectPath": str(matches[0]), "templateRoot":str(folder), "modifiedAt": path.stat().st_mtime})
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
    if __package__:
        from .template_versions import rank_versions
    else:
        from template_versions import rank_versions
    ranked=rank_versions(files,int(root.name) if root.name.isdigit() and len(root.name)==4 else None)
    if selected_paths and isinstance(explicit,dict):
        chosen=[item for item in files if Path(item['path']) in selected_paths]
        known=set(explicit.get('knownPaths',[]))
        if len(chosen)==len(selected_paths) and all(item['modifiedAt']==explicit.get('files',{}).get(item['path']) for item in chosen) and all(item['path'] in known for item in files):
            # Keep the saved selection for automatic export, while exposing
            # every compatible file to the chooser so selected and unselected
            # templates remain visible together.
            return {**reference,'status':'matched','candidates':chosen,
                    'availableCandidates':files,'message':''}
    files=ranked['candidates']
    if ranked['conflicts']:
        return {**reference,'status':'date_conflict','candidates':files,
                'message':'名称日期与文件修改时间指向不同版本，请对照日期选择套版。'}
    if selected_paths:
        chosen=[item for item in files if Path(item['path']) in selected_paths]
        changed=any(not path.is_file() for path in selected_paths) or len(chosen)!=len(selected_paths)
        if isinstance(explicit,dict):
            changed=changed or any(item['modifiedAt']!=explicit.get('files',{}).get(item['path']) for item in chosen)
            changed=changed or any(item['path'] not in explicit.get('knownPaths',[]) for item in files)
        return {**reference,'status':'template_updated' if changed else 'matched',
                'candidates':files if changed else chosen,
                'message':'项目套版有新版本或新候选，请重新确认。' if changed else ''}
    if selected_path and not selected_path.exists():
        return {**reference,'status': 'template_updated', 'candidates': files,
                'message': '上次选择的套版已不存在，请确认新的候选，不能自动替换。'}
    if selected_path and selected_path.is_file():
        chosen = next((item for item in files if Path(item['path']).resolve() == selected_path.resolve()), None)
        if chosen and isinstance(explicit, dict):
            changed = chosen['modifiedAt'] != explicit.get('modifiedAt')
            newer = any(item['modifiedAt'] > explicit.get('selectedAt', 0) and item['path'] != chosen['path'] for item in files)
            if explicit.get('knownPaths'):
                newer = newer or any(item['path'] not in explicit['knownPaths'] for item in files)
            if changed or newer:
                return {**reference,'status': 'template_updated', 'candidates': files,
                        'message': '已选套版被修改或目录中出现更新的套版，请重新确认。', 'preselected': chosen['path']}
        if chosen:
            files = [chosen]
    # Files are candidates until their aspect/size and project scope are selected by the user.
    return {**reference,"status": "matched" if len(matches) == 1 and len(files) == 1 else "ambiguous",
            "projectFolder": project, "projectPaths": [str(p) for p in matches],
            "candidates": files, "message": "" if len(matches) == 1 and len(files) == 1 else "找到多个项目目录或套版，请选择。"}


def _edge_band(image, color: str, threshold: int = 40, fraction: float = .97):
    """Inspect all four edges independently, including one-pixel and single-sided bands."""
    # Keep the edge rows/columns at source resolution. Scaling a 1px border
    # down to a 640px thumbnail averages it away on 1080p and 4K footage.
    rgb = image.convert("RGB")
    w, h = rgb.size
    from PIL import ImageChops
    red, green, blue = rgb.split()
    if color == "black":
        extremes = ImageChops.lighter(ImageChops.lighter(red, green), blue)
        mask = extremes.point([255 if value <= threshold else 0 for value in range(256)])
    else:
        extremes = ImageChops.darker(ImageChops.darker(red, green), blue)
        mask = extremes.point([255 if value >= 255 - threshold else 0 for value in range(256)])

    def band_at(row_or_col, vertical=True):
        length = h if vertical else w
        span = w if vertical else h
        required = max(1, int(span * fraction))
        def matches_band(pos):
            bounds = (0, pos, w, pos + 1) if vertical else (pos, 0, pos + 1, h)
            return mask.crop(bounds).histogram()[255] >= required
        # Only inspect contiguous edge bands. Histogram/threshold operations run
        # in Pillow instead of millions of Python pixel comparisons per video.
        start = 0
        while start < length and matches_band(start):
            start += 1
        end = length
        while end > start and matches_band(end - 1):
            end -= 1
        # A full black/white frame is a scene or fade, not a spatial border.
        if end <= start:
            return (0, 0)
        # A dark or white scene fills the whole frame. Treat an edge as a
        # line only when the adjacent interior band has a contrasting mean;
        # this prevents full black/white scenes and fades becoming borders.
        from PIL import ImageStat
        band = max(start, length-end)
        probe = min(length-1, max(start, band))
        bounds = (0, probe, w, probe+1) if vertical else (probe, 0, probe+1, h)
        adjacent = ImageStat.Stat(rgb.crop(bounds)).mean
        if vertical:
            edge_bounds = (0, 0, w, max(1,start)) if start else (0, max(0,end), w, length)
        else:
            edge_bounds = (0, 0, max(1,start), h) if start else (max(0,end), 0, w, h)
        edge_mean = ImageStat.Stat(rgb.crop(edge_bounds)).mean
        edge_value = max(edge_mean) if color == 'black' else min(edge_mean)
        adjacent_value = max(adjacent) if color == 'black' else min(adjacent)
        if band <= 1 and color == 'black' and adjacent_value - edge_value < 18:
            return (0, 0)
        if band <= 1 and color == 'white' and edge_value - adjacent_value < 18:
            return (0, 0)
        return (start, length - end)

    top, bottom = band_at(0, True)
    left, right = band_at(0, False)
    if top or bottom or left or right:
        return {"color": color, "top": top, "bottom": bottom, "left": left, "right": right,
                "width": w, "height": h}
    return None


def dark_seams(image):
    """Thin, long dark gaps inside an already decorated frame; report as candidates."""
    from PIL import Image
    rgb=image.convert('RGB')
    rgb.thumbnail((640,640))
    gray=rgb.convert('L')
    w,h=gray.size
    mask=gray.point([255 if value<=90 else 0 for value in range(256)])
    seams=[]
    for axis,length,span in (('horizontal',h,w),('vertical',w,h)):
        trim=max(1,round(span*.02))
        region=(trim,0,w-trim,h) if axis=='horizontal' else (0,trim,w,h-trim)
        reduced=(1,h) if axis=='horizontal' else (w,1)
        if span<=trim*2:
            continue
        means=list(gray.crop(region).resize(reduced,Image.Resampling.BOX).tobytes())
        flags=[value>=204 for value in mask.crop(region).resize(reduced,Image.Resampling.BOX).tobytes()]
        start=1
        while start<length-1:
            if not flags[start]:
                start+=1
                continue
            end=start+1
            while end<length and flags[end]:
                end+=1
            if end-start<=max(3,round(length*.012)) and end<length-2:
                before=max(means[max(0,start-3):start])
                after=max(means[end:min(length,end+3)])
                mean=sum(means[start:end])/(end-start)
                if mean<82 and min(before,after)-mean>18:
                    seams.append({'axis':axis,'start':start,'end':end,'position':round((start+end)/2/length,3)})
            start=end
    if not seams:
        return None
    return dict(color='black',kind='dark_seam',confidence='possible',top=0,bottom=0,left=0,right=0,
                width=w,height=h,bands=seams)


def detect_borders(image, include_internal=True):
    found=[result for color in ("black", "white")
           if (result := _edge_band(image, color)) is not None]
    if include_internal:
        seam=dark_seams(image)
        if seam:
            found.append(seam)
    return found


def group_border_samples(samples: list[dict], gap_seconds: float = 1.1) -> list[dict]:
    """Group same-color detections into clickable time ranges."""
    ordered = sorted(samples, key=lambda item: (item["color"], item.get('kind', ''), item["seconds"]))
    groups = []
    for item in ordered:
        if groups and groups[-1]["color"] == item["color"] and groups[-1].get('kind') == item.get('kind') and item["seconds"] - groups[-1]["end"] <= gap_seconds:
            groups[-1]["end"] = item["seconds"]
            groups[-1]["count"] += 1
        else:
            groups.append({"color": item["color"], "start": item["seconds"],
                           "end": item["seconds"], "count": 1})
            if item.get('kind'):
                groups[-1]['kind'] = item['kind']
        locations=[label for key,label in (('top','上'),('bottom','下'),('left','左'),('right','右')) if item.get(key,0)]
        locations.extend(('横向' if band['axis']=='horizontal' else '纵向')+f"暗缝 {band['position']:.0%}" for band in item.get('bands',[]))
        if locations:
            groups[-1]['locations']=list(dict.fromkeys(groups[-1].get('locations',[])+locations))
    return groups
