import os
from pathlib import Path

import pytest
from PIL import Image, ImageDraw

from plugins.framefit.server.naming import render_name, validate_pattern
from plugins.framefit.server.template_versions import rank_versions
from plugins.framefit.server.framefit_workflow import detect_borders


def test_naming_tokens_presets_and_sequence():
    assert render_name('[原名] [尺寸] [语言]', 'v968 1920X1080 V5.mp4', (1080,1920),
                       language='英') == 'v968 1080X1920 英.mp4'
    assert render_name('[项目]_[游戏]_[套版]_[比例]_[日期]_[序号:03].[扩展名]', 'clip.mp4',
                       (1080,1350), template='1-45EN.png',project='事件',game='超凡-问剑长生',
                       date='2026-10-16',index=2) == '事件_超凡-问剑长生_1-45EN_4-5_2026-10-16_002.mp4'
    assert render_name('[源文件名]', 'v968 1920X1080 V5.mp4',(2,2))=='v968 1920X1080 V5.mp4'
    for pattern in ('[未知]', '../[原名]', '[原名]:[语言]', '[序号:999]', '[原名]\n'):
        with pytest.raises(ValueError):
            validate_pattern(pattern)


def test_template_date_conflict_and_distinct_families():
    files=[{'path':'S 输出/2026-10-01/1-916EN.png','name':'1-916EN.png','width':1080,'height':1920,'modifiedAt':200},
           {'path':'S 输出/2026-10-15/1-916EN.png','name':'1-916EN.png','width':1080,'height':1920,'modifiedAt':100},
           {'path':'S 输出/2026-10-15/1-45JP.png','name':'1-45JP.png','width':1080,'height':1350,'modifiedAt':100}]
    result=rank_versions(files)
    assert result['conflicts']
    assert len(result['candidates'])==3
    files[1]['modifiedAt']=300
    result=rank_versions(files)
    assert not result['conflicts']
    assert len(result['candidates'])==2
    assert any('10-15/1-916EN' in item['path'] for item in result['candidates'])


def test_schedule_game_reference_replaces_unrelated_default_keywords(tmp_path):
    from plugins.framefit.server.framefit_workflow import find_project_candidates
    schedule=tmp_path/'任务排期/2026'
    project=schedule/'3月/超凡-问剑长生/AI漫剧完整渡劫事件'
    project.mkdir(parents=True)
    output=tmp_path/'素材/C 超凡/W 问剑长生/S 输出/仙逆联动贴图'
    output.mkdir(parents=True)
    Image.new('RGB',(160,90),'blue').save(output/'1-169EN.png')
    found=find_project_candidates('AI漫剧完整渡劫事件 1920X1080 V2.mp4',str(schedule),str(tmp_path/'素材'),
                                  {'gameKeywords':['超凡','无尽迷途']},direction='custom')
    assert found['status']=='matched'
    assert found['game']=='超凡-问剑长生'
    assert found['projectPaths']==[str(project)]
    assert found['candidates'][0]['path']==str(output/'1-169EN.png')


def test_schedule_search_does_not_crawl_project_media(tmp_path,monkeypatch):
    from plugins.framefit.server import framefit_workflow as workflow
    project=tmp_path/'2026/3月/Studio-Game/Clip'
    (project/'footage/raw/deep').mkdir(parents=True)
    seen=[]
    scan=workflow.os.scandir
    def track(path):
        seen.append(Path(path))
        return scan(path)
    monkeypatch.setattr(workflow.os,'scandir',track)
    workflow.find_project_candidates('Clip.mp4',str(tmp_path/'2026'),str(tmp_path/'missing'))
    assert not any(project==path or project in path.parents for path in seen)


def test_matching_switch_skips_network_lookup():
    from plugins.framefit.server.framefit_workflow import find_project_candidates
    result=find_project_candidates('sample.mp4','nonexistent/share','missing',{'matchPaths':False})
    assert result['status']=='disabled'


def test_unconfigured_paths_do_not_scan_the_working_directory(monkeypatch):
    from plugins.framefit.server import framefit_workflow as workflow
    def unexpected_scan(*args, **kwargs):
        raise AssertionError('Unconfigured installs must not scan current directory')
    monkeypatch.setattr(workflow.os,'scandir',unexpected_scan)
    result=workflow.find_project_candidates('example.mp4','','',{'matchPaths':True})
    assert result['status']=='root_unavailable'
    assert result['candidates']==[]


def test_solid_scenes_are_not_reported_as_spatial_lines():
    assert detect_borders(Image.new('RGB',(320,180),'black'),include_internal=False)==[]
    assert detect_borders(Image.new('RGB',(320,180),'white'),include_internal=False)==[]
    image=Image.new('RGB',(320,180),'#3d6070')
    draw=ImageDraw.Draw(image)
    draw.rectangle((0,0,319,17),fill='black')
    draw.rectangle((0,162,319,179),fill='black')
    found=detect_borders(image,include_internal=False)
    assert found and found[0]['top']>=17 and found[0]['bottom']>=18


def test_confirmed_date_conflict_can_be_reused_until_files_change(tmp_path):
    from plugins.framefit.server.framefit_workflow import find_project_candidates
    schedule=tmp_path/'2026'
    (schedule/'3月/Studio-Game/Clip').mkdir(parents=True)
    root=tmp_path/'Studio/Game/输出'
    files=[]
    for folder,mtime in [('2026-10-01',200),('2026-10-15',100)]:
        path=root/folder/'916EN.png'
        path.parent.mkdir(parents=True)
        Image.new('RGB',(90,160),'blue').save(path)
        os.utime(path,(mtime,mtime))
        files.append(path)
    result=find_project_candidates('Clip.mp4',str(schedule),str(tmp_path/'Studio'),direction='custom')
    assert result['status']=='date_conflict'
    mapping={'projects':{'Clip':{'paths':[str(files[1])],'files':{str(files[1]):100},
             'knownPaths':[str(path) for path in files],'templateRoots':[str(root)]}}}
    result=find_project_candidates('Clip.mp4',str(schedule),str(tmp_path/'Studio'),mapping,direction='custom')
    assert result['status']=='matched'
    assert result['candidates'][0]['path']==str(files[1])
    assert {item['path'] for item in result['availableCandidates']} == {str(path) for path in files}
