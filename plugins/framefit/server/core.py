"""Local image-window detection and FFmpeg composition; shared by GUI and tests."""
from __future__ import annotations

import io
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import uuid
import tempfile
from contextlib import contextmanager
from fractions import Fraction
from functools import lru_cache

from PIL import Image, ImageChops
from framefit_workflow import detect_borders, group_border_samples

ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
CREATE_FLAGS = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
_processing = threading.local()


@contextmanager
def cancellable(cancel):
    previous = getattr(_processing, 'cancel', None)
    _processing.cancel = cancel
    try:
        yield
    finally:
        _processing.cancel = previous


def binary(name):
    local = Path(os.getenv('FRAMEFIT_BIN_DIR') or ROOT / 'bin') / (name + '.exe')
    found = str(local) if local.exists() else shutil.which(name)
    if not found:
        raise RuntimeError(f'缺少 {name}，请完整解压软件文件夹后再启动。')
    return found


def run(args, timeout=60):
    import time
    cancel = getattr(_processing, 'cancel', None)
    if cancel is not None and cancel.is_set():
        raise Cancelled('已取消')
    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=CREATE_FLAGS)
    deadline = time.monotonic() + timeout
    try:
        while True:
            if cancel is not None and cancel.is_set():
                raise Cancelled('已取消')
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(args, timeout)
            try:
                stdout, stderr = process.communicate(timeout=.1)
                break
            except subprocess.TimeoutExpired:
                pass
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate()
    result = subprocess.CompletedProcess(args, process.returncode, stdout, stderr)
    if result.returncode:
        raise RuntimeError(result.stderr.decode('utf-8', 'replace')[-2500:] or '媒体文件读取失败。')
    return result


def probe(path):
    raw = run([binary('ffprobe'), '-v', 'error', '-show_streams', '-show_format',
               '-of', 'json', str(path)])
    data = json.loads(raw.stdout)
    video = next((s for s in data['streams'] if s['codec_type'] == 'video'), None)
    if not video:
        raise ValueError('文件中没有视频画面。')
    width, height = video['width'], video['height']
    rotation = float(video.get('tags', {}).get('rotate', 0))
    for side in video.get('side_data_list', []):
        rotation = float(side.get('rotation', rotation))
    if round(rotation) % 180:
        width, height = height, width
    rate = video.get('avg_frame_rate', '0/1')
    fps = float(Fraction(rate)) if rate not in ('0/0', '0/1') else 30.0
    duration = float(video.get('duration') or data['format'].get('duration') or 0)
    if not math.isfinite(duration) or duration <= 0:
        raise ValueError('无法读取视频时长。')
    return {'width': width, 'height': height, 'duration': duration,
            'fps': min(240, max(1, fps)),
            'audio': any(s['codec_type'] == 'audio' for s in data['streams'])}


def detect_window(image, color='auto'):
    """Prefer a transparent opening, otherwise choose the largest black/white rectangle."""
    if color not in ('auto', 'black', 'white', 'transparent'):
        raise ValueError('未知的窗口识别颜色。')
    alpha = image.convert('RGBA').getchannel('A')
    if color in ('auto', 'transparent'):
        transparent = _detect_solid_rectangle(alpha)
        if transparent is not None:
            return transparent
        if color == 'transparent':
            raise ValueError('没有找到足够大的透明矩形，请切换识别类型或手动设置窗口坐标。')
    rgb = image.convert('RGB')
    bands = rgb.split()
    candidates = []
    for selected in ('black', 'white') if color == 'auto' else (color,):
        if selected == 'black':
            distance = ImageChops.lighter(ImageChops.lighter(bands[0], bands[1]), bands[2])
        else:
            # Distance from white must be small in every channel, not just luminance.
            distance = ImageChops.invert(ImageChops.darker(ImageChops.darker(bands[0], bands[1]), bands[2]))
        # Hidden RGB values in transparent PNG pixels are arbitrary; do not classify them as colors.
        distance = ImageChops.lighter(distance, ImageChops.invert(alpha))
        rect = _detect_solid_rectangle(distance)
        if rect is not None:
            candidates.append(rect)
    if not candidates:
        name = {'auto': '黑色或白色', 'black': '黑色', 'white': '白色'}[color]
        raise ValueError(f'没有找到足够大的近纯{name}矩形。可切换识别颜色，或在窗口坐标中手动设置。')
    return max(candidates, key=lambda rect: rect[2] * rect[3])


