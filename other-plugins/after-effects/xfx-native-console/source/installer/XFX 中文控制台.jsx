#target aftereffects
// XFX Chinese toolbar. All icons and code are original; no FX Console assets are embedded.
(function (thisObj) {
    var globalObject = $.global;
    var registry = globalObject.__XFXConsoleCNPanels;
    if (!registry) registry = globalObject.__XFXConsoleCNPanels = {next: 1, clients: {}};
    registry.poll = function (id) {
        var client = registry.clients[id];
        if (client) client.poll();
    };
    registry.appearance = function (id) {var c=registry.clients[id];if(c)c.appearance();};
    registry.animate = function (id) { var c = registry.clients[id]; if(c) c.animate(); };
    registry.dismiss = function (id) { var c = registry.clients[id]; if(c) c.dismiss(); };
    var clientId = registry.next++;
    var docked = thisObj instanceof Panel;
    // The original palette starts at [100,100,500,600]; only the docked
    // Panel is constrained to the compact 182x78 toolbar and status footprint.
    var panel = docked ? thisObj : new Window("palette", "效果控制台",
        [100, 100, 500, 600], {resizeable: true});
    if (docked) {
        panel.preferredSize = [182, 78];
        panel.minimumSize = [182, 78];
    }
    panel.margins = [5, 8, 5, 8];
    var toolbar = panel.add("group", [5, 8, 177, 48]);
    toolbar.preferredSize = [172, 40];
    toolbar.minimumSize = [172, 40];
    var buttons = [];
    var positions = [0, 44, 88, 132];
    var commands = ["screenshot", "export", "preferences", "logo"];
    var tips = ["保存当前合成快照", "导出当前合成帧", "设置", "关于 效果控制台"];
    var ready = false, closed = false, pendingId = 0, taskId = 0, pendingSince = 0;
    var libraryPath = "";
    var appearance = {background:[32/255,32/255,32/255,1], ink:[176/255,176/255,176/255,1],
        accent:[20/255,111/255,203/255,1], pressed:[68/255,68/255,68/255,1]};
    var lastAppearanceCheck = -1000;
    var waves = [[],[],[],[]], animationTask = 0, dismissTask = 0, appearanceTask = 0, reduceMotion = false;
    var statusLabel = panel.add("statictext", [29, 53, 177, 73], "就绪");
    try {
        // ScriptUI font sizes are points: 9pt/7.5pt correspond to 12px/10px at 96 DPI.
        panel.graphics.font=ScriptUI.newFont("Microsoft YaHei UI","REGULAR",9);
        statusLabel.graphics.font=ScriptUI.newFont("Microsoft YaHei UI","REGULAR",7.5);
    } catch(unsupportedFont) {}
    function now() { return (new Date()).getTime(); }
    function blend(a,b,t) { return [a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t,1]; }
    function repaint() {
        if(closed) return;
        try { panel.notify("onDraw"); for(var i=0;i<buttons.length;++i) buttons[i].notify("onDraw"); }
        catch(ignored) { /* Host closing or a control has been disposed. */ }
    }
    function status(text,success) {
        if(closed) return;
        if(dismissTask) { try { app.cancelTask(dismissTask); } catch(ignored){} dismissTask=0; }
        try { statusLabel.text=text; } catch(invalidControl) { dispose(); return; }
        if(success) dismissTask=app.scheduleTask("$.global.__XFXConsoleCNPanels.dismiss("+clientId+");",2000,false);
        repaint();
    }
    function dismiss() { dismissTask=0; status("就绪",false); }
    function startWave(index,x,y) {
        if(closed || !buttons[index].enabled || reduceMotion) return;
        var list=waves[index]; if(list.length>=12) list.shift();
        list.push({x:x,y:y,time:now()});
        if(!animationTask) animationTask=app.scheduleTask("$.global.__XFXConsoleCNPanels.animate("+clientId+");",33,false);
        repaint();
    }
    function animate() {
        animationTask=0; if(closed) return;
        var active=false,stamp=now();
        for(var i=0;i<waves.length;++i) {
            for(var j=waves[i].length-1;j>=0;--j) if(stamp-waves[i][j].time>=700) waves[i].splice(j,1);
            active=active || waves[i].length>0;
        }
        repaint();
        if(active) animationTask=app.scheduleTask("$.global.__XFXConsoleCNPanels.animate("+clientId+");",33,false);
    }

    function setEnabled(enabled) {
        try {
            for (var i = 0; i < buttons.length; ++i) buttons[i].enabled = enabled;
            return true;
        } catch (invalidControl) {
            dispose();
            return false;
        }
    }
    function showError(message) { if (!closed) { status(message,false); alert("效果控制台\n" + message); } }
    function validInteger(text, minimum) {
        var number = Number(text);
        return /^[0-9]+$/.test(text) && isFinite(number) && Math.floor(number) === number &&
            number >= minimum && number <= 9007199254740991;
    }
    function parseReply(text) {
        if (typeof text !== "string") throw Error("原生插件返回了无法识别的结果，请更新配套组件。");
        var fields = text.split("\t");
        if (fields[0] !== "XFX1") throw Error("面板和原生插件版本不匹配，请重新安装。");
        var state = fields[1];
        if(state==="MOTION" && fields.length===3 && /^(0|1)$/.test(fields[2])) return {state:state,reduced:fields[2]==="1"};
        if (state === "THEME" && fields.length === 7 && /^(dark|light)$/.test(fields[2]) &&
            /^[0-9A-F]{6}$/.test(fields[3]) && /^[0-9A-F]{6}$/.test(fields[4]) &&
            /^[0-9A-F]{6}$/.test(fields[5]) && /^[0-9A-F]{6}$/.test(fields[6])) {
            function rgb(hex) { return [parseInt(hex.substr(0,2),16)/255,parseInt(hex.substr(2,2),16)/255,
                parseInt(hex.substr(4,2),16)/255,1]; }
            return {state:state, background:rgb(fields[3]), ink:rgb(fields[4]), accent:rgb(fields[5]), pressed:rgb(fields[6])};
        }
        if (state === "READY" && fields.length === 3 && validInteger(fields[2], 10003) && Number(fields[2]) < 20000) {
            return {state: state, version: Number(fields[2])};
        }
        if ((state === "QUEUED" || state === "PENDING") && fields.length === 3 && validInteger(fields[2], 1)) {
            return {state: state, id: Number(fields[2])};
        }
        if (state === "OK" && fields.length === 5 && validInteger(fields[2], 1) && validInteger(fields[3], 0)) {
            return {state: state, id: Number(fields[2]), message: fields[4]};
        }
        if (state === "ERROR" && fields.length === 5 && validInteger(fields[2], 0)) {
            return {state: state, id: Number(fields[2]), code: fields[3], message: fields[4]};
        }
        throw Error("原生插件返回了不完整的结果，请重试。");
    }
    function invoke(command, argument) {
        var library;
        try {
            library = new ExternalObject("lib:" + libraryPath);
            var version = Number(library.version);
            if (!isFinite(version) || version < 10003 || version >= 20000) {
                throw Error("原生插件版本不兼容，请安装与此面板配套的版本。");
            }
            return parseReply(argument === undefined ? library.execute(command) : library.execute(command, argument));
        } finally {
            if (library) library.unload();
        }
    }
    function applyAppearance(reply) {
        if (reply.state !== "THEME") throw Error(reply.message || "无法读取原生插件的界面主题。");
        appearance = reply;
        panel.graphics.backgroundColor = panel.graphics.newBrush(panel.graphics.BrushType.SOLID_COLOR, appearance.background);
        toolbar.graphics.backgroundColor = toolbar.graphics.newBrush(toolbar.graphics.BrushType.SOLID_COLOR, appearance.background);
        statusLabel.graphics.foregroundColor=statusLabel.graphics.newPen(statusLabel.graphics.PenType.SOLID_COLOR,blend(appearance.background,appearance.ink,.55),1);
        lastAppearanceCheck = (new Date()).getTime();
    }
    function refreshAppearance() {
        if (!ready || closed) return;
        var elapsed = (new Date()).getTime() - lastAppearanceCheck;
        if (elapsed >= 0 && elapsed < 500) return;
        lastAppearanceCheck = (new Date()).getTime();
        try { applyAppearance(invoke("appearance")); } catch (ignored) { /* Keep the last valid palette during repaint. */ }
    }
    function appearanceTick() {
        appearanceTask=0;if(closed || !ready) return;
        try {
            var probe=panel.graphics,previous=appearance.background.join(",")+appearance.ink.join(",")+appearance.accent.join(",");
            refreshAppearance();
            if(previous!==appearance.background.join(",")+appearance.ink.join(",")+appearance.accent.join(",")) repaint();
            appearanceTask=app.scheduleTask("$.global.__XFXConsoleCNPanels.appearance("+clientId+");",500,false);
        } catch(invalidPanel) {dispose();}
    }
    function schedulePoll() {
        taskId = app.scheduleTask("$.global.__XFXConsoleCNPanels.poll(" + clientId + ");", 100, false);
        if (!isFinite(taskId) || taskId <= 0) throw Error("无法安排结果检查，操作已取消，请重试。");
    }
    function clearPending() {
        pendingId = 0; taskId = 0;
        setEnabled(ready && !closed);
    }
    function poll() {
        taskId = 0;
        if (closed || !pendingId) return;
        try {
            var reply = invoke("requeststatus", pendingId);
            if (reply.id !== pendingId) throw Error("原生插件的请求编号不匹配。");
            if (reply.state === "PENDING") {
                repaint();
                if ((new Date()).getTime() - pendingSince > 30000) {
                    var cancellation = invoke("cancel", pendingId);
                    if (cancellation.code !== "running") {
                        clearPending();
                        if (cancellation.code !== "cancelled") showError(cancellation.message);
                        else status("操作已取消",false);
                        return;
                    }
                }
                schedulePoll(); return;
            }
            clearPending();
            if (reply.state !== "OK" && reply.code !== "cancelled") showError(reply.message || "操作失败，请重试。");
            else status(reply.code === "cancelled" ? "操作已取消" : (reply.message || "操作已完成"),reply.state === "OK");
        } catch (error) {
            if (pendingId) {
                try { invoke("cancel", pendingId); } catch (ignored) {}
            }
            clearPending(); showError(error.toString());
        }
    }
    function submit(command) {
        if (!ready || closed || pendingId) return;
        try {
            var reply = invoke(command);
            if (reply.state !== "QUEUED") { showError(reply.message || "操作没有提交，请重试。"); return; }
            pendingId = reply.id;
            pendingSince = (new Date()).getTime();
            status("正在处理…",false);
            if (!setEnabled(false)) return;
            schedulePoll();
        } catch (error) {
            if (pendingId) {
                try { invoke("cancel", pendingId); } catch (ignored) {}
            }
            clearPending(); showError(error.toString());
        }
    }
    function dispose() {
        closed = true;
        if(appearanceTask) {try {app.cancelTask(appearanceTask);}catch(ignoredAppearance){} appearanceTask=0;}
        if(animationTask) { try {app.cancelTask(animationTask);} catch(ignoredMotion){} }
        if(dismissTask) { try {app.cancelTask(dismissTask);} catch(ignoredDismiss){} }
        animationTask=0; dismissTask=0; waves=[[],[],[],[]];
        if (taskId) { try { app.cancelTask(taskId); } catch (ignored) {} }
        if (pendingId) { try { invoke("cancel", pendingId); } catch (ignoredAgain) {} }
        taskId = 0; pendingId = 0;
        delete registry.clients[clientId];
        return true;
    }
    function paintIcon(g,index,state,originX,originY) {
        var button=buttons[index], enabled=button ? button.enabled : true;
        var normal=blend(appearance.background,appearance.pressed,.35);
        var background=state && (state.mouseOver || state.leftButtonPressed) && enabled ? appearance.pressed : normal;
        var inkColor=enabled ? state && state.mouseOver ? blend(appearance.ink,[1,1,1,1],.35) : appearance.ink : blend(appearance.background,appearance.ink,.4);
        var ink=g.newPen(g.PenType.SOLID_COLOR,inkColor,1.5);
        function roundRect(color) {
            // Original path, 3px corners. No font glyphs or OS icons.
            g.newPath();
            var centers=[[37,3],[37,37],[3,37],[3,3]];
            for(var corner=0;corner<4;++corner) for(var n=0;n<=6;++n) {
                var angle=(-90+corner*90+n*15)*Math.PI/180;
                var xx=originX+centers[corner][0]+Math.cos(angle)*3,yy=originY+centers[corner][1]+Math.sin(angle)*3;
                if(corner===0 && n===0)g.moveTo(xx,yy);else g.lineTo(xx,yy);
            }
            g.lineTo(originX+37,originY);
            g.fillPath(g.newBrush(g.BrushType.SOLID_COLOR,color));
            g.strokePath(g.newPen(g.PenType.SOLID_COLOR,state && state.hasFocus ? appearance.accent : blend(background,appearance.ink,.22),state && state.hasFocus ? 2 : 1));
        }
        roundRect(background);
        if(!reduceMotion) for(var w=0;w<waves[index].length;++w) {
            var wave=waves[index][w],t=Math.max(0,Math.min(1,(now()-wave.time)/700));
            var dx=Math.max(wave.x,40-wave.x),dy=Math.max(wave.y,40-wave.y),radius=Math.sqrt(dx*dx+dy*dy)*(1-Math.pow(1-t,3));
            var color=blend(background,appearance.accent,.36*(1-t));
            // ScriptUI has no clip API. Horizontal spans clip the expanding
            // circle to the same inset rounded button, beneath the vector icon.
            for(var y=1;y<39;++y) if(Math.abs(y-wave.y)<radius) {
                var half=Math.sqrt(radius*radius-(y-wave.y)*(y-wave.y));
                var inset=y<3 || y>37 ? 3 : 1;
                var left=Math.max(inset,wave.x-half),right=Math.min(40-inset,wave.x+half);
                if(right>left) {g.newPath();g.rectPath(originX+left,originY+y,right-left,1);g.fillPath(g.newBrush(g.BrushType.SOLID_COLOR,color));}
            }
        }
        function rectangle(x,y,w,h) {g.newPath();g.rectPath(originX+20+x,originY+20+y,w,h);g.strokePath(ink);}
        function circle(x,y,w) {g.newPath();g.ellipsePath(originX+20+x,originY+20+y,w,w);g.strokePath(ink);}
        function line(points) {g.newPath();g.moveTo(originX+20+points[0][0],originY+20+points[0][1]);for(var j=1;j<points.length;++j) g.lineTo(originX+20+points[j][0],originY+20+points[j][1]);g.strokePath(ink);}
        if(index===0) {rectangle(-8.25,-4.25,16.5,11.5);rectangle(-4.25,-7.25,7.5,3);circle(-3,-2,6);}
        else if(index===1) {line([[0,-8.25],[0,4]]);line([[-5,-1],[0,4],[5,-1]]);line([[-8.25,3],[-8.25,8.25],[8.25,8.25],[8.25,3]]);}
        else if(index===2) {
            var gear=[];
            for(var i=0;i<32;++i) {var angle=(i-.5)*Math.PI/16,r=i%4===1 || i%4===2 ? 8.25 : 6.25;gear.push([Math.cos(angle)*r,Math.sin(angle)*r]);}
            gear.push(gear[0]);line(gear);circle(-2.75,-2.75,5.5);
        } else {circle(-8.25,-8.25,16.5);line([[0,-1],[0,5]]);rectangle(-.5,-5,1,1);}
    }
    function drawIcon(index,state) { refreshAppearance(); paintIcon(this.graphics,index,state,0,0); }
    function addButton(index) {
        var x=positions[index];
        var button=toolbar.add("iconbutton",[x,0,x+40,40],undefined,{style:"toolbutton"});
        button.preferredSize=[40,40];button.minimumSize=[40,40];button.helpTip=tips[index];button.enabled=false;
        button.onDraw=function(state){drawIcon.call(this,index,state);};
        button.addEventListener("mousedown",function(event){startWave(index,event.clientX,event.clientY);});
        button.addEventListener("keydown",function(event){if(event.keyName==="Space" || event.keyName==="Enter") startWave(index,20,20);});
        button.onClick=function(){
            if(!ready || closed || pendingId) return;
            if(!waves[index].length || now()-waves[index][waves[index].length-1].time>150) startWave(index,20,20);
            submit(commands[index]);
        };
        buttons.push(button);
    }
    for (var index = 0; index < commands.length; ++index) addButton(index);
    panel.onDraw = function () {
        refreshAppearance();
        var g = this.graphics;
        var brush = g.newBrush(g.BrushType.SOLID_COLOR, appearance.background);
        var width = this.size && this.size.length ? this.size[0] : 182;
        var height = this.size && this.size.length ? this.size[1] : 78;
        g.newPath(); g.rectPath(0, 0, width, height); g.fillPath(brush);
        if(pendingId) {
            var angle=reduceMotion ? 0 : (now()-pendingSince)%750/750*Math.PI*2;
            var track=g.newPen(g.PenType.SOLID_COLOR,blend(appearance.background,appearance.ink,.3),2);
            var active=g.newPen(g.PenType.SOLID_COLOR,appearance.accent,2);
            g.newPath();g.ellipsePath(8,55,12,12);g.strokePath(track);
            g.newPath();for(var n=0;n<=12;++n) {var a=angle-Math.PI/2+n/12*Math.PI*.6;var xx=14+Math.cos(a)*6,yy=61+Math.sin(a)*6;if(n===0)g.moveTo(xx,yy);else g.lineTo(xx,yy);}g.strokePath(active);
            g.newPath();g.rectPath(5,74,172,3);g.fillPath(g.newBrush(g.BrushType.SOLID_COLOR,blend(appearance.background,appearance.ink,.25)));
            var bar=65,left=reduceMotion ? 5 : 5-bar+(now()-pendingSince)%1100/1100*(172+bar);
            var end=Math.min(177,left+bar);left=Math.max(5,left);
            if(end>left) {g.newPath();g.rectPath(left,74,end-left,3);g.fillPath(g.newBrush(g.BrushType.SOLID_COLOR,appearance.accent));}
        }
        // Some AE hosts repaint the Panel without calling its child buttons.
        for (var i = 0; i < commands.length; ++i) {
            paintIcon(g, i, null, toolbar.bounds[0] + positions[i], toolbar.bounds[1]);
        }
    };
    registry.clients[clientId] = {poll:poll,animate:animate,dismiss:dismiss,appearance:appearanceTick};
    panel.onClose = dispose;
    panel.onResizing = panel.onResize = function () { toolbar.bounds = [5, 8, 177, 48]; repaint(); };
    panel.layout.layout(true);
    panel.onResize();
    try {
        var support = File($.fileName).parent.parent.parent;
        var candidates = [support.fsName + "/Plug-ins/XFXNativeConsole.aex",
            Folder.startup.fsName + "/Plug-ins/XFXNativeConsole.aex"];
        for (var candidate = 0; candidate < candidates.length; ++candidate) {
            if (File(candidates[candidate]).exists) { libraryPath = candidates[candidate]; break; }
        }
        if (!libraryPath) throw Error("没有找到 XFXNativeConsole.aex，请先使用完整安装器安装并重新打开 AE。");
        var nativeStatus = invoke("requeststatus");
        if (nativeStatus.state !== "READY") throw Error(nativeStatus.message || "原生插件尚未就绪，请重新打开 AE。");
        applyAppearance(invoke("appearance"));
        try { var motion=invoke("motion"); reduceMotion=motion.reduced; } catch(ignoredMotion) {}
        ready = true; setEnabled(true); appearanceTick();
    } catch (error) { showError(error.toString()); dispose(); }
    if (!docked) {
        panel.center();
        panel.show();
        try { panel.update(); } catch (ignored) {}
    }
})(this);
