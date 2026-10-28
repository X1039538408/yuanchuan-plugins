"""Local desktop surfaces, themes and delayed help. No network or telemetry."""
import math
from collections import OrderedDict
import tkinter as tk
from tkinter import ttk, font as tkfont
from PIL import Image, ImageDraw, ImageTk

# AiMaMi uses a restrained radius: larger surfaces are softly rounded while
# controls keep a tighter radius. The preview canvas is intentionally left
# square by app.py so the video frame remains an exact pixel surface.
CARD_RADIUS = 12
CONTROL_RADIUS = 8


def rounded_path(canvas, bounds, radius, **options):
    x,y,right,bottom=bounds
    r=min(radius,max(0,(right-x)/2),max(0,(bottom-y)/2))
    # Supersampling keeps small rounded controls smooth at Windows display scale.
    width,height=max(2,math.ceil(right-x+2)),max(2,math.ceil(bottom-y+2))
    fill,outline=options.get('fill'),options.get('outline')
    if fill:
        key=(width,height,round(r,2),fill,outline)
        cache=getattr(canvas,'_round_cache',None)
        if cache is None:
            cache=canvas._round_cache=OrderedDict()
        photo=cache.get(key)
        if photo is None:
            scale=3
            bitmap=Image.new('RGBA',(width*scale,height*scale),(0,0,0,0))
            draw=ImageDraw.Draw(bitmap)
            draw.rounded_rectangle((0,0,(width-2)*scale,(height-2)*scale),radius=r*scale,
                fill=fill,outline=outline or None,width=scale if outline else 1)
            bitmap=bitmap.resize((width,height),Image.Resampling.LANCZOS)
            if str(canvas.cget('bg')).lower()=='#ff00ff':
                # Color-keyed Windows popups cannot retain partial alpha: it
                # creates a magenta fringe. Keep a clean binary outer mask.
                bitmap.putalpha(bitmap.getchannel('A').point(lambda value:255 if value>=128 else 0))
            photo=ImageTk.PhotoImage(bitmap,master=canvas)
            cache[key]=photo
            while len(cache)>20:
                cache.popitem(last=False)
        return canvas.create_image(x,y,anchor='nw',image=photo,tags=options.get('tags',()))
    points=[]
    for cx,cy,start in ((right-r,y+r,-90),(right-r,bottom-r,0),
                        (x+r,bottom-r,90),(x+r,y+r,180)):
        for step in range(10):
            angle=math.radians(start+step*10)
            points.extend((cx+r*math.cos(angle),cy+r*math.sin(angle)))
    return canvas.create_polygon(*points,smooth=False,**options)


class Button(tk.Canvas):
    """Keyboard-accessible rounded button with the familiar Tk button interface."""
    radius=CONTROL_RADIUS

    def __init__(self,parent,**options):
        self.options=dict(text='',command=None,bg='#ffffff',fg='#252830',state='normal',
            font=('Microsoft YaHei UI',10),padx=12,pady=7,anchor='center',
            activebackground='#f1ecfa',activeforeground='#252830',disabledforeground='#818792',
            highlightbackground='#e6e8ed',highlightthickness=1)
        self.hovered=False
        self.focused=False
        self.bounce_offset=0
        self.bounce_after=None
        self.options.update(options)
        super().__init__(parent,bg=parent.cget('bg'),bd=0,highlightthickness=0,takefocus=True,cursor='hand2')
        tk.Canvas.bind(self,'<Configure>',lambda _event:self.redraw())
        tk.Canvas.bind(self,'<Enter>',lambda _event:self.set_hover(True))
        tk.Canvas.bind(self,'<Leave>',lambda _event:self.set_hover(False))
        tk.Canvas.bind(self,'<ButtonRelease-1>',self.release)
        tk.Canvas.bind(self,'<ButtonPress-1>',lambda _event:self.start_bounce(),add='+')
        tk.Canvas.bind(self,'<FocusIn>',lambda _event:self.set_focus(True))
        tk.Canvas.bind(self,'<FocusOut>',lambda _event:self.set_focus(False))
        tk.Canvas.bind(self,'<space>',lambda _event:self.invoke())
        tk.Canvas.bind(self,'<Return>',lambda _event:self.invoke())
        self.measure()

    def set_hover(self,value):
        self.hovered=value
        self.redraw()

    def set_focus(self,value):
        self.focused=value
        self.redraw()

    def measure(self):
        font=tkfont.Font(root=self,font=self.options['font'])
        width=font.measure(self.options['text'])+int(self.options['padx'])*2+4
        height=font.metrics('linespace')+int(self.options['pady'])*2+4
        tk.Canvas.configure(self,width=max(24,width),height=max(28,height))
        self.redraw()

    def redraw(self):
        if not self.winfo_exists():
            return
        p=self.options
        tk.Canvas.configure(self,bg=self.master.cget('bg'))
        self.delete('button')
        w,h=self.winfo_width(),self.winfo_height()
        disabled=p['state']=='disabled'
        color=p['activebackground'] if self.hovered and not disabled else p['bg']
        border=p['highlightbackground'] if p['highlightthickness'] or self.focused else color
        offset=self.bounce_offset
        rounded_path(self,(1,1+offset,max(2,w-1),max(2,h-1)+offset),self.radius,fill=color,outline=border,tags='button')
        anchor='w' if p['anchor']=='w' else 'center'
        x=int(p['padx'])+2 if anchor=='w' else w/2
        self.create_text(x,h/2+offset,text=p['text'],font=p['font'],anchor=anchor,
            fill=p['disabledforeground'] if disabled else p['fg'],tags='button')

    def start_bounce(self):
        if self.options['state']=='disabled':
            return
        if self.bounce_after:
            try:self.after_cancel(self.bounce_after)
            except tk.TclError:pass
        self.bounce_offset=1
        self.redraw()
        self.bounce_after=self.after(75,self.finish_bounce)

    def finish_bounce(self):
        self.bounce_after=None
        self.bounce_offset=0
        self.redraw()

    def configure(self,cnf=None,**kwargs):
        options=dict(cnf or {},**kwargs)
        if not options:
            return self.options
        native={key:value for key,value in options.items() if key not in self.options and key not in ('relief','bd')}
        self.options.update({key:value for key,value in options.items() if key in self.options})
        if native:
            tk.Canvas.configure(self,**native)
        if any(key in options for key in ('text','font','padx','pady')):
            self.measure()
        else:
            self.redraw()

    config=configure

    def cget(self,key):
        return self.options[key] if key in self.options else tk.Canvas.cget(self,key)

    def release(self,event):
        if 0<=event.x<self.winfo_width() and 0<=event.y<self.winfo_height():
            self.focus_set()
            self.invoke()

    def invoke(self):
        if self.options['state']!='disabled' and self.options['command']:
            return self.options['command']()


