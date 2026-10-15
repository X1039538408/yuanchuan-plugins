"""FrameFit — a portable Windows video framing utility."""
import json
import math
import os
from pathlib import Path
import queue
import sys
import threading
import time
import traceback
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from PIL import Image, ImageTk, ImageDraw
from tkinterdnd2 import DND_FILES, TkinterDnD

RUNTIME_ROOT = Path(sys.executable).resolve().parents[1] if getattr(sys, 'frozen', False) else Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RUNTIME_ROOT / 'server'))
import core
import engine as engine_module
core.ROOT = RUNTIME_ROOT
from framefit_workflow import find_project_candidates, parse_video_name
from updater import check_update, stage_update, activate_update

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


class App(TkinterDnD.Tk):
    def __init__(self):
        super().__init__()
        self.title('视频套版 FrameFit · 本地批量套版')
        self.geometry('1180x870')
        self.minsize(1040, 820)
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
        self.scan_slots = threading.Semaphore(2)
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
        self.quality_var = tk.StringVar(value='高画质')
        self.backend_var = tk.StringVar(value=list(BACKENDS)[0])
        self.detection_var = tk.StringVar(value=list(DETECTION)[0])
        self.backend_status = tk.StringVar(value='检测 GPU 编码能力中…')
        self.output_var = tk.StringVar(value=str(Path.home() / 'Videos' / '套版导出'))
        self.schedule_root_var = tk.StringVar(value=r'\\192.168.122.30\团队文件-圆串网络\任务排期\2026')
        self.template_root_var = tk.StringVar(value=r'D:\Wu liao')
        self.project_keywords_var = tk.StringVar(value='超凡,无尽迷途')
        self.machine_settings = {}
        self.language_var = tk.StringVar(value='自动识别')
        self.other_language_var = tk.StringVar()
        self.name_mode_var = tk.StringVar(value='精简命名')
        self.output_group_var = tk.StringVar(value='统一文件夹')
        self.recipe_name_var = tk.StringVar()
        self.recipe_var = tk.StringVar()
        self.recipes = {}
        self.status_var = tk.StringVar(value='添加竖屏视频，即可开始套版')
        self.template_var = tk.StringVar(value='内置 · 仙界横版套框')
        self.preview_var = tk.StringVar(value='套框预览  /  绿色区域用于视频初始对齐')
        self.queue_var = tk.StringVar(value='视频队列 · 0 个')
        self.progress_var = tk.DoubleVar()
        self.time_var = tk.StringVar(value='1.0')
        self.seek_var = tk.DoubleVar(value=1.0)
        self._style()
        self._layout()
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
                         self.quality_var, self.backend_var, self.output_var, self.language_var,
                         self.other_language_var, self.name_mode_var, self.output_group_var,
                         self.schedule_root_var, self.template_root_var, self.project_keywords_var]:
            variable.trace_add('write', self.schedule_save)
        self.after(500, self.monitor_task)
        self.update_zoom_controls()
        if self.local_service:
            self.after(0, self.local_service.mark_ready)

    def _style(self):
        style = ttk.Style(self)
        style.theme_use('clam')
        style.configure('Treeview', background=PANEL, fieldbackground=PANEL, foreground=TEXT,
                        rowheight=34, borderwidth=0, font=('Microsoft YaHei UI', 10))
        style.configure('Treeview.Heading', background='#26343d', foreground=MUTED,
                        relief='flat', font=('Microsoft YaHei UI', 9))
        style.map('Treeview', background=[('selected', '#315349')], foreground=[('selected', '#ffffff')])
        style.configure('TCombobox', fieldbackground='#26343d', background='#26343d', foreground=TEXT,
                        arrowcolor=ACCENT, padding=7)
        style.map('TCombobox', fieldbackground=[('readonly', '#26343d')], foreground=[('readonly', TEXT)])
        style.configure('Horizontal.TProgressbar', background=ACCENT, troughcolor='#26343d', borderwidth=0)

    def label(self, parent, text='', muted=False, **kwargs):
        return tk.Label(parent, text=text, bg=parent['bg'], fg=MUTED if muted else TEXT,
                        anchor='w', **kwargs)

    def button(self, parent, text, command, primary=False, lock=True):
        btn = tk.Button(parent, text=text, command=command, bg=ACCENT if primary else '#2a3942',
                        fg=BG if primary else TEXT, activebackground='#a5efda', activeforeground=BG,
                        relief='flat', bd=0, padx=14, pady=8, cursor='hand2', disabledforeground='#657780')
        if lock:
            self.controls.append((btn, 'normal'))
        return btn

    def entry(self, parent, variable, width=None):
        field = tk.Entry(parent, textvariable=variable, width=width, bg='#26343d', fg=TEXT,
                         insertbackground=ACCENT, relief='flat', highlightthickness=1,
                         highlightbackground='#344650', highlightcolor=ACCENT)
        self.controls.append((field, 'normal'))
        return field

    def combo(self, parent, variable, values):
        widget = ttk.Combobox(parent, textvariable=variable, values=list(values), state='readonly')
        # Widget bindings run before ttk's class binding, which otherwise changes
        # the selected value before the settings canvas receives the wheel event.
        widget.bind('<MouseWheel>', self.scroll_settings_wheel)
        widget.bind('<<ComboboxSelected>>', lambda _: self.schedule_preview())
        self.controls.append((widget, 'readonly'))
        return widget

    def scroll_settings_wheel(self, event):
        self.settings_canvas.yview_scroll(-int(event.delta / 120), 'units')
        return 'break'

    def _layout(self):
        header = tk.Frame(self, bg=BG)
        header.grid(row=0, column=0, sticky='ew', padx=26, pady=(20, 18))
        self.label(header, '视频套版', font=('Microsoft YaHei UI', 23, 'bold')).pack(side='left', padx=(0, 22))
        self.direction_buttons = {}
        for key, (title, _, _) in DIRECTIONS.items():
            btn = self.button(header, title, lambda selected=key: self.switch_direction(selected))
            btn.pack(side='left', padx=(0, 6))
            self.direction_buttons[key] = btn
        self.label(header, f'v{self.app_version()}  ·  本地处理', muted=True).pack(side='right')
        self.update_button = self.button(header, '检查更新', self.update_action, lock=False)
        self.update_button.pack(side='right', padx=(0, 14))
        body = tk.Frame(self, bg=BG)
        body.grid(row=1, column=0, sticky='nsew', padx=26)
        sidebar = tk.Frame(body, bg=PANEL, width=300)
        sidebar.pack(side='right', fill='y', padx=(18, 0))
        sidebar.pack_propagate(False)
        left = tk.Frame(body, bg=BG)
        left.pack(side='left', fill='both', expand=True)

        self.label(left, '画面预览', font=('Microsoft YaHei UI', 12, 'bold')).pack(anchor='w', pady=(0, 8))
        self.canvas = tk.Canvas(left, bg='#080e12', bd=0, highlightthickness=1, highlightbackground='#304049', height=240)
        self.canvas.pack(fill='both', expand=True)
        self.canvas.bind('<Configure>', lambda _: self.draw_preview())
        self.label(left, textvariable=self.preview_var, muted=True, font=('Microsoft YaHei UI', 9)).pack(fill='x', pady=(7, 10))
        timebar = tk.Frame(left, bg=BG)
        timebar.pack(fill='x', pady=(0, 12))
        self.label(timebar, '拖动时间轴预览', muted=True).pack(side='left')
        self.seek_slider = ttk.Scale(timebar, from_=0, to=1, orient='horizontal', variable=self.seek_var,
                                     command=self.on_seek)
        self.seek_slider.pack(side='left', fill='x', expand=True, padx=10)
        self.seek_slider.bind('<ButtonPress-1>', lambda _event: setattr(self, 'dragging_seek', True))
        self.seek_slider.bind('<ButtonRelease-1>', self.finish_seek)
        self.label(timebar, textvariable=self.time_var, muted=True, font=('Consolas', 9)).pack(side='right')
        self.storyboard = tk.Canvas(left, height=72, bg='#1b252c', bd=0, highlightthickness=0, cursor='hand2')
        self.storyboard.pack(fill='x', pady=(0, 6))
        self.storyboard.bind('<Button-1>', self.seek_storyboard)
        borderbox = tk.Frame(left, bg=BG)
        borderbox.pack(fill='x', pady=(0, 8))
        self.label(borderbox, textvariable=self.border_var, muted=True, wraplength=500,
                   justify='left', font=('Microsoft YaHei UI', 9)).pack(side='left', fill='x', expand=True)
        self.border_list = tk.Listbox(borderbox, height=2, width=24, bg=PANEL, fg=TEXT, relief='flat',
                                      selectbackground='#315349', font=('Microsoft YaHei UI', 9))
        self.border_list.pack(side='right', fill='y', padx=(8, 0))
        self.border_list.bind('<Double-Button-1>', self.seek_border_segment)
        toolbar = tk.Frame(left, bg=BG)
        toolbar.pack(fill='x', pady=(0, 8))
        self.label(toolbar, textvariable=self.queue_var, font=('Microsoft YaHei UI', 11, 'bold')).pack(side='left')
        self.button(toolbar, '移除', self.remove_selected).pack(side='right', padx=(7, 0))
        self.button(toolbar, '＋ 添加视频', self.choose_videos).pack(side='right')
        table = tk.Frame(left, bg=PANEL)
        table.pack(fill='x')
        self.tree = ttk.Treeview(table, columns=('name', 'size', 'duration', 'status'), show='headings', height=4)
        for col, title, width in [('name', '视频文件', 330), ('size', '尺寸', 100), ('duration', '时长', 60), ('status', '状态', 95)]:
            self.tree.heading(col, text=title)
            self.tree.column(col, width=width, minwidth=50, stretch=col == 'name')
        self.tree.pack(side='left', fill='both', expand=True)
        scroll = ttk.Scrollbar(table, orient='vertical', command=self.tree.yview)
        scroll.pack(side='right', fill='y')
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.bind('<<TreeviewSelect>>', self.select_queue_item)
        self.tree.bind('<Delete>', lambda _: self.remove_selected())
        self.tree.bind('<Double-1>', self.open_job)
        self.label(left, '把视频拖到窗口即可添加 · 支持多选 · 双击队列可打开视频或查看错误', muted=True,
                   font=('Microsoft YaHei UI', 9)).pack(anchor='w', pady=(8, 0))

        settings_canvas = tk.Canvas(sidebar, bg=PANEL, bd=0, highlightthickness=0)
        self.settings_canvas = settings_canvas
        settings_scroll = ttk.Scrollbar(sidebar, orient='vertical', command=settings_canvas.yview)
        settings_scroll.pack(side='right', fill='y')
        settings_canvas.pack(side='left', fill='both', expand=True)
        settings_canvas.configure(yscrollcommand=settings_scroll.set)
        inner = tk.Frame(settings_canvas, bg=PANEL)
        settings_window = settings_canvas.create_window((14, 18), window=inner, anchor='nw')
        inner.bind('<Configure>', lambda _: settings_canvas.configure(scrollregion=settings_canvas.bbox('all')))
        settings_canvas.bind('<Configure>', lambda e: settings_canvas.itemconfigure(settings_window, width=max(220, e.width-28)))
        self.bind('<MouseWheel>', lambda e: settings_canvas.yview_scroll(-int(e.delta/120), 'units')
                  if e.x_root >= sidebar.winfo_rootx() and e.y_root >= sidebar.winfo_rooty() else None, add='+')
        self.label(inner, '01   选择套框', font=('Microsoft YaHei UI', 12, 'bold')).pack(fill='x')
        self.label(inner, textvariable=self.template_var, muted=True, wraplength=255).pack(fill='x', pady=(8, 10))
        row = tk.Frame(inner, bg=PANEL)
        row.pack(fill='x')
        self.button(row, '更换套框', self.choose_template).pack(side='left')
        self.detect_button = self.button(row, '识别窗口', self.auto_detect)
        self.detect_button.pack(side='right')
        self.label(inner, '窗口识别类型', muted=True, font=('Microsoft YaHei UI', 9)).pack(fill='x', pady=(12, 5))
        self.detection_combo = self.combo(inner, self.detection_var, DETECTION)
        self.detection_combo.pack(fill='x')
        self.detection_combo.bind('<<ComboboxSelected>>', lambda _: self.auto_detect())
        self.label(inner, '窗口坐标（套框原图像素）', muted=True, font=('Microsoft YaHei UI', 9)).pack(fill='x', pady=(17, 7))
        coords = tk.Frame(inner, bg=PANEL)
        coords.pack(fill='x')
        for i, title in enumerate(('X', 'Y', '宽', '高')):
            box = tk.Frame(coords, bg=PANEL)
            box.pack(side='left', fill='x', expand=True, padx=(0, 5))
            self.label(box, title, muted=True).pack(fill='x')
            self.entry(box, self.rect_vars[i], 5).pack(fill='x', ipady=5)
        self.label(inner, '02   导出设置', font=('Microsoft YaHei UI', 12, 'bold')).pack(fill='x', pady=(26, 12))
        for label, var, values in [('编码加速', self.backend_var, BACKENDS), ('导出尺寸', self.size_var, SIZES), ('画面适配', self.mode_var, MODES), ('画质', self.quality_var, ['高画质', '标准画质（文件更小）'])]:
            self.label(inner, label, muted=True, font=('Microsoft YaHei UI', 9)).pack(fill='x', pady=(0, 5))
            widget = self.combo(inner, var, values)
            widget.pack(fill='x', pady=(0, 12))
            if var is self.mode_var:
                self.mode_combo = widget
                self.zoom_box = tk.Frame(inner, bg=PANEL)
                row = tk.Frame(self.zoom_box, bg=PANEL)
                row.pack(fill='x')
                self.label(row, '缩放比例', muted=True).pack(side='left')
                self.zoom_spin = tk.Spinbox(row, from_=10, to=500, increment=1,
                    textvariable=self.zoom_var, width=7, bg='#26343d', fg=TEXT,
                    insertbackground=ACCENT, buttonbackground='#344650', relief='flat')
                self.zoom_spin.pack(side='left', padx=8, ipady=5)
                self.controls.append((self.zoom_spin, 'normal'))
                self.label(row, '%', muted=True).pack(side='left')
                self.label(self.zoom_box, '100% 为等比填满；支持 10～500%，可输入小数。居中缩放，放大覆盖套版，缩小露出底图。',
                    muted=True, wraplength=248, justify='left', font=('Microsoft YaHei UI', 9)).pack(fill='x', pady=(5, 0))
            if var is self.size_var:
                self.size_combo = widget
                self.label(inner, textvariable=self.size_info_var, muted=True, wraplength=248,
                           justify='left', font=('Microsoft YaHei UI', 9)).pack(fill='x', pady=(0, 10))
        self.label(inner, '视频在上层，套版在底层。窗口用于初始对齐；视频可覆盖窗口外图案，仅裁掉成片画布外的部分。', muted=True,
                   wraplength=252, justify='left', font=('Microsoft YaHei UI', 9)).pack(fill='x', pady=(0, 17))
        self.label(inner, '保存位置', muted=True).pack(fill='x')
        self.entry(inner, self.output_var).pack(fill='x', ipady=6, pady=7)
        row = tk.Frame(inner, bg=PANEL)
        row.pack(fill='x')
        self.button(row, '选择文件夹', self.choose_output).pack(side='left')
        self.button(row, '打开', self.open_output, lock=False).pack(side='right')
        self.label(inner, 'MP4 / H.264 · 原视频帧率 · 保留原声\n同名文件自动编号', muted=True,
                   justify='left', font=('Microsoft YaHei UI', 9)).pack(fill='x', pady=(15, 0))
        machine = tk.LabelFrame(inner, text='本机项目匹配（仅保存在这台电脑）', bg=PANEL, fg=TEXT,
                                highlightthickness=0, bd=1, font=('Microsoft YaHei UI', 9))
        machine.pack(fill='x', pady=(20, 10))
        self.label(machine, '任务排期目录', muted=True).pack(fill='x', padx=7, pady=(8, 3))
        self.entry(machine, self.schedule_root_var).pack(fill='x', padx=7, ipady=5)
        self.label(machine, '本机套版根目录', muted=True).pack(fill='x', padx=7, pady=(8, 3))
        self.entry(machine, self.template_root_var).pack(fill='x', padx=7, ipady=5)
        self.label(machine, '游戏关键词（逗号分隔）', muted=True).pack(fill='x', padx=7, pady=(8, 3))
        self.entry(machine, self.project_keywords_var).pack(fill='x', padx=7, ipady=5, pady=(0, 8))
        self.label(machine, '输出语言', muted=True).pack(fill='x', padx=7, pady=(4, 3))
        self.language_combo = self.combo(machine, self.language_var, ['自动识别', '简中', '英', '日', '韩', '其他', '不添加'])
        self.language_combo.pack(fill='x', padx=7, pady=(0, 7))
        self.language_combo.bind('<<ComboboxSelected>>', self.update_language_controls)
        self.other_language_entry = self.entry(machine, self.other_language_var)
        if self.language_var.get() != '其他':
            self.other_language_entry.pack_forget()
        self.label(machine, '命名方式', muted=True).pack(fill='x', padx=7, pady=(0, 3))
        self.combo(machine, self.name_mode_var, ['精简命名', '完整信息']).pack(fill='x', padx=7, pady=(0, 8))
        self.label(machine, '输出分类', muted=True).pack(fill='x', padx=7, pady=(0, 3))
        self.combo(machine, self.output_group_var, ['统一文件夹', '按项目分文件夹', '按模板分文件夹']).pack(fill='x', padx=7, pady=(0, 8))
        self.label(machine, '本机批量配方', muted=True).pack(fill='x', padx=7, pady=(0, 3))
        self.recipe_combo = self.combo(machine, self.recipe_var, [])
        self.recipe_combo.pack(fill='x', padx=7, pady=(0, 5))
        self.entry(machine, self.recipe_name_var).pack(fill='x', padx=7, ipady=5, pady=(0, 5))
        recipe_buttons = tk.Frame(machine, bg=PANEL)
        recipe_buttons.pack(fill='x', padx=7, pady=(0, 8))
        self.button(recipe_buttons, '保存配方', self.save_recipe, lock=False).pack(side='left')
        self.button(recipe_buttons, '使用', self.apply_recipe, lock=False).pack(side='left', padx=5)
        self.button(recipe_buttons, '删除', self.delete_recipe, lock=False).pack(side='right')

        footer = tk.Frame(self, bg=BG)
        footer.grid(row=2, column=0, sticky='ew', padx=26, pady=18)
        self.start_button = self.button(footer, '开始批量导出 →', self.start_export, primary=True)
        self.start_button.pack(side='right', padx=(12, 0))
        self.resume_button = self.button(footer, '继续未完成', self.resume_export)
        self.resume_button.pack(side='right')
        self.cancel_button = self.button(footer, '取消导出', self.cancel_export, lock=False)
        self.cancel_button.pack(side='right', padx=(12, 0))
        self.cancel_button.configure(state='disabled')
        progressbox = tk.Frame(footer, bg=BG)
        progressbox.pack(side='left', fill='x', expand=True)
        self.label(progressbox, textvariable=self.backend_status, muted=True,
                   font=('Microsoft YaHei UI', 9)).pack(fill='x', pady=(0, 4))
        self.label(progressbox, textvariable=self.status_var).pack(fill='x', pady=(0, 8))
        ttk.Progressbar(progressbox, variable=self.progress_var, maximum=100).pack(fill='x')

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
        profiles = config.get('profiles', {})
        self.machine_settings = dict(config.get('machine', {}))
        self.schedule_root_var.set(self.machine_settings.get('scheduleRoot', self.schedule_root_var.get()))
        self.template_root_var.set(self.machine_settings.get('templateRoot', self.template_root_var.get()))
        self.project_keywords_var.set(','.join(self.machine_settings.get('gameKeywords') or ['超凡', '无尽迷途']))
        self.language_var.set(config.get('language', '自动识别'))
        self.other_language_var.set(config.get('otherLanguage', ''))
        self.name_mode_var.set(config.get('nameMode', '精简命名'))
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
        self.size_combo.configure(values=list(sizes))
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
            fallback = self.template if self.direction == 'custom' and self.template and self.template.is_file() \
                else core.ROOT / 'assets' / (default_asset or 'default.png')
            self.load_template(fallback)
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
            keywords = [value.strip() for value in self.project_keywords_var.get().replace('，', ',').split(',') if value.strip()]
            self.machine_settings.update(scheduleRoot=self.schedule_root_var.get().strip(),
                                         templateRoot=self.template_root_var.get().strip(),
                                         gameKeywords=keywords, outputKeyword='输出')
            data = dict(self.machine_settings, **self.profiles.get('landscape', {}), direction=self.direction,
                        profiles=self.profiles, machine=self.machine_settings,
                        output=self.output_var.get(), backend=self.backend_var.get(), quality=self.quality_var.get(),
                        language=self.language_var.get(), otherLanguage=self.other_language_var.get(),
                        nameMode=self.name_mode_var.get(), outputGroup=self.output_group_var.get(), recipes=self.recipes)
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
                job['rect'] = self.get_rect()
                if job.get('config') and self.template:
                    profile = {'template': str(self.template), 'rect': job['rect'], 'size': self.size_var.get(),
                               'mode': self.mode_var.get(), 'zoom': self.get_zoom(), 'detection': self.detection_var.get()}
                    job['config'] = dict(job['config'], **self._engine_profile(self.direction, profile))
            except (ValueError, TypeError):
                pass

    def _engine_profile(self, direction, profile):
        size_name = profile.get('size', '')
        resolution = 'original' if direction == 'custom' or '原始' in size_name else (
            '4k' if '4K' in size_name else '720p' if '720P' in size_name else '1080p')
        fit = {'等比填满（推荐）': 'cover', '完整显示': 'contain', '拉伸铺满': 'stretch', '自由缩放': 'zoom'}.get(profile.get('mode'), 'cover')
        config = {'direction': direction, 'templateId': 'portrait' if direction == 'portrait' else 'landscape',
                  'detection': {'自动（透明 / 黑 / 白）': 'auto', '仅黑色区域': 'black', '仅白色区域': 'white', '仅透明区域': 'transparent'}.get(profile.get('detection'), 'auto'),
                  'rect': list(profile['rect']) if profile.get('rect') is not None else None, 'fit': fit, 'zoom': profile.get('zoom', 100),
                  'resolution': resolution, 'quality': 'high' if self.quality_var.get() == '高画质' else 'standard',
                  'backend': {'自动（优先 GPU）': 'auto', 'NVIDIA GPU · NVENC': 'nvenc', 'CPU · 兼容模式': 'cpu'}.get(self.backend_var.get(), 'auto'),
                  'outputDir': self.output_var.get(),
                  'language': {'自动识别': 'auto', '不添加': 'none', '其他': 'other'}.get(self.language_var.get(), self.language_var.get()),
                  'otherLanguage': self.other_language_var.get(),
                  'nameMode': 'full' if self.name_mode_var.get() == '完整信息' else 'compact',
                  'outputGroup': {'统一文件夹': 'folder', '按项目分文件夹': 'project', '按模板分文件夹': 'template'}[self.output_group_var.get()]}
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
                if self.busy and action in ('prepare_config', 'update_config', 'export_videos'):
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
        video_paths = [video.get('path') for video in session.get('videos', []) if video.get('path')]
        self.remote_items.update({video['path']: video for video in session.get('videos', []) if video.get('path')})
        if video_paths:
            self.add_videos(video_paths)
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
        self.language_var.set({'auto': '自动识别', 'none': '不添加', 'other': '其他'}.get(config.get('language'), config.get('language', '自动识别')))
        self.other_language_var.set(config.get('otherLanguage', ''))
        self.name_mode_var.set('完整信息' if config.get('nameMode') == 'full' else '精简命名')
        self.output_group_var.set({'folder': '统一文件夹', 'project': '按项目分文件夹', 'template': '按模板分文件夹'}.get(config.get('outputGroup'), '统一文件夹'))
        self.update_language_controls()
        for video in session.get('videos', []):
            for iid, job in self.jobs.items():
                if str(job['path'].resolve()) == video['path'] and video.get('config'):
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
            messagebox.showinfo('保存批量配方', '请先填写配方名称。', parent=self)
            return
        try:
            self.remember_profile()
        except ValueError as exc:
            messagebox.showerror('无法保存配方', str(exc), parent=self)
            return
        self.recipes[name] = {
            'direction': self.direction,
            'profile': dict(self.profiles.get(self.direction, {})),
            'engineConfig': self._engine_profile(self.direction, self.profiles.get(self.direction, {})),
            'quality': self.quality_var.get(), 'backend': self.backend_var.get(),
            'output': self.output_var.get(), 'language': self.language_var.get(),
            'nameMode': self.name_mode_var.get(), 'outputGroup': self.output_group_var.get(),
            'otherLanguage': self.other_language_var.get(),
        }
        self.recipe_var.set(name)
        self.recipe_combo.configure(values=list(self.recipes))
        self.save_settings()
        self.status_var.set(f'本机配方“{name}”已保存')

    def apply_recipe(self):
        name = self.recipe_var.get()
        recipe = self.recipes.get(name)
        if not recipe:
            messagebox.showinfo('使用批量配方', '请先选择一个本机配方。', parent=self)
            return
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
        self.output_group_var.set(recipe.get('outputGroup', self.output_group_var.get()))
        self.save_settings()
        self.status_var.set(f'已使用本机配方“{name}”')

    def delete_recipe(self):
        name = self.recipe_var.get()
        if name not in self.recipes:
            return
        del self.recipes[name]
        self.recipe_combo.configure(values=list(self.recipes))
        self.recipe_var.set('')
        self.save_settings()
        self.status_var.set(f'本机配方“{name}”已删除')

    def update_language_controls(self, _event=None):
        if self.language_var.get() == '其他':
            self.other_language_entry.pack(fill='x', padx=7, ipady=5, pady=(0, 8))
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
        self.status_var.set('下载更新包并校验文件，请保持网络连接…')
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
            activate_update(self.staged_update, self.install_root, self.install_root / 'FrameFit.exe')
            if self.local_service:
                self.local_service.close()
            self.destroy()
        except Exception as exc:
            self.update_button.configure(state='normal', text='重试切换')
            self.backend_status.set(f'更新已下载但切换失败：{exc}')
            self.staged_update = None

    def load_template(self, path, quiet=False):
        try:
            with Image.open(path) as raw:
                image = raw.convert('RGBA')
            if min(image.size) < 2:
                raise ValueError('套框尺寸过小。')
            if (self.direction == 'portrait' and image.width >= image.height) or \
                    (self.direction == 'landscape' and image.height >= image.width):
                raise ValueError(f'当前为{DIRECTIONS[self.direction][0]}模式，请选择{"竖版" if self.direction == "portrait" else "横版"}套框，或先点击顶部切换模式。')
            self.template, self.template_image = Path(path), image
            selected = self.tree.selection() if hasattr(self, 'tree') else ()
            if selected and selected[0] in self.jobs:
                self.jobs[selected[0]]['template'] = str(self.template)
            names = {'default.png': '仙界横版套框', 'default_portrait.png': '仙界竖版套框'}
            self.template_var.set(f'{names.get(Path(path).name, Path(path).name)}\n{image.width} × {image.height}')
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
        if selected and selected[0] in self.jobs:
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
        size = DIRECTIONS[self.direction][1][self.size_var.get()]
        return size or tuple(v // 2 * 2 for v in self.template_image.size)

    def update_size_info(self):
        if self.template_image is None:
            return
        if self.size_var.get() not in DIRECTIONS[self.direction][1]:
            return
        width, height = self.output_size()
        divisor = math.gcd(width, height)
        text = f'导出：{width} × {height} · 比例 {width//divisor}:{height//divisor}'
        original_size = DIRECTIONS[self.direction][1][self.size_var.get()] is None
        if original_size and (width, height) != self.template_image.size:
            text += '\n原图含奇数边长，编码时向下调整1像素'
        self.size_info_var.set(text)

    def choose_template(self):
        orientation = {'portrait': '竖版', 'landscape': '横版', 'custom': '任意比例'}[self.direction]
        path = filedialog.askopenfilename(parent=self, title=f'选择{orientation}套框图片', filetypes=[('套框图片', '*.png *.jpg *.jpeg *.bmp *.webp')])
        if path and self.load_template(path):
            selected = self.tree.selection()
            if selected:
                self.jobs[selected[0]]['requiresTemplateReview'] = False
            self.save_settings()

    def select_queue_item(self, _event=None):
        selected = self.tree.selection()
        if selected:
            job = self.jobs.get(selected[0], {})
            if job.get('borderReport'):
                self.show_borders(selected[0], job['borderReport'])
            elif job.get('borderError'):
                self.show_borders(selected[0], None, job['borderError'])
            else:
                self.border_var.set('正在扫描原视频黑边和白边…')
                self.border_list.delete(0, 'end')
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
                self.mode_var.set({'cover': '等比填满（推荐）', 'contain': '完整显示', 'stretch': '拉伸铺满', 'zoom': '自由缩放'}[cfg['fit']])
                self.zoom_var.set(str(cfg['zoom']))
                self.detection_var.set({value: label for label,value in DETECTION.items()}.get(cfg.get('detection'), next(iter(DETECTION))))
                choices = list(DIRECTIONS[self.direction][1])
                index = {'1080p': 0, '720p': 1, '4k': 2, 'original': 3}[cfg['resolution']]
                self.size_var.set(choices[min(index, len(choices)-1)])
                self.quality_var.set('高画质' if cfg['quality'] == 'high' else '标准画质（文件更小）')
                self.backend_var.set({'auto': '自动（优先 GPU）', 'nvenc': 'NVIDIA GPU · NVENC', 'cpu': 'CPU · 兼容模式'}[cfg['backend']])
                self.output_var.set(cfg['outputDir'])
                self.language_var.set({'auto':'自动识别', 'none':'不添加', 'other':'其他'}.get(cfg.get('language'), cfg.get('language', '自动识别')))
                self.other_language_var.set(cfg.get('otherLanguage', ''))
                self.name_mode_var.set('完整信息' if cfg.get('nameMode') == 'full' else '精简命名')
                self.output_group_var.set({'folder':'统一文件夹','project':'按项目分文件夹','template':'按模板分文件夹'}.get(cfg.get('outputGroup'),'统一文件夹'))
            self.loading_selection = False
            self.schedule_preview()

    def choose_project_template(self, iid):
        job = self.jobs.get(iid)
        candidates = (job or {}).get('match', {}).get('candidates', [])
        if not candidates:
            return
        dialog = tk.Toplevel(self)
        dialog.title('选择本机项目套版')
        dialog.transient(self)
        dialog.grab_set()
        dialog.geometry('720x400')
        dialog.configure(bg=BG)
        self.label(dialog, f"{job['path'].name} · 找到多个候选套版，请按项目和尺寸选择", muted=True).pack(fill='x', padx=16, pady=12)
        body = tk.Frame(dialog, bg=BG)
        body.pack(fill='both', expand=True, padx=16)
        listing = ttk.Treeview(body, columns=('size', 'path'), show='headings', height=10)
        listing.heading('size', text='尺寸'); listing.heading('path', text='本机套版')
        listing.column('size', width=90, stretch=False); listing.column('path', width=460)
        listing.pack(side='left', fill='both', expand=True)
        preview = tk.Label(body, bg=BG, fg=TEXT, text='选择套版查看预览', width=18)
        preview.pack(side='right', fill='y', padx=(12, 0))
        photo = {'value': None}
        for index, candidate in enumerate(candidates):
            iid_row = str(index)
            listing.insert('', 'end', iid=iid_row, values=(f"{candidate['width']}×{candidate['height']}", candidate['path']))
        def show_preview(_event=None):
            selected = listing.selection()
            if not selected: return
            image = Image.open(candidates[int(selected[0])]['path']).convert('RGBA')
            image.thumbnail((180, 280), Image.Resampling.LANCZOS)
            photo['value'] = ImageTk.PhotoImage(image)
            preview.configure(image=photo['value'], text='')
        listing.bind('<<TreeviewSelect>>', show_preview)
        if candidates:
            listing.selection_set('0'); show_preview()
        buttons = tk.Frame(dialog, bg=BG)
        buttons.pack(fill='x', padx=16, pady=14)
        def apply():
            selected = listing.selection()
            if not selected: return
            candidate = candidates[int(selected[0])]
            job['template'] = candidate['path']
            job['requiresTemplateReview'] = False
            mapping = self.machine_settings.setdefault('projects', {})
            mapping[job.get('match', {}).get('projectFolder', '')] = {
                'path': candidate['path'], 'modifiedAt': Path(candidate['path']).stat().st_mtime,
                'selectedAt': time.time(), 'knownPaths': [item['path'] for item in candidates]}
            self.save_settings()
            self.tree.set(iid, 'status', '项目套版已选')
            dialog.destroy()
            self.select_queue_item()
        self.button(buttons, '暂不处理', dialog.destroy).pack(side='right', padx=(7, 0))
        self.button(buttons, '使用此套版', apply, primary=True).pack(side='right')

    def resolve_project_template(self, iid, job):
        result = job.get('match') or {}
        if result.get('status') == 'matched' and len(result.get('candidates', [])) == 1:
            candidate = result['candidates'][0]
            job['template'] = candidate['path']
            try:
                with Image.open(candidate['path']) as image:
                    job['rect'] = list(core.detect_window(image.convert('RGBA'), DETECTION[self.detection_var.get()]))
            except (OSError, ValueError):
                job['rect'] = None
            job['requiresTemplateReview'] = False
            self.tree.set(iid, 'status', '已匹配项目套版')
            return
        job['requiresTemplateReview'] = True
        if result.get('status') in ('ambiguous', 'size_missing', 'orientation_missing', 'template_updated'):
            self.tree.set(iid, 'status', '需选择项目套版')
            self.after(0, lambda iid=iid: self.choose_project_template(iid))
        elif result.get('status'):
            self.tree.set(iid, 'status', '未匹配 · 确认上次模板')
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
                iid = self.tree.insert('', 'end', values=(path.name, '读取中', '', '读取中'))
                self.jobs[iid] = {'path': path, 'info': None, 'error': '', 'output': None}
                existing.add(key)
                pending.append((iid, path))
        self.queue_var.set(f'视频队列 · {len(self.jobs)} 个')
        if not pending: return
        self.importing += len(pending)
        self.status_var.set('正在读取视频信息…')
        schedule_root = self.schedule_root_var.get().strip()
        template_root = self.template_root_var.get().strip()
        keywords = [value.strip() for value in self.project_keywords_var.get().replace('，', ',').split(',') if value.strip()]
        mapping = dict(self.machine_settings, gameKeywords=keywords, outputKeyword='输出')
        query_direction = self.direction
        query_size = self.output_size() if DIRECTIONS[self.direction][1][self.size_var.get()] is not None else None

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

    def remove_selected(self):
        if self.busy:
            return
        for iid in self.tree.selection():
            self.jobs.pop(iid, None)
            self.tree.delete(iid)
        self.queue_var.set(f'视频队列 · {len(self.jobs)} 个')
        self.schedule_preview()

    def choose_output(self):
        path = filedialog.askdirectory(parent=self, title='选择导出文件夹')
        if path: self.output_var.set(path)

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
        if self.preview_pending:
            self.after_cancel(self.preview_pending)
        self.preview_pending = self.after(350, self.refresh_preview)

    def on_seek(self, value):
        seconds = max(0.0, float(value))
        self.time_var.set(f'{seconds:.1f}s')
        selected = self.tree.selection()
        job = self.jobs.get(selected[0], {}) if selected else {}
        frames = job.get('timelineFrames', [])
        if frames:
            _second, frame = min(frames, key=lambda pair: abs(pair[0] - seconds))
            self.display_image = frame
            self.draw_preview()
        if not self.dragging_seek:
            self.schedule_preview()

    def finish_seek(self, _event=None):
        self.dragging_seek = False
        self.schedule_preview()

    def seek_storyboard(self, event):
        if not self.storyboard_times:
            return
        selected = self.tree.selection()
        job = self.jobs.get(selected[0], {}) if selected else {}
        strip = job.get('storyboard', (None,))[0]
        width = max(1, min(self.storyboard.winfo_width(), strip.width if strip else self.storyboard.winfo_width()))
        fraction = min(1.0, max(0.0, event.x / width))
        index = round(fraction * (len(self.storyboard_times) - 1))
        seconds = self.storyboard_times[index]
        self.seek_var.set(seconds)
        self.on_seek(seconds)

    def load_storyboard(self, iid, job):
        key = (str(self.template), tuple(self.get_rect()), tuple(self.output_size()), MODES[self.mode_var.get()], self.get_zoom())
        if job.get('storyboardPending') == key:
            return
        self.storyboard_ticket += 1
        ticket = self.storyboard_ticket
        self.storyboard_times = []
        self.storyboard.delete('all')
        if not job.get('info'):
            return
        if job.get('storyboard') and job.get('storyboardKey') == key:
            self.show_storyboard(iid, job['storyboard'][0], job['storyboard'][1])
            return
        duration = job['info']['duration']
        count = min(9, max(3, int(duration // 10) + 1))
        points = sorted(set(round(max(0, min(duration - .15, duration * i / (count - 1))), 2)
                            for i in range(count)))
        job['storyboardPending'] = key
        template, rect, size, mode, zoom = key

        def worker():
            try:
                frames = [core.preview(job['path'], template, rect, size, mode, seconds, zoom) for seconds in points]
                for frame in frames:
                    frame.thumbnail((640, 640), Image.Resampling.LANCZOS)
                timeline_frames = list(zip(points, [frame.copy() for frame in frames]))
                cell_w, cell_h = 112, 70
                strip = Image.new('RGB', (cell_w * len(frames), cell_h), '#1b252c')
                draw = ImageDraw.Draw(strip)
                for index, (frame, seconds) in enumerate(zip(frames, points)):
                    frame.thumbnail((cell_w - 4, cell_h - 20), Image.Resampling.LANCZOS)
                    x = index * cell_w + (cell_w - frame.width) // 2
                    strip.paste(frame, (x, 1))
                    draw.text((index * cell_w + 5, cell_h - 17), f'{seconds:.1f}s', fill='#b5efd0')
                self.events.put(('storyboard', ticket, iid, strip, points, key, timeline_frames))
            except Exception as exc:
                self.events.put(('storyboard_error', ticket, str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def show_storyboard(self, iid, strip, points):
        if iid not in self.jobs:
            return
        self.storyboard_times = points
        self.storyboard_photo = ImageTk.PhotoImage(strip)
        self.storyboard.delete('all')
        self.storyboard.create_image(0, 0, anchor='nw', image=self.storyboard_photo)
        self.storyboard.configure(scrollregion=(0, 0, strip.width, strip.height))
        self.jobs[iid]['storyboard'] = (strip, points)

    def scan_borders(self, iid, path):
        def worker():
            try:
                notify = lambda value: self.events.put(('border_progress', iid, value))
                with self.scan_slots:
                    report = self.engine.source_borders(path, on_progress=notify) if hasattr(self, 'engine') else core.analyze_borders(path, on_progress=notify)
                self.events.put(('borders', iid, report, ''))
            except Exception as exc:
                self.events.put(('borders', iid, None, str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def show_borders(self, iid, report, error=''):
        if not self.tree.selection() or self.tree.selection()[0] != iid:
            return
        self.border_list.delete(0, 'end')
        if error:
            self.border_var.set(f'黑白边检查失败：{error}')
            return
        segments = (report or {}).get('segments', [])
        self.border_points = segments
        if not segments:
            self.border_var.set('原视频未检测到黑边或白边')
            return
        self.border_var.set(f'原视频发现 {len(segments)} 段黑白边；双击时间段跳转（每秒抽样一次）')
        for segment in segments:
            label = '黑边' if segment['color'] == 'black' else '白边'
            self.border_list.insert('end', f"{label}  {segment['start']:.1f}–{segment['end']:.1f}s")

    def seek_border_segment(self, _event=None):
        selection = self.border_list.curselection()
        if selection and selection[0] < len(self.border_points):
            seconds = self.border_points[selection[0]]['start']
            self.seek_var.set(seconds)
            self.on_seek(seconds)

    def refresh_preview(self):
        self.preview_pending = None
        if self.template_image is None: return
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
            self.load_storyboard(selected[0], job)
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
        if self.display_image is None: return
        w, h = self.canvas.winfo_width(), self.canvas.winfo_height()
        if min(w, h) < 5: return
        im = self.display_image.copy()
        im.thumbnail((w-4, h-4), Image.Resampling.LANCZOS)
        self.photo = ImageTk.PhotoImage(im)
        self.canvas.delete('all')
        self.canvas.create_image(w/2, h/2, image=self.photo)

    def set_busy(self, value):
        self.busy = value
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
        if config:
            resolution = config.get('resolution', 'original')
            if config.get('direction') == 'custom' or resolution == 'original':
                size = tuple(v // 2 * 2 for v in template_size)
            else:
                size = {'1080p': (1920,1080), '720p': (1280,720), '4k': (3840,2160)}[resolution]
                if template_size[1] > template_size[0]:
                    size = size[::-1]
        elif direction == 'custom':
            size = tuple(v // 2 * 2 for v in template_size)
        else:
            choices = DIRECTIONS[direction][1]
            index = list(choices).index(resolution_label)
            original = list(choices.values())[index] is None
            orientation = 'portrait' if template_size[1] > template_size[0] else 'landscape'
            if original:
                size = tuple(v // 2 * 2 for v in template_size)
            else:
                edge = [1080, 720, 2160][index]
                size = (edge, edge * 16 // 9) if orientation == 'portrait' else (edge * 16 // 9, edge)
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
                for iid, job in pending:
                    if self.cancel.is_set():
                        raise ValueError('已取消边缘检查，未开始导出。')
                    template, rect, size = self.batch_geometry(job, direction, resolution_label, detection)
                    report = job.get('borderReport') or {'segments': []}
                    effective = job.get('config') or {}
                    composite = core.analyze_composite_borders(job['path'], template, rect, size, effective.get('fit', mode), effective.get('zoom', zoom),
                                                               report.get('segments', []))
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
            self.status_var.set('已取消边缘检查，未开始导出')
            return
        if error:
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
                return
            policy = 'keep' if choice else 'remove'
        for iid, job, _template, _rect, _size, _composite in results:
            if iid in self.jobs:
                existing = job.get('borderPolicy', (job.get('config') or {}).get('borderPolicy', 'ask'))
                self.jobs[iid]['borderPolicy'] = policy if existing == 'ask' else existing
        self.skip_border_preflight = True
        self.after(0, self.start_export)

    def confirm_batch(self, pending):
        self.save_settings()
        config = self._engine_profile(self.direction, self.profiles[self.direction])
        dialog = tk.Toplevel(self)
        dialog.title('核对批量导出 · 可编辑文件名')
        dialog.geometry('1120x490')
        dialog.transient(self)
        dialog.grab_set()
        dialog.configure(bg=BG)
        self.label(dialog, f'共 {len(pending)} 个视频 · 同名自动编号 · 每行显示实际模板、尺寸与输出去向').pack(fill='x', padx=16, pady=12)
        table = ttk.Treeview(dialog, columns=('source', 'template', 'size', 'folder', 'name'), show='headings')
        for column, label, width in [('source', '输入视频', 170), ('template', '套版', 140), ('size', '输出尺寸', 100), ('folder', '输出目录', 330), ('name', '建议文件名', 300)]:
            table.heading(column, text=label)
            table.column(column, width=width, stretch=True)
        table.pack(fill='both', expand=True, padx=16)
        suggestions = {}
        for iid, job in pending:
            template, rect, size = self.batch_geometry(job, self.direction, self.size_var.get(), DETECTION[self.detection_var.get()])
            cfg = dict(job.get('config') or config, template=str(template), rect=rect)
            item = {'name': job['path'].name, 'config': cfg, 'outputSize': size, 'match': job.get('match', {})}
            if job.get('outputName'):
                item['outputName'] = job['outputName']
            destination = Path(self.engine.planned_output(item))
            suggestions[iid] = destination.name
            table.insert('', 'end', iid=iid, values=(job['path'].name, template.name, f'{size[0]}×{size[1]}', str(destination.parent), destination.name))
        row = tk.Frame(dialog, bg=BG)
        row.pack(fill='x', padx=16, pady=12)
        self.label(row, '选中一行可修改名称：').pack(side='left')
        edit_var = tk.StringVar()
        entry = self.entry(row, edit_var)
        entry.pack(side='left', fill='x', expand=True, padx=8, ipady=7)
        def select(_event=None):
            selected = table.selection()
            if selected:
                edit_var.set(suggestions[selected[0]])
        table.bind('<<TreeviewSelect>>', select)
        def apply_name():
            selected = table.selection()
            if not selected:
                return True
            try:
                name = self.engine.safe_filename(edit_var.get().strip())
                if not name.lower().endswith('.mp4'):
                    raise ValueError('文件名需要以 .mp4 结尾。')
                suggestions[selected[0]] = name
                table.set(selected[0], 'name', name)
                return True
            except ValueError as exc:
                messagebox.showerror('文件名无效', str(exc), parent=dialog)
                return False
        self.button(row, '应用名称', apply_name, lock=False).pack(side='right')
        confirmed = [False]
        def accept():
            if not apply_name():
                return
            for iid, name in suggestions.items():
                self.jobs[iid]['outputName'] = name
                job = next(job for key, job in pending if key == iid)
                job['outputName'] = name
                if remember.get():
                    template = Path(job.get('template') or self.template)
                    project = (job.get('match') or {}).get('projectFolder') or parse_video_name(job['path'].name)['projectFolder']
                    candidates = (job.get('match') or {}).get('candidates', [])
                    self.machine_settings.setdefault('projects', {})[project] = {
                        'path': str(template), 'modifiedAt': template.stat().st_mtime, 'selectedAt': time.time(),
                        'knownPaths': [item['path'] for item in candidates] or [str(template)]}
            self.save_settings()
            confirmed[0] = True
            dialog.destroy()
        buttons = tk.Frame(dialog, bg=BG)
        buttons.pack(fill='x', padx=16, pady=(0, 14))
        remember = tk.BooleanVar(value=False)
        tk.Checkbutton(buttons, text='记住这些项目的模板选择（仅本机）', variable=remember,
                       bg=BG, fg=TEXT, selectcolor=PANEL, activebackground=BG).pack(side='left')
        self.button(buttons, '返回修改', dialog.destroy, lock=False).pack(side='right', padx=8)
        self.button(buttons, '确认并导出', accept, primary=True, lock=False).pack(side='right')
        self.wait_window(dialog)
        return confirmed[0]

    def start_export(self):
        if self.busy: return
        if self.importing:
            messagebox.showinfo('正在读取视频', '请等视频信息读取完成后开始导出。', parent=self)
            return
        pending = [(iid, dict(job)) for iid, job in self.jobs.items() if job['info'] and not job['output']]
        if not pending:
            messagebox.showinfo('添加视频', '请先添加视频。已完成的视频不会重复导出，需要重做时可移除再添加。', parent=self)
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
            return
        self.skip_border_preflight = False
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
        for iid, job in pending:
            template, job_rect, job_size = self.batch_geometry(job, self.direction, self.size_var.get(), DETECTION[self.detection_var.get()])
            with Image.open(template) as image:
                direction = (job.get('config') or {}).get('direction') or ('custom' if self.direction == 'custom' else 'portrait' if image.height > image.width else 'landscape')
            effective = job.get('config') or config
            override = dict(effective, **{'template': str(template), 'rect': list(job_rect), 'direction': direction,
                        'templateConfirmed': True, 'borderPolicy': job.get('borderPolicy', effective.get('borderPolicy', 'ask')),
                        'fit': job.get('fit', effective['fit']), 'zoom': job.get('zoom', effective['zoom'])})
            if job.get('outputName'):
                override['outputName'] = job['outputName']
            item_configs.append(override)
        def worker():
            try:
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
                self.attach_task(self.engine.get_task(last['taskId']))
        except (OSError, ValueError):
            pass

    def attach_task(self, task):
        self.active_task = task['taskId']
        self.monitoring_task = task['status'] in ('running', 'queued')
        engine_module.atomic_json(self.engine.state / 'desktop-last-task.json', {'taskId': self.active_task})
        existing = {os.path.normcase(str(job['path'].resolve())): iid for iid, job in self.jobs.items()}
        self.task_rows = {}
        for index, item in enumerate(task['items']):
            identity = os.path.normcase(str(Path(item['path']).resolve()))
            iid = existing.get(identity)
            if iid is None:
                info = item.get('info')
                iid = self.tree.insert('', 'end', values=(item['name'],
                    f"{info['width']}×{info['height']}" if info else '—',
                    f"{info['duration']:.1f}s" if info else '—', item['status']))
                self.jobs[iid] = {'path': Path(item['path']), 'info': info, 'output': item.get('output'), 'error': item.get('error', '')}
            cfg = item.get('config', task['config'])
            template = item.get('sourceTemplate') or cfg['template']
            if not Path(template).is_file():
                template = cfg['template']
            self.jobs[iid].update(template=template, rect=cfg['rect'], config=dict(cfg, template=template), outputName=item.get('outputName'))
            self.task_rows[index] = iid
        self.queue_var.set(f'视频队列 · {len(self.jobs)} 个')
        self.set_busy(task['status'] in ('running', 'queued'))
        if self.tree.selection():
            self.select_queue_item()
        self.update_task_display(task)

    def update_task_display(self, task):
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
            self.attach_task(self.engine.resume_task(self.active_task))
        except Exception as exc:
            messagebox.showerror('无法继续任务', str(exc), parent=self)

    def cancel_export(self):
        self.cancel.set()
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
                        self.status_var.set(f"发现 FrameFit {event[1]['version']}；点击右上角更新后才会安装")
                    else:
                        self.update_button.configure(state='normal', text='检查更新')
                        if event[2]:
                            self.backend_status.set(f'检查更新失败，可稍后重试：{event[2]}')
                        elif not self.pending_update:
                            self.backend_status.set(f'FrameFit {self.app_version()} · 当前没有可安装的稳定更新')
                elif kind == 'update_staged':
                    if event[1] is None:
                        self.update_button.configure(state='normal', text='重试更新')
                        self.backend_status.set(f'更新未安装，当前版本保持可用：{event[2]}')
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
                elif kind == 'backend':
                    self.jobs[event[1]]['backend'] = event[2]
                    self.backend_status.set(event[3])
                elif kind == 'probed' and event[1] in self.jobs:
                    _, iid, info = event
                    job = self.jobs[iid]
                    job['info'] = info
                    self.tree.item(iid, values=(job['path'].name, f'{info["width"]}×{info["height"]}',
                                               f'{info["duration"]:.1f}s', '匹配项目中'))
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
                                                  f'{info["duration"]:.1f}s' if info else '—', '待导出' if info else '读取失败'))
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
                elif kind == 'storyboard':
                    if event[2] in self.jobs:
                        self.jobs[event[2]].update(storyboard=(event[3], event[4]), storyboardKey=event[5], timelineFrames=event[6], storyboardPending=None)
                    if event[1] == self.storyboard_ticket:
                        self.show_storyboard(event[2], event[3], event[4])
                elif kind == 'storyboard_error' and event[1] == self.storyboard_ticket:
                    self.preview_var.set(f'缩略图读取失败：{event[2]}')
                elif kind == 'border_progress' and event[1] in self.jobs:
                    self.jobs[event[1]]['borderProgress'] = event[2]
                    if self.tree.selection() and self.tree.selection()[0] == event[1]:
                        self.border_var.set(f'正在后台扫描原视频黑白边 · {event[2]:.0%}，可继续预览和设置')
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
                elif kind == 'result':
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
            if not messagebox.askyesno('正在导出', '关闭窗口会取消当前导出，已完成文件会保留。确定关闭？', parent=self):
                return
            self.cancel_export()
            self.after(150, self.finish_close)
        else:
            self.save_settings()
            if self.local_service:
                self.local_service.close()
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