def _detect_solid_rectangle(gray):
    """Find a rectangle whose per-channel distance from the target is <= 8."""
    small = gray.copy()
    small.thumbnail((800, 800), Image.Resampling.NEAREST)
    width, height = small.size
    pixels = small.tobytes()
    heights = [0] * (width + 1)
    best = (0, 0, 0, 0, 0)
    for y in range(height):
        for x in range(width):
            heights[x] = heights[x] + 1 if pixels[y * width + x] <= 8 else 0
        stack = []
        for x in range(width + 1):
            start = x
            while stack and stack[-1][1] > heights[x]:
                left, h = stack.pop()
                area = h * (x - left)
                if area > best[0]:
                    best = (area, left, y - h + 1, x, y + 1)
                start = left
            if not stack or stack[-1][1] < heights[x]:
                stack.append((start, heights[x]))
    if best[0] < width * height * .01:
        return None
    sx, sy = gray.width / width, gray.height / height
    l, t, r, b = (round(best[1] * sx), round(best[2] * sy),
                  round(best[3] * sx), round(best[4] * sy))

    def matches(box):
        hist = gray.crop(box).histogram()
        return sum(hist[:9]) >= sum(hist) * .995

    # Coarse nearest-neighbour sampling may land just outside the true edge.
    while l < r - 2 and not matches((l, t, l + 1, b)): l += 1
    while r > l + 2 and not matches((r - 1, t, r, b)): r -= 1
    while t < b - 2 and not matches((l, t, r, t + 1)): t += 1
    while b > t + 2 and not matches((l, b - 1, r, b)): b -= 1
    while l > 0 and matches((l - 1, t, l, b)): l -= 1
    while r < gray.width and matches((r, t, r + 1, b)): r += 1
    while t > 0 and matches((l, t - 1, r, t)): t -= 1
    while b < gray.height and matches((l, b, r, b + 1)): b += 1
    return (l, t, r - l, b - t)


def validate_rect(rect, image_size):
    x, y, w, h = map(int, rect)
    iw, ih = image_size
    if x < 0 or y < 0 or w < 2 or h < 2 or x + w > iw or y + h > ih:
        raise ValueError('窗口必须位于套框内，宽高至少为 2 像素。')
    return x, y, w, h


def mapped_rect(rect, image_size, output_size):
    x, y, w, h = validate_rect(rect, image_size)
    sx, sy = output_size[0] / image_size[0], output_size[1] / image_size[1]
    left, top = math.floor(x * sx), math.floor(y * sy)
    return left, top, math.ceil((x + w) * sx) - left, math.ceil((y + h) * sy) - top


def validate_zoom(value):
    try:
        value = float(value)
    except (TypeError, ValueError):
        raise ValueError('缩放比例请输入 10～500 之间的数字，例如 101 或 101.5。') from None
    if not math.isfinite(value) or not 10 <= value <= 500:
        raise ValueError('缩放比例请输入 10～500 之间的数字，例如 101 或 101.5。')
    return value