class Field(tk.Frame):
    """Rounded, focusable outer surface around a native editable field."""
    radius=CONTROL_RADIUS

    def __init__(self,parent,palette,kind='entry',variable=None,values=(),width=None):
        super().__init__(parent,bg=parent.cget('bg'),bd=0)
        self.palette=palette
        self.kind=kind
        self.focused=False
        self.surface=tk.Canvas(self,bg=parent.cget('bg'),bd=0,highlightthickness=0)
        self.surface.pack(fill='both',expand=True)
        if kind=='combo':
            self.input=ttk.Combobox(self.surface,textvariable=variable,values=list(values),state='readonly',style='Rounded.TCombobox')
        elif kind=='spin':
            self.input=tk.Spinbox(self.surface,textvariable=variable,from_=10,to=500,increment=1,bd=0,relief='flat')
        else:
            self.input=tk.Entry(self.surface,textvariable=variable,bd=0,relief='flat')
        if width:
            self.input.configure(width=width)
        self.configure(width=(width or 24)*9+22,height=36)
        self.pack_propagate(False)
        self.input.place(x=9,y=6,relwidth=1,width=-34 if kind=='combo' else -18,relheight=1,height=-12)
        self.surface.bind('<Configure>',lambda _event:self.redraw())
        self.popup=None
        if kind=='combo':
            self.surface.bind('<Button-1>',self.show_choices)
            self.input.bind('<Button-1>',self.show_choices)
            for sequence in ('<space>','<Return>','<Alt-Down>','<F4>'):
                self.input.bind(sequence,self.show_choices)
            self.input.bind('<Destroy>',lambda _event:self.hide_choices(),add='+')
        self.input.bind('<FocusIn>',lambda _event:self.set_focus(True),add='+')
        self.input.bind('<FocusOut>',lambda _event:self.set_focus(False),add='+')
        self.refresh_palette(palette)

    def set_focus(self,value):
        self.focused=value
        self.redraw()

    def refresh_palette(self,palette):
        self.palette=palette
        self.hide_choices()
        tk.Frame.configure(self,bg=self.master.cget('bg'))
        if self.kind!='combo':
            self.input.configure(bg=palette['field'],fg=palette['text'],insertbackground=palette['accent'],
                disabledbackground=palette['field'],disabledforeground=palette['muted'],highlightthickness=0)
            if self.kind=='spin':
                self.input.configure(buttonbackground=palette['field'])
        self.redraw()

    def hide_choices(self, _event=None):
        popup=getattr(self,'popup',None)
        self.popup=None
        if popup is not None:
            try:
                popup.grab_release()
                popup.destroy()
                previous=getattr(self,'popup_previous_grab',None)
                if previous is not None and previous.winfo_exists():
                    previous.grab_set()
            except tk.TclError:
                pass

    def show_choices(self, _event=None):
        if str(self.input.cget('state'))=='disabled':
            return 'break'
        if self.popup is not None:
            self.hide_choices()
            return 'break'
        values=self.input.cget('values')
        if not values:
            return 'break'
        self.input.focus_set()
        popup=self.popup=tk.Toplevel(self)
        popup.withdraw()
        popup.overrideredirect(True)
        popup.transient(self.winfo_toplevel())
        popup.attributes('-topmost',True)
        transparent='#ff00ff'
        popup.configure(bg=transparent)
        try:
            popup.attributes('-transparentcolor',transparent)
        except tk.TclError:
            pass
        surface=tk.Canvas(popup,bg=transparent,highlightthickness=0,bd=0)
        surface.pack(fill='both',expand=True)
        p=self.palette
        listing=tk.Listbox(surface,height=min(9,len(values)),font=('Microsoft YaHei UI',10),
            bg=p['panel'],fg=p['text'],selectbackground=p['soft'],selectforeground=p['text'],
            bd=0,highlightthickness=0,activestyle='none',exportselection=False)
        for value in values:
            listing.insert('end',value)
        if self.get() in values:
            index=values.index(self.get())
            listing.selection_set(index)
            listing.activate(index)
            listing.see(index)
        width=max(self.winfo_width(), min(440,max(tkfont.Font(root=self,font=('Microsoft YaHei UI',10)).measure(v) for v in values)+36))
        height=listing.winfo_reqheight()+20
        surface.configure(width=width,height=height)
        rounded_path(surface,(1,1,width-1,height-1),CONTROL_RADIUS,fill=p['panel'],outline=p['line'])
        surface.create_window(10,10,window=listing,anchor='nw',width=width-20,height=height-20)
        # ``winfo_screenwidth`` is the primary monitor on Windows.  Use the
        # virtual desktop bounds so a popup opened on a secondary monitor (or
        # a monitor left of the primary display) stays beside its field.
        vx,vy=self.winfo_vrootx(),self.winfo_vrooty()
        vw,vh=self.winfo_vrootwidth(),self.winfo_vrootheight()
        x=min(max(self.winfo_rootx(),vx),vx+vw-width-4)
        y=self.winfo_rooty()+self.winfo_height()+4
        if y+height>vy+vh:
            y=max(vy+4,self.winfo_rooty()-height-4)
        popup.geometry(f'{width}x{height}+{x}+{y}')
        def choose(event=None):
            if event is not None and hasattr(event,'y') and event.type==tk.EventType.ButtonRelease:
                index=listing.nearest(event.y)
            else:
                index=next(iter(listing.curselection()),listing.index('active'))
            self.input.set(values[index])
            self.hide_choices()
            self.input.focus_set()
            self.input.event_generate('<<ComboboxSelected>>')
            return 'break'
        listing.bind('<ButtonRelease-1>',choose)
        listing.bind('<Return>',choose)
        Tooltip(listing,lambda: option_help(values[max(0,min(len(values)-1,listing.nearest(listing.winfo_pointery()-listing.winfo_rooty())))]))
        listing.bind('<Escape>',lambda _event:(self.hide_choices(),self.input.focus_set(),'break')[-1])
        listing.bind('<Tab>',lambda _event:(self.hide_choices(),self.input.tk_focusNext().focus_set(),'break')[-1])
        def outside(event):
            if not (0<=event.x_root-popup.winfo_rootx()<width and 0<=event.y_root-popup.winfo_rooty()<height):
                # The popup owns a grab while open. Forward the initiating
                # click to the widget under the pointer so dragging a panel's
                # scrollbar or pressing a neighbouring button still starts
                # immediately instead of requiring a second click.
                try: popup.grab_release()
                except tk.TclError: pass
                target=self.winfo_toplevel().winfo_containing(event.x_root,event.y_root)
                self.hide_choices()
                if target is not None and target is not popup:
                    try:
                        target.event_generate('<Button-1>',
                            x=max(0,event.x_root-target.winfo_rootx()),
                            y=max(0,event.y_root-target.winfo_rooty()),
                            rootx=event.x_root,rooty=event.y_root)
                    except tk.TclError:
                        pass
                return 'break'
        def wheel_outside(event):
            if not (0<=event.x_root-popup.winfo_rootx()<width and 0<=event.y_root-popup.winfo_rooty()<height):
                try: popup.grab_release()
                except tk.TclError: pass
                target=self.winfo_toplevel().winfo_containing(event.x_root,event.y_root)
                self.hide_choices()
                if target is not None and target is not popup:
                    try: target.event_generate('<MouseWheel>',delta=event.delta)
                    except tk.TclError: pass
                return 'break'
        popup.bind('<Button-1>',outside)
        popup.bind('<B1-Motion>',outside)
        popup.bind('<MouseWheel>',wheel_outside)
        popup.deiconify()
        popup.lift()
        self.popup_previous_grab=self.grab_current()
        popup.grab_set()
        listing.focus_force()
        return 'break'

    def redraw(self):
        if not self.winfo_exists():
            return
        p=self.palette
        self.surface.configure(bg=self.master.cget('bg'))
        self.surface.delete('field')
        rounded_path(self.surface,(1,1,max(2,self.winfo_width()-1),max(2,self.winfo_height()-1)),self.radius,
            fill=p['field'],outline=p['accent'] if self.focused else p['line'],tags='field')
        self.surface.tag_lower('field')
        if self.kind=='combo':
            x,y=self.winfo_width()-17,self.winfo_height()/2
            self.surface.create_line(x-4,y-2,x,y+2,x+4,y-2,fill=p['muted'],width=1.5,tags='field')

    def configure(self,cnf=None,**kwargs):
        options=dict(cnf or {},**kwargs)
        field_keys={'state','values','textvariable','from_','to','increment'}
        passed={key:value for key,value in options.items() if key in field_keys}
        if passed and hasattr(self,'input'):
            self.input.configure(**passed)
        rest={key:value for key,value in options.items() if key not in field_keys}
        if rest:
            tk.Frame.configure(self,**rest)

    config=configure

    def bind(self,sequence=None,func=None,add=None):
        if hasattr(self,'input'):
            return self.input.bind(sequence,func,add)
        return tk.Frame.bind(self,sequence,func,add)

    def event_generate(self,sequence,**kwargs):
        return self.input.event_generate(sequence,**kwargs)

    def get(self):
        return self.input.get()

    def set(self,value):
        return self.input.set(value)

    def focus_set(self):
        return self.input.focus_set()


