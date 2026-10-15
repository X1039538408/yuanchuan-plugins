from pathlib import Path
import os
import time

from PIL import Image, ImageDraw

from plugins.framefit.server.framefit_workflow import (
    detect_borders, find_project_candidates, group_border_samples,
    output_filename, parse_video_name,
)


def test_parse_filename_dimensions_version_and_language():
    info = parse_video_name("v968 1920X1080 V5.mp4")
    assert info["baseName"] == "v968"
    assert info["sourceSize"] == (1920, 1080)
    assert info["hadVersion"] is True
    assert output_filename("v968 1920X1080 V5.mp4", (1080, 1920), "英") == "v968 1080X1920 英.mp4"


def test_border_detection_finds_black_and_white_letterbox_bands():
    image = Image.new("RGB", (320, 180), (80, 130, 170))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 319, 16), fill="black")
    draw.rectangle((0, 163, 319, 179), fill="black")
    black = next(item for item in detect_borders(image) if item["color"] == "black")
    assert black["top"] > 10 and black["bottom"] > 10

    white_image = Image.new("RGB", (320, 180), (80, 130, 170))
    draw = ImageDraw.Draw(white_image)
    draw.rectangle((0, 0, 319, 12), fill="white")
    draw.rectangle((0, 167, 319, 179), fill="white")
    white = next(item for item in detect_borders(white_image) if item["color"] == "white")
    assert white["top"] > 5 and white["bottom"] > 5


def test_border_samples_group_contiguous_segments():
    groups = group_border_samples([
        {"color": "black", "seconds": 1.0},
        {"color": "black", "seconds": 2.0},
        {"color": "black", "seconds": 10.0},
    ])
    assert groups == [
        {"color": "black", "start": 1.0, "end": 2.0, "count": 2},
        {"color": "black", "start": 10.0, "end": 10.0, "count": 1},
    ]


def test_project_matching_keeps_multiple_template_candidates_local(tmp_path: Path):
    schedule = tmp_path / "schedule"
    project = schedule / "9月" / "超凡-无尽迷途" / "AE+AI-二合+剧情探索-废弃照相馆"
    project.mkdir(parents=True)
    output = tmp_path / "assets" / "C 超凡" / "W 无尽迷途" / "S 输出"
    output.mkdir(parents=True)
    Image.new("RGB", (720, 1280), "blue").save(output / "a.png")
    Image.new("RGB", (720, 1280), "red").save(output / "b.png")
    result = find_project_candidates(
        "AE+AI-二合+剧情探索-废弃照相馆 720X1280.mp4",
        str(schedule), str(tmp_path / "assets"),
        {"gameKeywords": ["超凡", "无尽迷途"]},
    )
    assert result["status"] == "ambiguous"
    assert len(result["candidates"]) == 2
    assert all(str(tmp_path) in item["path"] for item in result["candidates"])


def test_project_file_mapping_and_updated_template_require_reconfirmation(tmp_path):
    schedule = tmp_path / 'schedule'
    (schedule / '9月/游戏-项目/制作标题').mkdir(parents=True)
    output = tmp_path / 'templates/输出'
    output.mkdir(parents=True)
    selected = output / 'chosen.png'
    Image.new('RGB', (90,160), 'blue').save(selected)
    Image.new('RGB', (90,160), 'red').save(output / 'other.png')
    mapping = {'projects': {'制作标题': {'path': str(selected), 'modifiedAt': selected.stat().st_mtime, 'selectedAt': time.time() + 5}}}
    result = find_project_candidates('制作标题 90X160 V2.mp4', str(schedule), str(tmp_path / 'templates'), mapping)
    assert result['status'] == 'matched' and result['candidates'][0]['path'] == str(selected)
    os.utime(selected, (time.time()+10, time.time()+10))
    result = find_project_candidates('制作标题 90X160.mp4', str(schedule), str(tmp_path / 'templates'), mapping)
    assert result['status'] == 'template_updated' and len(result['candidates']) == 2
    selected.unlink()
    result = find_project_candidates('制作标题 90X160.mp4', str(schedule), str(tmp_path / 'templates'), mapping)
    assert result['status'] == 'template_updated' and len(result['candidates']) == 1


def test_project_name_keeps_separators_and_missing_size_does_not_silently_pick(tmp_path):
    title = 'AE+AI-二合+剧情探索-废弃照相馆'
    assert parse_video_name(title + ' 720X1280 V3.mp4')['projectFolder'] == title
    schedule = tmp_path / 'schedule'
    (schedule / '9月/超凡-无尽迷途' / title).mkdir(parents=True)
    templates = tmp_path / 'templates/C 超凡/W 无尽迷途/S 输出'
    templates.mkdir(parents=True)
    Image.new('RGB', (1080,1920), 'red').save(templates / 'large.png')
    result = find_project_candidates(title + ' 720X1280.mp4', str(schedule), str(tmp_path/'templates'))
    assert result['status'] == 'size_missing' and result['candidates']


def test_local_game_alias_requires_confirmed_project_mapping(tmp_path):
    schedule = tmp_path/'schedule'
    (schedule/'9月/超凡-无尽迷途/制作标题').mkdir(parents=True)
    output = tmp_path/'templates/C 超凡/W PathToNowhere/S 输出'
    output.mkdir(parents=True)
    Image.new('RGB',(720,1280),'blue').save(output/'portrait.png')
    result = find_project_candidates('制作标题 720X1280.mp4',str(schedule),str(tmp_path/'templates'),
        {'gameKeywords':['超凡','PathToNowhere','输出']})
    assert result['status'] == 'template_missing'
    result = find_project_candidates('制作标题 720X1280.mp4',str(schedule),str(tmp_path/'templates'),
        {'gameKeywords':['超凡','PathToNowhere','输出'],
         'projects':{'制作标题':str(output/'portrait.png')}})
    assert result['status'] == 'matched'


def test_shared_studio_keyword_cannot_select_another_game(tmp_path):
    schedule = tmp_path/'schedule'
    (schedule/'Oct/Studio-GameB/Project').mkdir(parents=True)
    output = tmp_path/'templates/Studio/GameA/Output'
    output.mkdir(parents=True)
    Image.new('RGB',(160,90),'blue').save(output/'wrong-game.png')
    result = find_project_candidates('Project.mp4',str(schedule),str(tmp_path/'templates'),
        {'gameKeywords':['Studio','GameA'],'outputKeyword':'Output'},
        output_size=[160,90],direction='landscape')
    assert result['status'] == 'template_missing'