def filter_graph(image_size, rect, output_size, mode, background_fps=None, zoom=100):
    ow, oh = output_size
    if ow % 2 or oh % 2 or min(ow, oh) < 2:
        raise ValueError('导出尺寸必须为正偶数。')
    x, y, w, h = mapped_rect(rect, image_size, output_size)
    # Normalize sample aspect ratio before fitting, respecting anamorphic inputs.
    video = 'scale=trunc(iw*sar/2)*2:ih,setsar=1,format=yuv444p'
    if mode == 'cover':
        video += f',scale={w}:{h}:force_original_aspect_ratio=increase:flags=lanczos'
    elif mode == 'contain':
        video += f',scale={w}:{h}:force_original_aspect_ratio=decrease:flags=lanczos'
    elif mode == 'stretch':
        video += f',scale={w}:{h}:flags=lanczos'
    elif mode == 'zoom':
        factor = validate_zoom(zoom) / 100
        zw, zh = max(1, round(w * factor)), max(1, round(h * factor))
        video += f',scale={zw}:{zh}:force_original_aspect_ratio=increase:flags=lanczos'
    else:
        raise ValueError('未知的画面适配方式。')
    background = f'scale={ow}:{oh}:flags=lanczos,format=yuv444p,setsar=1,setpts=PTS-STARTPTS'
    if background_fps is not None:
        # Decode/resize the static template once; reuse the prepared frame thereafter.
        background += f',loop=loop=-1:size=1:start=0,setpts=N/({background_fps}*TB)'
    return (f'[0:v]{video},setsar=1,setpts=PTS-STARTPTS[v];'
            f'[1:v]{background}[bg];'
            # The detected opening anchors the layer; only the output canvas clips it.
            f'[bg][v]overlay=x={x}+({w}-overlay_w)/2:y={y}+({h}-overlay_h)/2:'
            f'shortest=1:format=yuv444,format=yuv420p[out]')


def preview(video, template, rect, output_size, mode, seconds=1, zoom=100):
    info = probe(video)
    last_frame = max(0, (math.ceil(info['duration'] * info['fps']) - 1) / info['fps'])
    seconds = min(max(0, seconds), math.floor(last_frame * 1e6) / 1e6)
    with Image.open(template) as im:
        graph = filter_graph(im.size, rect, output_size, mode, zoom=zoom)
    args = [binary('ffmpeg'), '-hide_banner', '-loglevel', 'error', '-ss', str(seconds),
            '-i', str(video), '-i', str(template), '-filter_complex_threads', '2',
            '-filter_complex', graph, '-map', '[out]', '-frames:v', '1',
            '-f', 'image2pipe', '-c:v', 'png', 'pipe:1']
    result = run(args)
    return Image.open(io.BytesIO(result.stdout)).convert('RGB')


def thumbnail(video, seconds=0, maximum=(144, 88)):
    info = probe(video)
    seconds = min(max(0, seconds), max(0, (math.ceil(info['duration'] * info['fps']) - 1) / info['fps']))
    args = [binary('ffmpeg'), '-hide_banner', '-loglevel', 'error', '-ss', str(max(0, seconds)),
            '-i', str(video), '-frames:v', '1', '-vf',
            f'scale={maximum[0]}:{maximum[1]}:force_original_aspect_ratio=decrease:flags=area',
            '-f', 'image2pipe', '-c:v', 'mjpeg', 'pipe:1']
    result = run(args, timeout=30)
    return Image.open(io.BytesIO(result.stdout)).convert('RGB')