class NumberField(Field):
    """Compact labeled number with thresholded scrubbing and normal text editing."""
    def __init__(self,parent,palette,label,variable,bounds,mode=None):
        self.number_label=label
        self.variable=variable
        self.bounds=bounds
        self.mode=mode
        self.drag_origin=None
        self.scrubbing=False
        self.history=[]
        self._history_value=str(variable.get())
        self._history_silent=False
        self._drag_start_value=None
        super().__init__(parent,palette,variable=variable,width=5)
        self.configure(height=38)
        self.input.configure(justify='right',font=('Segoe UI',10))
        self.input.place(x=22,y=6,relwidth=1,width=-29,relheight=1,height=-12)
        # The value itself is the scrub target. Dragging the X/Y label should
        # still select the field and never change a coordinate accidentally.
        for sequence, callback in (('<ButtonPress-1>',self.begin_scrub),('<B1-Motion>',self.scrub),('<ButtonRelease-1>',self.end_scrub)):
            self.input.bind(sequence,callback,add='+')
        self.input.bind('<Up>',lambda _event:self.nudge(1))
        self.input.bind('<Down>',lambda _event:self.nudge(-1))
        self.input.bind('<Control-z>',self.undo,add='+')
        self.input.bind('<Control-Z>',self.undo,add='+')
        variable.trace_add('write',self._record_change)
        self.input.configure(cursor='sb_h_double_arrow')
        self.surface.configure(cursor='arrow')

    def redraw(self):
        super().redraw()
        self.surface.delete('number-label')
        self.surface.create_text(8,self.winfo_height()/2,text=self.number_label,anchor='w',
            font=('Microsoft YaHei UI',9),fill=self.palette['muted'],tags='number-label')

    def begin_scrub(self,event):
        if str(self.input.cget('state'))=='disabled':
            return 'break'
        if self.mode is not None and self.mode.get()!='拖动调整':
            self.drag_origin=None
            return
        try:
            value=int(self.variable.get())
        except ValueError:
            value=0
        self.drag_origin=(event.x_root,value)
        self._drag_start_value=str(self.variable.get())
        self.scrubbing=False
        self.input.focus_set()
        # Consume the native Entry press in scrub mode.  Otherwise Tk starts
        # a text selection while our motion handler changes the same value.
        return 'break'

    def scrub(self,event):
        if self.drag_origin is None or (self.mode is not None and self.mode.get()!='拖动调整'):
            return
        delta=event.x_root-self.drag_origin[0]
        if abs(delta)<4 and not self.scrubbing:
            return 'break'
        self.scrubbing=True
        step=10 if event.state & 1 else 1
        low,high=self.bounds()
        self.variable.set(str(max(low,min(high,self.drag_origin[1]+round(delta*step)))))
        self.input.selection_clear()
        return 'break'

    def end_scrub(self,_event=None):
        was_scrubbing=self.scrubbing
        self.drag_origin=None
        self.scrubbing=False
        if self._drag_start_value is not None and self._drag_start_value != str(self.variable.get()):
            self.history.append(self._drag_start_value)
            del self.history[:-40]
        self._drag_start_value=None
        if was_scrubbing:
            return 'break'
        # A click without horizontal movement is an intentional edit gesture:
        # select the whole value so the next key replaces it.  The explicit
        # “点击编辑” mode keeps native partial selection semantics instead.
        if self.mode is None or self.mode.get()=='拖动调整':
            self.input.selection_range(0,'end')
            self.input.icursor('end')
        return 'break'

    def _record_change(self,*_):
        if self._history_silent or self._drag_start_value is not None:
            self._history_value=str(self.variable.get())
            return
        current=str(self.variable.get())
        if current!=self._history_value:
            self.history.append(self._history_value)
            del self.history[:-40]
            self._history_value=current

    def undo(self,_event=None):
        if not self.history:
            return 'break'
        value=self.history.pop()
        self._history_silent=True
        try:self.variable.set(value)
        finally:
            self._history_silent=False
            self._history_value=str(value)
        return 'break'

    def nudge(self,amount):
        low,high=self.bounds()
        try:
            value=int(self.variable.get())
        except ValueError:
            value=low
        self.variable.set(str(max(low,min(high,value+amount))))
        return 'break'


