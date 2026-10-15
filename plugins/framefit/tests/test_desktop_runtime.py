"""Run the real Tk desktop, loopback RPC and detached encoder with isolated user state."""
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest


@pytest.mark.skipif(os.name != 'nt', reason='Windows desktop integration test')
def test_desktop_shares_latest_settings_recipes_and_durable_tasks(tmp_path):
    media = os.getenv('FRAMEFIT_BIN_DIR')
    if not media:
        pytest.skip('Set FRAMEFIT_BIN_DIR for real desktop and media integration')
    plugin = Path(__file__).resolve().parents[1]
    script = tmp_path / 'desktop_check.py'
    script.write_text(r'''
import json, os, sys, threading, time, traceback
from pathlib import Path
from PIL import Image, ImageDraw
plugin, state = map(Path, sys.argv[1:3])
sys.path.insert(0, str(plugin / 'desktop/source'))
sys.path.insert(0, str(plugin / 'server'))
from engine import atomic_json
from local_client import call_local
import core
import app as gui
gui.check_update = lambda *_args: None
template, video = state / 'template.png', state / 'v968 1920X1080 V5.mp4'
restart = len(sys.argv) > 3
if not restart:
    image = Image.new('RGB', (160,90), '#376659')
    ImageDraw.Draw(image).rectangle((20,10,139,79), fill='black')
    image.save(template)
    core.run([core.binary('ffmpeg'), '-v','error','-f','lavfi','-i','testsrc2=s=96x64:r=12:d=1',
              '-c:v','libx264','-pix_fmt','yuv420p',str(video)])
    atomic_json(Path(os.environ['LOCALAPPDATA']) / 'FrameFit/settings.json', {
        'profiles': {'landscape': {'template':str(template), 'rect':[20,10,120,70],
                                  'size':'套框原始尺寸','mode':'等比填满（推荐）'}},
        'machine': {'scheduleRoot':str(state / 'schedule'), 'templateRoot':str(state)},
        'output':str(state / 'out')})
app = gui.App()
app.withdraw()
errors, result = [], {}
app.report_callback_exception = lambda *args: errors.append(str(args))
gui.messagebox.showerror = lambda title, message, **kwargs: (errors.append(str(message)), print('gui-error:' + str(message), flush=True))
gui.messagebox.showinfo = lambda title, message, **kwargs: (errors.append(str(message)), print('gui-info:' + str(message), flush=True))
gui.messagebox.askyesno = lambda title, message, **kwargs: (print('gui-question:' + str(message), flush=True) or True)
if restart:
    print('restart:settings', flush=True)
    assert app.quality_var.get() == '标准画质（文件更小）'
    assert app.language_var.get() == '英'
    assert '英版配方' in app.recipes
    assert app.active_task
    app.after(200, app.close)
else:
    app.language_var.set('英')
    app.quality_var.set('标准画质（文件更小）')
    app.save_settings()
    def run():
        try:
            print('rpc:prepare', flush=True)
            session = call_local('prepare_config', {'videos':[str(video)], 'config':{
                'templateConfirmed':True, 'borderPolicy':'keep', 'backend':'cpu'},
                'itemConfigs':[{'fit':'zoom','zoom':135,'outputName':'chosen.mp4'}]})
            assert session['config']['language'] == '英'
            assert session['config']['quality'] == 'standard'
            print('rpc:preview', flush=True)
            assert call_local('preview_frame', {'sessionId':session['sessionId']})['previewDataUrl']
            assert call_local('thumbnail', {'sessionId':session['sessionId']})['thumbnailDataUrl']
            print('rpc:recipe', flush=True)
            call_local('manage_recipes', {'action':'save','name':'英版配方','sessionId':session['sessionId']})
            task = call_local('export_videos', {'sessionId':session['sessionId'],'requestId':'desktop-test'})
            print('rpc:task:' + task['taskId'], flush=True)
            for _ in range(200):
                task = call_local('get_task', {'taskId':task['taskId']})
                if task['status'] not in ('running','queued'):
                    break
                time.sleep(.1)
            assert task['status'] == 'completed', task
            assert Path(task['items'][0]['output']).name == 'chosen.mp4'
            result.update(ok=True, taskId=task['taskId'])
        except Exception:
            result.update(ok=False, error=traceback.format_exc())
        print(json.dumps(result), flush=True)
    threading.Thread(target=run, daemon=True).start()
    def finish():
        if not result:
            app.after(100,finish)
            return
        assert result['ok'], result
        if not result.get('desktopExport'):
            assert app.active_task == result['taskId']
        assert '英版配方' in app.recipes
        assert len(app.jobs) == 1
        job = next(iter(app.jobs.values()))
        assert job['config']['fit'] == 'zoom' and job['config']['zoom'] == 135
        assert job['outputName'] == 'chosen.mp4'
        if not result.get('desktopExport'):
            if app.busy:
                app.after(100, finish)
                return
            result['desktopExport'] = True
            job['output'] = None
            print('desktop-start:' + str((app.busy, app.importing, bool(job.get('info')), job.get('requiresTemplateReview'))), flush=True)
            app.confirm_batch = lambda _pending: True
            app.skip_border_preflight = True
            app.start_export()
            app.after(100, finish)
            return
        if app.active_task == result['taskId'] or app.busy:
            app.after(100, finish)
            return
        final = app.engine.get_task(app.active_task)
        assert final['status'] == 'completed', final
        assert final['items'][0]['config']['zoom'] == 135
        assert Path(final['items'][0]['output']).name == 'chosen (2).mp4'
        app.close()
    app.after(100,finish)
app.after(40000, lambda: (errors.append('timeout'), app.destroy()))
def watchdog():
    if errors:
        app.destroy()
    else:
        app.after(100, watchdog)
app.after(100, watchdog)
app.mainloop()
assert not errors, errors
if not restart:
    assert result.get('ok'), result
''', encoding='utf-8')
    environment = dict(os.environ, LOCALAPPDATA=str(tmp_path / 'local'), PYTHONUTF8='1')
    for extra in ([], ['restart']):
        completed = subprocess.run([sys.executable, str(script), str(plugin), str(tmp_path), *extra],
                                   env=environment, capture_output=True, encoding='utf-8', timeout=60)
        assert completed.returncode == 0, completed.stdout + completed.stderr
    settings = json.loads((tmp_path / 'local/FrameFit/settings.json').read_text(encoding='utf-8'))
    assert settings['quality'] == '标准画质（文件更小）'
    assert '英版配方' in settings['recipes']