def analyze_borders(video, interval=1.0, on_progress=None):
    info = probe(video)
    samples = []
    with tempfile.TemporaryDirectory(prefix='framefit-borders-') as temporary:
        pattern = str(Path(temporary) / 'frame-%08d.jpg')
        args = [binary('ffmpeg'), '-hide_banner', '-loglevel', 'error', '-nostdin',
                '-threads', '2', '-i', str(video), '-an', '-filter_threads', '1',
                '-vf', f'fps=1/{interval}:start_time=0,scale=640:-2:flags=area', '-fps_mode', 'passthrough',
                '-q:v', '4', '-progress', 'pipe:1', '-nostats', pattern]
        with tempfile.TemporaryFile() as errors:
            process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=errors, creationflags=CREATE_FLAGS)
            expired = threading.Event()
            def expire():
                expired.set()
                try:
                    process.kill()
                except OSError:
                    pass
            timer = threading.Timer(max(120, int(info['duration'] * 2)), expire)
            timer.daemon = True
            timer.start()
            cancel = getattr(_processing, 'cancel', None)
            finished = threading.Event()
            def watch_cancel():
                while not finished.wait(.1):
                    if cancel is not None and cancel.is_set():
                        try:
                            process.kill()
                        except OSError:
                            pass
                        return
            watcher = threading.Thread(target=watch_cancel,daemon=True)
            watcher.start()
            try:
                for raw in process.stdout:
                    if on_progress and raw.startswith(b'out_time_us='):
                        try:
                            fraction = int(raw.split(b'=', 1)[1]) / 1e6 / max(.001, info['duration'])
                        except ValueError:
                            continue
                        on_progress(min(.8, max(0, fraction * .8)))
                code = process.wait()
                if cancel is not None and cancel.is_set():
                    raise Cancelled('已取消边缘扫描')
                if expired.is_set():
                    raise TimeoutError('视频边缘抽样超时，请重试或检查源文件。')
                if code:
                    errors.seek(0)
                    raise RuntimeError(errors.read().decode('utf-8', 'replace')[-2500:] or '视频边缘抽样失败。')
            finally:
                timer.cancel()
                finished.set()
                if process.poll() is None:
                    process.kill()
                process.wait()
                process.stdout.close()
        frames = sorted(Path(temporary).glob('frame-*.jpg'))
        for index, path in enumerate(frames):
            with Image.open(path) as frame:
                samples.extend(dict(border, seconds=min(info['duration'], index * interval))
                               for border in detect_borders(frame))
            if on_progress:
                on_progress(.8 + .2 * (index + 1) / max(1, len(frames)))
    if on_progress:
        on_progress(1.0)
    return {'duration': info['duration'], 'sampleInterval': interval,
            'checkedFrames': len(frames), 'segments': group_border_samples(samples)}


def analyze_composite_borders(video, template, rect, output_size, mode, zoom=100, source_segments=None, on_progress=None):
    import copy
    def identity(path):
        path = Path(path).resolve()
        stat = path.stat()
        return str(path), stat.st_size, stat.st_mtime_ns
    key = (identity(video), identity(template), tuple(rect), tuple(output_size), mode, zoom)
    cache = getattr(analyze_composite_borders, '_cache', {})
    if key in cache:
        if on_progress:
            on_progress(1.0)
        return copy.deepcopy(cache[key])
    info = probe(video)
    points = sorted(set((0.0, max(0.0, info['duration'] / 2), max(0.0, info['duration'] - .15))))
    source_segments = source_segments or []
    results = []
    factor = min(1.0, 960 / max(output_size))
    check_size = tuple(max(2, round(value * factor / 2) * 2) for value in output_size)
    with Image.open(template) as background:
        sx, sy = check_size[0] / background.width, check_size[1] / background.height
    x, y, w, h = rect
    window = (round(x * sx), round(y * sy), round((x + w) * sx), round((y + h) * sy))
    for index, seconds in enumerate(points):
        cancel = getattr(_processing, 'cancel', None)
        if cancel is not None and cancel.is_set():
            raise Cancelled('已取消边缘检查')
        image = preview(video, template, rect, check_size, mode, seconds, zoom)
        # Bars can sit inside the template's video opening, away from canvas edges.
        original = detect_borders(thumbnail(video, seconds, (640, 640)))
        for border in detect_borders(image.crop(window)):
            def source_covers(source):
                if source['color'] != border['color']:
                    return False
                return all(border[side] / border[axis] <= source[side] / source[axis] + .01
                           for side, axis in (('top', 'height'), ('bottom', 'height'), ('left', 'width'), ('right', 'width')))
            existed = any(source_covers(source) for source in original)
            results.append(dict(border, seconds=seconds, origin='source' if existed else 'composite',
                                outputSize=list(output_size), template=str(template)))
        if on_progress:
            on_progress((index + 1) / len(points))
    report = {'samples': results, 'newBorders': [item for item in results if item['origin'] == 'composite']}
    if len(cache) >= 128:
        cache.pop(next(iter(cache)), None)
    cache[key] = copy.deepcopy(report)
    analyze_composite_borders._cache = cache
    return report