class PreviewButton(Button):
    """Drawn expand/restore corners; no font-dependent diagonal glyph."""
    def __init__(self,parent,**options):
        self.expanded=False
        super().__init__(parent,text='',**options)

    def measure(self):
        tk.Canvas.configure(self,width=34,height=34)
        self.redraw()

    def redraw(self):
        super().redraw()
        self.delete('preview-icon')
        width,height=self.winfo_width(),self.winfo_height()
        cx,cy=width/2,height/2
        color=self.options['fg']
        for sx,sy in ((-1,-1),(1,-1),(-1,1),(1,1)):
            x,y=cx+sx*7,cy+sy*7
            if self.expanded:
                x,y=cx+sx*3,cy+sy*3
                points=(x+sx*4,y,x,y,x,y+sy*4)
            else:
                points=(x-sx*4,y,x,y,x,y-sy*4)
            self.create_line(*points,fill=color,width=1.5,capstyle='round',joinstyle='round',tags='preview-icon')


class NavigationButton(Button):
    def __init__(self,parent,icon,**options):
        self.icon=icon
        super().__init__(parent,**options)

    def redraw(self):
        super().redraw()
        self.delete('nav-icon')
        y=self.winfo_height()/2
        color=self.options['fg']
        def line(points):
            self.create_line(*points,fill=color,width=1.5,capstyle='round',joinstyle='round',tags='nav-icon')
        if self.icon=='workbench':
            self.create_rectangle(14,y-7,29,y+6,outline=color,width=1.5,tags='nav-icon')
            line((14,y-2,29,y-2));line((19,y-7,19,y+6))
        elif self.icon=='templates':
            self.create_rectangle(14,y-7,26,y+4,outline=color,width=1.5,tags='nav-icon')
            line((18,y+7,30,y+7,30,y-3))
        else:
            for offset in (-5,1,7):
                line((14,y+offset,30,y+offset))
            for cx,cy in ((19,y-5),(26,y+1),(21,y+7)):
                self.create_oval(cx-2,cy-2,cx+2,cy+2,fill=self.options['bg'],outline=color,width=1.5,tags='nav-icon')


class CheckButton(Button):
    def __init__(self,parent,variable,**options):
        self.variable=variable
        self.caption=options.pop('text','')
        super().__init__(parent,text=self.caption,command=self.toggle,anchor='w',padx=38,**options)
        variable.trace_add('write',lambda *_:self.redraw())

    def toggle(self):
        self.variable.set(not self.variable.get())

    def redraw(self):
        super().redraw()
        y=self.winfo_height()/2
        self.delete('check-icon')
        rounded_path(self,(12,y-7,26,y+7),4,fill=self.options['activebackground'] if self.variable.get() else self.options['bg'],outline=self.options['fg'],tags='check-icon')
        if self.variable.get():
            self.create_line(15,y,18,y+3,23,y-3,fill=self.options['fg'],width=1.7,capstyle='round',tags='check-icon')

LIGHT = dict(bg='#f7f8fa', panel='#ffffff', text='#252830', muted='#818792',
             accent='#8a61cf', line='#e6e8ed', field='#f8f9fb', soft='#f1ecfa', success='#14ad88')
DARK = dict(bg='#14161d', panel='#1d202a', text='#eceef5', muted='#a1a8b9',
            accent='#ad8bec', line='#333847', field='#262b37', soft='#352b49', success='#45c7a5')


def system_theme():
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
            r'Software\Microsoft\Windows\CurrentVersion\Themes\Personalize') as key:
            return 'light' if winreg.QueryValueEx(key, 'AppsUseLightTheme')[0] else 'dark'
    except (OSError, ImportError):
        return 'light'


class Tooltip:
    def __new__(cls,widget,text):
        previous=getattr(widget,'tooltip',None)
        return previous if isinstance(previous,cls) else super().__new__(cls)

    def __init__(self, widget, text):
        if getattr(self,'widget',None) is widget:
            self.text=text
            return
        self.widget, self.text, self.popup, self.pending = widget, text, None, None
        widget.tooltip = self
        widget.bind('<Enter>', self.schedule, add='+')
        widget.bind('<Leave>', self.hide, add='+')
        widget.bind('<ButtonPress>', self.hide, add='+')
        widget.bind('<Destroy>', self.hide, add='+')
        self.last_text=None
        widget.bind('<Motion>',self.motion,add='+')

    def motion(self,_event=None):
        text=self.text() if callable(self.text) else self.text
        if text!=self.last_text:
            self.last_text=text
            self.schedule()

    def schedule(self, _event=None):
        self.hide()
        self.pending = self.widget.after(550, self.show)

    def show(self):
        self.pending = None
        if not self.widget.winfo_exists():
            return
        self.popup = tk.Toplevel(self.widget)
        self.popup.overrideredirect(True)
        self.popup.attributes('-topmost', True)
        transparency='#ff00ff'
        self.popup.configure(bg=transparency)
        try:
            self.popup.attributes('-transparentcolor',transparency)
        except tk.TclError:
            pass
        surface=tk.Canvas(self.popup,bg=transparency,highlightthickness=0,bd=0)
        surface.pack()
        text=self.text() if callable(self.text) else self.text
        if not text:
            self.popup.destroy();self.popup=None
            return
        label=tk.Label(surface,text=text,wraplength=320,justify='left',bg='#252935',fg='#ffffff',
            font=('Microsoft YaHei UI',9),bd=0)
        label.update_idletasks()
        width,height=label.winfo_reqwidth()+24,label.winfo_reqheight()+20
        surface.configure(width=width,height=height)
        rounded_path(surface,(1,1,width-1,height-1),CONTROL_RADIUS,fill='#252935',outline='#414756')
        surface.create_window(12,10,window=label,anchor='nw')
        self.popup.update_idletasks()
        x = min(self.widget.winfo_pointerx()+12, self.widget.winfo_screenwidth()-self.popup.winfo_reqwidth()-12)
        y = min(self.widget.winfo_rooty()+self.widget.winfo_height()+7,
                self.widget.winfo_screenheight()-self.popup.winfo_reqheight()-12)
        self.popup.geometry(f'+{max(4,x)}+{max(4,y)}')

    def hide(self, _event=None):
        if self.pending:
            try:
                self.widget.after_cancel(self.pending)
            except tk.TclError:
                pass
            self.pending = None
        popup,self.popup=self.popup,None
        if popup:
            try:
                if popup.winfo_exists():
                    popup.destroy()
            except tk.TclError:
                pass


