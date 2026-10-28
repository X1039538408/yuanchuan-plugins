from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

from PIL import Image, ImageDraw
import pytest

SERVER = Path(__file__).resolve().parents[1] / "server"
sys.path.insert(0, str(SERVER))
import core
from engine import Engine, atomic_json


@pytest.fixture
def setup(tmp_path, monkeypatch):
    media = os.getenv("FRAMEFIT_BIN_DIR")
    if not media:
        pytest.skip("Set FRAMEFIT_BIN_DIR to a directory containing ffmpeg/ffprobe for real media tests")
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path / "local"))
    root = tmp_path / "runtime"
    (root / "assets").mkdir(parents=True)
    atomic_json(root / "runtime.json", {"version": "1.0.0", "apiVersion": 1})
    atomic_json(root / "shared-presets.json", [{"name": "纵版", "config": {"direction": "portrait", "templateId": "portrait", "fit": "contain"}}])
    for name, size, rect in (("default.png", (160, 90), (50, 5, 110, 85)), ("default_portrait.png", (90, 160), (5, 50, 85, 110))):
        im = Image.new("RGB", size, "#376659")
        ImageDraw.Draw(im).rectangle(rect, fill="black")
        im.save(root / "assets" / name)
    video = tmp_path / "中文 clip ; $ &.mp4"
    subprocess.run([str(Path(media) / "ffmpeg.exe"), "-v", "error", "-f", "lavfi", "-i", "testsrc2=s=96x64:r=12:d=1",
                    "-f", "lavfi", "-i", "sine=frequency=440:duration=1", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", str(video)], check=True)
    engine = Engine(root, tmp_path / "state")
    return engine, video, tmp_path


@pytest.mark.parametrize("direction", ["landscape", "portrait", "custom"])
@pytest.mark.parametrize("fit", ["cover", "contain", "stretch", "zoom"])
def test_preview_export_geometry_and_audio(setup, direction, fit):
    engine, video, tmp = setup
    cfg = {"direction": direction, "templateId": "portrait" if direction == "portrait" else "landscape",
           "resolution": "original", "fit": fit, "zoom": 101.5, "backend": "cpu", "outputDir": str(tmp / "out")}
    if direction == "custom":
        cfg["template"] = str(engine.root / "assets" / "default.png")
    session = engine.prepare_config([str(video)], cfg)
    preview = engine.preview_frame(session["sessionId"], seconds=100)
    task = engine.export_videos(session["sessionId"], "first")
    final = wait(engine, task["taskId"])
    assert final["status"] == "completed", final
    output = final["items"][0]["output"]
    info = core.probe(output)
    assert [info["width"], info["height"]] == session["outputSize"]
    assert info["audio"] and abs(info["duration"] - 1) < .15
    assert abs(info["fps"] - 12) < .1
    assert preview["previewDataUrl"].startswith("data:image/jpeg;base64,")
    # Decode the same timestamp from the encoded result; preview and export must agree.
    encoded = core.run([core.binary("ffmpeg"), "-v", "error", "-ss", str(preview["seconds"]), "-i", output, "-frames:v", "1", "-f", "image2pipe", "-c:v", "png", "pipe:1"]).stdout
    import io
    from PIL import ImageChops, ImageStat
    exported = Image.open(io.BytesIO(encoded)).convert("RGB")
    expected = Image.open(preview["previewPath"]).convert("RGB")
    assert max(ImageStat.Stat(ImageChops.difference(exported, expected)).mean) < 12


def wait(engine, task_id, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data = engine.get_task(task_id)
        if data["status"] not in ("queued", "running"):
            return data
        time.sleep(.1)
    engine.cancel_task(task_id)
    raise AssertionError("Export timed out")


def test_template_language_matching_exports_distinct_localized_files(setup):
    engine, source, tmp = setup
    video=tmp/'v968 1920X1080 V5.mp4'
    shutil.copyfile(source,video)
    en, jp = tmp/'1-45EN.png', tmp/'1-916JP.png'
    shutil.copyfile(engine.root/'assets/default.png',en)
    shutil.copyfile(engine.root/'assets/default_portrait.png',jp)
    variants=[{'template':str(en),'direction':'custom','rect':[50,5,61,81],'outputSize':[160,90]},
              {'template':str(jp),'direction':'custom','rect':[5,50,81,61],'outputSize':[90,160]}]
    session=engine.prepare_config([str(video)],{'direction':'custom','template':str(en),
        'resolution':'original','language':'auto','backend':'cpu','borderPolicy':'keep','outputDir':str(tmp/'localized')},outputTemplates=variants)
    assert [Path(item['plannedOutput']).name for item in session['videos']]==['v968 160X90 英.mp4','v968 90X160 日.mp4']
    final=wait(engine,engine.export_videos(session['sessionId'],'languages')['taskId'])
    assert final['status']=='completed',final
    assert {Path(item['output']).name for item in final['items']}=={'v968 160X90 英.mp4','v968 90X160 日.mp4'}
    for item in final['items']:
        assert core.probe(item['output'])['audio']


def test_single_mode_keeps_saved_multi_templates_inactive(setup):
    engine, video, tmp=setup
    atomic_json(Path(os.environ['LOCALAPPDATA'])/'FrameFit/settings.json',{
        'exportMode':'single','outputTemplates':[{'template':str(engine.root/'assets/default_portrait.png'),'direction':'portrait'}]})
    session=engine.prepare_config([str(video)],{'resolution':'original'})
    assert len(session['videos'])==1
    assert session['videos'][0].get('outputKey','')==''


def test_custom_names_export_with_stable_date_and_sequence(setup):
    engine,source,tmp=setup
    en,jp=tmp/'169EN.png',tmp/'916JP.png'
    shutil.copyfile(engine.root/'assets/default.png',en)
    shutil.copyfile(engine.root/'assets/default_portrait.png',jp)
    variants=[{'template':str(en),'direction':'custom','rect':[50,5,61,81]},
              {'template':str(jp),'direction':'custom','rect':[5,50,81,61]}]
    session=engine.prepare_config([str(source)],{'template':str(en),'direction':'custom','backend':'cpu',
        'nameMode':'custom','namePattern':'[游戏]_[套版]_[语言]_[日期]_[序号:03]',
        'nameDate':'2026-10-16','nameGame':'问剑长生','borderPolicy':'keep','outputDir':str(tmp/'named')},outputTemplates=variants)
    names=[Path(item['plannedOutput']).name for item in session['videos']]
    assert names==['问剑长生_169EN_英_2026-10-16_001.mp4','问剑长生_916JP_日_2026-10-16_002.mp4']
    final=wait(engine,engine.export_videos(session['sessionId'],'named-export')['taskId'])
    assert final['status']=='completed',final
    assert [Path(item['output']).name for item in final['items']]==names
    assert engine.export_videos(session['sessionId'],'named-export')['taskId']==final['taskId']
    repeated=wait(engine,engine.export_videos(session['sessionId'],'explicit-repeat')['taskId'])
    assert repeated['status']=='completed'
    assert all('(2)' in Path(item['output']).stem for item in repeated['items'])


def test_confirmed_project_templates_expand_per_source_before_shared_defaults(setup):
    engine,source,tmp=setup
    title='AI漫剧完整渡劫事件'
    video=tmp/(title+' 1920X1080 V3.mp4')
    shutil.copyfile(source,video)
    schedule=tmp/'2026'
    (schedule/'3月/超凡-问剑长生'/title).mkdir(parents=True)
    output=tmp/'templates/C 超凡/W 问剑长生/S 输出/仙逆联动贴图'
    output.mkdir(parents=True)
    templates=[output/'1-169EN.png',output/'1-916JP.png']
    shutil.copyfile(engine.root/'assets/default.png',templates[0])
    shutil.copyfile(engine.root/'assets/default_portrait.png',templates[1])
    atomic_json(engine.desktop_settings_path(),{'exportMode':'auto','outputTemplates':[{'template':str(engine.root/'assets/default.png'),'direction':'custom'}],
        'machine':{'scheduleRoot':str(schedule),'templateRoot':str(tmp/'templates'),
            'projects':{title:{'paths':[str(path) for path in templates],
                'files':{str(path):path.stat().st_mtime for path in templates},
                'knownPaths':[str(path) for path in templates],'templateRoots':[str(output)]}}}})
    session=engine.prepare_config([str(video)],{'direction':'custom','template':str(engine.root/'assets/default.png'),'resolution':'original'})
    assert len(session['videos'])==2
    assert {Path(item['config']['template']).name for item in session['videos']}=={'1-169EN.png','1-916JP.png'}
    assert all(item['match']['game']=='超凡-问剑长生' for item in session['videos'])


def test_per_video_templates_names_and_immutable_snapshots(setup):
    engine, video, tmp = setup
    second = tmp / 'v968 1920X1080 V5.mp4'
    shutil.copyfile(video, second)
    session = engine.prepare_config([str(video), str(second)],
        {'resolution': 'original', 'backend': 'cpu', 'outputDir': str(tmp / 'batch'), 'language': '英'},
        itemConfigs=[{'template': str(engine.root / 'assets/default.png'), 'rect': [50, 5, 61, 81]},
                     {'direction': 'portrait', 'template': str(engine.root / 'assets/default_portrait.png'), 'rect': [5, 50, 81, 61]}])
    assert session['videos'][1]['plannedOutput'].endswith('v968 90X160 英.mp4')
    task = engine.export_videos(session['sessionId'], 'per-item')
    final = wait(engine, task['taskId'])
    assert final['status'] == 'completed', final
    assert final['items'][0]['config']['rect'] != final['items'][1]['config']['rect']
    assert core.probe(final['items'][0]['output'])['width'] == 160
    assert core.probe(final['items'][1]['output'])['width'] == 90
    assert all(Path(item['config']['template']).is_file() for item in final['items'])


def test_unique_project_match_preserves_explicit_windows_and_queue_item_overrides(setup):
    engine, video, tmp = setup
    title = video.stem
    schedule = tmp / 'schedule'
    (schedule / '9月/Game-Project' / title).mkdir(parents=True)
    templates = tmp / 'templates/Game/Project/输出'
    templates.mkdir(parents=True)
    chosen = templates / 'chosen.png'
    shutil.copyfile(engine.root / 'assets/default.png', chosen)
    atomic_json(engine.desktop_settings_path(), {'machine': {'scheduleRoot':str(schedule), 'templateRoot':str(tmp/'templates')}})
    session = engine.prepare_config([str(video)], {'rect':[1,1,50,50], 'resolution':'original'})
    assert session['videos'][0]['config']['rect'] == [1,1,50,50]
    session = engine.update_config(session['sessionId'], {'zoom':135})
    assert session['videos'][0]['config']['rect'] == [1,1,50,50]
    session = engine.update_config(session['sessionId'], {}, itemConfigs=[{'rect':[2,2,40,40], 'outputName':'chosen.mp4'}])
    assert session['videos'][0]['config']['rect'] == [2,2,40,40]
    session = engine.update_config(session['sessionId'], {'quality':'standard'})
    assert session['videos'][0]['config']['rect'] == [2,2,40,40]
    assert session['videos'][0]['outputName'] == 'chosen.mp4'
    if os.name == 'nt':
        import ctypes
        short_path = ctypes.WinDLL('kernel32').GetShortPathNameW
        short_path.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_ulong]
        short_path.restype = ctypes.c_ulong
        def shortened(path):
            buffer = ctypes.create_unicode_buffer(32768)
            assert short_path(str(path), buffer, len(buffer))
            return buffer.value
        atomic_json(engine.desktop_settings_path(), {'machine': {
            'scheduleRoot':shortened(schedule), 'templateRoot':shortened(tmp/'templates')}})
        session = engine.prepare_config([str(video)], {'rect':[3,3,60,60], 'resolution':'original'})
        session = engine.update_config(session['sessionId'], {'zoom':137})
        assert session['videos'][0]['config']['rect'] == [3,3,60,60]


def test_project_matching_uses_requested_output_instead_of_old_filename_dimensions(setup):
    engine, video, tmp = setup
    source = tmp / 'v968 1920X1080 V5.mp4'
    shutil.copyfile(video, source)
    schedule = tmp / 'schedule'
    (schedule/'9月/Game-Project/v968').mkdir(parents=True)
    templates = tmp/'templates/Game/Project/输出'
    templates.mkdir(parents=True)
    for name,size in [('landscape.png',(1920,1080)),('portrait.png',(1080,1920))]:
        image = Image.new('RGB',size,'#376659')
        ImageDraw.Draw(image).rectangle((100,100,size[0]-101,size[1]-101),fill='black')
        image.save(templates/name)
    atomic_json(engine.desktop_settings_path(),{'machine':{'scheduleRoot':str(schedule),'templateRoot':str(tmp/'templates')}})
    session = engine.prepare_config([str(source)], {'direction':'portrait','resolution':'1080p'})
    item = session['videos'][0]
    assert not item.get('requiresTemplateReview')
    assert item['config']['direction'] == 'portrait'
    assert Path(item['config']['template']).name == 'portrait.png'
    assert item['outputSize'] == [1080,1920]


def test_desktop_workflow_blocks_unmatched_and_new_inner_borders_then_removes(setup, monkeypatch):
    engine, video, tmp = setup
    atomic_json(engine.desktop_settings_path(), {'machine': {'scheduleRoot': str(tmp / 'unavailable'), 'templateRoot': str(tmp / 'templates')}})
    session = engine.prepare_config([str(video)], {'resolution': 'original', 'backend': 'cpu',
        'fit': 'contain', 'outputDir': str(tmp / 'border-out')})
    assert session['videos'][0]['requiresTemplateReview']
    with pytest.raises(ValueError, match='套版需要确认|不可访问'):
        engine.export_videos(session['sessionId'], 'unconfirmed')
    session = engine.update_config(session['sessionId'], {'templateConfirmed': True})
    real_scan = core.analyze_borders
    calls = []
    def scan(path):
        calls.append(path)
        return real_scan(path)
    monkeypatch.setattr(core, 'analyze_borders', scan)
    report = engine.analyze_borders(session['sessionId'])
    assert report['source']['segments'] == []
    assert report['newBorders'], report
    engine.analyze_borders(session['sessionId'])
    assert len(calls) == 1
    with pytest.raises(ValueError, match='新增黑白边'):
        engine.export_videos(session['sessionId'], 'ask-first')
    session = engine.update_config(session['sessionId'], {'borderPolicy': 'remove'})
    task = engine.export_videos(session['sessionId'], 'remove')
    final = wait(engine, task['taskId'])
    assert final['status'] == 'completed', final
    cfg = final['items'][0]['config']
    check = core.analyze_composite_borders(video, cfg['template'], cfg['rect'], final['items'][0]['outputSize'], cfg['fit'], cfg['zoom'])
    assert not check['newBorders'], check


def test_real_source_scan_reports_separate_black_and_white_time_ranges(setup):
    engine, _video, tmp = setup
    video = tmp / '黑白边分段.mp4'
    filters = ','.join([
        "drawbox=x=0:y=0:w=iw:h=18:color=black:t=fill:enable='lt(t,2)'",
        "drawbox=x=0:y=ih-18:w=iw:h=18:color=black:t=fill:enable='lt(t,2)'",
        "drawbox=x=0:y=0:w=iw:h=18:color=white:t=fill:enable='gte(t,3)*lt(t,5)'",
        "drawbox=x=0:y=ih-18:w=iw:h=18:color=white:t=fill:enable='gte(t,3)*lt(t,5)'",
    ])
    core.run([core.binary('ffmpeg'), '-v','error','-f','lavfi','-i','testsrc2=s=320x180:r=12:d=6',
              '-vf',filters,'-c:v','libx264','-pix_fmt','yuv420p',str(video)])
    progress = []
    report = engine.source_borders(video, on_progress=progress.append)
    assert progress and progress[-1] == 1
    assert all(0 <= value <= 1 for value in progress)
    assert progress == sorted(progress)
    cached_progress = []
    assert engine.source_borders(video, on_progress=cached_progress.append) == report
    assert cached_progress == [1.0]
    assert report['checkedFrames'] == 12
    assert {segment['color'] for segment in report['segments']} == {'black','white'}
    assert len(report['segments']) == 2
    assert next(s for s in report['segments'] if s['color']=='black')['start'] < 2
    assert next(s for s in report['segments'] if s['color']=='white')['start'] >= 2


def test_duplicate_request_missing_file_and_no_overwrite(setup):
    engine, video, tmp = setup
    session = engine.prepare_config([str(video), str(video), str(tmp / "missing.mp4")], {"resolution": "original", "backend": "cpu", "outputDir": str(tmp / "out")})
    assert len(session["videos"]) == 2
    first = engine.export_videos(session["sessionId"], "retry-key")
    assert engine.export_videos(session["sessionId"], "retry-key")["taskId"] == first["taskId"]
    result = wait(engine, first["taskId"])
    assert result["status"] == "partial"
    first_path = Path(result["items"][0]["output"])
    second = engine.export_videos(session["sessionId"], "another-export")
    result2 = wait(engine, second["taskId"])
    assert first_path.exists() and result2["items"][0]["output"] != str(first_path)
    assert video.exists()


def test_presets_change_direction_and_remain_local(setup):
    engine, video, tmp = setup
    session = engine.prepare_config([str(video)], {"preset": "纵版", "resolution": "original"})
    assert session["config"]["direction"] == "portrait" and session["config"]["fit"] == "contain"
    engine.manage_presets("save", "个人", session["sessionId"])
    personal = json.loads((engine.state / "presets.json").read_text(encoding="utf-8"))
    assert "template" not in personal["个人"]
    restored = engine.prepare_config([str(video)], {"preset": "个人"})
    assert restored["config"]["direction"] == "portrait"
    assert not (engine.root / "presets.json").exists()
    engine.manage_presets("delete", "个人")
    assert all(p["name"] != "个人" for p in engine.presets())


@pytest.mark.parametrize("color", ["black", "white", "transparent", "auto"])
def test_window_detection_odd_dimensions_and_validation(setup, color):
    engine, video, tmp = setup
    template = tmp / "odd.png"
    image = Image.new("RGBA", (121, 91), "#59746c")
    ImageDraw.Draw(image).rectangle((20, 10, 99, 79), fill={"black": (0, 0, 0, 255), "white": (255,255,255,255), "transparent": (255,0,255,0), "auto": (0,0,0,255)}[color])
    image.save(template)
    session = engine.prepare_config([str(video)], {"direction": "custom", "template": str(template), "detection": color})
    assert session["outputSize"] == [120, 90]
    assert session["rect"] == [20, 10, 80, 70]
    with pytest.raises(ValueError):
        engine.update_config(session["sessionId"], {"rect": [-1, 0, 2, 2]})
    with pytest.raises(ValueError):
        engine.update_config(session["sessionId"], {"zoom": float("nan")})
    with pytest.raises(ValueError):
        engine.preview_frame(session["sessionId"], seconds=float("inf"))


def test_cancel_resume_preserves_completed_and_pins_runtime(setup):
    engine, video, tmp = setup
    session = engine.prepare_config([str(video)], {"resolution": "4k", "backend": "cpu", "outputDir": str(tmp / "out")})
    task = engine.export_videos(session["sessionId"], "cancel-me")
    engine.cancel_task(task["taskId"])
    cancelled = wait(engine, task["taskId"])
    assert cancelled["status"] == "cancelled"
    assert not list((tmp / "out").glob(".framefit-*"))
    resumed = engine.resume_task(task["taskId"])
    completed = wait(engine, resumed["taskId"], 60)
    assert completed["status"] == "completed"
    assert completed["version"] == "1.0.0"
    assert engine.resume_task(task["taskId"])["status"] == "completed"


def test_video_above_template_is_not_clipped_to_window(setup):
    engine, video, tmp = setup
    im = Image.new("RGB", (160, 90), "red")
    ImageDraw.Draw(im).rectangle((60, 30, 99, 59), fill="black")
    template = tmp / "layer.png"
    im.save(template)
    session = engine.prepare_config([str(video)], {"template": str(template), "fit": "zoom", "zoom": 300, "resolution": "original"})
    preview = engine.preview_frame(session["sessionId"], seconds=0)
    pixel = Image.open(preview["previewPath"]).getpixel((45, 45))
    assert pixel != (254, 0, 0) and pixel[1] + pixel[2] > 10


def test_resume_excludes_removed_items_without_reexporting_completed(setup, monkeypatch):
    engine, video, tmp = setup
    started=[]
    monkeypatch.setattr(engine,'_start',lambda task:(started.append(task) or task))
    session=engine.prepare_config([str(video)],{'resolution':'original','backend':'cpu','outputDir':str(tmp/'out')})
    task=engine.export_videos(session['sessionId'],'filtered-resume')
    original=dict(task['items'][0])
    task.update(status='partial',items=[dict(original,status='completed',progress=1,output='keep.mp4'),
        dict(original,status='cancelled',progress=0),dict(original,status='cancelled',progress=0)])
    atomic_json(engine.task_path(task['taskId']),task)
    result=engine.resume_task(task['taskId'],excludeIndices=[2])
    assert result['taskId']!=task['taskId'] and result['resumedFrom']==task['taskId']
    assert [item['status'] for item in result['items']]==['completed','queued']
    assert result['items'][0]['output']=='keep.mp4'
    assert result['version']==task['version'] and result['runtimeRoot']==task['runtimeRoot']
    assert result['items'][1]['config']==original['config']
    assert len(engine.get_task(task['taskId'])['items'])==3, 'Original durable task was modified'


def test_silent_video_and_gpu_fallback(setup, monkeypatch):
    engine, video, tmp = setup
    silent = tmp / "silent.mp4"
    core.run([core.binary("ffmpeg"), "-v", "error", "-i", str(video), "-an", "-c:v", "copy", str(silent)])
    monkeypatch.setattr(core, "nvenc_available", lambda: (False, "test driver unavailable"))
    seen = []
    path = core.export(silent, engine.root / "assets" / "default.png", (50,5,61,81), (160,90), "contain", tmp / "out", backend="auto", on_backend=lambda mode, note: seen.append(mode))
    assert seen == ["cpu"] and core.probe(path)["audio"] is False
    with pytest.raises(core.HardwareEncodingError):
        core.export(silent, engine.root / "assets" / "default.png", (50,5,61,81), (160,90), "contain", tmp / "out", backend="nvenc")


def test_restart_reports_interrupted_and_invalid_ids(setup):
    engine, video, tmp = setup
    atomic_json(engine.task_path("stale"), {"taskId":"stale", "status":"running", "pid":999999999, "createdAt":0})
    assert engine.get_task("stale")["status"] == "interrupted"
    with pytest.raises(ValueError):
        engine.get_task("../../private")


def test_windows_state_replace_retries_while_reader_holds_file(tmp_path):
    if os.name != "nt":
        pytest.skip("Windows reader sharing regression")
    target = tmp_path / "state.json"
    atomic_json(target, {"revision": 1})
    held = threading.Event()
    def reader():
        with target.open("rb"):
            held.set()
            time.sleep(.15)
    thread = threading.Thread(target=reader)
    thread.start()
    assert held.wait(1)
    atomic_json(target, {"revision": 2})
    thread.join()
    assert json.loads(target.read_text())["revision"] == 2
    assert not list(tmp_path.glob("*.tmp"))


def test_rotated_video_preserves_display_orientation(setup):
    engine, video, tmp = setup
    rotated = tmp / "rotated.mov"
    core.run([core.binary("ffmpeg"), "-v", "error", "-display_rotation:v:0", "90", "-i", str(video), "-c", "copy", str(rotated)])
    info = core.probe(rotated)
    assert (info["width"], info["height"]) == (64, 96)
    session = engine.prepare_config([str(rotated)], {"direction":"portrait", "resolution":"original", "backend":"cpu", "outputDir":str(tmp / "out")})
    result = wait(engine, engine.export_videos(session["sessionId"], "rotate")["taskId"])
    assert result["status"] == "completed"
    output = core.probe(result["items"][0]["output"])
    assert (output["width"], output["height"]) == (90, 160)


def test_auto_gpu_failure_retries_cpu_and_forced_gpu_does_not(setup, monkeypatch):
    engine, video, tmp = setup
    monkeypatch.setattr(core, "nvenc_available", lambda: (True, ""))
    real_export = core._export_once
    calls = []
    def failing_gpu(*args):
        calls.append(args[-3])
        if args[-3] == "nvenc":
            raise core.HardwareEncodingError("nvenc stopped working")
        return real_export(*args)
    monkeypatch.setattr(core, "_export_once", failing_gpu)
    result = core.export(video, engine.root / "assets" / "default.png", (50,5,61,81), (160,90), "cover", tmp / "out", backend="auto")
    assert result.is_file() and calls == ["nvenc", "cpu"]
    with pytest.raises(core.HardwareEncodingError):
        core.export(video, engine.root / "assets" / "default.png", (50,5,61,81), (160,90), "cover", tmp / "out", backend="nvenc")


def test_profiles_and_template_snapshot_are_independent(setup):
    engine, video, tmp = setup
    template = tmp / "personal.png"
    shutil.copyfile(engine.root / "assets" / "default.png", template)
    session = engine.prepare_config([str(video)], {"template":str(template), "fit":"contain", "rect":None,
                                               "resolution":"original", "backend":"cpu", "outputDir":str(tmp / "out")})
    assert engine.prepare_config([])["config"]["fit"] == "contain"
    task = engine.export_videos(session["sessionId"], "snapshot")
    # Removing the editable input after submission must not break a running batch.
    template.unlink()
    result = wait(engine, task["taskId"])
    assert result["status"] == "completed"
    assert Path(result["config"]["template"]).is_file()


def test_one_video_three_output_templates_and_resume_identity(setup):
    engine, video, tmp = setup
    variants=[]
    for index,size in enumerate(((160,90),(90,160),(120,120))):
        template=tmp/f'variant-{index}.png'
        image=Image.new('RGB',size,'#376659')
        ImageDraw.Draw(image).rectangle((4,4,size[0]-5,size[1]-5),fill='black')
        image.save(template)
        variants.append({'template':str(template),'direction':'custom','resolution':'original',
                         'outputSize':list(size),'fit':'cover','borderPolicy':'keep'})
    session=engine.prepare_config([str(video)],{'resolution':'original','backend':'cpu','outputDir':str(tmp/'multi')},outputTemplates=variants)
    assert len(session['videos'])==3
    assert len({item['outputKey'] for item in session['videos']})==3
    session=engine.update_config(session['sessionId'],{'quality':'standard'})
    assert len(session['videos'])==3
    final=wait(engine,engine.export_videos(session['sessionId'],'three-outputs')['taskId'])
    assert final['status']=='completed',final
    assert len({item['output'] for item in final['items']})==3
    for item,size in zip(final['items'],((160,90),(90,160),(120,120))):
        info=core.probe(item['output'])
        assert (info['width'],info['height'])==size
        assert info['audio']
    directory=tmp/'sources'
    directory.mkdir()
    shutil.copyfile(video,directory/'a.mp4')
    shutil.copyfile(video,directory/'b.mp4')
    session=engine.prepare_config([str(directory)],{'resolution':'original'},outputTemplates=variants)
    assert len(session['videos'])==6
    assert {path:len([v for v in session['videos'] if v['path']==path]) for path in {v['path'] for v in session['videos']}} == {
        str((directory/'a.mp4').resolve()):3,str((directory/'b.mp4').resolve()):3}


def test_media_preflight_cancel_terminates_child_process():
    cancel=threading.Event()
    timer=threading.Timer(.2,cancel.set)
    timer.start()
    started=time.monotonic()
    try:
        with core.cancellable(cancel), pytest.raises(core.Cancelled):
            core.run([sys.executable,'-c','import time; time.sleep(30)'])
    finally:
        timer.cancel()
    assert time.monotonic()-started<3


@pytest.mark.parametrize('size', [(1920,1080), (3840,2160)])
def test_border_scan_preserves_native_one_pixel_edges(setup, size):
    _engine, _video, tmp = setup
    source = tmp / '单像素四边.png'
    image = Image.new('RGB', size, '#83aac5')
    draw = ImageDraw.Draw(image)
    draw.rectangle((0,0,size[0]-1,size[1]-1),outline='black',width=1)
    image.save(source)
    video = tmp / '精细边缘.mkv'
    core.run([core.binary('ffmpeg'), '-v','error','-loop','1','-framerate','2','-i',str(source),
              '-t','1','-c:v','ffv1','-pix_fmt','bgr0',str(video)])
    report = core.analyze_borders(video)
    assert report['checkedFrames'] == 2
    segment = next(item for item in report['segments'] if item['color'] == 'black')
    assert set(segment['locations']) == {'上','下','左','右'}, report
    border = next(item for item in core.detect_borders(core.border_frame(video,.5)) if item['color']=='black')
    assert all(border[side]==1 for side in ('top','bottom','left','right')), border


def test_composite_checks_thin_gaps_just_outside_template_window(setup):
    engine,video,tmp=setup
    template=tmp/'seams.png'
    image=Image.new('RGB',(160,240),'#83aac5')
    draw=ImageDraw.Draw(image)
    draw.rectangle((0,80,159,159),fill='black')
    draw.line((0,79,159,79),fill=(60,65,60))
    draw.line((0,160,159,160),fill=(60,65,60))
    image.save(template)
    report=core.analyze_composite_borders(video,template,[0,80,160,80],[160,240],'stretch')
    assert any(border.get('kind')=='dark_seam' for border in report['newBorders']),report