def remove_composite_borders(video, template, rect, output_size, mode, zoom=100, source_segments=None):
    """Find a verified uniform enlargement; never claim bars disappeared without rechecking."""
    candidate = max(100.0, zoom if mode == 'zoom' else 100.0)
    for _ in range(18):
        report = analyze_composite_borders(video, template, rect, output_size, 'zoom', candidate, source_segments)
        borders = report['newBorders']
        if not borders:
            return {'fit': 'zoom', 'zoom': candidate, 'report': report}
        ratio = max(max((b['top'] + b['bottom']) / b['height'],
                        (b['left'] + b['right']) / b['width']) for b in borders)
        candidate = math.ceil(candidate / max(.1, 1 - ratio) + 1)
        if candidate > 500:
            break
    raise ValueError('在 500% 缩放范围内仍检测到新增黑白边，请手动检查窗口或选择保留。')


class Cancelled(Exception):
    pass


class HardwareEncodingError(RuntimeError):
    pass


def encoder_args(backend, quality):
    if backend == 'nvenc':
        return ['-c:v', 'h264_nvenc', '-preset', 'p4', '-tune', 'hq',
                '-rc', 'vbr', '-cq', str(quality), '-b:v', '0', '-spatial-aq', '1']
    if backend == 'cpu':
        return ['-c:v', 'libx264', '-preset', 'fast', '-crf', str(quality), '-threads', '4']
    raise ValueError('未知的编码方式。')


@lru_cache(maxsize=1)
def nvenc_available():
    """A listed encoder is not proof of working hardware/driver; actually encode."""
    try:
        run([binary('ffmpeg'), '-v', 'error', '-nostdin', '-f', 'lavfi', '-i',
             'color=s=1920x1080:r=30', '-frames:v', '3', *encoder_args('nvenc', 18),
             '-f', 'null', '-'], timeout=15)
        return True, ''
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        return False, str(exc)


def unique_output(directory, stem, output_size=(1920, 1080)):
    suffix = '方版' if output_size[0] == output_size[1] else ('竖版' if output_size[1] > output_size[0] else '横版')
    path = Path(directory) / f'{stem}_{suffix}.mp4'
    count = 2
    while path.exists():
        path = Path(directory) / f'{stem}_{suffix}_{count}.mp4'
        count += 1
    return path


def export(video, template, rect, output_size, mode, directory, crf=18,
           cancel=None, progress=None, duration_limit=None, backend='auto', on_backend=None, zoom=100, output_name=None):
    if mode == 'zoom':
        zoom = validate_zoom(zoom)
    cancel = cancel or threading.Event()
    if cancel.is_set():
        raise Cancelled('已取消')
    if backend not in ('auto', 'nvenc', 'cpu'):
        raise ValueError('未知的编码方式。')
    selected, note = 'cpu', 'CPU 编码'
    if backend != 'cpu':
        available, reason = nvenc_available()
        if available:
            selected, note = 'nvenc', 'NVIDIA GPU 编码 · NVENC'
        elif backend == 'nvenc':
            raise HardwareEncodingError('NVIDIA GPU 编码不可用，请选择自动或 CPU。\n' + reason)
        else:
            note = 'GPU 不可用，已自动使用 CPU 编码'
    if cancel.is_set():
        raise Cancelled('已取消')
    if on_backend:
        on_backend(selected, note)
    try:
        return _export_once(video, template, rect, output_size, mode, directory, crf,
                            cancel, progress, duration_limit, selected, zoom, output_name)
    except HardwareEncodingError:
        if backend != 'auto' or selected != 'nvenc':
            raise
        if cancel.is_set():
            raise Cancelled('已取消')
        if on_backend:
            on_backend('cpu', 'GPU 编码失败，已自动改用 CPU 重试')
        if progress:
            progress(0)
        return _export_once(video, template, rect, output_size, mode, directory, crf,
                            cancel, progress, duration_limit, 'cpu', zoom, output_name)


