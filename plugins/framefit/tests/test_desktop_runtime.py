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
            assert job['output'], 'First result should stay completed before an explicit repeat export'
            print('desktop-start:' + str((app.busy, app.importing, bool(job.get('info')), job.get('requiresTemplateReview'))), flush=True)
            def accept_real_dialog():
                def walk(widget):
                    for child in widget.winfo_children():
                        yield child
                        yield from walk(child)
                for widget in walk(app):
                    if isinstance(widget,(gui.tk.Button,gui.Button)) and widget.cget('text')=='确认并导出':
                        widget.invoke()
                        return
                app.after(50,accept_real_dialog)
            app.after(50,accept_real_dialog)
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


def test_scroll_keeps_options_and_preview_does_not_wait_for_project_scan(tmp_path):
    plugin = Path(__file__).resolve().parents[1]
    script = tmp_path / 'responsive_gui.py'
    script.write_text(r'''
import os,sys,time,threading,traceback
from pathlib import Path
from PIL import Image,ImageDraw,ImageChops
plugin,state=map(Path,sys.argv[1:])
sys.path[:0]=[str(plugin/'desktop/source'),str(plugin/'server')]
import app as gui
import core
from engine import atomic_json
gui.check_update=lambda *_:None
video,template=state/'video.mp4',state/'template.png'
im=Image.new('RGB',(160,90),'#376659')
ImageDraw.Draw(im).rectangle((20,10,139,79),fill='black')
im.save(template)
matched_template=state/'matched.png'
im.save(matched_template)
core.run([core.binary('ffmpeg'),'-v','error','-f','lavfi','-i','color=red:s=96x64:r=12:d=2','-c:v','libx264',str(video)])
atomic_json(Path(os.environ['LOCALAPPDATA'])/'FrameFit/settings.json',{
 'profiles':{'landscape':{'template':str(template),'rect':[20,10,120,70],'size':'套框原始尺寸'}},
 'machine':{'scheduleRoot':str(state/'schedule'),'templateRoot':str(state)}})
gate=threading.Event()
def slow_match(*a,**kw):
    gate.wait(15)
    return {'status':'matched','candidates':[{'path':str(matched_template)}]}
def slow_scan(*a,**kw):
    gate.wait(15)
    return {'segments':[]}
gui.find_project_candidates=slow_match
core.analyze_borders=slow_scan
app=gui.App()
app.update()
errors=[]
app.report_callback_exception=lambda *args:errors.append(str(args))
try:
    assert app.theme_mode=='system'
    assert not app.bind('<F11>')
    assert app.focus_button.master is app.canvas
    app.apply_theme('light')
    assert app.palette['panel']=='#ffffff'
    app.apply_theme('dark')
    assert app.palette['panel']=='#1d202a'
    app.apply_theme('system')
    for page in ('templates','settings','workbench'):
        app.show_page(page)
        app.update()
        assert app.pages[page].winfo_ismapped(),(page,app.pages[page].grid_info(),app.state(),app.pages[page].winfo_width(),app.pages[page].winfo_height())
    app.focus_button.tooltip.show()
    assert app.focus_button.tooltip.popup.winfo_exists()
    app.focus_button.tooltip.hide()
    assert app.tree.winfo_height()>=55, 'Queue must show its heading and at least one complete row'
    assert app.tree.master.master.winfo_height()>=app.tree.winfo_height(), 'List rows extend outside their visible card'
    app.mode_combo.show_choices()
    app.update()
    popup=app.mode_combo.popup
    assert popup and popup.winfo_ismapped()
    choices=next(child for child in popup.winfo_children()[0].winfo_children() if isinstance(child,gui.tk.Listbox))
    choices.selection_clear(0,'end')
    choices.selection_set(1)
    choices.event_generate('<Return>')
    app.update()
    assert app.mode_var.get()==list(gui.MODES)[1]
    assert app.mode_combo.popup is None
    app.mode_var.set(list(gui.MODES)[0])
    # A themed prompt must return to its caller and preserve the same Tk loop.
    def dismiss_prompt():
        def walk(widget):
            for child in widget.winfo_children():
                yield child
                yield from walk(child)
        button=next(widget for widget in walk(app) if isinstance(widget,gui.Button) and widget.cget('text')=='确定')
        button.invoke()
    app.after(50,dismiss_prompt)
    assert gui.messagebox.showinfo('控件检查','圆角提示框验证',parent=app)=='ok'
    original=app.mode_var.get()
    app.mode_combo.event_generate('<MouseWheel>',delta=-120)
    app.update()
    assert app.mode_var.get()==original,'Mouse wheel changed fitting mode'
    app.add_videos([video])
    deadline=time.monotonic()+6
    while time.monotonic()<deadline:
        app.update()
        image=app.display_image
        if image and image.getpixel((80,45))[0]>180:
            break
        time.sleep(.02)
    assert not errors,errors
    assert image and image.getpixel((80,45))[0]>180,'Video preview waited for project/border scan'
    assert not gate.is_set()
    gate.set()
    deadline=time.monotonic()+4
    while app.importing and time.monotonic()<deadline:
        app.update()
        time.sleep(.02)
    assert not app.importing
    assert app.template==matched_template,'Preview retained old template after successful background matching'
    assert not app.direction_buttons and not hasattr(app,'storyboard')
    assert app.canvas.winfo_height()>350,app.canvas.winfo_height()
    assert app.seek_slider.master.winfo_rooty()+app.seek_slider.master.winfo_height()<=app.canvas.master.winfo_rooty()+app.canvas.master.winfo_height(), 'Timeline extends outside the preview card'
    height=app.canvas.winfo_height()
    app.toggle_preview_focus()
    app.update()
    assert app.canvas.winfo_height()>height,(height,app.canvas.winfo_height(),app.canvas.master.winfo_height(),[(w.winfo_height(),w.winfo_manager()) for w in app.queue_sections],app.canvas.master.master.master.master.grid_info(),app.canvas.master.master.master.master.winfo_height(),app.canvas.master.master.master.master.grid_bbox())
    app.toggle_preview_focus()
    app.ratio_var.set('1:1 · 1080×1080')
    app.change_ratio()
    assert app.output_size()==(1080,1080)
    app.ratio_var.set('跟随套版')
    app.change_ratio()
    app.add_template_paths([template,matched_template])
    job=next(iter(app.jobs.values()))
    job['borderScanning']=True
    app.start_export()
    assert len(app.jobs)==1,'Expanding outputs lost an in-flight source scan'
    job['borderScanning']=False
    app.expand_output_jobs()
    assert len(app.jobs)==2
    assert len({job['outputKey'] for job in app.jobs.values()})==2
    first=next(iter(app.jobs))
    app.tree.selection_set(first)
    app.select_queue_item()
    deadline=time.monotonic()+5
    while time.monotonic()<deadline and not app.jobs[first].get('scrubFrames'):
        app.update()
        time.sleep(.02)
    assert app.jobs[first].get('scrubFrames'),'No timeline cache generated'
    app.dragging_seek=True
    app.on_seek(.5)
    assert app.display_image is not None
    app.dragging_seek=False
    for iid in list(app.jobs):
        app.tree.delete(iid)
    app.jobs.clear()
    variants=[{'template':str(template),'rect':[20,10,120,70],'direction':'custom','resolution':'original','outputSize':size}
              for size in ([160,160],[160,200])]
    session=app.engine.prepare_config([str(video)],{'direction':'custom','template':str(template),'resolution':'original'},outputTemplates=variants)
    app.apply_remote_config(session)
    assert len(app.jobs)==2,'MCP variants collapsed to one row'
    for iid,job in app.jobs.items():
        app.tree.selection_set(iid)
        app.select_queue_item()
        assert list(app.output_size())==job['config']['outputSize']
        assert list(app.output_size()) in ([160,160],[160,200])
    def slow_preflight(*args,**kwargs):
        core.run([sys.executable,'-c','import time; time.sleep(30)'])
    core.analyze_composite_borders=slow_preflight
    app.run_border_preflight(list(app.jobs.items()),app.direction,app.size_var.get(),'auto','cover',100)
    app.after(250,app.cancel_export)
    deadline=time.monotonic()+3
    while app.busy and time.monotonic()<deadline:
        app.update()
        time.sleep(.02)
    assert not app.busy,'Cancel did not stop preflight media process'
    gui.messagebox.askyesno=lambda *args,**kwargs:True
    app.run_border_preflight(list(app.jobs.items()),app.direction,app.size_var.get(),'auto','cover',100)
    app.after(250,app.close)
    deadline=time.monotonic()+3
    closed=False
    while time.monotonic()<deadline:
        try:
            app.update()
            if not app.winfo_exists():
                closed=True
                break
        except gui.tk.TclError:
            closed=True
            break
        time.sleep(.02)
    assert closed,'Closing window waited for a stalled preflight'
finally:
    gate.set()
    try:
        if app.winfo_exists():
            app.close()
    except gui.tk.TclError:
        pass
''',encoding='utf-8')
    env = dict(os.environ, LOCALAPPDATA=str(tmp_path/'local'), PYTHONUTF8='1')
    completed = subprocess.run([sys.executable,str(script),str(plugin),str(tmp_path)],
                               env=env,capture_output=True,encoding='utf-8',timeout=35)
    assert completed.returncode == 0, completed.stdout+completed.stderr
