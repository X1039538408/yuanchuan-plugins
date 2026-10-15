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
        calls.append(args[-2])
        if args[-2] == "nvenc":
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