class Card(tk.Frame):
    def __init__(self, parent, palette, padding=16, **kwargs):
        super().__init__(parent, bg=parent.cget('bg'), bd=0, **kwargs)
        self.pack_propagate(False)
        self.palette, self.padding = palette, padding
        self.surface = tk.Canvas(self, bg=parent.cget('bg'), bd=0, highlightthickness=0)
        self.surface.pack(fill='both', expand=True)
        self.content = tk.Frame(self.surface, bg=palette['panel'], bd=0)
        self.content._card_owner = self
        self.window = self.surface.create_window(padding,padding,window=self.content,anchor='nw')
        self.surface.bind('<Configure>',self.redraw)
        self.content.bind('<Configure>',self.measure)
        self.bind('<Map>',self.measure,add='+')
        self.after_idle(self.measure)

    def measure(self, _event=None):
        if getattr(self,'_fixed_height',False):
            return
        desired = self.content.winfo_reqheight()+self.padding*2
        if self.winfo_reqheight()!=desired:
            self.configure(height=desired)
            ancestor=self.master
            while ancestor is not None:
                owner=getattr(ancestor,'_card_owner',None)
                if owner is not None:
                    owner.after_idle(owner.measure)
                    break
                ancestor=ancestor.master

    def redraw(self, _event=None):
        p=self.palette
        w,h=self.surface.winfo_width(),self.surface.winfo_height()
        self.surface.configure(bg=self.master.cget('bg'))
        self.configure(bg=self.master.cget('bg'))
        self.content.configure(bg=p['panel'])
        self.surface.delete('surface')
        rounded_path(self.surface,(1,1,w-1,h-1),CARD_RADIUS,fill=p['panel'],outline=p['line'],tags='surface')
        self.surface.tag_lower('surface')
        available=max(1,h-self.padding*2)
        natural=self.content.winfo_reqheight()
        # Leave height automatic during initial layout. Pinning a 1px window
        # before its Treeview/Listbox requests a size hides all list rows and
        # prevents the content Configure event from measuring them later.
        self.surface.itemconfigure(self.window,width=max(1,w-self.padding*2),
            height=available if natural>1 else 0)


class Scrollbar(tk.Canvas):
    """Quiet rounded thumb shared by lists and the settings inspector."""
    def __init__(self,parent,palette,command=None,orient='vertical',**kwargs):
        super().__init__(parent,width=12,height=12,bg=parent.cget('bg'),bd=0,highlightthickness=0,**kwargs)
        self.palette,self.command,self.orient=palette,command,orient
        self.first,self.last=0.,1.
        self.drag=None
        self.bind('<Configure>',lambda _event:self.redraw())
        self.bind('<Button-1>',self.press)
        self.bind('<B1-Motion>',self.move)
        self.bind('<ButtonRelease-1>',lambda _event:setattr(self,'drag',None))

    def refresh_palette(self,palette):
        self.palette=palette
        self.redraw()

    def set(self,first,last):
        self.first,self.last=float(first),float(last)
        self.redraw()

    def geometry_range(self):
        length=self.winfo_height() if self.orient=='vertical' else self.winfo_width()
        span=max(1,length-6)
        size=min(span,max(24,span*(self.last-self.first)))
        start=3+(span-size)*self.first/max(.0001,1-(self.last-self.first))
        return start,size,span

    def redraw(self):
        self.configure(bg=self.master.cget('bg'))
        self.delete('all')
        if self.last-self.first>=.999:
            return
        start,size,_span=self.geometry_range()
        bounds=(3,start,9,start+size) if self.orient=='vertical' else (start,3,start+size,9)
        rounded_path(self,bounds,3,fill=self.palette['muted'],outline='')

    def press(self,event):
        if not self.command:
            return
        position=event.y if self.orient=='vertical' else event.x
        start,size,_span=self.geometry_range()
        if start<=position<=start+size:
            self.drag=(position,self.first)
        else:
            self.command('scroll',-1 if position<start else 1,'pages')

    def move(self,event):
        if self.drag and self.command:
            position=event.y if self.orient=='vertical' else event.x
            _start,size,span=self.geometry_range()
            offset=(position-self.drag[0])/max(1,span-size)*(1-(self.last-self.first))
            self.command('moveto',max(0,min(1,self.drag[1]+offset)))


class Progress(tk.Canvas):
    def __init__(self,parent,palette,variable,maximum=100):
        super().__init__(parent,height=6,bg=parent.cget('bg'),bd=0,highlightthickness=0)
        self.palette,self.variable,self.maximum=palette,variable,maximum
        self.trace=variable.trace_add('write',lambda *_:self.redraw())
        self.bind('<Configure>',lambda _event:self.redraw())
        self.bind('<Destroy>',self.cleanup)

    def cleanup(self,_event=None):
        self.variable.trace_remove('write',self.trace)

    def refresh_palette(self,palette):
        self.palette=palette
        self.redraw()

    def redraw(self):
        self.configure(bg=self.master.cget('bg'))
        self.delete('all')
        w,h=self.winfo_width(),self.winfo_height()
        rounded_path(self,(0,0,w,h),h/2,fill=self.palette['line'],outline='')
        value=max(0,min(1,float(self.variable.get())/self.maximum))
        if value:
            rounded_path(self,(0,0,max(h,w*value),h),h/2,fill=self.palette['accent'],outline='')


