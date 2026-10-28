"""Exercise the actual Tk queue, report navigation and responsive export dialog."""
import os
from pathlib import Path
import subprocess
import sys

import pytest


@pytest.mark.skipif(os.name != 'nt', reason='Windows Tk integration')
def test_queue_report_and_export_dialog(tmp_path):
    script = tmp_path / 'check.py'
    script.write_text(r'''
import os, sys, time
from pathlib import Path
from PIL import Image
plugin, state = map(Path, sys.argv[1:])
sys.path[:0] = [str(plugin/'desktop/source'), str(plugin/'server')]
import app as gui
from template_catalog import identity
gui.App.start_local_service = lambda self: None
gui.App.detect_gpu = lambda self: None
gui.App.restore_last_task = lambda self: None
gui.App.ensure_timeline_cache = lambda *a: None
gui.check_update = lambda *a: None
app = gui.App()
assert app.template is None and app.display_image is None, 'Fresh startup must not load a built-in black preview'
app.engine = gui.engine_module.Engine(plugin, state/'engine')
errors = []
app.report_callback_exception = lambda *args: errors.append(str(args))
gui.messagebox.showerror = lambda title, message, **kw: errors.append(str(message))
try:
    en, jp = state/'1-45EN.png', state/'1-916JP.png'
    Image.new('RGB', (320,180), '#467997').save(en)
    Image.new('RGB', (180,320), '#467997').save(jp)
    app.load_template(en, quiet=True)
    app.ratio_var.set('跟随套版')
    assert app.output_size() == (320,180), 'Follow-template size still overridden by legacy resolution'
    assert not hasattr(app, 'size_combo'), 'Duplicate size selector remains visible'
    video = state/'v968 1920X1080 V5.mp4'
    video.touch()
    cfg = app._engine_profile(app.direction, {'template':str(en),'rect':[0,0,320,180]})
    info = {'width':320,'height':180,'duration':10}
    def add():
        iid = app.tree.insert('', 'end', values=(video.name,'320x180','10s','ready'))
        app.jobs[iid] = {'path':video,'info':info,'output':None,'error':'','template':str(en),'rect':[0,0,320,180]}
        app.tree.selection_set(iid)
        return iid
    iid = add()
    app.add_template_paths([en,jp])
    def remove_managed_template():
        try:
            dialog = next(w for w in app.winfo_children() if isinstance(w,gui.tk.Toplevel))
            browser=app.template_browser
            browser.selected={identity(browser.ordered[0]['path'])}
            app.update()
            browser.delete_selected()
            app.update()
            assert len(app.output_templates)==2, 'Global manager must not change current queue selection'
            assert len(app.template_library)==1, 'Delete removes only the global library record'
            dialog.destroy()
        except Exception as exc:
            errors.append(repr(exc))
            dialog.destroy()
    app.after(80,remove_managed_template)
    app.manage_output_templates()
    # The manager is nonblocking; process its scheduled keyboard action.
    deadline=time.monotonic()+2
    while len(app.output_templates)==2 and time.monotonic()<deadline:
        app.update()
        time.sleep(.01)
        assert len(app.output_templates)==2
    app.add_template_paths([en,jp])
    app.expand_output_jobs()
    assert len(app.jobs) == 2
    app.reset_pending_variants()
    assert len(app.jobs) == 1, 'Changing templates must return expanded rows to their source'
    assert not any('单套版' in value or '多套版' in value for value in gui.TEMPLATE_ACTIONS)
    for key in list(app.jobs):
        app.jobs.pop(key)
        app.tree.delete(key)
    task = {'taskId':'regression-task','version':'1.3.0','status':'completed','config':cfg,'progress':1,
        'items':[{'path':str(video),'name':video.name,'info':info,'status':'completed','progress':1,'config':cfg}]}
    app.attach_task(task)
    iid = next(iter(app.jobs))
    app.tree.selection_set(iid)
    app.tree.focus_force()
    app.update()
    app.tree.event_generate('<Delete>')
    app.update()
    assert not app.jobs, 'Delete failed to remove selected queue row'
    app.attach_task(task)
    assert not app.jobs, 'Previously removed video reappeared from old task'
    iid = add()
    app.tree.focus_force()
    app.update()
    app.canvas.focus_force()
    app.canvas.event_generate('<Delete>')
    app.update()
    assert not app.jobs, 'Delete fails after moving focus to preview'
    iid = add()
    app.show_borders(iid, {'segments':[{'color':'black','kind':'dark_seam','locations':['横向暗缝 50%'],'start':2.5,'end':3}]})
    assert '疑似暗缝' in app.seek_slider.markers[0]['label']
    assert 'inspection' not in app.pages and 'inspection' not in app.nav_buttons
    from types import SimpleNamespace
    app.seek_slider.configure(to=10)
    old_seek=app.seek_var.get()
    x=8+(app.seek_slider.winfo_width()-16)*.275
    app.seek_slider.marker_motion(SimpleNamespace(x=x))
    assert app.seek_slider.marker_popup is not None, 'Hovering a colored segment must show detail'
    assert app.seek_var.get()==old_seek, 'Hovering must not seek'
    app.seek_slider.hide_marker_hint()
    app.seek_slider.dragging=True
    app.seek_slider.marker_motion(SimpleNamespace(x=x))
    assert app.seek_slider.marker_popup is None, 'Do not show a hover popup while dragging'
    app.seek_slider.dragging=False
    fields=[]
    def all_widgets(widget):
        for child in widget.winfo_children():
            yield child
            yield from all_widgets(child)
    fields=[widget for widget in all_widgets(app) if isinstance(widget,gui.NumberField)]
    assert len(fields)>=5, 'Coordinates and free zoom must share the numeric editor'
    field=fields[0]
    app.rect_vars[2].set('100')
    field.begin_scrub(SimpleNamespace(x_root=100))
    field.scrub(SimpleNamespace(x_root=120,state=0))
    field.end_scrub()
    assert app.rect_vars[0].get()=='20'
    app.rect_vars[0].set('0');app.rect_vars[2].set('320')
    gui.filedialog.askdirectory=lambda **kw:str(state/'chosen schedule')
    app.choose_path(app.schedule_root_var)
    assert app.schedule_root_var.get()==str(state/'chosen schedule')
    app.jobs[iid]['config'] = cfg
    app.output_var.set(str(state/'out'))
    app.jobs[iid]['config']['outputDir'] = str(state/'out')
    def inspect_dialog():
        try:
            dialog = next(w for w in app.winfo_children() if isinstance(w, gui.tk.Toplevel))
            dialog.geometry('760x560')
            app.update()
            def walk(w):
                for child in w.winfo_children():
                    yield child
                    yield from walk(child)
            widgets = list(walk(dialog))
            table = next(w for w in widgets if isinstance(w,gui.ttk.Treeview))
            assert 'language' in table['columns']
            assert table.set(iid, 'language') == '英', 'Template EN language not matched'
            for title in ('确认并导出', '返回修改', '应用名称'):
                button = next(w for w in widgets if isinstance(w,gui.Button) and w.cget('text') == title)
                assert button.winfo_viewable(), title
                assert button.winfo_rooty()+button.winfo_height() <= dialog.winfo_rooty()+dialog.winfo_height(), title+' clipped'
            next(w for w in widgets if isinstance(w,gui.Button) and w.cget('text') == '返回修改').invoke()
        except Exception as exc:
            errors.append(repr(exc))
            dialog.destroy()
    app.after(80,inspect_dialog)
    assert app.confirm_batch([(iid,dict(app.jobs[iid]))]) is False
    def choose_japanese_and_accept():
        try:
            dialog=next(w for w in app.winfo_children() if isinstance(w,gui.tk.Toplevel))
            def walk(w):
                for child in w.winfo_children():
                    yield child
                    yield from walk(child)
            widgets=list(walk(dialog))
            combo=next(w for w in widgets if isinstance(w,gui.Field) and w.kind=='combo' and list(w.input.cget('values'))==gui.LANGUAGE_OPTIONS)
            combo.set('日')
            combo.event_generate('<<ComboboxSelected>>')
            next(w for w in widgets if isinstance(w,gui.Button) and w.cget('text')=='确认并导出').invoke()
        except Exception as exc:
            errors.append(repr(exc))
            dialog.destroy()
    app.after(80,choose_japanese_and_accept)
    pending=[(iid,dict(app.jobs[iid]))]
    assert app.confirm_batch(pending) is True
    assert pending[0][1]['config']['language']=='日'
    assert pending[0][1]['outputName'].endswith(' 日.mp4')
    app.toggle_queue()
    app.update()
    assert app.queue_expanded
    for number in range(15):
        row=app.tree.insert('', 'end',values=(f'{number}.mp4','320x180','10s','ready'))
        app.jobs[row]={'path':state/f'{number}.mp4','info':info,'output':None,'error':''}
    app.tree.yview_moveto(1)
    app.update()
    assert app.tree.yview()[0]>0
    app.toggle_queue()
    app.refresh_template_choices(app.jobs[iid])
    assert len(app.preview_variants)==2
    app.preview_template_var.set('2 · '+Path(app.preview_variants[1]['template']).name)
    app.change_preview_template()
    assert str(app.template)==app.preview_variants[1]['template']
    app.show_page('settings')
    app.match_paths_var.set(False)
    app.save_settings()
    assert app.machine_settings['matchPaths'] is False
    app.name_pattern_var.set('[原名]_[尺寸]_[语言]_[序号:03]')
    app.name_mode_var.set('自定义规则')
    assert '001' in app.name_example_var.get()
    app.output_templates=[]
    for key,job in app.jobs.items():
        job.update(templates=[],borderReport={'segments':[]},requiresTemplateReview=False)
    app.jobs[iid].update(output='previous-result.mp4',outputKey='completed',config=cfg)
    captured=[]
    app.confirm_batch=lambda pending:(captured.append([key for key,_ in pending]) or False)
    app.tree.selection_set(iid)
    app.skip_border_preflight=True
    app.start_export()
    assert captured[-1]==[iid], 'Explicit repeat must include the selected completed video only'
    app.tree.selection_remove(*app.tree.selection())
    app.skip_border_preflight=True
    app.start_export()
    assert set(captured[-1])==set(app.jobs), 'No selection must include the whole queue, including completed videos'
    app.export_scope=[iid]
    gui.messagebox.askyesnocancel=lambda *args,**kwargs:None
    app.continue_after_border_preflight([(iid,app.jobs[iid],en,[0,0,320,180],(320,180),
        {'newBorders':[{'seconds':1,'color':'black'}]})],'')
    assert app.export_scope is None, 'Returning from the border dialog must discard the previous selection scope'
    another=next(key for key in app.jobs if key!=iid)
    app.tree.selection_set(another)
    app.skip_border_preflight=True
    app.start_export()
    assert captured[-1]==[another], 'The next export must respect the newly selected video after cancelling preflight'
    app.output_templates=[]
    app.jobs[another]['templates']=[]
    app.tree.selection_set(another)
    app.add_template_paths([en,jp])
    expanded=app.expand_output_jobs([another])
    app.tree.selection_set(expanded[-1])
    gui.filedialog.askopenfilenames=lambda **kwargs:(str(en),)
    app.template_action_var.set('选择模板…')
    app.template_action()
    assert app.tree.selection(), 'Replacing templates must preserve source selection after collapsing variants'
    replaced=app.expand_output_jobs(list(app.tree.selection()))
    assert len(replaced)==1 and app.jobs[replaced[0]]['template']==str(en), 'Old per-source variants survived explicit replacement'
    app.active_task='resume-test'
    app.hidden_task_items['resume-test']=[2]
    resumed=[]
    app.engine.resume_task=lambda taskId,**kwargs:(resumed.append((taskId,kwargs)) or {'taskId':'filtered'})
    app.attach_task=lambda task:None
    app.resume_export()
    assert resumed==[('resume-test',{'excludeIndices':[2]})], 'Continue must use the frozen task and exclude removed items'
    previous_backend=app.backend_status.get()
    app.events.put(('update_check',None,'HTTP Error 404: Not Found'))
    app.poll()
    assert app.backend_status.get()==previous_backend
    assert '检查失败' in app.update_status_var.get()
    assert '404' in app.update_button.tooltip.text
    assert not errors, errors
finally:
    app.close()
''', encoding='utf-8')
    environment = dict(os.environ, LOCALAPPDATA=str(tmp_path/'local'), PYTHONUTF8='1')
    result = subprocess.run([sys.executable, str(script), str(Path(__file__).resolve().parents[1]), str(tmp_path)],
                            env=environment, capture_output=True, encoding='utf-8', timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
