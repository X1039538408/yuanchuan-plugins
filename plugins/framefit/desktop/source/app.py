"""FrameFit — a portable Windows video framing utility."""
import json
import math
import os
import ctypes
from pathlib import Path
import queue
import sys
import threading
import time
import traceback
import tkinter as tk
from tkinter import ttk, filedialog
try:
    import winsound
except ImportError:
    winsound=None

from PIL import Image, ImageTk, ImageDraw
from tkinterdnd2 import DND_FILES, TkinterDnD

RUNTIME_ROOT = Path(sys.executable).resolve().parents[1] if getattr(sys, 'frozen', False) else Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RUNTIME_ROOT / 'server'))
import core
import engine as engine_module
import naming
core.ROOT = RUNTIME_ROOT
from framefit_workflow import find_project_candidates, parse_video_name, output_language
from updater import check_update, stage_update, activate_update
from ui import Card, Button, Field, NumberField, PreviewButton, NavigationButton, CheckButton, Scrollbar, Progress, Slider, Dialogs, Tooltip, LIGHT, DARK, system_theme, button_help, option_help, rounded_path
from template_browser import TemplateBrowser

messagebox = Dialogs()
simpledialog = messagebox

BG = '#11181d'
PANEL = '#1b252c'
TEXT = '#edf3f2'
MUTED = '#9daeb7'
ACCENT = '#7ee2c4'
MODES = {'等比填满（推荐）': 'cover', '完整显示': 'contain', '拉伸铺满': 'stretch', '自由缩放': 'zoom'}
SIZES = {'1080P · 1920 × 1080': (1920, 1080), '720P · 1280 × 720': (1280, 720),
         '4K · 3840 × 2160': (3840, 2160), '套框原始尺寸': None}
PORTRAIT_SIZES = {'1080P · 1080 × 1920': (1080, 1920), '720P · 720 × 1280': (720, 1280),
                  '4K · 2160 × 3840': (2160, 3840), '套框原始尺寸': None}
CUSTOM_SIZES = {'跟随套版原始尺寸': None}
DIRECTIONS = {'landscape': ('竖转横', SIZES, 'default.png'),
              'portrait': ('横转竖', PORTRAIT_SIZES, 'default_portrait.png'),
              'custom': ('自定义比例', CUSTOM_SIZES, None)}
BACKENDS = {'自动（优先 GPU）': 'auto', 'NVIDIA GPU · NVENC': 'nvenc', 'CPU · 兼容模式': 'cpu'}
DETECTION = {'自动（透明 / 黑 / 白）': 'auto', '仅黑色区域': 'black',
             '仅白色区域': 'white', '仅透明区域': 'transparent'}
CONFIG = Path(os.getenv('LOCALAPPDATA', Path.home())) / 'FrameFit' / 'settings.json'
EXTENSIONS = {'.mp4', '.mov', '.mkv', '.avi', '.webm', '.m4v', '.ts', '.wmv'}
RATIOS = {'跟随套版': None, '16:9 · 1920×1080': (1920,1080), '9:16 · 1080×1920': (1080,1920),
          '1:1 · 1080×1080': (1080,1080), '4:5 · 1080×1350': (1080,1350),
          '3:4 · 1080×1440': (1080,1440), '4:3 · 1440×1080': (1440,1080),
          '2:3 · 1080×1620': (1080,1620), '3:2 · 1620×1080': (1620,1080),
          '21:9 · 2520×1080': (2520,1080), '自定义尺寸…': None}
LANGUAGE_OPTIONS = ['自动识别', '简中', '繁中', '英', '日', '韩', '其他', '不添加']
TEMPLATE_ACTIONS = ['管理套版…']