class Slider(tk.Canvas):
    """Rounded timeline with a circular handle and keyboard seeking."""
    def __init__(self,parent,palette,variable,command=None,from_=0,to=1,**kwargs):
        kwargs.pop('orient',None)
        super().__init__(parent,height=28,bg=parent.cget('bg'),bd=0,highlightthickness=0,takefocus=True,**kwargs)
        self.palette,self.variable,self.command=palette,variable,command
        self.minimum,self.maximum=float(from_),float(to)
        self.markers=[]
        self.marker_hint=None
        self.marker_popup=None
        self.dragging=False
        self.trace=variable.trace_add('write',lambda *_:self.redraw())
        self.bind('<Configure>',lambda _event:self.redraw())
        self.bind('<Button-1>',self.seek)
        self.bind('<B1-Motion>',self.seek)
        self.bind('<Motion>',self.marker_motion)
        self.bind('<Leave>',self.hide_marker_hint)
        self.bind('<ButtonPress-1>',lambda _event:(setattr(self,'dragging',True),self.hide_marker_hint()))
        self.bind('<ButtonRelease-1>',lambda _event:setattr(self,'dragging',False))
        for key,offset in (('Left',-1),('Right',1),('Home',None),('End',None)):
            self.bind(f'<{key}>',lambda _event,key=key,offset=offset:self.keyboard(key,offset))
        self.bind('<Destroy>',lambda _event:variable.trace_remove('write',self.trace))
        self.bind('<Destroy>',self.hide_marker_hint)

    def bind(self,sequence=None,func=None,add=None):
        # Caller bindings mark dragging/release without replacing seeking itself.
        return tk.Canvas.bind(self,sequence,func,'+' if func is not None else add)

    def configure(self,cnf=None,**kwargs):
        options=dict(cnf or {},**kwargs)
        if 'to' in options:
            self.maximum=float(options.pop('to'))
        if 'from_' in options:
            self.minimum=float(options.pop('from_'))
        if options:
            tk.Canvas.configure(self,**options)
        if hasattr(self,'variable'):
            self.redraw()

    config=configure

    def refresh_palette(self,palette):
        self.palette=palette
        self.redraw()

    def set_markers(self,markers):
        self.markers=list(markers or [])
        self.redraw()

    def marker_motion(self,event):
        if self.dragging or not self.markers or self.maximum<=self.minimum:
            self.hide_marker_hint()
            return
        value=self.minimum+(self.maximum-self.minimum)*max(0,min(1,(event.x-8)/max(1,self.winfo_width()-16)))
        markers=self.markers_at(value)
        if not markers:
            self.hide_marker_hint()
            return
        text='\n'.join(item.get('label','边缘提示') for item in markers)
        if self.marker_hint==text and self.marker_popup is not None:
            return
        self.hide_marker_hint()
        self.marker_hint=text
        popup=tk.Toplevel(self)
        self.marker_popup=popup
        popup.overrideredirect(True)
        popup.attributes('-topmost',True)
        transparent='#ff00ff'
        popup.configure(bg=transparent)
        try: popup.attributes('-transparentcolor',transparent)
        except tk.TclError: pass
        canvas=tk.Canvas(popup,bg=transparent,bd=0,highlightthickness=0)
        canvas.pack()
        label=tk.Label(canvas,text=text,bg=self.palette['panel'],fg=self.palette['text'],font=('Microsoft YaHei UI',9),justify='left')
        label.update_idletasks()
        width,height=label.winfo_reqwidth()+24,label.winfo_reqheight()+20
        canvas.configure(width=width,height=height)
        rounded_path(canvas,(0,0,width-1,height-1),8,fill=self.palette['panel'],outline=self.palette['line'])
        canvas.create_window(12,10,window=label,anchor='nw')
        popup.update_idletasks()
        x=min(self.winfo_pointerx()+10,self.winfo_screenwidth()-popup.winfo_reqwidth()-8)
        y=max(4,self.winfo_rooty()-popup.winfo_reqheight()-4)
        popup.geometry(f'+{max(4,x)}+{y}')

    def markers_at(self,seconds):
        return [item for item in self.markers if item.get('start',0)<=seconds<=item.get('end',item.get('start',0))]

    def hide_marker_hint(self,_event=None):
        self.marker_hint=None
        popup,self.marker_popup=self.marker_popup,None
        if popup is not None:
            try:
                if popup.winfo_exists():
                    popup.destroy()
            except tk.TclError:
                pass

    def set_value(self,value):
        self.variable.set(max(self.minimum,min(self.maximum,value)))
        if self.command:
            self.command(self.variable.get())

    def seek(self,event):
        self.focus_set()
        fraction=max(0,min(1,(event.x-8)/max(1,self.winfo_width()-16)))
        self.set_value(self.minimum+(self.maximum-self.minimum)*fraction)

    def keyboard(self,key,offset):
        self.set_value(self.minimum if key=='Home' else self.maximum if key=='End' else self.variable.get()+offset*.1)
        return 'break'

    def redraw(self):
        tk.Canvas.configure(self,bg=self.master.cget('bg'))
        self.delete('all')
        width,height=self.winfo_width(),self.winfo_height()
        fraction=max(0,min(1,(self.variable.get()-self.minimum)/max(.0001,self.maximum-self.minimum)))
        x=8+max(0,width-16)*fraction
        rounded_path(self,(8,height/2-2,width-8,height/2+2),2,fill=self.palette['line'],outline='')
        if x>8:
            rounded_path(self,(8,height/2-2,x,height/2+2),2,fill=self.palette['accent'],outline='')
        for marker in self.markers:
            start=8+(width-16)*max(0,min(1,(marker.get('start',0)-self.minimum)/max(.0001,self.maximum-self.minimum)))
            end=8+(width-16)*max(0,min(1,(marker.get('end',marker.get('start',0))-self.minimum)/max(.0001,self.maximum-self.minimum)))
            color=marker.get('color',self.palette['accent'])
            self.create_rectangle(start,height/2-6,max(start+2,end),height/2+6,fill=color,outline='',tags='marker')
        self.create_oval(x-6,height/2-6,x+6,height/2+6,fill=self.palette['accent'],outline='')