def _export_once(video, template, rect, output_size, mode, directory, crf,
                 cancel, progress, duration_limit, backend, zoom=100, output_name=None):
    cancel = cancel or threading.Event()
    info = probe(video)
    duration = min(info['duration'], duration_limit) if duration_limit else info['duration']
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    temporary = directory / f'.framefit-{uuid.uuid4().hex}.mp4'
    with Image.open(template) as im:
        graph = filter_graph(im.size, rect, output_size, mode, background_fps=info['fps'], zoom=zoom)
    args = [binary('ffmpeg'), '-hide_banner', '-loglevel', 'error', '-nostdin',
            '-i', str(video), '-framerate', str(info['fps']), '-i', str(template),
            '-filter_complex_threads', '2', '-filter_complex', graph,
            '-map', '[out]', '-map', '0:a:0?', '-map_metadata', '-1',
            *encoder_args(backend, crf),
            '-pix_fmt', 'yuv420p', '-r', str(info['fps']),
            '-c:a', 'aac', '-b:a', '192k', '-af', 'asetpts=PTS-STARTPTS',
            '-t', str(duration), '-movflags', '+faststart',
            '-progress', 'pipe:1', '-nostats', '-n', str(temporary)]
    # Spool stderr to disk so the progress reader cannot deadlock on a full pipe.
    import tempfile
    try:
        with tempfile.TemporaryFile() as errors:
            process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=errors,
                                       creationflags=CREATE_FLAGS)
            done = threading.Event()

            def watch_cancel():
                while not done.wait(.15):
                    if cancel.is_set():
                        try:
                            process.kill()
                        except OSError:
                            pass
                        return

            watcher = threading.Thread(target=watch_cancel, daemon=True)
            watcher.start()
            try:
                for raw in process.stdout:
                    line = raw.decode('utf-8', 'replace').strip()
                    if line.startswith('out_time_us=') and progress:
                        try:
                            progress(min(.999, max(0, int(line.split('=')[1]) / 1e6 / duration)))
                        except ValueError:
                            pass
                code = process.wait()
            finally:
                done.set()
                if process.poll() is None:
                    process.kill()
                process.wait()
                process.stdout.close()
                watcher.join(timeout=1)
            if cancel.is_set():
                raise Cancelled('已取消')
            if code:
                errors.seek(0)
                detail = errors.read().decode('utf-8', 'replace')[-2500:] or '视频导出失败。'
                if backend == 'nvenc' and any(word in detail.lower() for word in
                        ('nvenc', 'cuda', 'nvcuda', 'nvencode', 'no capable devices')):
                    raise HardwareEncodingError(detail)
                raise RuntimeError(detail)
        result = probe(temporary)
        if (result['width'], result['height']) != tuple(output_size):
            raise RuntimeError('导出文件尺寸校验失败。')
        # Windows rename does not replace another task's output, even if created concurrently.
        while True:
            if output_name:
                name = Path(output_name)
                if name.name != output_name or name.suffix.lower() != '.mp4' or any(c in output_name for c in '<>:"/\\|?*'):
                    raise ValueError('输出文件名无效。')
                destination = directory / name.name
                count = 2
                while destination.exists():
                    destination = directory / f'{name.stem} ({count}).mp4'
                    count += 1
            else:
                destination = unique_output(directory, Path(video).stem, output_size)
            try:
                temporary.rename(destination)
                break
            except FileExistsError:
                continue
        if progress:
            progress(1.0)
        return destination
    finally:
        temporary.unlink(missing_ok=True)