class App(TkinterDnD.Tk):
    def __init__(self):
        if os.name=='nt':
            try:
                shell32=ctypes.WinDLL('shell32',use_last_error=True)
                shell32.SetCurrentProcessExplicitAppUserModelID.argtypes=[ctypes.c_wchar_p]
                shell32.SetCurrentProcessExplicitAppUserModelID.restype=ctypes.c_long
                # A new AppUserModelID is required because Windows caches the
                # taskbar button icon by ID; the old ID was associated with
                # Tk's feather icon on existing installations.
                shell32.SetCurrentProcessExplicitAppUserModelID('Yuanchuan.FrameFitStudio.160')
            except (AttributeError,OSError):
                pass
        # The default Tk window class is shared with Python's feather icon.
        # Give FrameFit its own native class so the taskbar never resolves the
        # generic Tk class icon, even when WM_SETICON is delayed by Windows.
        super().__init__(className='FrameFitStudio')
        self.theme_mode = 'system'
        try:
            self.theme_mode = json.loads(CONFIG.read_text(encoding='utf-8')).get('theme','system')
        except (OSError,ValueError):
            pass
        self.theme_mode = self.theme_mode if self.theme_mode in ('system','light','dark') else 'system'
        self.resolved_theme = system_theme() if self.theme_mode=='system' else self.theme_mode
        self.palette = dict(LIGHT if self.resolved_theme=='light' else DARK)
        self.set_palette_globals()
        self.title('视频套版 FrameFit · 本地批量套版')
        self._apply_window_icon(self)
        self.geometry('1440x960')
        self.minsize(1100, 760)
        self.configure(bg=BG)
        self.columnconfigure(0, weight=1)
        self.rowconfigure(1, weight=1)
        self.option_add('*Font', ('Microsoft YaHei UI', 10))
        self.events = queue.Queue()
        self.jobs = {}
        self.busy = False
        self.active_task = None
        self.monitoring_task = False
        self.remote_items = {}
        self.task_rows = {}
        self.save_pending = None
        self.loading_selection = False
        self.dragging_seek = False
        self.importing = 0
        self.cancel = threading.Event()
        self.scan_slots = threading.Semaphore(1)
        self.output_templates = []
        self.template_library = []
        self.export_mode_var = tk.StringVar(value='single')
        self.template_action_var = tk.StringVar(value='选择模板…')
        self.export_scope = None
        self.queue_expanded = False
        self.hidden_task_items = {}
        self.notified_tasks = set()
        self.border_source_iid = None
        self.timeline_stop = threading.Event()
        self.scrub_running = False
        self.scrub_target = None
        self.closing = False
        self.preview_slots = threading.Semaphore(2)
        self.border_preflight_running = False
        self.skip_border_preflight = False
        self.skip_batch_summary = False
        self.preview_id = 0
        self.preview_pending = None
        self.storyboard_ticket = 0
        self.storyboard_times = []
        self.storyboard_photo = None
        self.border_var = tk.StringVar(value='原视频黑白边扫描会在加入后自动开始')
        self.border_points = []
        self.display_image = None
        self.template_image = None
        self.template = None
        self.direction = 'landscape'
        self.profiles = {}
        self.controls = []
        self.rect_vars = [tk.StringVar(value=v) for v in ('0', '0', '100', '100')]
        self.size_var = tk.StringVar(value=list(SIZES)[0])
        self.size_info_var = tk.StringVar()
        self.mode_var = tk.StringVar(value=list(MODES)[0])
        self.zoom_var = tk.StringVar(value='100')
        self.numeric_mode_var = tk.StringVar(value='拖动调整')
        self.quality_var = tk.StringVar(value='高画质')
        self.backend_var = tk.StringVar(value=list(BACKENDS)[0])
        self.detection_var = tk.StringVar(value=list(DETECTION)[0])
        self.backend_status = tk.StringVar(value='检测 GPU 编码能力中…')
        self.output_var = tk.StringVar(value=str(Path.home() / 'Videos' / '套版导出'))
        # Each installation chooses its own project and template locations.
        # Existing settings loaded below still take precedence.
        self.schedule_root_var = tk.StringVar(value='')
        self.template_root_var = tk.StringVar(value='')
        self.project_keywords_var = tk.StringVar(value='')
        self.match_paths_var = tk.BooleanVar(value=True)
        self.notify_completion_var = tk.BooleanVar(value=True)
        self.sound_completion_var = tk.BooleanVar(value=False)
        self.completion_sound_var = tk.StringVar(value='')
        self.update_status_var = tk.StringVar(value='')
        self.preview_template_var = tk.StringVar(value='未选择套版')
        self.preview_variants=[]
        self.preview_override=None
        self.machine_settings = {}
        self.language_var = tk.StringVar(value='自动识别')
        self.other_language_var = tk.StringVar()
        self.name_mode_var = tk.StringVar(value='精简命名')
        self.name_pattern_var = tk.StringVar(value=naming.DEFAULT_PATTERN)
        self.name_preset_var = tk.StringVar(value='简洁尺寸语言')
        self.name_presets = {'简洁尺寸语言':naming.DEFAULT_PATTERN,'项目与套版':'[原名]_[项目]_[套版]_[语言]',
                             '尺寸与日期序号':'[原名]_[尺寸]_[语言]_[日期]_[序号:03]'}
        self.name_example_var = tk.StringVar()
        self.match_reference_var = tk.StringVar(value='选择视频后显示：项目 → 排期游戏 → 本机套版目录')
        self.output_group_var = tk.StringVar(value='统一文件夹')
        self.recipe_name_var = tk.StringVar()
        self.recipe_var = tk.StringVar()
        self.ratio_var = tk.StringVar(value='跟随套版')
        self.multi_var = tk.StringVar(value='单套版 · 使用当前设置')
        self.recipes = {}
        self.status_var = tk.StringVar(value='添加竖屏视频，即可开始套版')
        self.template_var = tk.StringVar(value='内置 · 仙界横版套框')
        self.preview_var = tk.StringVar(value='套框预览  /  绿色区域用于视频初始对齐')
        self.queue_var = tk.StringVar(value='视频队列 · 0 个')
        self.progress_var = tk.DoubleVar()
        self.time_var = tk.StringVar(value='0.0s')
        self.seek_var = tk.DoubleVar(value=0.0)
        self._style()
        self._layout()
        self.apply_theme(self.theme_mode, save=False)
        self.after(1500,self.watch_system_theme)
        self.protocol('WM_DELETE_WINDOW', self.close)
        self.drop_target_register(DND_FILES)
        self.dnd_bind('<<Drop>>', self.drop)
        self.after(80, self.poll)
        self.load_settings()
        self.save_settings()
        self.local_service = self.start_local_service()
        self.pending_update = None
        self.staged_update = None
        self.restore_last_task()
        self.install_root = Path(os.getenv('FRAMEFIT_INSTALL_ROOT', '')).resolve() if os.getenv('FRAMEFIT_INSTALL_ROOT') else None
        self.after(2500, self.check_for_updates)
        self.detect_gpu()
        for var in self.rect_vars:
            var.trace_add('write', lambda *_: self.schedule_preview())
        self.size_var.trace_add('write', lambda *_: self.update_size_info())
        self.mode_var.trace_add('write', self.update_zoom_controls)
        self.zoom_var.trace_add('write', self.schedule_preview)
        for variable in [*self.rect_vars, self.size_var, self.mode_var, self.zoom_var,
                         self.ratio_var, self.quality_var, self.backend_var, self.output_var, self.language_var,
                         self.other_language_var, self.name_mode_var, self.output_group_var,
                         self.schedule_root_var, self.template_root_var, self.match_paths_var, self.numeric_mode_var,
                         self.notify_completion_var, self.sound_completion_var, self.completion_sound_var]:
            variable.trace_add('write', self.schedule_save)
        self.name_pattern_var.trace_add('write',self.update_name_example)
        self.name_pattern_var.trace_add('write',self.schedule_save)
        self.update_name_example()
        self.after(500, self.monitor_task)
        self.update_zoom_controls()
        if self.local_service:
            self.after(0, self.local_service.mark_ready)

    def set_palette_globals(self):
        global BG,PANEL,TEXT,MUTED,ACCENT
        BG,PANEL,TEXT,MUTED,ACCENT = (self.palette[key] for key in ('bg','panel','text','muted','accent'))

    def _apply_window_icon(self, window):
        """Set Tk, PNG and native Win32 icons so the taskbar cannot fall back."""
        ico=RUNTIME_ROOT/'assets'/'icon.ico'
        png=RUNTIME_ROOT/'assets'/'icon.png'
        if ico.is_file():
            try: window.iconbitmap(str(ico))
            except (OSError,tk.TclError): pass
        try:
            with Image.open(png if png.is_file() else ico) as image:
                photo=ImageTk.PhotoImage(image.convert('RGBA').resize((32,32),Image.Resampling.LANCZOS),master=window)
            window._framefit_icon_photo=photo
            window.iconphoto(True,photo)
        except (OSError,tk.TclError):
            pass
        if os.name=='nt' and ico.is_file():
            try:
                user32=ctypes.WinDLL('user32',use_last_error=True)
                user32.LoadImageW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_uint,ctypes.c_int,ctypes.c_int,ctypes.c_uint]
                user32.LoadImageW.restype=ctypes.c_void_p
                user32.SendMessageW.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_void_p]
                user32.SendMessageW.restype=ctypes.c_void_p
                # LR_LOADFROMFILE | LR_DEFAULTSIZE, IMAGE_ICON.
                handle=user32.LoadImageW(None,str(ico),1,0,0,0x00000010|0x00000040)
                if handle:
                    hwnd=ctypes.c_void_p(window.winfo_id())
                    user32.SendMessageW(hwnd,0x0080,1,handle)  # WM_SETICON/ICON_BIG
                    user32.SendMessageW(hwnd,0x0080,0,handle)  # WM_SETICON/ICON_SMALL
                    user32.SendMessageW(hwnd,0x0080,2,handle)  # ICON_SMALL2 used by taskbar
                    # Some Windows 11 taskbar paths query the window class
                    # icon instead of WM_GETICON, especially for Tk windows.
                    set_class=user32.SetClassLongPtrW
                    set_class.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p]
                    set_class.restype=ctypes.c_void_p
                    set_class(hwnd,-14,handle)  # GCLP_HICON
                    set_class(hwnd,-34,handle)  # GCLP_HICONSM
                    window._framefit_native_icon=handle
                ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID('Yuanchuan.FrameFitStudio.160')
            except (AttributeError,OSError,TypeError):
                pass
        # Tk creates the native taskbar window only after the first map.  A
        # WM_SETICON sent during __init__ can therefore be ignored; retry once
        # after mapping so the taskbar receives the same FrameFit icon.
        if getattr(window,'_framefit_icon_retry_count',0) < 2:
            window._framefit_icon_retry_count=getattr(window,'_framefit_icon_retry_count',0)+1
            window._framefit_icon_retry=True
            try: window.after(350,lambda:self._retry_window_icon(window))
            except tk.TclError: pass

    def _retry_window_icon(self, window):
        try:
            if window.winfo_exists(): self._apply_window_icon(window)
        except tk.TclError:
            pass

    def apply_theme(self, mode, save=True):
        self.theme_mode = mode
        self.resolved_theme = system_theme() if mode=='system' else mode
        self.palette = dict(LIGHT if self.resolved_theme=='light' else DARK)
        self.set_palette_globals()
        role_values = {'bg':{'#11181d'},'panel':{'#1b252c'},'text':{'#edf3f2'},'muted':{'#9daeb7','#657780'},
            'accent':{'#7ee2c4'},'line':{'#344650','#304049'},'field':{'#26343d','#2a3942'},'soft':{'#315349','#a5efda'}}
        for palette in (LIGHT,DARK):
            for role,color in palette.items():
                role_values.setdefault(role,set()).add(color)
        def recolor(widget):
            if isinstance(widget,(Field,Scrollbar,Progress,Slider)):
                widget.refresh_palette(self.palette)
            if isinstance(widget,Card):
                widget.palette=self.palette
                widget.redraw()
            if not hasattr(widget,'_color_roles'):
                widget._color_roles={}
                for option in ('bg','fg','background','foreground','highlightbackground','insertbackground','selectbackground','activebackground','activeforeground','disabledforeground'):
                    try:
                        value=widget.cget(option)
                    except tk.TclError:
                        continue
                    for role,values in role_values.items():
                        if value in values:
                            widget._color_roles[option]=role
                            break
            for option,role in widget._color_roles.items():
                try:
                    widget.configure(**{option:self.palette[role]})
                except tk.TclError:
                    pass
            if isinstance(widget,(tk.Button,Button)):
                widget.configure(bg=ACCENT if getattr(widget,'primary',False) else self.palette['field'],
                    fg='#ffffff' if getattr(widget,'primary',False) else TEXT,highlightbackground=self.palette['line'])
            for child in widget.winfo_children():
                recolor(child)
        recolor(self)
        self.configure(bg=BG)
        self._style()
        for key,widget in getattr(self,'theme_buttons',{}).items():
            widget.configure(bg=self.palette['soft'] if key==mode else PANEL,fg=ACCENT if key==mode else MUTED)
        self.refresh_navigation()
        if hasattr(self,'canvas'):
            self.draw_preview()
        if save and hasattr(self,'template_image'):
            self.save_settings()

    def watch_system_theme(self):
        if self.theme_mode=='system' and system_theme()!=self.resolved_theme:
            self.apply_theme('system',save=False)
        self.after(1500,self.watch_system_theme)

    def refresh_navigation(self):
        for key,button in getattr(self,'nav_buttons',{}).items():
            active=key==getattr(self,'active_page','workbench')
            button.configure(bg=self.palette['soft'] if active else BG,fg=ACCENT if active else MUTED)

    def show_page(self, name):
        self.active_page=name
        for key,page in self.pages.items():
            if key==name:
                page.grid()
            else:
                page.grid_remove()
        self.page_title_var.set({'workbench':'套版工作台','templates':'输出模板','settings':'设置'}[name])
        self.refresh_navigation()

    def card(self,parent,**pack):
        card=Card(parent,self.palette)
        if pack:
            card.pack(**pack)
        card.content.card=card
        return card.content

    def dialog_surface(self, title, geometry):
        """Create a modal dialog whose visible content follows the app radius rules."""
        dialog = tk.Toplevel(self)
        dialog.withdraw()
        dialog.title(title)
        self._apply_window_icon(dialog)
        dialog.transient(self)
        width,height=map(int,geometry.split('x'))
        x=max(4,min(self.winfo_screenwidth()-width-12,self.winfo_rootx()+(self.winfo_width()-width)//2))
        y=max(4,min(self.winfo_screenheight()-height-45,self.winfo_rooty()+(self.winfo_height()-height)//2))
        dialog.geometry(f'{geometry}+{x}+{y}')
        dialog.configure(bg=BG)
        shell = Card(dialog, self.palette, padding=16)
        shell.pack(fill='both', expand=True, padx=8, pady=8)
        dialog._framefit_shell = shell
        dialog.deiconify()
        dialog.grab_set()
        return dialog, shell.content

    def _style(self):
        style = ttk.Style(self)
        style.theme_use('clam')
        style.configure('Treeview', background=PANEL, fieldbackground=PANEL, foreground=TEXT,
                        rowheight=32, borderwidth=0, font=('Microsoft YaHei UI', 10))
        style.layout('Treeview', [('Treeview.treearea', {'sticky':'nswe'})])
        style.configure('Treeview.Heading', background=self.palette['field'], foreground=MUTED,
                        relief='flat', font=('Microsoft YaHei UI', 9))
        style.map('Treeview', background=[('selected', self.palette['soft'])], foreground=[('selected', TEXT)])
        style.configure('TCombobox', fieldbackground=self.palette['field'], background=self.palette['field'], foreground=TEXT,
                        arrowcolor=ACCENT, padding=7)
        style.map('TCombobox', fieldbackground=[('readonly', self.palette['field'])], foreground=[('readonly', TEXT)])
        style.configure('Rounded.TCombobox',borderwidth=0,relief='flat',padding=0,arrowsize=12)
        style.layout('Rounded.TCombobox', [('Combobox.padding', {'sticky':'nswe','children':[
            ('Combobox.textarea',{'sticky':'nswe'})]})])
        style.configure('Horizontal.TProgressbar', background=ACCENT, troughcolor=self.palette['field'], borderwidth=0)
        style.configure('Horizontal.TScale',background=PANEL,troughcolor=self.palette['line'],borderwidth=0)
        style.configure('Vertical.TScrollbar',background=self.palette['line'],troughcolor=PANEL,borderwidth=0,arrowsize=10)

    def label(self, parent, text='', muted=False, **kwargs):
        widget=tk.Label(parent, text=text, bg=parent['bg'], fg=MUTED if muted else TEXT,anchor='w', **kwargs)
        Tooltip(widget,lambda:option_help(widget.cget('text')))
        return widget

    def button(self, parent, text, command, primary=False, lock=True):
        btn = Button(parent, text=text, command=command, bg=ACCENT if primary else self.palette['field'],
                        fg='#ffffff' if primary else TEXT, activebackground=self.palette['soft'], activeforeground=TEXT,
                        relief='flat', bd=0, highlightthickness=1,highlightbackground=self.palette['line'],
                        padx=12, pady=7, cursor='hand2', disabledforeground=MUTED)
        btn.primary = primary
        Tooltip(btn,button_help(text))
        btn.bind('<Enter>',lambda _event:btn.configure(bg=self.palette['soft'] if not primary else ACCENT),add='+')
        btn.bind('<Leave>',lambda _event:btn.configure(bg=ACCENT if primary else self.palette['field']),add='+')
        if lock:
            self.controls.append((btn, 'normal'))
        return btn

    def entry(self, parent, variable, width=None):
        field = Field(parent,self.palette,variable=variable,width=width)
        self.controls.append((field, 'normal'))
        Tooltip(field.input,lambda:variable.get() or '点击输入；设置仅保存在本机。')
        return field

    def combo(self, parent, variable, values):
        widget = Field(parent,self.palette,kind='combo',variable=variable,values=list(values))
        # Widget bindings run before ttk's class binding, which otherwise changes
        # the selected value before the settings canvas receives the wheel event.
        widget.bind('<<ComboboxSelected>>', lambda _: self.schedule_preview())
        self.controls.append((widget, 'readonly'))
        Tooltip(widget.input,lambda:option_help(variable.get()))
        return widget

    def scroll_settings_wheel(self, event):
        return self.route_mousewheel(event)

    def install_scroll_router(self, root):
        """Put one wheel binding before widget class bindings.

        Tk children do not bubble a wheel event to their parent Canvas.  A
        custom bindtag fixes labels, fields, buttons and combo boxes while
        leaving native popup lists alone.
        """
        tag='FrameFitScrollRouter'
        self.bind_class(tag,'<MouseWheel>',self.route_mousewheel)
        def visit(widget):
            tags=list(widget.bindtags())
            if tag not in tags:
                index=tags.index(str(widget.winfo_class())) if str(widget.winfo_class()) in tags else len(tags)
                tags.insert(index,tag); widget.bindtags(tuple(tags))
            for child in widget.winfo_children(): visit(child)
        visit(root)

    def route_mousewheel(self,event):
        widget=event.widget
        top=widget.winfo_toplevel()
        canvas=None
        browser=getattr(self,'template_browser',None)
        if browser and browser.alive and top is browser.dialog:
            canvas=browser.canvas
        elif top is self:
            candidates=[]
            if getattr(self,'active_page',None)=='settings' and hasattr(self,'machine_canvas'):
                candidates.append(self.machine_canvas)
            if hasattr(self,'settings_canvas'): candidates.append(self.settings_canvas)
            if hasattr(self,'tree'): candidates.append(self.tree)
            for candidate in candidates:
                path=str(widget)
                if path==str(candidate) or path.startswith(str(candidate)+'.'):
                    canvas=candidate; break
        if canvas is None:
            return None
        delta=event.delta
        if not delta: return 'break'
        canvas.yview_scroll(-max(1,abs(int(delta/120)))*(1 if delta>0 else -1),'units')
        first,last=canvas.yview()
        if first<0: canvas.yview_moveto(0)
        elif last>1: canvas.yview_moveto(1)
        return 'break'

    def toggle_preview_focus(self):
        focused = not getattr(self,'preview_focused',False)
        self.preview_focused = focused
        for index,section in enumerate(self.queue_sections):
            if focused:
                section.grid_remove()
            else:
                section.grid()
        self.focus_button.expanded=focused
        self.focus_button.redraw()
        self.focus_button.tooltip.text='恢复输出队列。' if focused else button_help('放大预览')

    def change_ratio(self, _event=None):
        if self.ratio_var.get()=='自定义尺寸…':
            raw = simpledialog.askstring('自定义输出尺寸', '输入宽×高，例如 1440×1920（正偶数，最大 16384）',parent=self)
            import re
            values = re.fullmatch(r'\s*(\d+)\s*[xX×*]\s*(\d+)\s*',raw or '')
            if not values or any(int(value)<2 or int(value)>16384 or int(value)%2 for value in values.groups()):
                self.ratio_var.set('跟随套版')
                return
            self.restore_output_size(list(map(int,values.groups())))
        self.remember_profile()
        if self.direction != 'custom':
            self.profiles['custom'] = dict(self.profiles.get(self.direction, {}))
            self.direction = 'custom'
            self.apply_profile()
        selected = self.tree.selection()
        if self.preview_override is not None:
            self.preview_override.update(direction='custom',outputSize=list(self.output_size()))
        elif selected and self.jobs.get(selected[0], {}).get('config'):
            self.jobs[selected[0]]['config'].update(direction='custom', outputSize=list(self.output_size()))
        self.update_size_info()
        self.schedule_preview()
        self.schedule_save()

    def restore_output_size(self, size):
        if not size:
            self.ratio_var.set('跟随套版')
            return
        # A template-native output is a mode, not a custom ratio.  Keeping the
        # semantic value prevents selecting a video from turning the combo into
        # a misleading “自定义 · 1080×1920” entry.
        if self.template_image is not None and list(size)==[self.template_image.width//2*2,self.template_image.height//2*2]:
            self.ratio_var.set('跟随套版')
            return
        label = next((label for label,value in RATIOS.items() if value and list(value)==list(size)), None)
        if label is None:
            label = f'自定义 · {size[0]}×{size[1]}'
            RATIOS[label] = tuple(size)
            self.ratio_combo.configure(values=list(RATIOS))
        self.ratio_var.set(label)

    def update_output_templates_label(self):
        self.export_mode_var.set('auto')
        self.template_action_var.set('管理套版…')
        selected=self.tree.selection() if hasattr(self,'tree') else ()
        variants=(self.jobs.get(selected[0],{}).get('templates') if selected else None) or self.output_templates
        count=len(variants) or (1 if self.template else 0)
        self.multi_var.set(f'{count} 个套版 · 每个视频输出 {count} 份' if count else '尚未选择套版')

    def template_action(self, _event=None):
        # Keep settings files from older builds readable; this legacy value is
        # no longer offered in the UI, but an old automation may still send
        # it while upgrading an existing queue.
        if self.template_action_var.get() == '选择模板…':
            self.choose_template()
            return
        self.update_output_templates_label()
        self.manage_output_templates()
        self.save_settings()

    def reset_pending_variants(self):
        """Return unfinished expanded outputs to sources before changing mode."""
        if self.busy:
            return
        selected=set(self.tree.selection())
        selected_paths={os.path.normcase(str(self.jobs[iid]['path'].resolve()))
                        for iid in selected if iid in self.jobs}
        sources = {}
        for iid, job in list(self.jobs.items()):
            if not job.get('outputKey'):
                continue
            self.hide_task_item(job)
            identity = os.path.normcase(str(job['path'].resolve()))
            if identity not in sources:
                for key in ('outputKey', 'config', 'outputName', '_taskId', '_taskIndex'):
                    job.pop(key, None)
                job.update(template=str(self.template), rect=self.get_rect())
                sources[identity] = iid
            else:
                self.jobs.pop(iid)
                self.tree.delete(iid)
        retained=[iid for iid,job in self.jobs.items() if iid in selected or
                  os.path.normcase(str(job['path'].resolve())) in selected_paths]
        if retained:
            self.tree.selection_set(retained)
        self.queue_var.set(f'视频队列 · {len(self.jobs)} 个')

    def template_initial_dir(self, job=None):
        job=job or (self.jobs.get(self.tree.selection()[0]) if hasattr(self,'tree') and self.tree.selection() else None)
        roots=((job or {}).get('match') or {}).get('templateRoots') or []
        for value in roots:
            path=Path(value).expanduser()
            if path.is_dir():return str(path)
        current=Path((job or {}).get('template','')).expanduser()
        if current.is_file():return str(current.parent)
        configured=Path(self.template_root_var.get()).expanduser()
        return str(configured if configured.is_dir() else Path.home())

    def add_output_templates(self):
        paths = filedialog.askopenfilenames(parent=self, title='选择模板（每个模板独立导出）',
            initialdir=self.template_initial_dir(),
            filetypes=[('模板图片', '*.png *.jpg *.jpeg *.bmp *.webp')])
        self.add_template_paths(paths)

    def add_template_paths(self, paths):
        paths = list(paths)
        if paths:
            self.reset_pending_variants()
            self.export_mode_var.set('multi')
            self.ratio_var.set('跟随套版')
        current = {item['template'] for item in self.output_templates}
        for path in paths:
            if str(Path(path)) in current:
                continue
            with Image.open(path) as image:
                try:
                    rect = core.detect_window(image.convert('RGBA'), DETECTION[self.detection_var.get()])
                except ValueError:
                    rect = (0,0,*image.size)
                native = [value // 2 * 2 for value in image.size]
            cfg = self._engine_profile('custom', {'template':str(path), 'rect':rect,
                'size':'跟随套版原始尺寸', 'mode':self.mode_var.get(), 'zoom':self.get_zoom()})
            cfg.update(direction='custom', resolution='original', outputSize=native, templateConfirmed=True)
            self.output_templates.append(cfg)
            if not any(os.path.normcase(str(Path(item.get('template','')).resolve())) == os.path.normcase(str(Path(path).resolve())) for item in self.template_library):
                self.template_library.append(dict(cfg))
            current.add(str(Path(path)))
        if paths:
            for iid in self.tree.selection():
                if iid in self.jobs:
                    self.jobs[iid]['templates']=list(self.output_templates)
                    self.jobs[iid]['requiresTemplateReview']=False
                    if self.output_templates:
                        self.jobs[iid].update(template=self.output_templates[0]['template'],rect=self.output_templates[0].get('rect'))
                    self.update_queue_template(iid,self.output_templates)
        # Keep a source's chosen set in sync even when its row was temporarily
        # replaced by expanded output variants and focus moved to the manager.
        if paths and not self.tree.selection():
            for iid,job in self.jobs.items():
                if not job.get('outputKey'):
                    job['templates']=list(self.output_templates)
                    self.update_queue_template(iid,self.output_templates)
        self.update_output_templates_label()
        self.save_settings()
        self.refresh_template_choices(self.jobs.get(self.tree.selection()[0]) if self.tree.selection() else None)

    def manage_output_templates(self):
        """Manage the persistent machine-wide template library only."""
        if not self.template_library:
            legacy=list(self.output_templates)
            for job in self.jobs.values(): legacy.extend(job.get('templates') or [])
            seen=set()
            for cfg in legacy:
                path=cfg.get('template') if isinstance(cfg,dict) else None
                if not path or not Path(path).is_file(): continue
                key=os.path.normcase(str(Path(path).resolve()))
                if key not in seen: self.template_library.append(dict(cfg)); seen.add(key)
            if legacy: self.save_settings()
        from template_catalog import inspect_image
        configs=[dict(cfg) for cfg in self.template_library if isinstance(cfg,dict) and cfg.get('template') and Path(cfg['template']).is_file()]
        items=[]
        for cfg in configs:
            try:item=inspect_image(cfg['template'])
            except (OSError,ValueError): continue
            item['config']=cfg; items.append(item)
        targets=list(self.tree.selection()) if hasattr(self,'tree') else []
        selected_paths={os.path.normcase(str(Path(cfg.get('template','')).resolve()))
                        for iid in targets for cfg in (self.jobs.get(iid,{}).get('templates') or [])}
        def apply(chosen, _folder, _old):
            if not targets:
                self.status_var.set('套版总库已更新；选择视频后可在这里应用套版。')
                return
            chosen_cfg=[dict(item.get('config') or next((cfg for cfg in configs
                if os.path.normcase(str(Path(cfg.get('template','')).resolve()))==os.path.normcase(str(Path(item['path']).resolve()))),{})) for item in chosen]
            chosen_cfg=[cfg for cfg in chosen_cfg if cfg.get('template')]
            if not chosen_cfg:return
            self.reset_pending_variants()
            self.output_templates=list(chosen_cfg)
            for iid in targets:
                if iid not in self.jobs: continue
                self.jobs[iid]['templates']=list(chosen_cfg)
                self.jobs[iid].update(template=chosen_cfg[0]['template'],rect=chosen_cfg[0].get('rect'),requiresTemplateReview=False,output=None,error='')
                self.update_queue_template(iid,chosen_cfg)
            self.load_template(chosen_cfg[0]['template'],quiet=True)
            self.update_output_templates_label()
            self.select_queue_item()
            self.save_settings()
            self.status_var.set(f'已为 {len(targets)} 个视频选择 {len(chosen_cfg)} 个套版，将输出 {len(targets)*len(chosen_cfg)} 个视频')
        def remove(chosen):
            removed={os.path.normcase(str(Path(item['path']).resolve())) for item in chosen}
            self.template_library=[cfg for cfg in self.template_library if os.path.normcase(str(Path(cfg.get('template','')).resolve())) not in removed]
            self.save_settings()
        folder=str(Path(items[0]['path']).parent) if items else (self.template_root_var.get() or str(Path.home()))
        TemplateBrowser(self,items,[item['path'] for item in items if os.path.normcase(str(Path(item['path']).resolve())) in selected_paths],folder,apply,
            reference=('选择套版后点击“应用到当前队列”；未选视频时这里只管理本机总库。' if targets else
                       '本机总库：可导入、预览或删除套版。删除只移出总库，不影响已选视频、历史任务或正在导出的配置。'),
            manager=True,on_import=self.import_library_paths,on_delete=remove)

    def import_library_paths(self, paths):
        """Import images into the global library without assigning them."""
        existing={os.path.normcase(str(Path(cfg.get('template','')).resolve())) for cfg in self.template_library if isinstance(cfg,dict)}
        for path in paths:
            try:
                with Image.open(path) as image:
                    rgba=image.convert('RGBA')
                    try: rect=core.detect_window(rgba, DETECTION[self.detection_var.get()])
                    except ValueError: rect=(0,0,*image.size)
                    size=[value//2*2 for value in image.size]
            except (OSError,ValueError): continue
            key=os.path.normcase(str(Path(path).resolve()))
            if key in existing: continue
            cfg=self._engine_profile('custom', {'template':str(path),'rect':rect,'size':'跟随套版原始尺寸','mode':self.mode_var.get(),'zoom':self.get_zoom()})
            cfg.update(direction='custom',resolution='original',outputSize=size,templateConfirmed=True)
            self.template_library.append(cfg); existing.add(key)
        self.save_settings()
    def save_output_template_parameters(self):
        index = getattr(self, 'editing_output_index', None)
        if index is None or index >= len(self.output_templates):
            self.status_var.set('先在“管理”中选择需要编辑的套版')
            return
        previous=self.output_templates[index]['template']
        self.output_templates[index] = self._engine_profile('custom', {
            'template':str(self.template), 'rect':self.get_rect(), 'size':self.size_var.get(),
            'mode':self.mode_var.get(), 'zoom':self.get_zoom(), 'detection':self.detection_var.get()})
        self.output_templates[index].update(outputSize=list(self.output_size()), templateConfirmed=True)
        for job in self.jobs.values():
            if job.get('templates'):
                job['templates']=[self.output_templates[index] if cfg['template']==previous else cfg for cfg in job['templates']]
            if job.get('outputKey') and job.get('template')==previous:
                job.update(config=dict(self.output_templates[index]),template=str(self.template),rect=self.get_rect())
                job.pop('outputName',None)
        self.save_settings()
        self.save_template_button.pack_forget()
        self.editing_output_index=None
        self.status_var.set('输出套版参数已保存')

    def toggle_queue(self):
        self.queue_expanded=not self.queue_expanded
        self.queue_table_card.configure(height=280 if self.queue_expanded else 170)
        self.tree.configure(height=7 if self.queue_expanded else 3)
        self.queue_expand_button.configure(text='收起列表' if self.queue_expanded else '展开列表')

    def coordinate_bounds(self,index):
        dimensions=self.template_image.size if self.template_image is not None else (16384,16384)
        try:
            values=[int(var.get()) for var in self.rect_vars]
        except ValueError:
            values=[0,0,1,1]
        return (0,max(0,dimensions[index]-max(1,values[index+2]))) if index<2 else (1,max(1,dimensions[index-2]-max(0,values[index-2])))

    def update_name_example(self,*args):
        try:
            selected=self.tree.selection()
            job=self.jobs.get(selected[0],{}) if selected else {}
            source=Path(job.get('path') or 'v968 1920X1080 V5.mp4').name
            result=naming.render_name(self.name_pattern_var.get(),source,self.output_size(),
                language=output_language(source,str(self.template or '')) or '英',template=str(self.template or ''),
                project=(job.get('match') or {}).get('projectFolder','示例项目'),game=(job.get('match') or {}).get('game','示例游戏'))
            self.name_example_var.set('示例：'+result)
        except (ValueError,TypeError,AttributeError) as exc:
            self.name_example_var.set('规则提示：'+str(exc))

    def apply_name_preset(self,_event=None):
        pattern=self.name_presets.get(self.name_preset_var.get())
        if pattern is not None:
            self.name_pattern_var.set(pattern)
            self.name_mode_var.set('自定义规则')
            self.save_settings()

    def save_name_preset(self):
        try:
            naming.validate_pattern(self.name_pattern_var.get())
            name=simpledialog.askstring('保存命名预设','预设名称',parent=self)
            if not name or not name.strip():
                return
            self.name_presets[name.strip()]=self.name_pattern_var.get()
            self.name_preset_var.set(name.strip())
            self.name_preset_combo.configure(values=list(self.name_presets))
            self.name_mode_var.set('自定义规则')
            self.save_settings()
        except ValueError as exc:
            messagebox.showerror('命名规则无效',str(exc),parent=self)

    def delete_name_preset(self):
        self.name_presets.pop(self.name_preset_var.get(),None)
        self.name_preset_var.set('')
        self.name_preset_combo.configure(values=list(self.name_presets))
        self.save_settings()

    def insert_name_token(self,_event=None):
        token=self.token_var.get()
        if token.startswith('['):
            self.name_pattern_var.set(self.name_pattern_var.get()+token)
            self.name_mode_var.set('自定义规则')
        self.token_var.set('插入命名字段…')

    def expand_output_jobs(self, only_ids=None):
        import hashlib
        result=[]
        selected=set(self.tree.selection())
        next_selection=[]
        for iid, original in list(self.jobs.items()):
            if only_ids is not None and iid not in only_ids:
                continue
            variants=original.get('templates') or self.output_templates
            if original.get('outputKey') or not variants or not original.get('info'):
                result.append(iid)
                if iid in selected:
                    next_selection.append(iid)
                continue
            for cfg in variants:
                key = hashlib.sha256(json.dumps(cfg, sort_keys=True).encode()).hexdigest()[:16]
                job = dict(original, template=cfg['template'], rect=cfg['rect'], config=dict(cfg),
                    outputKey=key, requiresTemplateReview=False, output=None)
                for cache_key in ('scrubFrames','scrubCaching','scrubReady','scrubProcess'):
                    job.pop(cache_key,None)
                job.pop('outputName', None)
                row = self.tree.insert('', 'end', values=(original['path'].name, '×'.join(map(str,cfg.get('outputSize',[]))),
                    f"{original['info']['duration']:.1f}s", Path(cfg['template']).stem, '待导出'))
                self.jobs[row] = job
                result.append(row)
                if iid in selected:
                    next_selection.append(row)
            del self.jobs[iid]
            self.tree.delete(iid)
        self.queue_var.set(f'输出队列 · {len(self.jobs)} 项')
        if next_selection:
            self.tree.selection_set(next_selection)
        return result

    def _layout(self):
        self.columnconfigure(0,weight=0)
        self.columnconfigure(1,weight=1)
        self.rowconfigure(0,weight=1)
        self.rowconfigure(1,weight=0)
        navigation=tk.Frame(self,bg=BG,width=236)
        navigation.grid(row=0,column=0,sticky='ns')
        navigation.pack_propagate(False)
        brand=tk.Frame(navigation,bg=BG)
        brand.pack(fill='x',padx=18,pady=(26,30))
        mark=tk.Canvas(brand,width=36,height=36,bg=BG,bd=0,highlightthickness=0)
        self.brand_mark=mark
        rounded_path(mark,(1,1,35,35),11,fill=ACCENT,outline='')
        mark.create_line(10,13,10,9,16,9,fill='#ffffff',width=2,capstyle='round')
        mark.create_line(20,9,26,9,26,15,fill='#ffffff',width=2,capstyle='round')
        mark.create_line(26,21,26,27,20,27,fill='#ffffff',width=2,capstyle='round')
        mark.create_line(16,27,10,27,10,21,fill='#ffffff',width=2,capstyle='round')
        mark.pack(side='left',padx=(0,9))
        Tooltip(mark,'FrameFit · 本机视频套版；素材和项目配置保留在本机。')
        words=tk.Frame(brand,bg=BG)
        words.pack(side='left')
        self.label(words,'FrameFit',font=('Segoe UI',14,'bold')).pack(anchor='w')
        self.label(words,'本机视频工作台',muted=True,font=('Microsoft YaHei UI',8)).pack(anchor='w',pady=(2,0))
        self.nav_buttons={}
        self.active_page='workbench'
        for key,title in [('workbench','套版工作台'),('templates','输出模板'),('settings','设置')]:
            button=NavigationButton(navigation,key,text=title,command=lambda key=key:self.show_page(key),bg=BG,fg=TEXT,
                activebackground=self.palette['soft'],anchor='w',padx=42,pady=12,highlightthickness=0)
            Tooltip(button,option_help(title))
            button.pack(fill='x',padx=10,pady=3)
            button.bind('<Leave>',lambda _event:self.refresh_navigation(),add='+')
            self.nav_buttons[key]=button

        # Anchor the complete utility group at the bottom in reading order:
        # counters, appearance controls, then version and update status.
        sidebar_bottom=self.sidebar_bottom=tk.Frame(navigation,bg=BG)
        sidebar_bottom.pack(side='bottom',fill='x',padx=12,pady=(12,16))
        self.metric_vars={}
        sidebar_metrics=self.sidebar_metrics=tk.Frame(sidebar_bottom,bg=BG)
        sidebar_metrics.pack(fill='x',pady=(0,8))
        sidebar_metrics.columnconfigure(0,weight=1)
        sidebar_metrics.columnconfigure(1,weight=1)
        for index,(key,title,accent) in enumerate([
            ('sources','素材视频','#4c8cf5'),('outputs','计划输出','#e9a51b'),
            ('complete','已完成','#17b68b'),('borders','边缘提示',ACCENT)
        ]):
            box=Card(sidebar_metrics,self.palette,padding=7)
            box.grid(row=index//2,column=index%2,sticky='nsew',padx=(0,5) if index%2==0 else 0,pady=(0,6))
            tk.Frame(box.content,bg=accent,height=2,width=22).pack(anchor='w',pady=(0,3))
            self.label(box.content,title,muted=True,font=('Microsoft YaHei UI',8)).pack(anchor='w')
            self.metric_vars[key]=tk.StringVar(value='0')
            self.label(box.content,textvariable=self.metric_vars[key],font=('Segoe UI',14,'bold')).pack(anchor='w')

        theme_box=self.theme_box=tk.Frame(sidebar_bottom,bg=BG)
        theme_box.pack(fill='x',pady=(0,10))
        self.label(theme_box,'外观',muted=True,font=('Microsoft YaHei UI',9)).pack(anchor='w',padx=4,pady=(0,8))
        self.theme_buttons={}
        for key,title in [('system','跟随系统'),('light','☀ 浅色'),('dark','☾ 深色')]:
            button=self.button(theme_box,title,lambda key=key:self.apply_theme(key),lock=False)
            button.pack(fill='x',pady=2)
            self.theme_buttons[key]=button
        sidebar_status=self.sidebar_status=Card(sidebar_bottom,self.palette,padding=10)
        sidebar_status.pack(fill='x')
        status_content=sidebar_status.content
        self.label(status_content,f'FrameFit v{self.app_version()} · 本机处理',muted=True,
                   font=('Microsoft YaHei UI',8)).pack(anchor='w',pady=(0,6))
        self.update_button=self.button(status_content,'检查更新',self.update_action,lock=False)
        self.update_button.pack(fill='x')
        self.update_status_label=self.label(status_content,textvariable=self.update_status_var,muted=True,
            font=('Microsoft YaHei UI',8),wraplength=185,justify='left')
        self.update_status_label.pack(fill='x',pady=(5,0))
        main=tk.Frame(self,bg=BG)
        main.grid(row=0,column=1,sticky='nsew',padx=(0,16),pady=16)
        main.columnconfigure(0,weight=1)
        main.rowconfigure(0,weight=1)
        self.page_title_var=tk.StringVar(value='套版工作台')
        page_stack=tk.Frame(main,bg=BG)
        page_stack.grid(row=0,column=0,sticky='nsew')
        page_stack.grid_propagate(False)
        page_stack.columnconfigure(0,weight=1)
        page_stack.rowconfigure(0,weight=1)
        self.pages={key:tk.Frame(page_stack,bg=BG) for key in ('workbench','templates','settings')}
        for page in self.pages.values():
            page.grid(row=0,column=0,sticky='nsew')
        work=self.pages['workbench']
        inspector=Card(work,self.palette)
        inspector.pack(side='right',fill='y',padx=(14,0))
        inspector.configure(width=320)
        inspector.pack_propagate(False)
        left=tk.Frame(work,bg=BG)
        left.pack(side='left',fill='both',expand=True)
        left.grid_propagate(False)
        left.columnconfigure(0,weight=1)
        left.rowconfigure(0,weight=1)
        preview_card=Card(left,self.palette,padding=10)
        preview_card.grid(row=0,column=0,sticky='nsew')
        preview=preview_card.content
        preview_header=tk.Frame(preview,bg=PANEL)
        preview_header.pack(fill='x',pady=(0,8))
        self.label(preview_header,'画面预览',font=('Microsoft YaHei UI',11,'bold')).pack(side='left')
        self.preview_template_combo=self.combo(preview_header,self.preview_template_var,[])
        self.preview_template_combo.configure(width=240)
        self.preview_template_combo.pack(side='right')
        self.preview_template_combo.bind('<<ComboboxSelected>>',self.change_preview_template)
        Tooltip(self.preview_template_combo.input,lambda:'套版预览：'+self.preview_template_var.get()+'。只切换预览，不减少导出套版数量。')
        self.canvas=tk.Canvas(preview,bg=self.palette['field'],height=390,bd=0,highlightthickness=0)
        self.canvas.pack(fill='both',expand=True)
        self.canvas.bind('<Configure>',lambda _event:self.draw_preview())
        self.focus_button=PreviewButton(self.canvas,command=self.toggle_preview_focus,
            bg=PANEL,fg=TEXT,activebackground=self.palette['soft'],highlightbackground=self.palette['line'])
        Tooltip(self.focus_button,button_help('放大预览'))
        self.focus_button.place(relx=1,rely=1,x=-12,y=-12,anchor='se')
        self.label(preview,textvariable=self.preview_var,muted=True,font=('Microsoft YaHei UI',8)).pack(fill='x',pady=(8,6))
        timebar=tk.Frame(preview,bg=PANEL)
        timebar.pack(fill='x')
        self.label(timebar,'时间轴',muted=True,font=('Microsoft YaHei UI',9)).pack(side='left')
        self.seek_slider=Slider(timebar,self.palette,from_=0,to=1,variable=self.seek_var,command=self.on_seek)
        self.seek_slider.pack(side='left',fill='x',expand=True,padx=12)
        self.seek_slider.bind('<ButtonPress-1>',lambda _event:setattr(self,'dragging_seek',True))
        self.seek_slider.bind('<ButtonRelease-1>',self.finish_seek)
        self.label(timebar,textvariable=self.time_var,muted=True,font=('Consolas',9)).pack(side='right')
        self.button(timebar,'检查此帧',self.check_current_frame,lock=False).pack(side='right',padx=8)
        self.label(preview,textvariable=self.border_var,muted=True,font=('Microsoft YaHei UI',8)).pack(fill='x',pady=(6,0))
        queue_card=Card(left,self.palette,padding=12)
        queue_card.grid(row=1,column=0,sticky='ew',pady=(12,0))
        queue=queue_card.content
        toolbar=tk.Frame(queue,bg=PANEL)
        toolbar.pack(fill='x',pady=(0,9))
        self.label(toolbar,textvariable=self.queue_var,font=('Microsoft YaHei UI',10,'bold')).pack(side='left')
        self.button(toolbar,'移除',self.remove_selected).pack(side='right',padx=(7,0))
        self.queue_expand_button=self.button(toolbar,'展开列表',self.toggle_queue,lock=False)
        self.queue_expand_button.pack(side='right',padx=(7,0))
        self.button(toolbar,'＋ 添加视频',self.choose_videos).pack(side='right')
        table=self.card(queue,fill='x')
        self.queue_table_card=table.card
        self.queue_table_card._fixed_height=True
        table.card.configure(height=170)
        self.tree=ttk.Treeview(table,columns=('name','size','duration','template','status'),show='headings',height=3,selectmode='extended')
        for col,title,width in [('name','视频文件',300),('size','尺寸',100),('duration','时长',60),('template','套版',220),('status','状态',100)]:
            self.tree.heading(col,text=title,anchor='center' if col!='name' else 'w')
            self.tree.column(col,width=width,minwidth=45,stretch=col=='name',anchor='center' if col!='name' else 'w')
        scroll=Scrollbar(table,self.palette,orient='vertical',command=self.tree.yview)
        scroll.pack(side='right',fill='y')
        self.tree.pack(side='left',fill='both',expand=True)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.bind('<<TreeviewSelect>>',self.select_queue_item)
        self.tree.bind('<Delete>',self.remove_selected)
        self.bind('<Delete>',self.delete_queue_key,add='+')
        self.tree.bind('<Double-1>',self.open_job)
        self.tree.bind('<Button-1>',self.replace_queue_template_click,add='+')
        Tooltip(self.tree,lambda:self.queue_hover_text())
        self.queue_sections=[queue_card]
        self.direction_buttons={}
        settings_canvas=tk.Canvas(inspector.content,bg=PANEL,bd=0,highlightthickness=0,width=270)
        self.settings_canvas=settings_canvas
        settings_scroll=Scrollbar(inspector.content,self.palette,orient='vertical',command=settings_canvas.yview)
        settings_scroll.pack(side='right',fill='y')
        settings_canvas.pack(side='left',fill='both',expand=True)
        settings_canvas.configure(yscrollcommand=settings_scroll.set)
        inner=tk.Frame(settings_canvas,bg=PANEL)
        window=settings_canvas.create_window((0,0),window=inner,anchor='nw')
        inner.bind('<Configure>',lambda _event:settings_canvas.configure(scrollregion=settings_canvas.bbox('all')))
        settings_canvas.bind('<Configure>',lambda event:settings_canvas.itemconfigure(window,width=max(210,event.width-10)))
        self.label(inner, '模板与窗口', font=('Microsoft YaHei UI', 12, 'bold')).pack(fill='x')
        self.template_label=self.label(inner, textvariable=self.template_var, muted=True)
        self.template_label.pack(fill='x', pady=(8, 10))
        Tooltip(self.template_label,'当前套版名称与原始尺寸。用下方菜单选择、添加或管理套版。')
        row = tk.Frame(inner, bg=PANEL)
        row.pack(fill='x')
        self.template_menu = self.button(row,'管理套版',self.manage_output_templates,lock=False)
        self.template_menu.pack(side='left',fill='x',expand=True,padx=(0,8))
        Tooltip(self.template_menu,'打开管理套版：可在缩略图中单选或多选，应用到当前视频队列；未选视频时仅维护本机总库。')
        self.detect_button = self.button(row, '识别窗口', self.auto_detect)
        self.detect_button.pack(side='right')
        self.label(inner,textvariable=self.multi_var,muted=True,wraplength=252).pack(fill='x',pady=(8,0))
        coordinate_header=tk.Frame(inner,bg=PANEL)
        coordinate_header.pack(fill='x',pady=(17,7))
        self.label(coordinate_header, '窗口坐标（套框原图像素）', muted=True, font=('Microsoft YaHei UI', 9)).pack(side='left')
        Tooltip(coordinate_header,'按住数字左右拖动可连续调整；只点击数字会选中当前值并进入编辑。松开后再输入不会改变拖动逻辑。')
        coords = tk.Frame(inner, bg=PANEL)
        coords.pack(fill='x')
        for i, title in enumerate(('X', 'Y', '宽', '高')):
            coords.columnconfigure(i,weight=1,uniform='coordinate')
            field=NumberField(coords,self.palette,title,self.rect_vars[i],lambda i=i:self.coordinate_bounds(i))
            field.configure(width=55)
            field.grid(row=0,column=i,sticky='ew',padx=(0,4) if i<3 else 0)
            self.controls.append((field,'normal'))
            Tooltip(field.input,'点击输入数值，左右拖动调整；Shift 加速，↑↓ 微调。范围受套版尺寸限制。')
        self.label(inner, '导出尺寸与比例', font=('Microsoft YaHei UI', 12, 'bold')).pack(fill='x', pady=(18, 10))
        self.ratio_combo = self.combo(inner, self.ratio_var, RATIOS)
        self.ratio_combo.pack(fill='x', pady=(0, 6))
        self.ratio_combo.bind('<<ComboboxSelected>>', self.change_ratio)
        self.label(inner,textvariable=self.size_info_var,muted=True,wraplength=252,
                   justify='left',font=('Microsoft YaHei UI',9)).pack(fill='x',pady=(0,10))
        self.save_template_button=self.button(inner,'保存套版参数',self.save_output_template_parameters)
        for label, var, values in [('画面适配', self.mode_var, MODES), ('画质', self.quality_var, ['高画质', '标准画质（文件更小）'])]:
            caption=self.label(inner, label, muted=True, font=('Microsoft YaHei UI', 9))
            caption.pack(fill='x',pady=(0,5))
            if var is self.mode_var:
                self.fit_title=caption
            widget = self.combo(inner, var, values)
            widget.pack(fill='x', pady=(0, 12))
            if var is self.mode_var:
                self.mode_combo = widget
                self.zoom_box = tk.Frame(inner, bg=PANEL)
                row = tk.Frame(self.zoom_box, bg=PANEL)
                row.pack(fill='x')
                self.zoom_spin = NumberField(row,self.palette,'缩放',self.zoom_var,lambda:(10,500))
                self.zoom_spin.pack(side='left', padx=8, ipady=5)
                self.controls.append((self.zoom_spin, 'normal'))
                self.label(row, '%', muted=True).pack(side='left')
                Tooltip(self.zoom_spin.input,'100% 为等比填满，范围 10～500%。居中缩放；视频在上层，放大可覆盖套版装饰。')
        self.label(inner, '保存位置', muted=True).pack(fill='x')
        self.entry(inner, self.output_var).pack(fill='x', ipady=6, pady=7)
        row = tk.Frame(inner, bg=PANEL)
        row.pack(fill='x')
        self.button(row, '选择文件夹', self.choose_output).pack(side='left')
        self.button(row, '打开', self.open_output, lock=False).pack(side='right')
        self.label(inner, 'H.264 · 保留原声 · 同名自动编号', muted=True,
                   justify='left', font=('Microsoft YaHei UI', 9)).pack(fill='x', pady=(15, 0))
        templates_page=self.pages['templates']
        intro=self.card(templates_page,fill='x',pady=(0,14))
        self.label(intro,'输出模板',font=('Microsoft YaHei UI',15,'bold')).pack(anchor='w',pady=(0,7))
        self.label(intro,'保存一整套多尺寸输出规则，下次直接调用。模板、路径和配方只存本机。',muted=True).pack(anchor='w')
        template_form=self.card(templates_page,fill='x')
        self.label(template_form, '输出模板', muted=True).pack(fill='x', padx=7, pady=(0, 3))
        self.recipe_combo = self.combo(template_form, self.recipe_var, [])
        self.recipe_combo.pack(fill='x', padx=7, pady=(0, 5))
        self.entry(template_form, self.recipe_name_var).pack(fill='x', padx=7, ipady=5, pady=(0, 5))
        recipe_buttons = tk.Frame(template_form, bg=PANEL)
        recipe_buttons.pack(fill='x', padx=7, pady=(0, 8))
        self.button(recipe_buttons, '保存输出模板', self.save_recipe, lock=False).pack(side='left')
        self.button(recipe_buttons, '使用', self.apply_recipe, lock=False).pack(side='left', padx=5)
        self.button(recipe_buttons, '删除', self.delete_recipe, lock=False).pack(side='right')

        self.label(template_form,textvariable=self.multi_var,muted=True).pack(anchor='w',padx=7,pady=10)
        action=tk.Frame(template_form,bg=PANEL)
        action.pack(fill='x',padx=7,pady=(0,12))
        self.button(action,'管理套版',self.manage_output_templates).pack(side='left')
        machine_shell=self.card(self.pages['settings'],fill='both',expand=True)
        machine_canvas=tk.Canvas(machine_shell,bg=PANEL,bd=0,highlightthickness=0)
        machine_scroll=Scrollbar(machine_shell,self.palette,orient='vertical',command=machine_canvas.yview)
        machine_scroll.pack(side='right',fill='y')
        machine_canvas.pack(side='left',fill='both',expand=True)
        machine_canvas.configure(yscrollcommand=machine_scroll.set)
        machine=tk.Frame(machine_canvas,bg=PANEL)
        machine_window=machine_canvas.create_window((0,0),window=machine,anchor='nw')
        machine.bind('<Configure>',lambda _event:machine_canvas.configure(scrollregion=machine_canvas.bbox('all')))
        machine_canvas.bind('<Configure>',lambda event:machine_canvas.itemconfigure(machine_window,width=max(1,event.width)))
        self.machine_canvas=machine_canvas
        self.label(machine,'设置',font=('Microsoft YaHei UI',15,'bold')).pack(anchor='w',padx=7,pady=(0,14))
        for title,variable in [('任务排期目录',self.schedule_root_var),('本机套版根目录',self.template_root_var)]:
            self.label(machine,title,muted=True).pack(fill='x',padx=7,pady=(8,3))
            path_row=tk.Frame(machine,bg=PANEL)
            path_row.pack(fill='x',padx=7)
            self.button(path_row,'选择目录',lambda variable=variable:self.choose_path(variable)).pack(side='right',padx=(8,0))
            self.entry(path_row,variable).pack(side='left',fill='x',expand=True)
        match_toggle=CheckButton(machine,self.match_paths_var,text='关键词匹配路径',bg=self.palette['field'],fg=TEXT,
            activebackground=self.palette['soft'],highlightbackground=self.palette['line'])
        match_toggle.pack(fill='x',padx=7,pady=10)
        Tooltip(match_toggle,option_help('关键词匹配路径'))
        self.label(machine, '项目匹配参考', muted=True).pack(fill='x', padx=7, pady=(8, 3))
        self.label(machine,textvariable=self.match_reference_var,wraplength=760,justify='left').pack(fill='x',padx=7,pady=(0,8))
        self.label(machine,'视频名查排期项目，上一级目录识别公司与游戏，再定位本机输出套版；日期冲突时手动确认。',
                   muted=True,wraplength=760,justify='left',font=('Microsoft YaHei UI',9)).pack(fill='x',padx=7,pady=(0,10))
        self.label(machine,'编码加速',muted=True).pack(fill='x',padx=7,pady=(4,3))
        self.combo(machine,self.backend_var,BACKENDS).pack(fill='x',padx=7,pady=(0,8))
        self.label(machine,'导出完成提示',muted=True).pack(fill='x',padx=7,pady=(8,3))
        notify_toggle=CheckButton(machine,self.notify_completion_var,text='完成后显示通知',bg=self.palette['field'],fg=TEXT,
            activebackground=self.palette['soft'],highlightbackground=self.palette['line'])
        notify_toggle.pack(fill='x',padx=7,pady=(0,5))
        Tooltip(notify_toggle,'导出成功、部分失败或取消后显示本机完成通知；不会上传任务信息。')
        sound_toggle=CheckButton(machine,self.sound_completion_var,text='完成后播放声音',bg=self.palette['field'],fg=TEXT,
            activebackground=self.palette['soft'],highlightbackground=self.palette['line'])
        sound_toggle.pack(fill='x',padx=7,pady=(0,5))
        Tooltip(sound_toggle,'开启后在任务结束时播放自定义 WAV 或 MP3；未选择声音时使用系统提示音。试听不会改变此开关。')
        sound_row=tk.Frame(machine,bg=PANEL)
        sound_row.pack(fill='x',padx=7,pady=(0,8))
        self.entry(sound_row,self.completion_sound_var).pack(side='left',fill='x',expand=True)
        self.button(sound_row,'选择声音',self.choose_completion_sound,lock=False).pack(side='right',padx=(8,0))
        self.button(sound_row,'试听',self.preview_completion_sound,lock=False).pack(side='right',padx=(8,0))
        self.label(machine,'窗口识别类型',muted=True).pack(fill='x',padx=7,pady=(4,3))
        self.detection_combo=self.combo(machine,self.detection_var,DETECTION)
        self.detection_combo.pack(fill='x',padx=7,pady=(0,8))
        self.detection_combo.bind('<<ComboboxSelected>>',lambda _:self.auto_detect() if self.template_image else None)
        self.label(machine, '输出语言', muted=True).pack(fill='x', padx=7, pady=(4, 3))
        self.language_combo = self.combo(machine, self.language_var, LANGUAGE_OPTIONS)
        self.language_combo.pack(fill='x', padx=7, pady=(0, 7))
        self.language_combo.bind('<<ComboboxSelected>>', self.update_language_controls)
        self.other_language_entry = self.entry(machine, self.other_language_var)
        if self.language_var.get() != '其他':
            self.other_language_entry.pack_forget()
        self.name_mode_label=self.label(machine, '命名方式', muted=True)
        self.name_mode_label.pack(fill='x', padx=7, pady=(0, 3))
        self.combo(machine, self.name_mode_var, ['精简命名', '完整信息','自定义规则']).pack(fill='x', padx=7, pady=(0, 8))
        presets=tk.Frame(machine,bg=PANEL)
        presets.pack(fill='x',padx=7,pady=(0,6))
        self.name_preset_combo=self.combo(presets,self.name_preset_var,list(self.name_presets))
        self.name_preset_combo.pack(side='left',fill='x',expand=True,padx=(0,6))
        self.name_preset_combo.bind('<<ComboboxSelected>>',self.apply_name_preset)
        self.button(presets,'存为预设',self.save_name_preset,lock=False).pack(side='left',padx=(0,6))
        self.button(presets,'删除预设',self.delete_name_preset,lock=False).pack(side='left')
        pattern_entry=self.entry(machine,self.name_pattern_var)
        pattern_entry.pack(fill='x',padx=7,pady=(0,6))
        pattern_entry.bind('<KeyRelease>',lambda _event:self.name_mode_var.set('自定义规则'))
        tokens=tk.Frame(machine,bg=PANEL)
        tokens.pack(fill='x',padx=7,pady=(0,6))
        self.token_var=tk.StringVar(value='插入命名字段…')
        token_field=self.combo(tokens,self.token_var,['['+token+']' for token in naming.TOKENS]+['[序号:03]'])
        token_field.pack(fill='x')
        token_field.bind('<<ComboboxSelected>>',self.insert_name_token)
        self.label(machine,textvariable=self.name_example_var,muted=True,wraplength=760,justify='left').pack(fill='x',padx=7,pady=(0,10))
        self.label(machine, '输出分类', muted=True).pack(fill='x', padx=7, pady=(0, 3))
        self.combo(machine, self.output_group_var, ['统一文件夹', '按项目分文件夹', '按模板分文件夹']).pack(fill='x', padx=7, pady=(0, 8))
        footer=tk.Frame(main,bg=BG)
        footer.grid(row=1,column=0,sticky='ew',pady=(12,0))
        row=tk.Frame(footer,bg=BG)
        row.pack(fill='x',pady=(0,7))
        self.start_button=self.button(row,'开始批量导出 →',self.start_export,primary=True)
        self.start_button.pack(side='right',padx=(8,0))
        self.cancel_button=self.button(row,'取消当前操作',self.cancel_export,lock=False)
        self.cancel_button.pack(side='right',padx=(8,0))
        self.cancel_button.configure(state='disabled')
        self.resume_button=self.button(row,'继续未完成',self.resume_export)
        self.resume_button.pack(side='right')
        info=tk.Frame(row,bg=BG)
        info.pack(side='left',fill='x',expand=True)
        self.label(info,textvariable=self.status_var,font=('Microsoft YaHei UI',9)).pack(anchor='w')
        self.label(info,textvariable=self.backend_status,muted=True,font=('Microsoft YaHei UI',8)).pack(anchor='w',pady=(4,0))
        Progress(footer,self.palette,variable=self.progress_var,maximum=100).pack(fill='x')
        self.show_page('workbench')
        self.install_scroll_router(self.settings_canvas)
        self.install_scroll_router(self.machine_canvas)

    def load_settings(self):
        config = {}
        try:
            config = json.loads(CONFIG.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            pass
        if not isinstance(config, dict):
            config = {}
        if config.get('backend') in BACKENDS:
            self.backend_var.set(config['backend'])
        if config.get('quality') in ('高画质', '标准画质（文件更小）'):
            self.quality_var.set(config['quality'])
        if config.get('output'):
            self.output_var.set(config['output'])
        self.output_templates = config.get('outputTemplates', [])
        self.template_library = config.get('templateLibrary', []) if isinstance(config.get('templateLibrary', []),list) else []
        self.notify_completion_var.set(bool(config.get('notifyCompletion',True)))
        self.sound_completion_var.set(bool(config.get('soundCompletion',False)))
        self.completion_sound_var.set(str(config.get('completionSound','')))
        self.export_mode_var.set(config.get('exportMode', 'multi' if self.output_templates else 'single'))
        self.hidden_task_items = config.get('hiddenTaskItems', {}) if isinstance(config.get('hiddenTaskItems'),dict) else {}
        saved_ratio = config.get('ratio', '')
        if saved_ratio.startswith('自定义 · '):
            parts = saved_ratio.removeprefix('自定义 · ').split('×')
            if len(parts)==2 and all(value.isdigit() for value in parts):
                RATIOS[saved_ratio] = tuple(map(int,parts))
                self.ratio_combo.configure(values=list(RATIOS))
        # A new workbench always starts from the selected template's native
        # size.  Explicit dimensions remain frozen inside saved recipes and
        # task configs, so switching the UI back to a template never rewrites
        # an existing export.
        self.ratio_var.set('跟随套版')
        self.numeric_mode_var.set(config.get('numericMode','拖动调整') if config.get('numericMode') in ('拖动调整','点击编辑') else '拖动调整')
        self.update_output_templates_label()
        profiles = config.get('profiles', {})
        self.machine_settings = dict(config.get('machine', {}))
        self.schedule_root_var.set(self.machine_settings.get('scheduleRoot', self.schedule_root_var.get()))
        self.template_root_var.set(self.machine_settings.get('templateRoot', self.template_root_var.get()))
        self.match_paths_var.set(self.machine_settings.get('matchPaths',True))
        self.language_var.set(config.get('language', '自动识别'))
        self.other_language_var.set(config.get('otherLanguage', ''))
        self.name_mode_var.set(config.get('nameMode', '精简命名'))
        self.name_pattern_var.set(config.get('namePattern',naming.DEFAULT_PATTERN))
        if isinstance(config.get('namePresets'),dict):
            self.name_presets={name:pattern for name,pattern in config['namePresets'].items() if isinstance(name,str) and isinstance(pattern,str)}
        self.name_preset_var.set(config.get('namePreset','简洁尺寸语言'))
        self.name_preset_combo.configure(values=list(self.name_presets))
        self.output_group_var.set(config.get('outputGroup', '统一文件夹'))
        self.recipes = config.get('recipes', {}) if isinstance(config.get('recipes'), dict) else {}
        self.recipe_combo.configure(values=list(self.recipes))
        if isinstance(profiles, dict):
            self.profiles = {k: v for k, v in profiles.items() if k in DIRECTIONS and isinstance(v, dict)}
        if 'landscape' not in self.profiles:
            self.profiles['landscape'] = {k: config[k] for k in ('template', 'rect', 'size', 'mode') if k in config}
        self.direction = config.get('direction') if config.get('direction') in DIRECTIONS else 'landscape'
        self.apply_profile()
        self.update_language_controls()
        self.refresh_template_choices()
        self.draw_preview()

    def remember_profile(self):
        if self.template_image is None:
            return
        previous = self.profiles.get(self.direction, {})
        # Do not discard the user's unfinished coordinate edits by silently switching.
        self.profiles[self.direction] = dict(previous, template=str(self.template), rect=self.get_rect(),
                                              size=self.size_var.get(), mode=self.mode_var.get(),
                                              zoom=self.get_zoom(),
                                              detection=self.detection_var.get())

    def apply_profile(self):
        title, sizes, default_asset = DIRECTIONS[self.direction]
        profile = self.profiles.get(self.direction, {})
        self.size_var.set(profile.get('size') if profile.get('size') in sizes else next(iter(sizes)))
        saved_mode = profile.get('mode')
        if saved_mode == '完整显示（可能留黑边）':
            saved_mode = '完整显示'
        self.mode_var.set(saved_mode if saved_mode in MODES else next(iter(MODES)))
        try:
            self.zoom_var.set(f"{core.validate_zoom(profile.get('zoom', 100)):g}")
        except ValueError:
            self.zoom_var.set('100')
        self.detection_var.set(profile.get('detection') if profile.get('detection') in DETECTION else next(iter(DETECTION)))
        template = Path(profile.get('template') or '')
        restored = template.is_file() and self.load_template(template, quiet=True)
        if not restored:
            self.detection_var.set(next(iter(DETECTION)))
            self.template=None
            self.template_image=None
            self.display_image=None
            self.template_var.set('未选择套版')
        elif isinstance(profile.get('rect'), (list, tuple)) and len(profile['rect']) == 4:
            try:
                rect = core.validate_rect(profile['rect'], self.template_image.size)
                for var, value in zip(self.rect_vars, rect): var.set(str(value))
            except (ValueError, TypeError):
                pass
        for key, btn in self.direction_buttons.items():
            btn.configure(bg=ACCENT if key == self.direction else '#2a3942',
                          fg=BG if key == self.direction else TEXT,
                          relief='sunken' if key == self.direction else 'flat')
        if self.direction == 'custom':
            self.status_var.set('自定义比例 · 更换任意比例套版，导出尺寸自动跟随图片')
        else:
            self.status_var.set(f'{title}模式 · 添加{"横屏" if self.direction == "portrait" else "竖屏"}视频后导出')
        if self.template_image is None:
            self.status_var.set('选择视频与套版开始处理')
        self.update_size_info()
        self.schedule_preview()

    def switch_direction(self, direction):
        if self.busy or direction == self.direction or direction not in DIRECTIONS:
            return
        try:
            self.remember_profile()
        except ValueError as exc:
            messagebox.showerror('请检查当前设置', str(exc), parent=self)
            return
        self.direction = direction
        self.apply_profile()
        for iid, job in self.jobs.items():
            if job['info']:
                job.update(output=None, error='')
                job.pop('backend', None)
                self.tree.set(iid, 'status', '待导出')
        self.progress_var.set(0)
        self.save_settings()

    def save_settings(self):
        try:
            CONFIG.parent.mkdir(parents=True, exist_ok=True)
            self.remember_profile()
            # Keep legacy landscape fields so the user's older executable can still read its settings.
            self.machine_settings.update(scheduleRoot=self.schedule_root_var.get().strip(),
                                         templateRoot=self.template_root_var.get().strip(),
                                         matchPaths=self.match_paths_var.get(),outputKeyword='输出')
            self.machine_settings.pop('gameKeywords',None)
            data = dict(self.machine_settings, **self.profiles.get('landscape', {}), direction=self.direction,
                        profiles=self.profiles, machine=self.machine_settings,
                        output=self.output_var.get(), backend=self.backend_var.get(), quality=self.quality_var.get(),
                        language=self.language_var.get(), otherLanguage=self.other_language_var.get(),
                        nameMode=self.name_mode_var.get(),namePattern=self.name_pattern_var.get(),
                        namePresets=self.name_presets,namePreset=self.name_preset_var.get(),outputGroup=self.output_group_var.get(), recipes=self.recipes,
                        outputTemplates=self.output_templates, exportMode=self.export_mode_var.get(),
                        templateLibrary=self.template_library, notifyCompletion=self.notify_completion_var.get(),
                        soundCompletion=self.sound_completion_var.get(), completionSound=self.completion_sound_var.get(),
                        hiddenTaskItems=self.hidden_task_items, ratio=self.ratio_var.get(), numericMode=self.numeric_mode_var.get(), theme=self.theme_mode)
            engine_module.atomic_json(CONFIG, data)
            self.sync_engine_profiles()
            return True
        except (OSError, ValueError) as exc:
            self.status_var.set(f'本机设置未保存：{exc}')
            return False

    def schedule_save(self, *_):
        if self.loading_selection:
            return
        if self.save_pending:
            self.after_cancel(self.save_pending)
        self.save_pending = self.after(250, self.save_settings)
        selected = self.tree.selection()
        if selected and selected[0] in self.jobs:
            job = self.jobs[selected[0]]
            try:
                if self.preview_override is not None:
                    self.preview_override.update(self._engine_profile('custom',{'template':str(self.template),
                        'rect':self.get_rect(),'mode':self.mode_var.get(),'zoom':self.get_zoom(),'detection':self.detection_var.get()}))
                    self.preview_override['outputSize']=list(self.output_size())
                    return
                job['rect'] = self.get_rect()
                if job.get('config') and self.template:
                    profile = {'template': str(self.template), 'rect': job['rect'], 'size': self.size_var.get(),
                               'mode': self.mode_var.get(), 'zoom': self.get_zoom(), 'detection': self.detection_var.get()}
                    job['config'] = dict(job['config'], **self._engine_profile(self.direction, profile))
            except (ValueError, TypeError):
                pass

    def _engine_profile(self, direction, profile):
        resolution = 'original'
        fit = {'等比填满（推荐）': 'cover', '完整显示': 'contain', '拉伸铺满': 'stretch', '自由缩放': 'zoom'}.get(profile.get('mode'), 'cover')
        config = {'direction': direction, 'templateId': 'portrait' if direction == 'portrait' else 'landscape',
                  'detection': {'自动（透明 / 黑 / 白）': 'auto', '仅黑色区域': 'black', '仅白色区域': 'white', '仅透明区域': 'transparent'}.get(profile.get('detection'), 'auto'),
                  'rect': list(profile['rect']) if profile.get('rect') is not None else None, 'fit': fit, 'zoom': profile.get('zoom', 100),
                  'resolution': resolution, 'quality': 'high' if self.quality_var.get() == '高画质' else 'standard',
                  'backend': {'自动（优先 GPU）': 'auto', 'NVIDIA GPU · NVENC': 'nvenc', 'CPU · 兼容模式': 'cpu'}.get(self.backend_var.get(), 'auto'),
                  'outputDir': self.output_var.get(),
                  'language': {'自动识别': 'auto', '不添加': 'none', '其他': 'other'}.get(self.language_var.get(), self.language_var.get()),
                  'otherLanguage': self.other_language_var.get(),
                  'nameMode': {'完整信息':'full','自定义规则':'custom'}.get(self.name_mode_var.get(),'compact'),
                  'namePattern':self.name_pattern_var.get(),'nameDate':'',
                  'outputGroup': {'统一文件夹': 'folder', '按项目分文件夹': 'project', '按模板分文件夹': 'template'}[self.output_group_var.get()],
                  'outputSize': list(RATIOS[self.ratio_var.get()]) if RATIOS.get(self.ratio_var.get()) else None}
        template = profile.get('template')
        if template and Path(template).is_file():
            config['template'] = template
        return config

    def sync_engine_profiles(self):
        state = Path(os.getenv('LOCALAPPDATA', Path.home())) / 'FrameFitCodex'
        state.mkdir(parents=True, exist_ok=True)
        profiles = {direction: self._engine_profile(direction, profile)
                    for direction, profile in self.profiles.items() if direction in DIRECTIONS}
        engine_module.atomic_json(state / 'profiles.json', profiles)

    def start_local_service(self):
        try:
            state = Path(os.getenv('LOCALAPPDATA', Path.home())) / 'FrameFitCodex'
            engine = engine_module.Engine(RUNTIME_ROOT, state)
            self.engine = engine
            from local_service import FrameFitLocalService
            service = FrameFitLocalService(engine, str(state),
                on_config=lambda result: self.events.put(('remote_config', result)),
                before_call=self.sync_before_remote,
                on_task=lambda result: self.events.put(('task_started', result)))
            service.start()
            self.backend_status.set(f'本机 Codex 连接已就绪 · FrameFit {engine.version}')
            return service
        except Exception as exc:
            self.backend_status.set(f'本机 Codex 连接启动失败：{exc}')
            try:
                state = Path(os.getenv('LOCALAPPDATA', Path.home())) / 'FrameFitCodex'
                state.mkdir(parents=True, exist_ok=True)
                (state / 'desktop-startup.log').write_text(
                    f'{exc}\n{traceback.format_exc()}', encoding='utf-8')
            except OSError:
                pass
            return None

    def sync_before_remote(self, action):
        if action not in ('prepare_config', 'update_config', 'export_videos', 'manage_presets', 'manage_recipes'):
            return
        done = threading.Event()
        errors = []
        def synchronize():
            try:
                # ``busy`` also covers short UI refreshes and stale controls
                # left disabled after a cancelled preflight.  Remote calls
                # must be blocked only while a real task or border preflight
                # is active; otherwise a new export can be rejected before it
                # even starts.
                task_busy=self.monitoring_task or self.border_preflight_running
                if task_busy and action in ('prepare_config', 'update_config', 'export_videos'):
                    raise ValueError('桌面正在处理任务，请等待完成或取消后再修改配置。')
                if not self.save_settings():
                    raise ValueError('桌面当前参数无效或本机设置无法保存，请先修正。')
            except Exception as exc:
                errors.append(exc)
            finally:
                done.set()
        self.events.put(('main_call', synchronize))
        if not done.wait(15):
            raise RuntimeError('桌面有未关闭的对话框，请先处理后重试。')
        if errors:
            raise errors[0]

    def apply_remote_config(self, session):
        if isinstance((session or {}).get('recipes'), list):
            self.recipes = {recipe['name']: recipe.get('config', {})
                            for recipe in session['recipes'] if isinstance(recipe, dict) and recipe.get('name')}
            self.recipe_combo.configure(values=list(self.recipes))
            self.save_settings()
        config = (session or {}).get('config')
        if not isinstance(config, dict):
            return
        self.loading_selection = True
        variants = {}
        for item in session.get('videos',[]):
            if item.get('outputKey') and item.get('config'):
                variants[item['outputKey']] = dict(item['config'])
        if variants:
            self.output_templates = list(variants.values())
            self.export_mode_var.set('multi')
            self.update_output_templates_label()
        video_paths = [video.get('path') for video in session.get('videos', []) if video.get('path')]
        for video in session.get('videos', []):
            cfg = video.get('config')
            if not cfg:
                continue
            identity = (os.path.normcase(str(Path(video['path']).resolve())), video.get('outputKey',''))
            iid = next((key for key,job in self.jobs.items() if
                (os.path.normcase(str(job['path'].resolve())),job.get('outputKey',''))==identity), None)
            if iid is None:
                iid = self.tree.insert('', 'end', values=(video['name'], '×'.join(map(str,video.get('outputSize',[]))),
                    f"{video.get('info',{}).get('duration',0):.1f}s", Path(cfg['template']).stem, '已准备'))
                self.jobs[iid] = {'path':Path(video['path']), 'info':video.get('info'), 'output':None, 'error':video.get('error','')}
            self.jobs[iid].update(outputKey=video.get('outputKey',''),template=cfg['template'],rect=cfg['rect'],config=dict(cfg),
                outputName=video.get('outputName'),match=video.get('match',{}),requiresTemplateReview=video.get('requiresTemplateReview',False))
            self.update_queue_template(iid,[cfg])
            if not self.jobs[iid].get('borderReport') and not self.jobs[iid].get('borderScanning'):
                self.jobs[iid]['borderScanning'] = True
                self.scan_borders(iid,self.jobs[iid]['path'])
        self.queue_var.set(f'输出队列 · {len(self.jobs)} 项')
        if not self.tree.selection() and self.tree.get_children():
            self.tree.selection_set(self.tree.get_children()[0])
        direction = config.get('direction', 'landscape')
        if direction in DIRECTIONS and direction != self.direction:
            self.switch_direction(direction)
        self.mode_var.set({'cover': '等比填满（推荐）', 'contain': '完整显示', 'stretch': '拉伸铺满', 'zoom': '自由缩放'}.get(config.get('fit'), '等比填满（推荐）'))
        self.zoom_var.set(str(config.get('zoom', 100)))
        resolution_names = {'1080p': 0, '720p': 1, '4k': 2, 'original': 3}
        sizes = list(DIRECTIONS[self.direction][1])
        if config.get('resolution') in resolution_names:
            self.size_var.set(sizes[min(len(sizes) - 1, resolution_names[config['resolution']])])
        self.quality_var.set('高画质' if config.get('quality') == 'high' else '标准画质（文件更小）')
        detection_names = {value: label for label, value in DETECTION.items()}
        self.detection_var.set(detection_names.get(config.get('detection'), next(iter(DETECTION))))
        backend_names = {'auto': '自动（优先 GPU）', 'nvenc': 'NVIDIA GPU · NVENC', 'cpu': 'CPU · 兼容模式'}
        if config.get('backend') in backend_names:
            self.backend_var.set(backend_names[config['backend']])
        if config.get('template') and Path(config['template']).is_file():
            self.load_template(config['template'], quiet=True)
        if isinstance(config.get('rect'), (list, tuple)):
            for var, value in zip(self.rect_vars, config['rect']):
                var.set(str(value))
        self.output_var.set(config.get('outputDir', self.output_var.get()))
        self.language_var.set({'auto': '自动识别', 'none': '不添加', 'other': '其他', '繁中': '繁中'}.get(config.get('language'), config.get('language', '自动识别')))
        self.other_language_var.set(config.get('otherLanguage', ''))
        self.name_mode_var.set({'full':'完整信息','custom':'自定义规则'}.get(config.get('nameMode'),'精简命名'))
        self.name_pattern_var.set(config.get('namePattern',naming.DEFAULT_PATTERN))
        self.output_group_var.set({'folder': '统一文件夹', 'project': '按项目分文件夹', 'template': '按模板分文件夹'}.get(config.get('outputGroup'), '统一文件夹'))
        self.update_language_controls()
        for video in session.get('videos', []):
            for iid, job in self.jobs.items():
                if str(job['path'].resolve()) == video['path'] and job.get('outputKey','')==video.get('outputKey','') and video.get('config'):
                    job.update(template=video['config']['template'], rect=video['config']['rect'],
                               config=dict(video['config']), outputName=video.get('outputName'),
                               match=video.get('match', {}), requiresTemplateReview=video.get('requiresTemplateReview', False))
        self.loading_selection = False
        if self.tree.selection():
            self.select_queue_item()
        self.save_settings()

    def save_recipe(self):
        name = self.recipe_name_var.get().strip()
        if not name:
            messagebox.showinfo('保存输出模板', '请先填写输出模板名称。', parent=self)
            return
        try:
            self.remember_profile()
        except ValueError as exc:
            messagebox.showerror('无法保存输出模板', str(exc), parent=self)
            return
        self.recipes[name] = {
            'outputTemplates': list(self.output_templates), 'exportMode': self.export_mode_var.get(), 'ratio': self.ratio_var.get(),
            'direction': self.direction,
            'profile': dict(self.profiles.get(self.direction, {})),
            'engineConfig': self._engine_profile(self.direction, self.profiles.get(self.direction, {})),
            'quality': self.quality_var.get(), 'backend': self.backend_var.get(),
            'output': self.output_var.get(), 'language': self.language_var.get(),
            'nameMode': self.name_mode_var.get(), 'outputGroup': self.output_group_var.get(),
            'namePattern':self.name_pattern_var.get(),
            'otherLanguage': self.other_language_var.get(),
        }
        self.recipe_var.set(name)
        self.recipe_combo.configure(values=list(self.recipes))
        self.save_settings()
        self.status_var.set(f'输出模板“{name}”已保存')

    def apply_recipe(self):
        name = self.recipe_var.get()
        recipe = self.recipes.get(name)
        if not recipe:
            messagebox.showinfo('使用输出模板', '请先选择一个输出模板。', parent=self)
            return
        self.output_templates = list(recipe.get('outputTemplates', []))
        self.export_mode_var.set(recipe.get('exportMode','multi' if self.output_templates else 'single'))
        saved_ratio = recipe.get('ratio', '跟随套版')
        if saved_ratio.startswith('自定义 · '):
            parts = saved_ratio.removeprefix('自定义 · ').split('×')
            if len(parts)==2 and all(value.isdigit() for value in parts):
                self.restore_output_size(list(map(int,parts)))
        else:
            self.ratio_var.set(saved_ratio)
        self.update_output_templates_label()
        direction = recipe.get('direction')
        profile = recipe.get('profile')
        if direction in DIRECTIONS and isinstance(profile, dict):
            if self.direction != direction:
                self.direction = direction
            self.profiles[direction] = dict(profile)
            self.apply_profile()
        self.quality_var.set(recipe.get('quality', self.quality_var.get()))
        self.backend_var.set(recipe.get('backend', self.backend_var.get()))
        self.output_var.set(recipe.get('output', self.output_var.get()))
        self.language_var.set(recipe.get('language', '自动识别'))
        self.other_language_var.set(recipe.get('otherLanguage', ''))
        self.name_mode_var.set(recipe.get('nameMode', '精简命名'))
        self.name_pattern_var.set(recipe.get('namePattern',naming.DEFAULT_PATTERN))
        self.output_group_var.set(recipe.get('outputGroup', self.output_group_var.get()))
        self.update_language_controls()
        self.save_settings()
        self.status_var.set(f'已使用输出模板“{name}”')

    def delete_recipe(self):
        name = self.recipe_var.get()
        if name not in self.recipes:
            return
        del self.recipes[name]
        self.recipe_combo.configure(values=list(self.recipes))
        self.recipe_var.set('')
        self.save_settings()
        self.status_var.set(f'输出模板“{name}”已删除')

    def update_language_controls(self, _event=None):
        if self.language_var.get() == '其他':
            self.other_language_entry.pack(fill='x', padx=7, ipady=5, pady=(0, 8),before=self.name_mode_label)
        else:
            self.other_language_entry.pack_forget()

    def app_version(self):
        try:
            version_path = Path(__file__).resolve().with_name('version.json')
            return json.loads(version_path.read_text(encoding='utf-8')).get('version', '1.1.0')
        except (OSError, ValueError):
            return '1.1.0'

    def check_for_updates(self):
        if self.update_button:
            self.update_button.configure(state='disabled', text='检查更新中…')
            self.update_status_var.set('连接稳定版本清单…')
        def worker():
            try:
                manifest = check_update(self.app_version())
                self.events.put(('update_check', manifest, ''))
            except Exception as exc:
                self.events.put(('update_check', None, str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def update_action(self):
        if self.pending_update:
            self.install_update()
        else:
            self.check_for_updates()

    def install_update(self):
        if not self.install_root:
            messagebox.showerror('无法更新', '请从 FrameFit.exe 启动软件后再更新。', parent=self)
            return
        manifest = self.pending_update
        self.update_button.configure(state='disabled', text='正在下载并校验…')
        self.update_status_var.set('下载并校验更新…')
        def worker():
            try:
                target = stage_update(manifest, self.install_root)
                self.events.put(('update_staged', target, ''))
            except Exception as exc:
                self.events.put(('update_staged', None, str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def activate_staged_update(self):
        if not self.staged_update or not self.install_root:
            return
        try:
            launcher=self.install_root / 'FrameFitLauncher.exe'
            if not launcher.is_file(): launcher=self.install_root / 'FrameFit.exe'
            activate_update(self.staged_update, self.install_root, launcher)
            if self.local_service:
                self.local_service.close()
            self.destroy()
        except Exception as exc:
            self.update_button.configure(state='normal', text='重试切换')
            self.update_status_var.set('切换失败，点击重试')
            self.update_button.tooltip.text=f'更新已下载但切换失败：{exc}'
            self.staged_update = None

    def load_template(self, path, quiet=False):
        try:
            with Image.open(path) as raw:
                image = raw.convert('RGBA')
            if min(image.size) < 2:
                raise ValueError('套框尺寸过小。')
            if (self.direction == 'portrait' and image.width >= image.height) or \
                    (self.direction == 'landscape' and image.height >= image.width):
                self.direction = 'custom'
            self.template, self.template_image = Path(path), image
            selected = self.tree.selection() if hasattr(self, 'tree') else ()
            if selected and selected[0] in self.jobs and not self.loading_selection:
                self.jobs[selected[0]]['template'] = str(self.template)
            names = {'default.png': '仙界横版套框', 'default_portrait.png': '仙界竖版套框'}
            self.template_var.set(f'{names.get(Path(path).name, Path(path).name)}  ·  {image.width} × {image.height}')
            self.auto_detect(quiet=quiet)
            self.update_size_info()
            return True
        except Exception as exc:
            if not quiet:
                messagebox.showerror('套框读取失败', str(exc), parent=self)
            return False

    def auto_detect(self, quiet=False):
        if self.template_image is None:
            return
        try:
            rect = core.detect_window(self.template_image, DETECTION[self.detection_var.get()])
            self.status_var.set(f'已识别窗口 · X={rect[0]} Y={rect[1]} · {rect[2]}×{rect[3]}')
        except ValueError as exc:
            rect = (0, 0, self.template_image.width, self.template_image.height)
            if not quiet:
                messagebox.showwarning('请手动设置窗口', str(exc), parent=self)
        for var, value in zip(self.rect_vars, rect): var.set(str(value))
        selected = self.tree.selection() if hasattr(self, 'tree') else ()
        if selected and selected[0] in self.jobs and not self.loading_selection:
            self.jobs[selected[0]]['rect'] = list(rect)
        self.schedule_preview()

    def get_rect(self):
        return core.validate_rect([int(v.get()) for v in self.rect_vars], self.template_image.size)

    def get_zoom(self):
        return core.validate_zoom(self.zoom_var.get()) if MODES[self.mode_var.get()] == 'zoom' else 100

    def update_zoom_controls(self, *_):
        if MODES[self.mode_var.get()] == 'zoom':
            self.zoom_box.pack(after=self.mode_combo, fill='x', pady=(0, 12))
            self.after(100, self.reveal_zoom_controls)
        else:
            self.zoom_box.pack_forget()
        self.schedule_preview()

    def reveal_zoom_controls(self):
        if MODES[self.mode_var.get()] != 'zoom':
            return
        canvas = self.settings_canvas
        bounds = canvas.bbox('all')
        bottom = 18 + self.zoom_box.winfo_y() + self.zoom_box.winfo_height() + 12
        if bounds and bottom > canvas.canvasy(canvas.winfo_height()):
            canvas.yview_moveto(max(0, bottom - canvas.winfo_height()) / bounds[3])

    def output_size(self):
        if RATIOS.get(self.ratio_var.get()):
            return RATIOS[self.ratio_var.get()]
        return tuple(v // 2 * 2 for v in self.template_image.size) if self.template_image else (1920,1080)

    def update_size_info(self):
        if self.template_image is None:
            return
        width, height = self.output_size()
        divisor = math.gcd(width, height)
        if self.ratio_var.get() == '跟随套版':
            text = f'导出：跟随套版（当前 {width} × {height}）· 比例 {width//divisor}:{height//divisor}'
        else:
            text = f'导出：{width} × {height} · 比例 {width//divisor}:{height//divisor}'
        original_size = not RATIOS.get(self.ratio_var.get())
        if original_size and (width, height) != self.template_image.size:
            text += '\n原图含奇数边长，编码时向下调整1像素'
        self.size_info_var.set(text)

    def choose_template(self):
        orientation = {'portrait': '竖版', 'landscape': '横版', 'custom': '任意比例'}[self.direction]
        selected_job=self.jobs.get(self.tree.selection()[0]) if self.tree.selection() else None
        paths = filedialog.askopenfilenames(parent=self, title=f'选择{orientation}模板（可单选或多选）',
            initialdir=self.template_initial_dir(selected_job), filetypes=[('模板图片', '*.png *.jpg *.jpeg *.bmp *.webp')])
        if paths:
            self.reset_pending_variants()
            self.output_templates=[]
            self.add_template_paths(paths)
            self.load_template(paths[0],quiet=True)
            selected = self.tree.selection()
            for iid in selected:
                if iid in self.jobs:
                    self.jobs[iid]['requiresTemplateReview'] = False
            self.save_settings()

    def replace_queue_template_click(self,event):
        if self.busy or self.tree.identify_column(event.x) != '#4':
            return
        iid=self.tree.identify_row(event.y)
        if iid and iid in self.jobs:
            self.tree.selection_set(iid)
            self.tree.focus(iid)
            self.after(0,self.manage_output_templates)
            return 'break'

    def select_queue_item(self, _event=None):
        selected = self.tree.selection()
        self.queue_var.set(f'视频队列 · {len(self.jobs)} 项'+(f' · 选中 {len(selected)}' if selected else ''))
        if selected:
            self.preview_override=None
            # Never carry the previous row's composed frame into a newly
            # selected video.  The worker will publish the first correct
            # frame after the selected job/template pair is synchronized.
            self.display_image=None
            self.preview_var.set('正在切换视频预览…')
            job = self.jobs.get(selected[0], {})
            self.update_queue_template(selected[0],job.get('templates') or ([job.get('config')] if job.get('config') else []))
            reference=job.get('match') or {}
            self.match_reference_var.set(f'{reference.get("projectFolder",job["path"].stem)} → {reference.get("game","未匹配排期游戏")} → '+
                ('、'.join(reference.get('templateRoots',[])) or '手动选择套版'))
            if job.get('borderReport'):
                self.show_borders(selected[0], job['borderReport'])
            elif job.get('borderError'):
                self.show_borders(selected[0], None, job['borderError'])
            else:
                self.border_var.set('正在扫描原视频黑边和白边…')
                self.border_points=[]
                self.border_source_iid=None
                self.set_timeline_markers([])
            path = job.get('template')
            saved_rect = job.get('rect')
            self.loading_selection = True
            if path and Path(path).is_file() and (not self.template or Path(path) != self.template):
                with Image.open(path) as image:
                    template_size = image.size
                target_direction = (job.get('config') or {}).get('direction') or ('portrait' if template_size[1] > template_size[0] else 'landscape' if template_size[0] > template_size[1] else 'custom')
                if self.direction != target_direction:
                    if self.busy:
                        self.direction = target_direction
                        self.apply_profile()
                    else:
                        self.switch_direction(target_direction)
                self.load_template(path, quiet=True)
            if saved_rect:
                for variable, value in zip(self.rect_vars, saved_rect):
                    variable.set(str(value))
                job['rect'] = saved_rect
            cfg = job.get('config') or {}
            if cfg:
                self.restore_output_size(cfg.get('outputSize'))
                self.mode_var.set({'cover': '等比填满（推荐）', 'contain': '完整显示', 'stretch': '拉伸铺满', 'zoom': '自由缩放'}[cfg['fit']])
                self.zoom_var.set(str(cfg['zoom']))
                self.detection_var.set({value: label for label,value in DETECTION.items()}.get(cfg.get('detection'), next(iter(DETECTION))))
                choices = list(DIRECTIONS[self.direction][1])
                index = {'1080p': 0, '720p': 1, '4k': 2, 'original': 3}[cfg['resolution']]
                self.size_var.set(choices[min(index, len(choices)-1)])
                self.quality_var.set('高画质' if cfg['quality'] == 'high' else '标准画质（文件更小）')
                self.output_var.set(cfg['outputDir'])
                self.language_var.set({'auto':'自动识别', 'none':'不添加', 'other':'其他', '繁中':'繁中'}.get(cfg.get('language'), cfg.get('language', '自动识别')))
                self.other_language_var.set(cfg.get('otherLanguage', ''))
                self.name_mode_var.set({'full':'完整信息','custom':'自定义规则'}.get(cfg.get('nameMode'),'精简命名'))
                self.name_pattern_var.set(cfg.get('namePattern',naming.DEFAULT_PATTERN))
                self.output_group_var.set({'folder':'统一文件夹','project':'按项目分文件夹','template':'按模板分文件夹'}.get(cfg.get('outputGroup'),'统一文件夹'))
            self.loading_selection = False
            self.update_output_templates_label()
            self.refresh_template_choices(job)
            self.schedule_preview()

    def choose_project_template(self, iid):
        job=self.jobs.get(iid)
        match=(job or {}).get('match',{})
        candidates=match.get('availableCandidates') or match.get('candidates',[])
        if not candidates:return
        previous=self.machine_settings.get('projects',{}).get(match.get('projectFolder',''),{})
        folder=(match.get('templateRoots') or [str(Path(candidates[0]['path']).parent)])[0]
        def apply(chosen, _folder, _old):
            self.apply_project_templates(iid,chosen)
            mapping=self.machine_settings.setdefault('projects',{})
            mapping[match.get('projectFolder','')]={'paths':[item['path'] for item in chosen],
                'files':{item['path']:Path(item['path']).stat().st_mtime for item in chosen},
                'templateRoots':match.get('templateRoots',[]),'selectedAt':time.time(),
                'knownPaths':[item['path'] for item in candidates]+[path for item in candidates for path in item.get('olderVersions',[])]}
            self.save_settings()
            self.update_queue_template(iid,self.jobs[iid].get('templates',[]))
            self.select_queue_item()
        TemplateBrowser(self,candidates,[item['path'] for item in previous.get('paths',[])],folder,apply,
                        reference=f"{job['path'].name} → {match.get('game','未识别游戏')} → {folder}",
                        previous=previous.get('paths',[]),selected_at=previous.get('selectedAt',0))
    def apply_project_templates(self,iid,candidates):
        if iid not in self.jobs or not candidates:return
        job=self.jobs[iid]
        configs=[]
        for candidate in candidates:
            with Image.open(candidate['path']) as image:
                try:rect=list(core.detect_window(image.convert('RGBA'),DETECTION[self.detection_var.get()]))
                except ValueError:rect=[0,0,*image.size]
                size=[value//2*2 for value in image.size]
            cfg=self._engine_profile('custom',{'template':candidate['path'],'rect':rect,'mode':self.mode_var.get(),'zoom':self.get_zoom()})
            cfg.update(outputSize=size,templateConfirmed=True)
            configs.append(cfg)
        job.update(template=candidates[0]['path'],rect=configs[0]['rect'],templates=configs,requiresTemplateReview=False)
        known={os.path.normcase(str(Path(cfg.get('template','')).resolve())) for cfg in self.template_library}
        for cfg in configs:
            if os.path.normcase(str(Path(cfg['template']).resolve())) not in known:
                self.template_library.append(dict(cfg))
        self.update_queue_template(iid,configs)
        self.tree.set(iid,'status','待导出')

    def update_queue_template(self,iid,configs=None):
        if iid not in self.jobs or not hasattr(self,'tree'):
            return
        configs=configs if configs is not None else self.jobs[iid].get('templates') or self.output_templates or []
        names=[Path(cfg.get('template','')).stem for cfg in configs if cfg.get('template')]
        self.tree.set(iid,'template','、'.join(names) if names else '未选择')

    def resolve_project_template(self, iid, job):
        result = job.get('match') or {}
        if result.get('status')=='disabled':
            job['requiresTemplateReview']=False
            return
        if result.get('status') == 'matched' and result.get('candidates'):
            self.apply_project_templates(iid,result['candidates'])
            return
        job['requiresTemplateReview'] = True
        if result.get('status') in ('ambiguous', 'size_missing', 'orientation_missing', 'template_updated','date_conflict'):
            self.tree.set(iid, 'template', '待选择')
            self.tree.set(iid, 'status', '待确认')
            self.after(0, lambda iid=iid: self.choose_project_template(iid))
        elif result.get('status'):
            self.tree.set(iid, 'template', '未匹配')
            self.tree.set(iid, 'status', '待确认')
            self.status_var.set(result.get('message', '项目模板需要确认。'))

    def choose_videos(self):
        orientation = {'portrait': '横屏', 'landscape': '竖屏', 'custom': ''}[self.direction]
        paths = filedialog.askopenfilenames(parent=self, title=f'添加{orientation}视频（可多选）', filetypes=[('视频', '*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.ts *.wmv'), ('所有文件', '*.*')])
        self.add_videos(paths)

    def drop(self, event):
        if not self.busy:
            self.add_videos(self.tk.splitlist(event.data))
        return event.action

    def add_videos(self, paths):
        if self.busy: return
        existing = {os.path.normcase(str(job['path'].resolve())) for job in self.jobs.values()}
        pending = []
        for path in paths:
            path = Path(path)
            key = os.path.normcase(str(path.resolve()))
            if path.is_file() and path.suffix.lower() in EXTENSIONS and key not in existing:
                iid = self.tree.insert('', 'end', values=(path.name, '读取中', '', '未选择', '读取中'))
                self.jobs[iid] = {'path': path, 'info': None, 'error': '', 'output': None}
                existing.add(key)
                pending.append((iid, path))
        self.queue_var.set(f'视频队列 · {len(self.jobs)} 个')
        if not pending: return
        self.importing += len(pending)
        self.status_var.set('正在读取视频信息…')
        schedule_root = self.schedule_root_var.get().strip()
        template_root = self.template_root_var.get().strip()
        mapping = dict(self.machine_settings, matchPaths=self.match_paths_var.get(),outputKeyword='输出')
        query_direction = 'custom' if not RATIOS.get(self.ratio_var.get()) else self.direction
        query_size = self.output_size() if RATIOS.get(self.ratio_var.get()) else None

        def worker():
            probed = []
            for iid, path in pending:
                try:
                    info = core.probe(path)
                    self.events.put(('probed', iid, info))
                    probed.append((iid, path, info))
                except Exception as exc:
                    self.events.put(('import', iid, None, str(exc), None))
            # Network project lookup must never delay local metadata or preview.
            for iid, path, info in probed:
                try:
                    match = find_project_candidates(path.name, schedule_root, template_root,
                                                    mapping, output_size=query_size, direction=query_direction)
                except Exception as exc:
                    match = {'status': 'root_unavailable', 'message': str(exc), 'candidates': []}
                self.events.put(('import', iid, info, '', match))
        threading.Thread(target=worker, daemon=True).start()

    def delete_queue_key(self, event):
        focus = self.focus_get()
        if focus is not None and (focus.winfo_toplevel() != self or
            isinstance(focus,(tk.Entry,tk.Text,tk.Spinbox,ttk.Combobox,tk.Listbox))):
            return
        if self.active_page == 'workbench':
            return self.remove_selected(event)

    def hide_task_item(self, job):
        if job.get('_taskId') is not None:
            hidden=self.hidden_task_items.setdefault(job['_taskId'],[])
            if job['_taskIndex'] not in hidden:
                hidden.append(job['_taskIndex'])

    def remove_selected(self, _event=None):
        if self.busy:
            self.status_var.set('正在处理，请先取消当前操作，再移除队列项')
            return 'break'
        selected = list(self.tree.selection())
        if not selected:
            return 'break'
        children = list(self.tree.get_children())
        next_index = min((children.index(iid) for iid in selected), default=0)
        for iid in selected:
            job = self.jobs.pop(iid, {})
            self.hide_task_item(job)
            process=job.get('scrubProcess')
            if process and process.poll() is None:
                process.kill()
            self.tree.delete(iid)
        self.task_rows={index:iid for index,iid in self.task_rows.items() if iid in self.jobs}
        self.scrub_target = None
        self.dragging_seek = False
        self.border_points = []
        self.border_source_iid = None
        self.set_timeline_markers([])
        self.border_var.set('请选择视频查看边缘报告')
        self.display_image = None
        remaining = self.tree.get_children()
        if remaining:
            successor=remaining[min(next_index,len(remaining)-1)]
            self.tree.selection_set(successor)
            self.tree.focus(successor)
        self.tree.focus_set()
        self.queue_var.set(f'视频队列 · {len(self.jobs)} 个')
        self.save_settings()
        self.schedule_preview()
        return 'break'

    def choose_output(self):
        path = filedialog.askdirectory(parent=self, title='选择导出文件夹')
        if path: self.output_var.set(path)

    def choose_path(self,variable):
        path=filedialog.askdirectory(parent=self,title='选择目录',initialdir=str(Path.home()))
        if path:
            variable.set(path)
            self.save_settings()

    def choose_completion_sound(self):
        path=filedialog.askopenfilename(parent=self,title='选择完成提示音',initialdir=str(Path.home()),
            filetypes=[('WAV 声音','*.wav'),('音频文件','*.wav *.mp3'),('所有文件','*.*')])
        if path:self.completion_sound_var.set(path)

    def preview_completion_sound(self):
        self._play_completion_sound(self.completion_sound_var.get().strip())

    def _play_completion_sound(self, path=''):
        if not winsound:
            return
        path=str(path or '').strip()
        if not path or not Path(path).is_file():
            try: winsound.MessageBeep(winsound.MB_OK)
            except (OSError,RuntimeError): pass
            return
        if path.lower().endswith('.wav'):
            try: winsound.PlaySound(path,winsound.SND_FILENAME|winsound.SND_ASYNC)
            except (OSError,RuntimeError): pass
            return
        def convert_and_play():
            try:
                import subprocess, hashlib
                cache=CONFIG.parent/'sound-cache'; cache.mkdir(parents=True,exist_ok=True)
                target=cache/(hashlib.sha1(path.encode('utf-8')).hexdigest()+'.wav')
                if not target.is_file():
                    subprocess.run([core.binary('ffmpeg'),'-y','-v','error','-i',path,'-ac','2','-ar','44100',str(target)],
                                   check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=core.CREATE_FLAGS)
                winsound.PlaySound(str(target),winsound.SND_FILENAME|winsound.SND_ASYNC)
            except (OSError,subprocess.SubprocessError,RuntimeError):
                try: winsound.MessageBeep(winsound.MB_OK)
                except (OSError,RuntimeError): pass
        threading.Thread(target=convert_and_play,daemon=True).start()

    def notify_completion(self,status,completed=0,failed=0):
        if self.notify_completion_var.get():
            toast=tk.Toplevel(self)
            toast.overrideredirect(True);toast.attributes('-topmost',True)
            surface=tk.Canvas(toast,bg=self.palette['panel'],highlightthickness=0,bd=0,width=320,height=72)
            surface.pack()
            rounded_path(surface,(1,1,319,71),12,fill=self.palette['panel'],outline=self.palette['line'])
            surface.create_text(18,22,text='FrameFit 导出完成',anchor='w',fill=self.palette['text'],font=('Microsoft YaHei UI',11,'bold'))
            surface.create_text(18,48,text=f'{status} · 完成 {completed} 个 · 失败 {failed} 个',anchor='w',fill=self.palette['muted'],font=('Microsoft YaHei UI',9))
            toast.geometry(f'+{self.winfo_rootx()+self.winfo_width()-340}+{self.winfo_rooty()+self.winfo_height()-105}')
            self.after(4200,lambda:toast.destroy() if toast.winfo_exists() else None)
        if self.sound_completion_var.get():
            self._play_completion_sound(self.completion_sound_var.get().strip())

    def queue_hover_text(self):
        y=self.tree.winfo_pointery()-self.tree.winfo_rooty()
        iid=self.tree.identify_row(y)
        if iid in self.jobs:
            job=self.jobs[iid]
            return str(job['path'])+'\n'+('已完成：'+str(job['output']) if job.get('output') else '可重复导出；Ctrl / Shift 多选，Delete 移除队列项。')
        return '视频队列：点击选择素材，Ctrl / Shift 多选；无选中时导出全部。滚轮或滚动条可浏览列表。'

    def refresh_template_choices(self,job=None):
        variants=(job or {}).get('templates') or self.output_templates
        if not variants and self.template:
            variants=[{'template':str(self.template),'rect':self.get_rect(),'outputSize':list(self.output_size()),
                       'fit':MODES[self.mode_var.get()],'zoom':self.get_zoom()}]
        self.preview_variants=variants
        choices=[f'{index+1} · {Path(cfg["template"]).name}' for index,cfg in enumerate(variants)]
        self.preview_template_combo.configure(values=choices)
        current=str(self.template or '')
        index=next((index for index,cfg in enumerate(variants) if cfg['template']==current),0)
        self.preview_template_var.set(choices[index] if choices else '未选择套版')

    def change_preview_template(self,_event=None):
        try:
            index=int(self.preview_template_var.get().split(' · ')[0])-1
            cfg=self.preview_variants[index]
        except (ValueError,IndexError):
            return
        self.loading_selection=True
        try:
            self.preview_override=cfg
            self.direction='custom'
            self.load_template(cfg['template'],quiet=True)
            for var,value in zip(self.rect_vars,cfg.get('rect') or (0,0,*self.template_image.size)):
                var.set(str(value))
            self.restore_output_size(cfg.get('outputSize'))
            self.mode_var.set({value:key for key,value in MODES.items()}.get(cfg.get('fit'),'等比填满（推荐）'))
            self.zoom_var.set(str(cfg.get('zoom',100)))
        finally:
            self.loading_selection=False
        selected=self.tree.selection()
        if selected and selected[0] in self.jobs:
            self.jobs[selected[0]].pop('compositeReport',None)
            self.show_borders(selected[0],self.jobs[selected[0]].get('borderReport'))
        self.update_size_info()
        self.schedule_preview()

    def open_output(self):
        try:
            path = Path(self.output_var.get()).expanduser()
            path.mkdir(parents=True, exist_ok=True)
            os.startfile(path)
        except OSError as exc:
            messagebox.showerror('无法打开文件夹', str(exc), parent=self)

    def open_job(self, _=None):
        selected = self.tree.selection()
        if not selected: return
        job = self.jobs[selected[0]]
        if job['error']:
            messagebox.showerror('该视频的错误信息', job['error'], parent=self)
        else:
            try: os.startfile(job['output'] or job['path'])
            except OSError as exc: messagebox.showerror('无法打开', str(exc), parent=self)

    def schedule_preview(self, *_):
        self.preview_id += 1
        # Coalesce rapid coordinate motion instead of perpetually postponing
        # the preview until the pointer stops.  The latest Tk variables are
        # read by refresh_preview, giving a bounded ~30 FPS update cadence.
        if self.preview_pending:
            return
        self.preview_pending = self.after(35, self.refresh_preview)

    def on_seek(self, value):
        seconds = max(0.0, float(value))
        self.time_var.set(f'{seconds:.1f}s')
        selected = self.tree.selection()
        job = self.jobs.get(selected[0], {}) if selected else {}
        frames = job.get('scrubFrames', [])
        if frames:
            _second, frame = min(frames, key=lambda pair: abs(pair[0] - seconds))
            self.display_image = self.compose_scrub(frame)
            self.draw_preview()
        if self.dragging_seek:
            self.scrub_target = (selected[0], seconds) if selected else None
            self.start_scrub_worker()
        else:
            self.schedule_preview()

    def compose_scrub(self, frame):
        from PIL import ImageOps
        width,height = self.output_size()
        factor = min(1,800/max(width,height))
        template=self.template_image.convert('RGBA')
        canvas=Image.new('RGBA',template.size,self.palette['field'])
        canvas.alpha_composite(template)
        canvas=canvas.convert('RGB').resize((max(1,round(width*factor)),max(1,round(height*factor))),Image.Resampling.BILINEAR)
        x,y,w,h = core.mapped_rect(self.get_rect(), self.template_image.size, canvas.size)
        mode = MODES[self.mode_var.get()]
        factor = self.get_zoom()/100 if mode=='zoom' else 1
        width,height = max(1,round(w*factor)),max(1,round(h*factor))
        if mode=='stretch':
            video = frame.resize((width,height),Image.Resampling.BILINEAR)
        else:
            ratio = (min if mode=='contain' else max)(width/frame.width,height/frame.height)
            video = frame.resize((max(1,round(frame.width*ratio)), max(1,round(frame.height*ratio))),Image.Resampling.BILINEAR)
        canvas.paste(video,(round(x+(w-video.width)/2),round(y+(h-video.height)/2)))
        return canvas

    def ensure_timeline_cache(self, iid, job):
        if job.get('scrubCaching') or job.get('scrubReady') or not job.get('info'):
            return
        job['scrubCaching'] = True
        job['scrubFrames'] = []
        path = job['path']
        duration = job['info']['duration']
        rate = min(5.0, 600/max(.2,duration))
        def worker():
            import subprocess
            process = None
            try:
                # One low-resolution decode supplies the whole draggable timeline;
                # no visible filmstrip and no full-size composition for each movement.
                process = subprocess.Popen([core.binary('ffmpeg'),'-v','error','-nostdin','-threads','1','-i',str(path),
                    '-an','-filter_threads','1','-vf',f'fps={rate}:start_time=0,scale=320:180:force_original_aspect_ratio=decrease,pad=320:180:(ow-iw)/2:(oh-ih)/2',
                    '-pix_fmt','rgb24','-f','rawvideo','pipe:1'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,creationflags=core.CREATE_FLAGS)
                job['scrubProcess'] = process
                index = 0
                while not self.timeline_stop.is_set() and iid in self.jobs:
                    data = process.stdout.read(320*180*3)
                    if len(data)!=320*180*3:
                        break
                    # Crop decoder padding using the source display aspect ratio.
                    image = Image.frombytes('RGB',(320,180),data)
                    aspect = job['info']['width']/job['info']['height']
                    width,height = (min(320,round(180*aspect)),180) if aspect<320/180 else (320,min(180,round(320/aspect)))
                    image = image.crop(((320-width)//2,(180-height)//2,(320+width)//2,(180+height)//2))
                    job['scrubFrames'].append((index/rate,image))
                    index += 1
            except Exception:
                pass
            finally:
                if process:
                    if process.poll() is None:
                        process.kill()
                    process.wait()
                    process.stdout.close()
                job['scrubCaching'] = False
                job['scrubReady'] = True
                job.pop('scrubProcess',None)
        threading.Thread(target=worker,daemon=True).start()

    def start_scrub_worker(self):
        if self.scrub_running:
            return
        self.scrub_running = True
        def worker():
            try:
                while self.scrub_target and not self.timeline_stop.is_set():
                    target = self.scrub_target
                    self.scrub_target = None
                    iid,seconds = target
                    job = self.jobs.get(iid)
                    if not job:
                        continue
                    frames = job.get('scrubFrames',[])
                    if frames and min(abs(point-seconds) for point,_ in frames)<.25:
                        continue
                    with core.cancellable(self.timeline_stop):
                        frame = core.thumbnail(job['path'],seconds,(480,480))
                    self.events.put(('scrub',iid,seconds,frame))
            except Exception:
                pass
            finally:
                self.scrub_running = False
        threading.Thread(target=worker,daemon=True).start()

    def finish_seek(self, _event=None):
        self.dragging_seek = False
        self.scrub_target = None
        self.schedule_preview()

    def scan_borders(self, iid, path):
        def worker():
            try:
                notify = lambda value: self.events.put(('border_progress', iid, value))
                with self.scan_slots:
                    with core.cancellable(self.timeline_stop):
                        report = self.engine.source_borders(path, on_progress=notify) if hasattr(self, 'engine') else core.analyze_borders(path, on_progress=notify)
                self.events.put(('borders', iid, report, ''))
            except Exception as exc:
                self.events.put(('borders', iid, None, str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def show_borders(self, iid, report, error=''):
        if not self.tree.selection() or self.tree.selection()[0] != iid:
            return
        self.border_points = []
        self.border_source_iid = iid
        if error:
            self.border_var.set(f'黑白边检查失败：{error}')
            return
        segments = list((report or {}).get('segments', []))
        job=self.jobs.get(iid,{})
        for finding in (job.get('compositeReport') or {}).get('newBorders',[]):
            seconds=finding.get('seconds',0)
            locations=[label for key,label in (('top','上'),('bottom','下'),('left','左'),('right','右')) if finding.get(key)]
            segments.append(dict(finding,start=seconds,end=seconds,locations=locations,origin='套版新增'))
        self.border_points = segments
        self.set_timeline_markers(segments)
        if not segments:
            self.border_var.set('原片抽样未检出边缘；套版接缝将在导出前单独检查')
            return
        places=sum(len(segment.get('locations',[])) or 1 for segment in segments)
        self.border_var.set(f'{len(segments)} 个片段 · {places} 处边缘提示 · 悬停时间轴色段查看详情')

    def set_timeline_markers(self, segments):
        colors={'black':'#d96a6a','white':'#e3b85d','dark_seam':'#c18af3'}
        markers=[]
        for segment in segments or []:
            kind=segment.get('kind') or segment.get('color','black')
            label='疑似暗缝' if kind=='dark_seam' else '黑边' if segment.get('color')=='black' else '白边'
            locations='、'.join(segment.get('locations',[]))
            markers.append({'start':segment.get('start',0),'end':max(segment.get('start',0)+.5,segment.get('end',segment.get('start',0))),
                            'color':colors.get(kind,colors.get(segment.get('color'),'#d96a6a')),
                            'label':f'{segment.get("origin","原片")} · {label} · {locations or "边缘"} · {segment.get("start",0):.1f}–{max(segment.get("end",segment.get("start",0)),segment.get("start",0)+.5):.1f}s（抽样）'})
        if hasattr(self,'seek_slider'):
            self.seek_slider.set_markers(markers)

    def check_current_frame(self):
        selected=self.tree.selection()
        if not selected or not self.jobs[selected[0]].get('info'):
            self.status_var.set('先选择一个已读取的视频，再检查当前时间点')
            return
        iid=selected[0]
        job=self.jobs[iid]
        seconds=float(self.seek_var.get())
        template,rect,size,fit,zoom=self.template,self.get_rect(),self.output_size(),MODES[self.mode_var.get()],self.get_zoom()
        self.border_var.set(f'正在检查 {seconds:.2f}s 的四侧边缘和套版接缝…')
        def worker():
            try:
                source=core.detect_borders(core.border_frame(job['path'],seconds))
                report=core.analyze_composite_borders(job['path'],template,rect,size,fit,zoom,sample_points=[seconds])
                self.events.put(('frame_borders',iid,seconds,source,report))
            except Exception as exc:
                self.events.put(('frame_border_error',str(exc)))
        threading.Thread(target=worker,daemon=True).start()

    def refresh_preview(self):
        self.preview_pending = None
        if self.template_image is None:
            self.display_image=None
            self.seek_var.set(0)
            self.time_var.set('0.0s')
            self.preview_var.set('添加视频与套版开始预览')
            self.draw_preview()
            return
        try:
            rect = self.get_rect()
            size = self.output_size()
            seconds = float(self.seek_var.get())
            zoom = self.get_zoom()
            if not 0 <= seconds < 1e7:
                raise ValueError('预览秒数无效')
        except ValueError as exc:
            self.preview_var.set(f'请检查窗口坐标、预览秒数或缩放比例：{exc}')
            self.display_image = None
            self.canvas.delete('all')
            return
        selected = self.tree.selection()
        job = self.jobs.get(selected[0]) if selected else None
        if job:
            self.ensure_timeline_cache(selected[0], job)
            if job.get('info'):
                maximum = max(0, job['info']['duration'] - .15)
                self.seek_slider.configure(to=maximum)
                self.seek_var.set(min(self.seek_var.get(), maximum))
        if not job or not job['info']:
            im = self.template_image.copy()
            draw = ImageDraw.Draw(im)
            x, y, w, h = rect
            draw.rectangle((x, y, x+w-1, y+h-1), outline=ACCENT, width=max(3, im.width // 250))
            self.display_image = im
            self.preview_var.set('套框预览  /  绿色区域用于视频初始对齐')
            self.draw_preview()
            return
        ticket = self.preview_id
        seconds = min(seconds, max(0, job['info']['duration'] - .15))
        path, template, mode = job['path'], self.template, MODES[self.mode_var.get()]
        self.preview_var.set('正在生成合成预览…')
        # Use output resolution for identical crop/rounding; render off the UI thread.
        def worker():
            try:
                with self.preview_slots:
                    if ticket != self.preview_id:
                        return
                    im = core.preview(path, template, rect, size, mode, seconds, zoom=zoom)
                self.events.put(('preview', ticket, im, f'{path.name}  ·  {seconds:.1f}s  ·  {size[0]} × {size[1]}'))
            except Exception as exc:
                self.events.put(('preview_error', ticket, str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def draw_preview(self):
        w, h = self.canvas.winfo_width(), self.canvas.winfo_height()
        if min(w, h) < 5: return
        self.canvas.configure(bg=self.palette['field'])
        self.canvas.delete('all')
        if self.display_image is None:
            cx,cy=w/2,h/2
            self.canvas.create_rectangle(cx-27,cy-38,cx+27,cy+2,outline=self.palette['line'],width=2)
            self.canvas.create_line(cx-18,cy-6,cx-3,cy-21,cx+7,cy-10,cx+16,cy-18,fill=self.palette['muted'],width=1.5)
            self.canvas.create_text(cx,cy+29,text='添加视频与套版',fill=self.palette['text'],font=('Microsoft YaHei UI',13))
            self.canvas.create_text(cx,cy+57,text='把视频拖到这里，或从下方添加',fill=self.palette['muted'],font=('Microsoft YaHei UI',9))
            return
        im = self.display_image.copy()
        if im.mode=='RGBA':
            backdrop=Image.new('RGBA',im.size,self.palette['field'])
            backdrop.alpha_composite(im)
            im=backdrop.convert('RGB')
        im.thumbnail((w-4, h-4), Image.Resampling.LANCZOS)
        self.photo = ImageTk.PhotoImage(im)
        self.canvas.create_image(w/2, h/2, image=self.photo)

    def set_busy(self, value):
        self.busy = value
        self.controls = [(widget,normal) for widget,normal in self.controls if widget.winfo_exists()]
        for widget, normal in self.controls:
            widget.configure(state='disabled' if value else normal)
        self.cancel_button.configure(state='normal' if value else 'disabled')

    def detect_gpu(self):
        def worker():
            available, _ = core.nvenc_available()
            self.events.put(('gpu_available', available))
        threading.Thread(target=worker, daemon=True).start()

    def batch_geometry(self, job, direction, resolution_label, detection):
        template = Path(job.get('template') or self.template)
        with Image.open(template) as image:
            template_size = image.size
            rect = job.get('rect') or core.detect_window(image.convert('RGBA'), detection)
        config = job.get('config') or {}
        if config.get('outputSize'):
            return template, rect, tuple(config['outputSize'])
        if not config and RATIOS.get(self.ratio_var.get()):
            return template, rect, RATIOS[self.ratio_var.get()]
        if config:
            resolution = config.get('resolution', 'original')
            if config.get('direction') == 'custom' or resolution == 'original':
                size = tuple(v // 2 * 2 for v in template_size)
            else:
                size = {'1080p': (1920,1080), '720p': (1280,720), '4k': (3840,2160)}[resolution]
                if template_size[1] > template_size[0]:
                    size = size[::-1]
        else:
            size = tuple(v // 2 * 2 for v in template_size)
        return template, rect, size

    def run_border_preflight(self, pending, direction, resolution_label, detection, mode, zoom):
        if self.border_preflight_running:
            return
        self.border_preflight_running = True
        self.cancel.clear()
        self.set_busy(True)
        self.status_var.set('正在检查套版预览中新增的黑边或白边…')
        def worker():
            results = []
            try:
                for index,(iid, job) in enumerate(pending):
                    if self.cancel.is_set():
                        raise ValueError('已取消边缘检查，未开始导出。')
                    template, rect, size = self.batch_geometry(job, direction, resolution_label, detection)
                    report = job.get('borderReport') or {'segments': []}
                    effective = job.get('config') or {}
                    def progress(value):
                        self.events.put(('preflight_progress',(index+value)/len(pending),
                            f'套版边缘预检 {index+1}/{len(pending)} · {template.name} · {value:.0%} · 可取消'))
                    with core.cancellable(self.cancel):
                        composite = core.analyze_composite_borders(job['path'], template, rect, size, effective.get('fit', mode), effective.get('zoom', zoom),
                                                                  report.get('segments', []), on_progress=progress)
                    job['compositeReport'] = composite
                    results.append((iid, job, template, rect, size, composite))
                self.events.put(('border_preflight', results, ''))
            except Exception as exc:
                self.events.put(('border_preflight', [], str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def continue_after_border_preflight(self, results, error):
        self.border_preflight_running = False
        self.set_busy(False)
        if self.cancel.is_set():
            self.export_scope=None
            self.status_var.set('已取消边缘检查，未开始导出')
            return
        if error:
            self.export_scope=None
            messagebox.showerror('套版边缘检查失败', error, parent=self)
            return
        added = [(iid, job, border) for iid, job, _template, _rect, _size, composite in results
                 for border in composite.get('newBorders', [])
                 if job.get('borderPolicy', (job.get('config') or {}).get('borderPolicy', 'ask')) == 'ask']
        policy = 'ask'
        if added:
            details = [f"{job['path'].name} · {border['seconds']:.1f}s · 新增{'黑边' if border['color'] == 'black' else '白边'}"
                       for _iid, job, border in added[:12]]
            choice = messagebox.askyesnocancel('套版预览发现新增黑白边',
                f"检测到 {len(added)} 处新增边缘：\n" + '\n'.join(details) +
                '\n\n是：保留边缘并导出\n否：等比例放大并重新检测，消除后导出\n取消：返回队列', parent=self)
            if choice is None:
                self.export_scope=None
                return
            policy = 'keep' if choice else 'remove'
        for iid, job, _template, _rect, _size, _composite in results:
            if iid in self.jobs:
                self.jobs[iid]['compositeReport']=_composite
                existing = job.get('borderPolicy', (job.get('config') or {}).get('borderPolicy', 'ask'))
                self.jobs[iid]['borderPolicy'] = (policy if added else 'keep') if existing == 'ask' else existing
                self.show_borders(iid,self.jobs[iid].get('borderReport'))
        self.skip_border_preflight = True
        self.after(0, self.start_export)

    def confirm_batch(self, pending):
        self.save_settings()
        # A delayed tooltip from a closed template browser must not be mistaken
        # for the export dialog by the modal loop or keyboard automation.
        for popup in list(self.winfo_children()):
            if isinstance(popup,tk.Toplevel):
                try: popup.destroy()
                except tk.TclError: pass
        config = self._engine_profile(self.direction, self.profiles[self.direction])
        dialog, content = self.dialog_surface('核对批量导出 · 语言与文件名', '1120x650')
        dialog.minsize(min(760,self.winfo_screenwidth()-40),min(560,self.winfo_screenheight()-90))
        content.columnconfigure(0,weight=1)
        content.rowconfigure(3,weight=1)
        self.label(content, f'共 {len(pending)} 项输出 · 同名自动编号 · 可逐行核对语言、文件名和去向',
                   wraplength=680,justify='left').grid(row=0,column=0,sticky='ew',pady=(0,8))
        preset_row=tk.Frame(content,bg=PANEL)
        preset_row.grid(row=2,column=0,sticky='ew',pady=(0,8))
        preset_row.columnconfigure(1,weight=1)
        self.label(preset_row,'导出预设',muted=True).grid(row=0,column=0,sticky='w',padx=(0,8))
        preset_var=tk.StringVar(value='选择已保存预设…')
        preset_field=Field(preset_row,self.palette,kind='combo',variable=preset_var,values=['选择已保存预设…',*self.recipes])
        preset_field.grid(row=0,column=1,sticky='ew')
        Tooltip(preset_field.input,'选择本机已保存的套版、命名、画质和输出路径组合；应用后会刷新下方全部文件名。')
        output_var=tk.StringVar(value=self.output_var.get())
        self.label(preset_row,'输出路径',muted=True).grid(row=1,column=0,sticky='w',padx=(0,8),pady=(7,0))
        output_field=Field(preset_row,self.palette,variable=output_var)
        output_field.grid(row=1,column=1,sticky='ew',pady=(7,0))
        def choose_confirm_output():
            path=filedialog.askdirectory(parent=dialog,title='选择导出目录',initialdir=output_var.get())
            if path: output_var.set(path); [update_suggestion(iid) for iid in items]
        self.button(preset_row,'选择目录',choose_confirm_output,lock=False).grid(row=1,column=2,padx=(8,0),pady=(7,0))
        language_row=tk.Frame(content,bg=PANEL)
        language_row.grid(row=1,column=0,sticky='ew',pady=(0,10))
        language_row.columnconfigure(1,weight=1)
        self.label(language_row,'所选行语言').grid(row=0,column=0,sticky='w',padx=(0,8))
        language_var=tk.StringVar(value='自动识别')
        language_field=Field(language_row,self.palette,kind='combo',variable=language_var,values=LANGUAGE_OPTIONS)
        language_field.grid(row=0,column=1,sticky='ew')
        other_language=tk.StringVar()
        other_field=Field(language_row,self.palette,variable=other_language,width=7)
        self.label(language_row,'自动识别：优先套版名（EN、JP、CN、繁中等），其次视频名；未识别时留空。',
                   muted=True,wraplength=680,font=('Microsoft YaHei UI',9)).grid(row=1,column=0,columnspan=4,sticky='w',pady=(5,0))
        listing_card=Card(content,self.palette,padding=10)
        listing_card.grid(row=3,column=0,sticky='nsew')
        listing_surface=listing_card.content
        listing_surface.columnconfigure(0,weight=1)
        listing_surface.rowconfigure(0,weight=1)
        table = ttk.Treeview(listing_surface, columns=('source', 'template', 'size', 'language', 'folder', 'name'), show='headings',selectmode='extended')
        for column, label, width in [('source', '输入视频', 180), ('template', '套版', 150), ('size', '输出尺寸', 100), ('language', '语言', 70), ('folder', '输出目录', 270), ('name', '建议文件名', 280)]:
            table.heading(column, text=label)
            table.column(column, width=width, minwidth=width, stretch=column in ('folder','name'))
        table.grid(row=0,column=0,sticky='nsew')
        vertical=Scrollbar(listing_surface,self.palette,orient='vertical',command=table.yview)
        vertical.grid(row=0,column=1,sticky='ns')
        horizontal=Scrollbar(listing_surface,self.palette,orient='horizontal',command=table.xview)
        horizontal.grid(row=1,column=0,sticky='ew')
        table.configure(yscrollcommand=vertical.set,xscrollcommand=horizontal.set)
        Tooltip(table,lambda:' · '.join(map(str,table.item(table.identify_row(table.winfo_pointery()-table.winfo_rooty()),'values')))
            if table.identify_row(table.winfo_pointery()-table.winfo_rooty()) else '选择一行或多行设置语言；下方可编辑文件名。表格支持横向滚动。')
        suggestions, items, jobs = {}, {}, dict(pending)
        def update_suggestion(iid):
            item=items[iid]
            destination=Path(self.engine.planned_output(item))
            suggestions[iid]=destination.name
            cfg=item['config']
            language=output_language(item['name'],cfg['template']) if cfg['language']=='auto' else cfg.get('otherLanguage','') if cfg['language']=='other' else '' if cfg['language']=='none' else cfg['language']
            values=(item['name'],Path(cfg['template']).name,f'{item["outputSize"][0]}×{item["outputSize"][1]}',language or '不添加',str(destination.parent),destination.name)
            if table.exists(iid):
                table.item(iid,values=values)
            else:
                table.insert('', 'end',iid=iid,values=values)
        for sequence,(iid, job) in enumerate(pending,1):
            template, rect, size = self.batch_geometry(job, self.direction, self.size_var.get(), DETECTION[self.detection_var.get()])
            cfg = dict(job.get('config') or config, template=str(template), rect=rect,
                       nameMode=config['nameMode'],namePattern=config['namePattern'],nameDate='')
            item = {'name': job['path'].name, 'config': cfg, 'outputSize': size, 'match': job.get('match', {}),'sequence':sequence}
            if job.get('outputName'):
                item['outputName'] = job['outputName']
            items[iid]=item
            update_suggestion(iid)
        row = tk.Frame(content, bg=PANEL)
        row.grid(row=4,column=0,sticky='ew',pady=10)
        row.columnconfigure(0,weight=1)
        self.label(row, '所选行文件名（可编辑）').grid(row=0,column=0,sticky='w',pady=(0,5))
        edit_var = tk.StringVar()
        entry = Field(row,self.palette,variable=edit_var)
        entry.grid(row=1,column=0,sticky='ew',padx=(0,8))
        def select(_event=None):
            selected = table.selection()
            if selected:
                edit_var.set(suggestions[selected[0]])
                cfg=items[selected[0]]['config']
                language_var.set({'auto':'自动识别','none':'不添加','other':'其他'}.get(cfg['language'],cfg['language']))
                other_language.set(cfg.get('otherLanguage',''))
                show_other()
        def show_other():
            if language_var.get()=='其他':
                other_field.grid(row=0,column=2,padx=6)
            else:
                other_field.grid_remove()
        table.bind('<<TreeviewSelect>>', select)
        def apply_language(all_rows=False):
            choice=language_var.get()
            if choice=='其他' and not other_language.get().strip():
                messagebox.showinfo('填写输出语言','请在语言输入框中填写语言，例如泰语。',parent=dialog)
                return
            for iid in (list(items) if all_rows else table.selection()):
                cfg=items[iid]['config']
                cfg['language']={'自动识别':'auto','不添加':'none','其他':'other','繁中':'other'}.get(choice,choice)
                cfg['otherLanguage']='繁中' if choice=='繁中' else other_language.get().strip()
                items[iid].pop('outputName',None)
                update_suggestion(iid)
            select()
        language_field.bind('<<ComboboxSelected>>',lambda _event:(show_other(),apply_language()))
        other_field.bind('<Return>',lambda _event:apply_language())
        self.button(language_row,'应用整批语言',lambda:apply_language(True),lock=False).grid(row=0,column=3,padx=(8,0))
        def apply_name():
            selected = table.selection()
            if not selected:
                return True
            try:
                name = self.engine.safe_filename(edit_var.get().strip())
                if not name.lower().endswith('.mp4'):
                    raise ValueError('文件名需要以 .mp4 结尾。')
                suggestions[selected[0]] = name
                items[selected[0]]['outputName'] = name
                table.set(selected[0], 'name', name)
                return True
            except ValueError as exc:
                messagebox.showerror('文件名无效', str(exc), parent=dialog)
                return False
        self.button(row, '应用名称', apply_name, lock=False).grid(row=1,column=1,sticky='e')
        confirmed = [False]
        def accept():
            if not apply_name():
                return
            for iid, name in suggestions.items():
                if iid in self.jobs:
                    self.jobs[iid]['outputName'] = name
                job = jobs[iid]
                job['outputName'] = name
                job['config'] = items[iid]['config']
                if iid in self.jobs:
                    self.jobs[iid]['config'] = dict(job['config'])
                job['config']['outputDir']=output_var.get()
            self.save_settings()
            confirmed[0] = True
            dialog.destroy()
        buttons = tk.Frame(content, bg=PANEL)
        buttons.grid(row=5,column=0,sticky='ew')
        buttons.columnconfigure(0,weight=1)
        preset_name=tk.StringVar(value='')
        preset_entry=Field(buttons,self.palette,variable=preset_name,width=18)
        preset_entry.grid(row=0,column=0,sticky='w',pady=(0,8))
        self.button(buttons,'保存为导出预设',lambda:self.save_confirm_preset(preset_name.get(),items,output_var.get()),lock=False).grid(row=0,column=1,padx=(8,0),sticky='w',pady=(0,8))
        actions=tk.Frame(buttons,bg=PANEL)
        actions.grid(row=1,column=0,sticky='e')
        self.button(actions, '返回修改', dialog.destroy, lock=False).pack(side='right',padx=(8,0))
        self.button(actions, '确认并导出', accept, primary=True, lock=False).pack(side='right')
        if pending:
            table.selection_set(pending[0][0])
            select()
        self.wait_window(dialog)
        return confirmed[0]

    def save_confirm_preset(self,name,items,output):
        name=name.strip()
        if not name:
            messagebox.showinfo('保存导出预设','请输入预设名称。',parent=self)
            return
        first=next(iter(items.values()),None)
        if not first:return
        cfg=first['config']
        self.recipes[name]={'outputTemplates':list(self.output_templates),'exportMode':'auto',
            'direction':self.direction,'profile':dict(self.profiles.get(self.direction,{})),
            'engineConfig':dict(cfg),'quality':self.quality_var.get(),'backend':self.backend_var.get(),
            'output':output,'language':self.language_var.get(),'nameMode':self.name_mode_var.get(),
            'namePattern':cfg.get('namePattern',naming.DEFAULT_PATTERN),'outputGroup':self.output_group_var.get()}
        self.recipe_var.set(name)
        self.recipe_combo.configure(values=list(self.recipes))
        self.save_settings()

    def start_export(self):
        if self.busy: return
        if self.template_image is None and not self.output_templates and not any(job.get('templates') for job in self.jobs.values()):
            messagebox.showinfo('选择套版','请先选择一个或多个套版，再开始导出。',parent=self)
            return
        if self.importing:
            messagebox.showinfo('正在读取视频', '请等视频信息读取完成后开始导出。', parent=self)
            return
        scope=list(self.export_scope or self.tree.selection() or self.jobs)
        if any(self.jobs.get(iid,{}).get('borderScanning') for iid in scope):
            self.status_var.set('原片边缘仍在后台扫描，可继续预览；完成后再导出')
            return
        scope=self.expand_output_jobs(scope)
        pending = [(iid,dict(self.jobs[iid])) for iid in scope if iid in self.jobs and self.jobs[iid].get('info')]
        if not pending:
            self.export_scope=None
            messagebox.showinfo('添加视频', '请先添加视频，或选择需要导出的队列项目。已完成项目可再次导出，同名自动编号。', parent=self)
            return
        try:
            rect, size, mode = self.get_rect(), self.output_size(), MODES[self.mode_var.get()]
            zoom = self.get_zoom()
            if not self.output_var.get().strip(): raise ValueError('请选择保存文件夹。')
            directory = Path(self.output_var.get()).expanduser().resolve()
            directory.mkdir(parents=True, exist_ok=True)
        except (ValueError, OSError) as exc:
            messagebox.showerror('请检查导出设置', str(exc), parent=self)
            return
        # A unique project result already carries a concrete template. Treat it
        # as confirmed even when an older queued row retained the legacy review
        # flag; only ambiguous/missing candidates should interrupt export.
        for _iid,_job in pending:
            match=_job.get('match') or {}
            if _job.get('requiresTemplateReview') and match.get('status')=='matched' and Path(_job.get('template','')).is_file():
                _job['requiresTemplateReview']=False
                if _iid in self.jobs:self.jobs[_iid]['requiresTemplateReview']=False
        unresolved = [(iid, job) for iid, job in pending if job.get('requiresTemplateReview')]
        if unresolved:
            names = '\n'.join(job['path'].name for _, job in unresolved[:8])
            if not messagebox.askyesno('项目模板需要确认',
                f'{len(unresolved)} 个视频没有唯一的本机项目套版匹配。\n{names}\n\n是否对这些视频使用当前套版？', parent=self):
                return
            for iid, job in unresolved:
                job['template'] = str(self.template)
                job['rect'] = rect
                job['requiresTemplateReview'] = False
                if iid in self.jobs:
                    self.jobs[iid].update(template=str(self.template), rect=rect, requiresTemplateReview=False)
                self.tree.set(iid, 'status', '已确认使用上次套版')
        if not self.skip_border_preflight:
            incomplete = [job['path'].name for _, job in pending
                          if job.get('borderScanning') or (not job.get('borderReport') and not job.get('borderError'))]
            if incomplete:
                messagebox.showinfo('正在检查原视频边缘', '请等待边缘扫描完成后再导出：\n' + '\n'.join(incomplete[:8]), parent=self)
                return
            errors = [job['path'].name for _, job in pending if job.get('borderError')]
            if errors and not messagebox.askyesno('原视频边缘扫描失败',
                '以下视频无法完成黑白边扫描，是否跳过检查并继续？\n' + '\n'.join(errors[:8]), parent=self):
                return
            self.run_border_preflight(pending, self.direction, self.size_var.get(),
                                      DETECTION[self.detection_var.get()], MODES[self.mode_var.get()], self.get_zoom())
            self.export_scope=[iid for iid,_ in pending]
            return
        self.skip_border_preflight = False
        self.export_scope=None
        if not hasattr(self, 'engine'):
            messagebox.showerror('处理服务不可用', '本机处理服务启动失败，请查看本机 desktop-startup.log。', parent=self)
            return
        if not self.confirm_batch(pending):
            return
        self.skip_batch_summary = False
        self.save_settings()
        self.cancel.clear()
        self.set_busy(True)
        self.progress_var.set(0)
        config = self._engine_profile(self.direction, self.profiles[self.direction])
        item_configs = []
        for sequence,(iid, job) in enumerate(pending,1):
            template, job_rect, job_size = self.batch_geometry(job, self.direction, self.size_var.get(), DETECTION[self.detection_var.get()])
            with Image.open(template) as image:
                direction = (job.get('config') or {}).get('direction') or ('custom' if self.direction == 'custom' else 'portrait' if image.height > image.width else 'landscape')
            effective = job.get('config') or config
            override = dict(effective, **{'template': str(template), 'rect': list(job_rect), 'direction': direction,
                        'backend': config['backend'],
                        'nameProject':job.get('match',{}).get('projectFolder',''),'nameGame':job.get('match',{}).get('game',''),
                        'templateConfirmed': True, 'borderPolicy': job.get('borderPolicy', effective.get('borderPolicy', 'ask')),
                        'outputKey': job.get('outputKey', ''), 'outputSize': list(job_size),
                        'fit': job.get('fit', effective['fit']), 'zoom': job.get('zoom', effective['zoom'])})
            if job.get('outputName'):
                override['outputName'] = job['outputName']
            item_configs.append(override)
        def worker():
            try:
                with core.cancellable(self.cancel):
                    session = self.engine.prepare_config([str(job['path']) for _, job in pending], config, itemConfigs=item_configs)
                    if self.cancel.is_set():
                        raise ValueError('已取消，未开始导出。')
                    task = self.engine.export_videos(session['sessionId'], 'desktop-export')
                if self.cancel.is_set():
                    self.engine.cancel_task(task['taskId'])
                self.events.put(('task_started', task))
            except Exception as exc:
                self.events.put(('task_error', str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def restore_last_task(self):
        if not hasattr(self, 'engine'):
            return
        try:
            last = engine_module.read_json(self.engine.state / 'desktop-last-task.json', {})
            if last.get('taskId'):
                self.attach_task(self.engine.get_task(last['taskId']), notify_terminal=False)
        except (OSError, ValueError):
            pass

    def attach_task(self, task, notify_terminal=True):
        # The local service and the export worker can both announce the same
        # task.  Reconcile its status in place instead of inserting a second
        # copy of every source/output row.
        if task.get('taskId') == self.active_task and self.task_rows:
            self.monitoring_task = task.get('status') in ('running','queued')
            self.update_task_display(task, notify=notify_terminal)
            return
        self.active_task = task['taskId']
        self.monitoring_task = task['status'] in ('running', 'queued')
        engine_module.atomic_json(self.engine.state / 'desktop-last-task.json', {'taskId': self.active_task})
        existing = {(os.path.normcase(str(job['path'].resolve())), job.get('outputKey', '')): iid for iid, job in self.jobs.items()}
        self.task_rows = {}
        for index, item in enumerate(task['items']):
            if index in self.hidden_task_items.get(task['taskId'],[]):
                continue
            identity = os.path.normcase(str(Path(item['path']).resolve()))
            iid = existing.get((identity, item.get('outputKey', '')))
            if iid is None:
                info = item.get('info')
                iid = self.tree.insert('', 'end', values=(item['name'],
                    f"{info['width']}×{info['height']}" if info else '—',
                    f"{info['duration']:.1f}s" if info else '—', Path(item.get('config',task['config']).get('template','')).stem, item['status']))
                self.jobs[iid] = {'path': Path(item['path']), 'info': info, 'output': item.get('output'), 'error': item.get('error', '')}
            cfg = item.get('config', task['config'])
            template = item.get('sourceTemplate') or cfg['template']
            if not Path(template).is_file():
                template = cfg['template']
            self.jobs[iid].update(outputKey=item.get('outputKey', ''), template=template, rect=cfg['rect'], config=dict(cfg, template=template), outputName=item.get('outputName'),
                                  _taskId=task['taskId'],_taskIndex=index)
            self.update_queue_template(iid,[self.jobs[iid]['config']])
            self.task_rows[index] = iid
        self.queue_var.set(f'视频队列 · {len(self.jobs)} 个')
        self.set_busy(task['status'] in ('running', 'queued'))
        if self.tree.selection():
            self.select_queue_item()
        self.update_task_display(task, notify=notify_terminal)

    def update_task_display(self, task, notify=True):
        labels = {'queued': '等待导出', 'running': '正在导出', 'completed': '已完成',
                  'cancelled': '已取消', 'failed': '失败 · 双击查看', 'interrupted': '已中断', 'partial': '部分完成'}
        for index, item in enumerate(task['items']):
            iid = self.task_rows.get(index)
            if iid not in self.jobs:
                continue
            self.jobs[iid].update(output=item.get('output'), error=item.get('error', ''))
            label = labels.get(item['status'], item['status'])
            if item['status'] == 'running':
                label += f" {item['progress']:.0%}"
            self.tree.set(iid, 'status', label)
            if item.get('backendNote'):
                self.backend_status.set(f"FrameFit {task['version']} · {item['backendNote']}")
        self.progress_var.set(task.get('progress', 0) * 100)
        complete = sum(item['status'] == 'completed' for item in task['items'])
        self.status_var.set(f"任务 {task['taskId'][:8]} · {labels.get(task['status'], task['status'])} · {complete}/{len(task['items'])}")
        if task['status'] not in ('running', 'queued'):
            self.monitoring_task = False
            self.set_busy(False)
            if notify and task.get('taskId') not in self.notified_tasks:
                self.notified_tasks.add(task.get('taskId'))
                failed=len([item for item in task.get('items',[]) if item.get('status') not in ('completed',)])
                self.notify_completion({'completed':'导出完成','partial':'部分完成','cancelled':'已取消','failed':'导出失败','interrupted':'已中断'}.get(task.get('status'),'任务结束'),complete,failed)
            if self.staged_update:
                self.activate_staged_update()

    def monitor_task(self):
        if self.active_task and self.monitoring_task and hasattr(self, 'engine'):
            try:
                self.update_task_display(self.engine.get_task(self.active_task))
            except (OSError, ValueError) as exc:
                self.backend_status.set(f'任务查询失败：{exc}')
        self.after(500, self.monitor_task)

    def resume_export(self):
        if self.busy or not self.active_task or not hasattr(self, 'engine'):
            return
        try:
            if self.hidden_task_items.get(self.active_task):
                # Preserve the frozen runtime/configuration and completed outputs;
                # the durable engine forks the task without the removed rows.
                self.attach_task(self.engine.resume_task(self.active_task,
                    excludeIndices=list(self.hidden_task_items[self.active_task])))
                return
            self.attach_task(self.engine.resume_task(self.active_task))
        except Exception as exc:
            messagebox.showerror('无法继续任务', str(exc), parent=self)

    def cancel_export(self):
        self.cancel.set()
        self.export_scope=None
        if self.active_task and hasattr(self, 'engine'):
            self.engine.cancel_task(self.active_task)
        self.cancel_button.configure(state='disabled')
        self.status_var.set('正在取消，已完成的视频会保留…')

    def poll(self):
        try:
            for _ in range(150):
                event = self.events.get_nowait()
                kind = event[0]
                if kind == 'main_call':
                    event[1]()
                elif kind == 'remote_config':
                    self.apply_remote_config(event[1])
                elif kind == 'task_started':
                    self.attach_task(event[1])
                elif kind == 'task_error':
                    self.set_busy(False)
                    messagebox.showerror('导出未开始', event[1], parent=self)
                elif kind == 'gpu_available':
                    if not self.busy:
                        self.backend_status.set('NVIDIA GPU 编码可用 · NVENC' if event[1] else 'GPU 编码不可用 · 自动模式使用 CPU')
                elif kind == 'update_check':
                    if event[1]:
                        self.pending_update = event[1]
                        self.update_button.configure(state='normal', text=f"更新到 {event[1]['version']}")
                        self.update_status_var.set(f"发现 {event[1]['version']}，点击更新安装")
                    else:
                        self.update_button.configure(state='normal', text='检查失败 · 点击重试' if event[2] else '检查更新')
                        if event[2]:
                            self.update_status_var.set('检查失败，点击重试')
                            self.update_button.tooltip.text=f'检查更新失败：{event[2]}。当前本机版本保持可用。'
                        elif not self.pending_update:
                            self.update_status_var.set('暂无稳定更新')
                elif kind == 'update_staged':
                    if event[1] is None:
                        self.update_button.configure(state='normal', text='重试更新')
                        self.update_status_var.set('安装失败，当前版本保持可用')
                        self.update_button.tooltip.text=f'更新未安装：{event[2]}'
                    else:
                        self.staged_update = event[1]
                        if self.busy:
                            self.update_button.configure(state='disabled', text='导出完成后重启更新')
                            self.status_var.set('更新已验证；当前批次继续使用原版本，导出完成后自动重启')
                        else:
                            self.activate_staged_update()
                            return
                elif kind == 'border_preflight':
                    self.continue_after_border_preflight(event[1], event[2])
                elif kind == 'backend' and event[1] in self.jobs:
                    self.jobs[event[1]]['backend'] = event[2]
                    self.backend_status.set(event[3])
                elif kind == 'probed' and event[1] in self.jobs:
                    _, iid, info = event
                    job = self.jobs[iid]
                    job['info'] = info
                    self.tree.item(iid, values=(job['path'].name, f'{info["width"]}×{info["height"]}',
                                               f'{info["duration"]:.1f}s', '匹配项目中', '读取中'))
                    if not self.tree.selection():
                        self.tree.selection_set(iid)
                    if not job.get('borderReport') and not job.get('borderScanning'):
                        job['borderScanning'] = True
                        self.scan_borders(iid, job['path'])
                    if self.tree.selection() and self.tree.selection()[0] == iid:
                        self.schedule_preview()
                elif kind == 'import':
                    _, iid, info, error, match = event
                    self.importing -= 1
                    if iid in self.jobs:
                        job = self.jobs[iid]
                        job.update(info=info, error=error, match=match)
                        self.tree.item(iid, values=(job['path'].name, f'{info["width"]}×{info["height"]}' if info else '—',
                                                  f'{info["duration"]:.1f}s' if info else '—', '匹配中', '待导出' if info else '读取失败'))
                        if not self.tree.selection(): self.tree.selection_set(iid)
                        remote = self.remote_items.pop(str(job['path'].resolve()), None)
                        if remote and remote.get('config'):
                            job.update(template=remote['config']['template'], rect=remote['config']['rect'],
                                       config=dict(remote['config']), outputName=remote.get('outputName'),
                                       match=remote.get('match', {}), requiresTemplateReview=remote.get('requiresTemplateReview', False))
                        elif match:
                            self.resolve_project_template(iid, job)
                        if self.tree.selection() and self.tree.selection()[0] == iid:
                            self.select_queue_item()
                        if info and not job.get('borderReport') and not job.get('borderScanning'):
                            job['borderScanning'] = True
                            self.scan_borders(iid, job['path'])
                    if self.importing == 0:
                        self.status_var.set('视频已就绪，检查预览后开始批量导出')
                        self.schedule_preview()
                elif kind == 'border_progress' and event[1] in self.jobs:
                    self.jobs[event[1]]['borderProgress'] = event[2]
                    if self.tree.selection() and self.tree.selection()[0] == event[1]:
                        self.border_var.set(f'正在后台扫描原视频黑白边 · {event[2]:.0%}，可继续预览和设置')
                elif kind == 'scrub' and self.dragging_seek:
                    selected = self.tree.selection()
                    if selected and selected[0]==event[1] and abs(self.seek_var.get()-event[2])<.5:
                        self.display_image = self.compose_scrub(event[3])
                        self.draw_preview()
                elif kind == 'preflight_progress':
                    self.progress_var.set(event[1]*100)
                    self.status_var.set(event[2])
                elif kind == 'frame_borders':
                    _,iid,seconds,source,report=event
                    if iid not in self.jobs or self.tree.selection() != (iid,):
                        continue
                    self.border_points=[]
                    self.border_source_iid=iid
                    for origin,findings in (('原片',source),('套版新增',report['newBorders'])):
                        for finding in findings:
                            locations=[label for key,label in (('top','上'),('bottom','下'),('left','左'),('right','右')) if finding.get(key)]
                            label='疑似暗缝' if finding.get('kind')=='dark_seam' else ('黑边' if finding['color']=='black' else '白边')
                            self.border_points.append({'start':seconds})
                    details=[]
                    for finding in source:
                        details.append(('黑边' if finding.get('color')=='black' else '白边')+' '+('/'.join([label for key,label in (('top','上'),('bottom','下'),('left','左'),('right','右')) if finding.get(key)])))
                    self.border_var.set(f'{seconds:.2f}s：'+('、'.join(details) if details else '当前帧未发现原片边缘')+'；套版新增边缘请结合预览确认')
                    self.jobs[iid]['compositeReport']=report
                    segments=list((self.jobs[iid].get('borderReport') or {}).get('segments',[]))
                    segments.extend(dict(finding,start=seconds,end=seconds,locations=['当前帧'],origin=origin)
                                    for origin,findings in (('原片',source),('套版新增',report['newBorders'])) for finding in findings)
                    self.set_timeline_markers(segments)
                elif kind == 'frame_border_error':
                    self.border_var.set('该帧检查失败：'+event[1])
                elif kind == 'borders' and event[1] in self.jobs:
                    job = self.jobs[event[1]]
                    job['borderScanning'] = False
                    if event[2] is None:
                        job['borderError'] = event[3]
                    else:
                        job['borderReport'] = event[2]
                    self.show_borders(event[1], event[2], event[3])
                elif kind == 'preview' and event[1] == self.preview_id:
                    self.display_image = event[2]
                    self.preview_var.set(event[3])
                    self.draw_preview()
                elif kind == 'preview_error' and event[1] == self.preview_id:
                    self.preview_var.set('预览失败，请检查文件、坐标或换一个预览时间')
                    self.display_image = None
                    self.canvas.delete('all')
                elif kind == 'job_status' and event[1] in self.jobs:
                    self.tree.set(event[1], 'status', event[2])
                elif kind == 'progress':
                    self.progress_var.set(event[1])
                    if not self.cancel.is_set(): self.status_var.set(event[2])
                elif kind == 'result' and event[1] in self.jobs:
                    _, iid, output, error = event
                    self.jobs[iid].update(output=output, error=error)
                    engine = 'GPU' if self.jobs[iid].get('backend') == 'nvenc' else 'CPU'
                    self.tree.set(iid, 'status', f'{engine} 已完成' if output else '失败 · 双击查看')
                elif kind == 'finished':
                    self.set_busy(False)
                    self.status_var.set(f'{"已取消" if event[3] else "导出结束"} · 完成 {event[1]} 个，失败 {event[2]} 个')
                    if not event[3]: self.progress_var.set(100)
                    if self.staged_update:
                        self.activate_staged_update()
                        return
        except queue.Empty:
            pass
        if hasattr(self,'metric_vars'):
            sources={str(job['path']) for job in self.jobs.values()}
            self.metric_vars['sources'].set(str(len(sources)))
            self.metric_vars['outputs'].set(str(sum(1 if job.get('outputKey') else max(1,len(job.get('templates') or self.output_templates)) for job in self.jobs.values())))
            self.metric_vars['complete'].set(str(sum(bool(job.get('output')) for job in self.jobs.values())))
            self.metric_vars['borders'].set(str(sum(len((job.get('borderReport') or {}).get('segments',[])) for job in self.jobs.values())))
        self.after(80, self.poll)

    def close(self):
        if self.busy and self.monitoring_task and self.active_task and hasattr(self, 'engine'):
            try:
                latest = self.engine.get_task(self.active_task)
                if latest['status'] not in ('running', 'queued'):
                    self.update_task_display(latest)
            except (OSError, ValueError):
                pass
        if self.busy:
            if not messagebox.askyesno('正在处理', '关闭窗口会取消当前检查或导出，已完成文件会保留。确定关闭？', parent=self):
                return
            self.closing = True
            self.cancel_export()
            self.timeline_stop.set()
            for job in self.jobs.values():
                process = job.get('scrubProcess')
                if process and process.poll() is None:
                    process.kill()
            # Workers are daemon threads; all preflight media calls observe cancel.
            # The durable export receives its own cancellation marker before exit.
            self.set_busy(False)
            self.after(200, self.close)
        else:
            self.timeline_stop.set()
            for job in self.jobs.values():
                process = job.get('scrubProcess')
                if process and process.poll() is None:
                    process.kill()
            self.save_settings()
            if self.local_service:
                self.local_service.close()
            # Dismiss child popups before Tk takes its recursive destruction
            # snapshot. A popup closing from its owner's Destroy handler can
            # otherwise be destroyed a second time by that same snapshot.
            def dismiss(widget):
                if not widget.winfo_exists():return
                tooltip=getattr(widget,'tooltip',None)
                if tooltip is not None:tooltip.hide()
                if isinstance(widget,Field):widget.hide_choices()
                if isinstance(widget,Slider):widget.hide_marker_hint()
                for child in list(widget.winfo_children()):
                    if child.winfo_exists():dismiss(child)
            dismiss(self)
            for callback in self.tk.call('after', 'info'):
                self.after_cancel(callback)
            self.destroy()

    def finish_close(self):
        if self.busy:
            self.after(150, self.finish_close)
        else:
            self.close()


if __name__ == '__main__':
    App().mainloop()