class Dialogs:
    """Application prompts use the same cards and buttons as the workbench."""
    def show(self,title,message,parent=None,choices=(('确定','ok'),),editable=False):
        parent=parent or tk._default_root
        root=parent
        while root is not None and not hasattr(root,'palette'):
            root=root.master
        p=root.palette if root is not None else LIGHT
        dialog=tk.Toplevel(parent)
        dialog.withdraw()
        dialog.title(title)
        dialog.transient(parent.winfo_toplevel())
        dialog.configure(bg=p['bg'])
        dialog.resizable(False,False)
        card=Card(dialog,p,padding=20)
        card.pack(fill='both',expand=True,padx=8,pady=8)
        tk.Label(card.content,text=title,bg=p['panel'],fg=p['text'],font=('Microsoft YaHei UI',12,'bold'),anchor='w').pack(fill='x',pady=(0,12))
        tk.Label(card.content,text=message,bg=p['panel'],fg=p['text'],wraplength=540,justify='left',anchor='w').pack(fill='x',pady=(0,16))
        value=tk.StringVar()
        if editable:
            field=Field(card.content,p,variable=value)
            field.pack(fill='x',pady=(0,16))
        row=tk.Frame(card.content,bg=p['panel'])
        row.pack(fill='x')
        result=[None]
        previous=parent.grab_current()
        def finish(answer):
            result[0]=value.get() if editable and answer=='ok' else answer
            dialog.destroy()
        for index,(text,answer) in enumerate(reversed(choices)):
            button=Button(row,text=text,command=lambda answer=answer:finish(answer),
                bg=p['accent'] if index==0 else p['field'],fg='#ffffff' if index==0 else p['text'],
                activebackground=p['soft'],highlightbackground=p['line'])
            button.primary=index==0
            button.pack(side='right',padx=(8,0))
            Tooltip(button,f'{text}：'+('确认当前对话框中的操作。' if answer is not None else '关闭对话框并返回当前操作。'))
        dialog.bind('<Escape>',lambda _event:finish(None))
        dialog.bind('<Return>',lambda _event:finish(choices[-1][1]))
        dialog.update_idletasks()
        width,height=600,card.content.winfo_reqheight()+56
        x=max(0,parent.winfo_rootx()+(parent.winfo_width()-width)//2)
        y=max(0,parent.winfo_rooty()+(parent.winfo_height()-height)//2)
        dialog.geometry(f'{width}x{height}+{x}+{y}')
        dialog.deiconify()
        dialog.grab_set()
        if editable:
            field.focus_set()
        dialog.wait_window()
        if previous is not None and previous.winfo_exists():
            previous.grab_set()
        return result[0]

    def showinfo(self,title,message,**kwargs):
        return self.show(title,message,**kwargs)

    showwarning=showerror=showinfo

    def askyesno(self,title,message,**kwargs):
        return bool(self.show(title,message,choices=(('否',False),('是',True)),**kwargs))

    def askyesnocancel(self,title,message,**kwargs):
        return self.show(title,message,choices=(('取消',None),('否',False),('是',True)),**kwargs)

    def askstring(self,title,message,**kwargs):
        return self.show(title,message,choices=(('取消',None),('确定','ok')),editable=True,**kwargs)


HELP = {
    '检查此帧':'检查时间轴当前位置的原片四侧边缘、内部暗缝和套版接缝，结果标在时间轴上。',
    '套版工作台':'返回素材队列、拖动预览以及当前套版和导出参数。',
    '输出模板':'管理保存在本机的多套版组合、窗口、画质、命名和输出规则。',
    '边缘检查':'查看原片与套版边缘报告；双击时间段返回对应视频位置。',
    '本机设置':'修改这台电脑的项目排期、套版目录、语言和输出分类。路径不会上传。',
    '返回预览':'返回工作台，保留当前时间点和参数。',
    '编辑所选':'将选中套版载入工作台，修改窗口与尺寸后点击保存套版参数。',
    '移除所选':'从多套版组合中移除选中项，不删除模板图片。',
    '关闭':'关闭当前面板并保留已保存的设置。',
    '暂不处理':'暂时关闭候选窗口，保留当前未确认状态，等待你明确选择。',
    '使用此套版':'确认当前候选并记入本机项目映射。',
    '添加视频':'选择一个或多个本机视频；也可直接拖进窗口。素材不会上传。',
    '移除':'从当前队列移除选中的条目，保留源视频和已完成的成片。支持 Delete。',
    '更换套框':'选择本机 PNG、JPG、BMP 或 WebP 套版。视频在套版上方合成。',
    '识别窗口':'重新识别套版中的透明、黑色或白色窗口；识别后可手动调整坐标。',
    '添加多套版':'选择多张套版，每条视频会按各套版分别导出一份；默认使用套版原始尺寸。',
    '添加套版':'在当前多套版组合中添加本机图片，每个套版单独导出；窗口、尺寸与语言分别核对。',
    '应用整批语言':'将所选语言应用到当前核对表的所有输出行，并重新生成文件名；自动识别会逐行读取套版名和视频名。',
    '管理':'查看、编辑或移除多套版组合中的项目，每个项目有独立的窗口与输出尺寸。',
    '保存套版参数':'先在多套版管理中选择编辑项目，再保存当前窗口、尺寸、适配和缩放。',
    '保存输出模板':'把当前多套版组合、窗口、画质、命名及输出目录保存为本机输出模板。',
    '使用':'读取所选输出模板，恢复它保存的多套版组合和参数。',
    '删除':'移除所选的本机输出模板定义，不删除图片、视频或成片。',
    '开始批量导出':'检查套版和边缘后显示输出清单；确认文件名和去向后开始编码。',
    '确认并导出':'按核对表中的名称、模板、尺寸和保存位置开始导出。同名文件自动编号。',
    '取消当前操作':'取消正在进行的预检或编码；保留已完成成片并清理未完成输出。',
    '继续未完成':'使用原任务固定版本和参数继续失败或取消的条目，跳过已完成结果。',
    '检查更新':'查询公开稳定版本；发现新版后，需再次点击更新按钮才会下载和安装。',
    '应用名称':'把输入框中的 .mp4 文件名应用到当前选中的输出行。',
    '返回修改':'关闭核对表返回工作台，不启动导出。',
    '选择文件夹':'选择本机导出目录；可结合项目或套版分类规则创建子目录。',
    '打开':'在本机打开导出文件夹或已生成的视频。',
    '放大预览':'收起输出队列，让画面占用更多高度；再次点击恢复。',
    '跟随系统':'跟随 Windows 的应用浅色或深色主题，并自动响应后续系统主题变化。',
    '浅色':'使用浅色卡片、浅灰背景和深色文字；不修改 Windows 系统设置。',
    '深色':'使用深色卡片和浅色文字；不修改 Windows 系统设置。',
}

OPTION_HELP = {
    '模板与窗口':'选择套版并设置视频在图片中的初始对齐区域；一个视频可按多个套版分别输出。',
    '输出模板':'将当前套版组合、窗口、画质和命名设置保存为本机配方，之后可一键恢复。',
    '画面预览':'查看当前视频与当前套版的合成画面；右上角可切换不同套版，右下角可放大。',
    '套版工作台':'管理视频队列、套版参数、时间轴预览与批量导出。',
    '外观':'选择浅色、深色或跟随 Windows 系统主题。',
    '项目匹配参考':'显示当前视频项目、排期上级公司游戏与本机套版路径的对应关系。',
    '缩放比例':'相对等比填满大小的百分比，100% 为基准，10～500%；视频保持居中。',
    '输出语言':'只影响输出文件名的语言标签，不翻译视频、字幕或模板文字。',
    '输出分类':'选择所有成片写入同一文件夹，或按项目、套版创建子文件夹。',
    '统一文件夹':'将所有成片写入保存位置，不另外按项目或套版建文件夹。',
    '按项目分文件夹':'在保存位置下，以匹配到的项目名称建立子文件夹。',
    '按模板分文件夹':'在保存位置下，以套版文件名建立子文件夹。',
    '存为预设':'将当前命名表达式保存为一个本机命名预设，可填写自己的预设名称。',
    '删除预设':'移除当前所选命名预设；保留源视频、套版图片与成片。',
    '选择目录':'打开文件夹选择窗口；选择后保存为此电脑的路径设置。',
    '画面适配':'决定视频如何放进套版窗口：可等比填满、完整显示、拉伸或自由缩放。',
    '等比填满（推荐）':'保持视频比例并填满套版窗口；超出窗口的画面可能被裁切或覆盖模板装饰。',
    '完整显示':'保持视频比例并完整显示画面；比例不一致时会露出底层套版，可能形成留边。',
    '拉伸铺满':'把视频宽高分别拉伸至窗口大小；不留边，但人物和物体可能变形。',
    '自由缩放':'以等比填满为基准，按缩放百分比调整；可放大裁切或缩小露出套版。',
    '窗口识别类型':'指定识别套版图片里的透明、黑色或白色区域，用作视频初始对齐窗口。',
    '自动（透明 / 黑 / 白）':'依次尝试套版中的透明、黑色和白色窗口；识别不正确时可手动选择类型或修改坐标。',
    '仅黑色区域':'将套版中的黑色矩形区域识别为视频窗口。',
    '仅白色区域':'将套版中的白色矩形区域识别为视频窗口。',
    '仅透明区域':'将 PNG 等图片中的透明区域识别为视频窗口。',
    '关键词匹配路径':'开启后，用视频项目名查找排期，再按上级公司的游戏名匹配本机套版路径。关闭后按手动选择的套版处理。',
    '导出尺寸与比例':'选择成片的宽高与比例；跟随套版使用图片原始尺寸，自定义尺寸须为偶数。',
    '跟随套版':'使用当前套版图片原始宽高；奇数边长为兼容 H.264 向下调整 1 像素。',
    '窗口坐标':'X、Y 是窗口左上角坐标，宽、高是窗口尺寸，单位为套版原图像素；可直接输入或左右拖动。',
    '高画质':'采用较高质量的 H.264 编码，文件通常更大；不改变源视频实际细节。',
    '标准画质（文件更小）':'增加压缩以减小文件体积，细节可能有所损失。',
    '自动（优先 GPU）':'优先尝试 NVIDIA NVENC；不可用时回退 CPU 编码。',
    'NVIDIA GPU · NVENC':'强制使用 NVIDIA 硬件编码；无可用驱动或编码器时报告失败。',
    'CPU · 兼容模式':'使用 CPU 软件编码，兼容性较好，耗时取决于处理器性能。',
    '自动识别':'输出语言优先读取套版名称中的 EN、JP、CN 等标记，然后读取视频名；仅用于命名，不翻译内容。',
    '命名方式':'选择简洁命名、完整信息或自定义字段规则；自定义规则可保存为本机命名预设。',
    '精简命名':'清理原视频名称中的旧尺寸和版本标记，加入实际输出尺寸与语言。',
    '完整信息':'按原名、项目、语言和画面比例生成文件名。',
    '自定义规则':'组合 [原名]、[项目]、[套版]、[尺寸] 等字段，自定分隔符、顺序和编号。',
    '任务排期目录':'从此根目录查找与视频名对应的项目文件夹，可使用本地路径、映射盘或网络共享路径。',
    '本机套版根目录':'在此目录下按排期中的公司与游戏名寻找输出套版，可包含不同版式和语言子目录。',
    '时间轴':'拖动定位视频；红色为黑边、金色为白边、紫色为疑似暗缝。未拖动时悬停色段查看详情。',
    '画质':'控制导出压缩质量与文件大小；输出分辨率由尺寸设置决定。',
    '保存位置':'成片写入本机此目录；同名文件自动编号，不覆盖已有成片。',
    '套版预览':'切换当前视频的不同套版预览，每张套版使用独立的窗口、输出尺寸与适配设置。',
    '展开列表':'展开视频队列以显示更多行；滚轮与右侧滚动条可浏览所有素材。',
    '收起列表':'缩小视频队列，为画面预览保留更多空间。',
    '检查此帧':'检查当前时间点，发现的黑边、白边或暗缝将标在时间轴上。',
    '设置':'设置本机路径、窗口识别、编码、命名预设和匹配开关。',
}


def option_help(text):
    if text in OPTION_HELP:
        return OPTION_HELP[text]
    if text in HELP:
        return HELP[text]
    for label in sorted(OPTION_HELP,key=len,reverse=True):
        if label in str(text):
            return OPTION_HELP[label]
    return str(text)


def button_help(text):
    normalized=text.replace('＋','').replace('→','').strip()
    for label in sorted(HELP,key=len,reverse=True):
        if label in normalized:
            return HELP[label]
    return f'{normalized}。此操作只作用于当前本机软件和任务。'
