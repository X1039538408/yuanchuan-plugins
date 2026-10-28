"""Shared thumbnail workspace for project, manual and managed template selection."""
from pathlib import Path
import queue
import threading
import tkinter as tk

from PIL import Image,ImageTk

from ui import Card,Field,Scrollbar,Tooltip,CheckButton
from template_catalog import identity,scan_folder,sort_catalog


class TemplateBrowser:
    def __init__(self,app,items,selected,folder,on_apply,reference='',previous=(),selected_at=0,on_edit=None,on_delete=None,manager=False,on_import=None):
        self.app=app
        self.palette=app.palette
        self.items=list(items)
        self.selected={identity(path) for path in selected}
        self.previous=list(previous)
        self.selected_at=selected_at
        self.on_apply=on_apply
        self.on_edit=on_edit
        self.on_delete=on_delete
        self.manager=manager
        self.on_import=on_import
        self.photos={}
        self.tiles={}
        self.messages=queue.Queue()
        self.revision=0
        self.alive=True
        self.anchor=None
        self.dialog,content=app.dialog_surface('套版管理' if manager else '套版库 · 选择输出套版','1060x720')
        self.dialog.minsize(790,540)
        self.dialog.bind('<Destroy>',self.destroyed,add='+')
        content.columnconfigure(0,weight=1)
        content.rowconfigure(3,weight=1)
        self.folder_var=tk.StringVar(value=str(folder))
        self.sort_var=tk.StringVar(value='最新日期')
        self.recursive=tk.BooleanVar(value=True)
        self.count_var=tk.StringVar()
        self.info_var=tk.StringVar(value=reference or '单击选择；Ctrl / Shift 多选；每张套版独立输出。')
        app.label(content,textvariable=self.info_var,muted=True,wraplength=960,justify='left').grid(row=0,column=0,sticky='ew',pady=(0,10))
        pathrow=tk.Frame(content,bg=self.palette['panel'])
        pathrow.grid(row=1,column=0,sticky='ew',pady=(0,8))
        pathrow.columnconfigure(0,weight=1)
        field=Field(pathrow,self.palette,variable=self.folder_var)
        field.grid(row=0,column=0,sticky='ew')
        field.bind('<Return>',lambda _event:self.load_folder())
        Tooltip(field.input,'可直接粘贴其他套版目录，按 Enter 打开；更换目录后重新展示缩略图。')
        app.button(pathrow,'选择目录',self.choose_folder,lock=False).grid(row=0,column=1,padx=(8,0))
        app.button(pathrow,'上一级',self.parent_folder,lock=False).grid(row=0,column=2,padx=(8,0))
        app.button(pathrow,'刷新',self.load_folder,lock=False).grid(row=0,column=3,padx=(8,0))
        if manager:
            app.button(pathrow,'导入套版',self.import_files,primary=True,lock=False).grid(row=0,column=4,padx=(8,0))
        tools=tk.Frame(content,bg=self.palette['panel'])
        tools.grid(row=2,column=0,sticky='ew',pady=(0,9))
        app.label(tools,'排序',muted=True).pack(side='left',padx=(0,8))
        sort=app.combo(tools,self.sort_var,['最新日期','名称','尺寸','语言'])
        sort.configure(width=160)
        sort.pack(side='left')
        sort.bind('<<ComboboxSelected>>',lambda _event:self.render())
        Tooltip(sort.input,'默认按最新日期排列；该项目上次选择与新发现的套版优先置顶。')
        toggle=CheckButton(tools,self.recursive,text='包含子文件夹',bg=self.palette['field'],fg=self.palette['text'],
                           activebackground=self.palette['soft'],highlightbackground=self.palette['line'])
        toggle.pack(side='left',padx=10)
        self.recursive.trace_add('write',lambda *_: self.load_folder())
        Tooltip(toggle,'开启后展示当前目录及子文件夹的图片；也可以双击文件夹单独浏览。')
        app.button(tools,'全选',self.select_all,lock=False).pack(side='right')
        app.button(tools,'清空选择',self.clear_selection,lock=False).pack(side='right',padx=8)
        self.shell=Card(content,self.palette,padding=8)
        self.shell.grid(row=3,column=0,sticky='nsew')
        self.canvas=tk.Canvas(self.shell.content,bg=self.palette['panel'],bd=0,highlightthickness=0)
        scroll=Scrollbar(self.shell.content,self.palette,orient='vertical',command=self.canvas.yview)
        scroll.pack(side='right',fill='y')
        self.canvas.pack(side='left',fill='both',expand=True)
        self.canvas.configure(yscrollcommand=scroll.set)
        self.grid=tk.Frame(self.canvas,bg=self.palette['panel'])
        self.window=self.canvas.create_window(0,0,window=self.grid,anchor='nw')
        self.grid.bind('<Configure>',lambda _event:self.canvas.configure(scrollregion=self.canvas.bbox('all')))
        self.columns=4
        self.canvas.bind('<Configure>',self.resize)
        self.dialog.bind('<Delete>',self.delete_selected)
        self.dialog.bind('<Control-a>',lambda _event:self.select_all())
        footer=tk.Frame(content,bg=self.palette['panel'])
        footer.grid(row=4,column=0,sticky='ew',pady=(10,0))
        app.label(footer,textvariable=self.count_var,muted=True).pack(side='left')
        app.button(footer,'关闭',self.dialog.destroy,lock=False).pack(side='right',padx=(8,0))
        self.accept_button=app.button(footer,'使用所选套版',self.accept,primary=True,lock=False)
        self.accept_button.pack(side='right')
        if manager:
            assignable=bool(getattr(app,'tree',None) and app.tree.selection())
            self.accept_button.configure(text='应用到当前队列' if assignable else '关闭管理',
                                         command=self.accept if assignable else self.dialog.destroy)
        self.render()
        # Route wheel events from card children before their native class
        # bindings, so hovering text, buttons or blank grid space scrolls the
        # thumbnail canvas instead of changing a combo value.
        app.install_scroll_router(self.dialog)
        self.dialog.after(60,self.poll)
        app.template_browser=self

    def destroyed(self,event):
        if event.widget is self.dialog:
            self.alive=False
            if getattr(self.app,'template_browser',None) is self:
                self.app.template_browser=None

    def resize(self,event):
        self.canvas.itemconfigure(self.window,width=max(1,event.width))
        columns=max(2,event.width//190)
        if columns!=self.columns:
            self.columns=columns
            self.layout_tiles()

    def import_files(self):
        from tkinter import filedialog
        paths=filedialog.askopenfilenames(parent=self.dialog,title='导入套版到本机总库',initialdir=self.folder_var.get(),
            filetypes=[('模板图片','*.png *.jpg *.jpeg *.bmp *.webp')])
        if paths and self.on_import:
            self.on_import(paths)
            self.load_folder()

    def choose_folder(self):
        from tkinter import filedialog
        path=filedialog.askdirectory(parent=self.dialog,title='选择套版目录',initialdir=self.folder_var.get())
        if path:
            self.folder_var.set(path)
            self.load_folder()

    def parent_folder(self):
        self.folder_var.set(str(Path(self.folder_var.get()).parent))
        self.load_folder()

    def load_folder(self):
        self.revision+=1
        revision=self.revision
        folder=self.folder_var.get().strip().strip('"')
        recursive=self.recursive.get()
        self.info_var.set('正在读取套版目录…')
        def scan():
            try:self.messages.put(('catalog',revision,scan_folder(folder,recursive),''))
            except Exception as exc:self.messages.put(('catalog',revision,[],str(exc)))
        threading.Thread(target=scan,daemon=True).start()

    def render(self):
        for widget in self.grid.winfo_children():widget.destroy()
        self.tiles={}
        self.photos={}
        self.ordered=sort_catalog(self.items,self.sort_var.get(),self.previous,self.selected_at)
        for index,item in enumerate(self.ordered):
            key=identity(item['path'])
            tile=Card(self.grid,self.palette,padding=10)
            tile.content.configure(width=160,height=170)
            picture=tk.Label(tile.content,bg=self.palette['field'],fg=self.palette['muted'],text='加载预览…',width=20,height=7)
            picture.pack(fill='both',expand=True)
            title=self.app.label(tile.content,item.get('name',Path(item['path']).name),wraplength=164,justify='left',font=('Microsoft YaHei UI',9))
            title.pack(fill='x',pady=(7,2))
            meta=self.app.label(tile.content,f"{item.get('width','?')}×{item.get('height','?')} · {item.get('modifiedDate','')[:10]}",muted=True,font=('Segoe UI',8))
            meta.pack(fill='x')
            badge=self.app.label(tile.content,'',font=('Microsoft YaHei UI',8))
            badge.pack(fill='x',pady=(3,0))
            tile.browser_badge=badge
            tile.browser_picture=picture
            for widget in (tile,tile.content,picture,title,meta,badge):
                widget.bind('<Button-1>',lambda event,index=index:self.click(index,event))
                widget.bind('<Double-1>',lambda _event,index=index:self.edit(index))
                Tooltip(widget,lambda item=item:f"{item['path']}\n{item.get('nameDate') or '名称无日期'} · 修改 {item.get('modifiedDate','')}\n单击选择，Ctrl / Shift 多选；双击调整参数。")
            self.tiles[key]=tile
            def load_thumb(key=key,path=item['path']):
                if not self.alive or key not in self.tiles:return
                try:
                    with Image.open(path) as source:
                        source.draft('RGB',(300,220))
                        image=source.convert('RGBA')
                        image.thumbnail((160,110),Image.Resampling.LANCZOS)
                    background=Image.new('RGBA',(164,114),self.palette['field'])
                    background.alpha_composite(image,((164-image.width)//2,(114-image.height)//2))
                    self.photos[key]=ImageTk.PhotoImage(background)
                    self.tiles[key].browser_picture.configure(image=self.photos[key],text='',width=164,height=114)
                except (OSError,ValueError):self.tiles[key].browser_picture.configure(text='图片无法读取')
            self.dialog.after(index*2,load_thumb)
        self.layout_tiles()
        self.mark_selection()
        if not self.ordered:
            self.app.label(self.grid,'此目录没有可用套版。可更换路径或开启“包含子文件夹”。',muted=True).grid(padx=20,pady=50)

    def layout_tiles(self):
        for index,tile in enumerate(self.tiles.values()):
            tile.grid(row=index//self.columns,column=index%self.columns,sticky='nsew',padx=5,pady=5)
        for column in range(self.columns):self.grid.columnconfigure(column,weight=1,uniform='thumb')

    def click(self,index,event):
        key=identity(self.ordered[index]['path'])
        if event.state & 1 and self.anchor is not None:
            if not event.state & 4:self.selected.clear()
            self.selected.update(identity(self.ordered[i]['path']) for i in range(min(index,self.anchor),max(index,self.anchor)+1))
        elif event.state & 4:
            if key in self.selected:self.selected.remove(key)
            else:self.selected.add(key)
            self.anchor=index
        else:
            self.selected={key}
            self.anchor=index
        self.mark_selection()

    def mark_selection(self):
        for item in self.ordered:
            key=identity(item['path'])
            selected=key in self.selected
            badge='已选择' if selected else '点击选择'
            if item.get('newVersion'):badge+=' · 新发现'
            elif item.get('previouslySelected'):badge+=' · 上次使用'
            tile=self.tiles[key]
            tile.browser_badge.configure(text=('✓ ' if selected else '○ ')+badge,fg=self.palette['accent'] if selected else self.palette['muted'])
            tile.border_color=self.palette['accent'] if selected else self.palette['line']
            tile.surface.configure(highlightthickness=0)
            tile.redraw()
        count=sum(identity(item['path']) in self.selected for item in self.ordered)
        self.count_var.set(f'{len(self.ordered)} 张套版 · 已选 {count} 张 · 每张输出一份')

    def select_all(self):
        self.selected={identity(item['path']) for item in self.ordered}
        self.mark_selection()

    def clear_selection(self):
        self.selected.clear()
        self.mark_selection()

    def delete_selected(self,_event=None):
        chosen=[item for item in self.ordered if identity(item['path']) in self.selected]
        if not chosen:return 'break'
        if self.on_delete:
            self.on_delete(chosen)
            removed={identity(item['path']) for item in chosen}
            self.items=[item for item in self.items if identity(item['path']) not in removed]
            self.selected.difference_update(removed)
            self.render()
        else:
            self.clear_selection()
        return 'break'

    def edit(self,index):
        if self.on_edit:
            item=self.ordered[index]
            self.dialog.destroy()
            self.on_edit(item)

    def accept(self):
        chosen=[item for item in self.ordered if identity(item['path']) in self.selected]
        if not chosen:
            self.info_var.set('请至少选择一张套版；Delete 仅取消选择，不会删除图片文件。')
            return
        if self.manager:
            self.on_apply(chosen,self.folder_var.get(),self.items)
            self.dialog.destroy()
            return
        self.on_apply(chosen,self.folder_var.get(),self.items)
        self.dialog.destroy()

    def poll(self):
        if not self.alive:return
        try:
            while True:
                kind,revision,items,error=self.messages.get_nowait()
                if revision!=self.revision:continue
                if error:self.info_var.set(error)
                else:
                    if self.manager:
                        library_keys={identity(cfg.get('template','')) for cfg in self.app.template_library
                                      if isinstance(cfg,dict) and cfg.get('template')}
                        items=[item for item in items if identity(item.get('path','')) in library_keys]
                    self.items=items
                    self.info_var.set('目录已更新 · 单击选择，Ctrl / Shift 多选；排序优先展示上次使用与新发现套版。')
                    self.render()
        except queue.Empty:pass
        self.dialog.after(70,self.poll)
