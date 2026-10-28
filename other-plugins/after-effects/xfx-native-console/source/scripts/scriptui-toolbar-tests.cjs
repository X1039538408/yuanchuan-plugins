const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../installer/XFX 中文控制台.jsx'), 'utf8')
    .replace(/^\uFEFF/, '').replace(/^#target[^\r\n]*/gm, '');
let checks = 0;
function fixture(options = {}) {
    const calls = [], objects = [], tasks = new Map(), motionTasks = new Map(), toastTasks = new Map(), appearanceTasks = new Map(), cancelled = [], alerts = [];
    let serial = 0, unloaded = 0;
    function installBounds(item, initial) {
        let bounds;
        Object.defineProperty(item, 'bounds', {enumerable:true, configurable:true,
            get(){return bounds;},
            set(value){
                if (!options.strictBounds) { bounds=value; return; }
                // AE Bounds exposes numeric coordinates but rejects implicit conversion.
                bounds={0:value[0],1:value[1],2:value[2],3:value[3],length:4,
                    toString(){throw TypeError('Expected Array of Numbers, found Bounds.');},
                    valueOf(){throw TypeError('Expected Array of Numbers, found Bounds.');}};
            }});
        item.bounds=initial;
    }
    function widget(kind, parent) {
        const item = {kind, parent, children: [], enabled: true, size: [0,0],
            bounds: [0,0,0,0], layout: {layout() {}, resize() {}},
            graphics: {PenType:{SOLID_COLOR:0},BrushType:{SOLID_COLOR:0},pens:[],brushes:[],geometry:[],
                drawOSControl(){},newPen(type,color){this.pens.push(Array.from(color));return {};},
                newBrush(type,color){this.brushes.push(Array.from(color));return {};},
                newPath(){return {};},
                rectPath(...values){this.geometry.push(['rectPath',...values]);},
                ellipsePath(...values){this.geometry.push(['ellipsePath',...values]);},
                moveTo(...values){this.geometry.push(['moveTo',...values]);},
                lineTo(...values){this.geometry.push(['lineTo',...values]);},
                strokePath(){},fillPath(){},
                drawString(text,pen,x,y){this.geometry.push(['drawString',text,x,y]);}},
            add(type, bounds, text) { const child=widget(type,this); child.bounds=bounds || [0,0,0,0]; child.text=text;
                this.children.push(child); objects.push(child); return child; },
            addEventListener(name,handler){ if(!this.events)this.events={};this.events[name]=handler; },
            notify(name){if(this[name]) this[name]({});},
            show(){this.shown=true;},center(){this.centered=true;},update(){this.updated=true;},
            close(){ if (this.onClose) return this.onClose(); }};
        installBounds(item,item.bounds);
        return item;
    }
    function Panel() { Object.assign(this, widget('panel',null)); installBounds(this,this.bounds); }
    function Window(kind,title,bounds) {
        Object.assign(this,widget(kind,null));
        this.title=title;
        installBounds(this,bounds || this.bounds);
        objects.push(this);
    }
    function File(filename) {
        if (!(this instanceof File)) return new File(filename);
        this.fsName=filename; this.exists=options.missing ? false : /Plug-ins[/\\]XFXNativeConsole\.aex$/.test(filename);
        const bits=filename.replace(/\\/g,'/').split('/'); bits.pop();
        this.parent=folder(bits.join('/'));
    }
    function folder(name) { const bits=name.split('/'); bits.pop(); return {fsName:name,
        get parent(){return folder(bits.join('/'));}}; }
    const fakeDate = function(){this.getTime=()=>options.time || 0;};
    function ExternalObject(library) {
        this.version = options.version === undefined ? 10003 : options.version;
        this.execute = function(command, argument) {
            calls.push({command, argument, library});
            if (options.throwOn === command) throw Error('native failure');
            if (command === 'requeststatus' && argument === undefined)
                return options.unavailable ? 'XFX1\tERROR\t0\tunavailable\t原生插件未就绪' : 'XFX1\tREADY\t10003';
            if (command === 'motion') return 'XFX1\tMOTION\t'+(options.reducedMotion?'1':'0');
            if (command === 'appearance') return options.appearanceReply || (options.lightTheme ?
                'XFX1\tTHEME\tlight\tEBEBEB\t505050\t0066B8\tCFCFCF' : 'XFX1\tTHEME\tdark\t2B2B2B\tC8C8C8\t005CAA\t5A5A5A');
            if (command === 'requeststatus') return options.pending ? `XFX1\tPENDING\t${argument}` :
                (options.failure ? `XFX1\tERROR\t${argument}\thost_error\t没有活动合成` : `XFX1\tOK\t${argument}\t1\t已完成`);
            if (command === 'cancel') return `XFX1\tERROR\t${argument}\tcancelled\t已取消`;
            return options.invalidReply || 'XFX1\tQUEUED\t7';
        };
        this.unload = ()=>{unloaded++;};
    }
    const context={Panel,Window,File,Folder:{startup:folder('C:/Adobe/Support Files')},ExternalObject,
        app:{scheduleTask(script,delay,repeat){if(options.scheduleFail)return 0; const id=++serial; const target=script.includes('.animate(')?motionTasks:script.includes('.dismiss(')?toastTasks:script.includes('.appearance(')?appearanceTasks:tasks;
                target.set(id,{script,delay,repeat}); return id;},
            cancelTask(id){cancelled.push(id);tasks.delete(id);motionTasks.delete(id);toastTasks.delete(id);appearanceTasks.delete(id);}},
        $:{fileName:'C:/中文路径/Support Files/Scripts/ScriptUI Panels/效果控制台.jsx',global:{}},
        alert(text){alerts.push(String(text));},Date:fakeDate,Math,Number,String,isFinite};
    const panel = options.docked ? new Panel() : null;
    context.__panel=panel;
    vm.createContext(context);
    vm.runInContext(`(function(){${source}\n}).call(__panel || this);`, context);
    function runTask() {
        const entry=tasks.entries().next().value; assert(entry,'a poll task must exist');
        tasks.delete(entry[0]); vm.runInContext(entry[1].script,context);
    }
    function runMotion() {const entry=motionTasks.entries().next().value; assert(entry);motionTasks.delete(entry[0]);vm.runInContext(entry[1].script,context);}
    return {context, panel, calls, objects, tasks,motionTasks,toastTasks,appearanceTasks,cancelled,alerts,runTask,runMotion,
        get unloaded(){return unloaded;},
        button(tooltip){return objects.find(item=>item.helpTip===tooltip);}};
}
function test(name, action) { action(); ++checks; console.log('PASS '+name); }
test('palette and docked Panel have four compact actions without a gallery entry', ()=>{
    for (const docked of [false,true]) {
        const f=fixture({docked});
        if (!docked) assert.deepEqual(Array.from(f.objects.find(item=>item.kind==='palette').bounds),[100,100,500,600]);
        const buttons=f.objects.filter(item=>item.kind==='iconbutton');
        assert.deepEqual(buttons.map(item=>item.helpTip),['保存当前合成快照','导出当前合成帧','设置','关于 效果控制台']);
        assert.deepEqual(buttons.map(item=>Array.from(item.bounds)),[[0,0,40,40],[44,0,84,40],[88,0,128,40],[132,0,172,40]]);
        assert.equal(f.button('打开快照图库'),undefined);
        const toolbar=f.objects.find(item=>item.kind==='group');
        assert.deepEqual(Array.from(toolbar.bounds),[5,8,177,48]);
        assert.deepEqual(Array.from(toolbar.preferredSize),[172,40]);
        if (docked) {
            assert.deepEqual(Array.from(f.panel.preferredSize),[182,78]);
            assert.deepEqual(Array.from(f.panel.minimumSize),[182,78]);
        }
        assert.equal(f.objects.filter(item=>item.shown).length,docked?0:1);
        assert(f.calls[0].library.includes('中文路径/Support Files/Plug-ins/XFXNativeConsole.aex'));
        assert.equal(f.unloaded,f.calls.length);
    }
});
test('strict AE Bounds cannot interrupt palette initialization before its first refresh', ()=>{
    let f;
    assert.doesNotThrow(()=>{f=fixture({strictBounds:true});});
    const palette=f.objects.find(item=>item.kind==='palette');
    assert.equal(palette.shown,true);
    assert.equal(palette.updated,true);
    assert.deepEqual(Array.from(palette.bounds),[100,100,500,600]);
    assert.equal(f.alerts.length,0);
    f.button('保存当前合成快照').onClick();
    assert.equal(f.calls.at(-1).command,'screenshot');
    f.runTask();
    assert.equal(f.button('保存当前合成快照').enabled,true);
});
test('four surviving actions retain their matching vector icons and bridge commands', ()=>{
    const expected=[
        {command:'screenshot',geometry:['rectPath',11.75,15.75,16.5,11.5]},
        {command:'export',geometry:['moveTo',20,11.75]},
        {command:'preferences',geometry:['ellipsePath',17.25,17.25,5.5,5.5]},
        {command:'logo',geometry:['ellipsePath',11.75,11.75,16.5,16.5]}
    ];
    for (const docked of [false,true]) {
        const f=fixture({docked,strictBounds:true});
        const buttons=f.objects.filter(item=>item.kind==='iconbutton');
        assert.equal(buttons.length,4);
        for (let index=0; index<buttons.length; ++index) {
            const button=buttons[index];
            button.onDraw({});
            assert(button.graphics.geometry.some(item=>JSON.stringify(item)===JSON.stringify(expected[index].geometry)),
                'wrong icon for '+expected[index].command);
            button.onClick();
            assert.equal(f.calls.at(-1).command,expected[index].command);
            f.runTask();
            assert.equal(button.enabled,true);
        }
        assert(!f.calls.some(call=>call.command==='gallery'));
    }
});
test('icon buttons retain a stable 40px size under ScriptUI auto-layout', ()=>{
    const f=fixture();
    for (const button of f.objects.filter(item=>item.kind==='iconbutton')) {
        assert.deepEqual(Array.from(button.preferredSize),[40,40]);
        assert.deepEqual(Array.from(button.minimumSize),[40,40]);
    }
});
test('the palette owns a repaint handler for hosts that do not paint child controls automatically', ()=>{
    const f=fixture();
    assert.equal(typeof f.objects.find(item=>item.kind==='palette').onDraw,'function');
});
test('parent repaint draws original vector symbols without font glyphs',()=>{
    for(const docked of [false,true]) {
        const f=fixture({docked,strictBounds:true});
        const parent=docked?f.panel:f.objects.find(item=>item.kind==='palette');
        const toolbar=f.objects.find(item=>item.kind==='group');
        toolbar.bounds=[11,13,183,53];
        parent.graphics.geometry.length=0; parent.onDraw();
        const geometry=parent.graphics.geometry;
        assert(geometry.some(item=>item[0]==='rectPath' && item[1]===22.75 && item[2]===28.75 && item[3]===16.5 && item[4]===11.5));
        assert(geometry.some(item=>item[0]==='ellipsePath' && item[3]===16.5));
        assert(!geometry.some(item=>item[0]==='drawString'));
        assert.equal(f.tasks.size,0);
    }
});
test('each button submits a finite command and only completion restores buttons', ()=>{
    const cases=[['保存当前合成快照','screenshot'],['导出当前合成帧','export'],['设置','preferences'],['关于 效果控制台','logo']];
    for(const [tooltip, command] of cases) {
        const f=fixture(); f.button(tooltip).onClick();
        assert.equal(f.calls.at(-1).command,command);
        assert.equal(f.button(tooltip).enabled,false);
        assert.equal(f.tasks.size,1);
        assert.equal(f.alerts.length,0);
        f.runTask();
        assert.equal(f.calls.at(-1).argument,7);
        assert.equal(f.button(tooltip).enabled,true);
        assert.equal(f.tasks.size,0);
        assert.equal(f.alerts.length,0);
        assert.equal(f.unloaded,f.calls.length);
    }
});
test('pending results keep polling and host errors show the actual Chinese message',()=>{
    const pending=fixture({pending:true}); pending.button('保存当前合成快照').onClick(); pending.runTask();
    assert.equal(pending.tasks.size,1); assert.equal(pending.button('保存当前合成快照').enabled,false);
    const failed=fixture({failure:true}); failed.button('保存当前合成快照').onClick(); failed.runTask();
    assert(failed.alerts.at(-1).includes('没有活动合成')); assert.equal(failed.button('保存当前合成快照').enabled,true);
});
test('missing, incompatible and uninitialized native plugin cannot enable actions',()=>{
    for(const option of [{missing:true},{version:1},{version:NaN},{unavailable:true}]) {
        const f=fixture(option);
        assert(f.alerts.length>0);
        assert(f.objects.filter(item=>item.kind==='iconbutton').every(item=>!item.enabled));
        assert(!f.calls.some(call=>call.command==='screenshot'));
        assert.equal(Object.keys(f.context.$.global.__XFXConsoleCNPanels.clients).length,0);
    }
});
test('unloading is guaranteed on invocation errors and invalid protocol never schedules',()=>{
    const thrown=fixture({throwOn:'screenshot'}); thrown.button('保存当前合成快照').onClick();
    assert.equal(thrown.unloaded,thrown.calls.length); assert.equal(thrown.tasks.size,0); assert(thrown.alerts.length);
    const bad=fixture({invalidReply:'XFX1\tQUEUED\tnot-a-number'}); bad.button('保存当前合成快照').onClick();
    assert.equal(bad.tasks.size,0); assert(bad.alerts.length);
});
test('closing a palette cancels a queued native action and its scheduled poll',()=>{
    const f=fixture({pending:true}); f.button('保存当前合成快照').onClick();
    const palette=f.objects.find(item=>item.kind==='palette'); palette.close();
    assert.equal(f.calls.at(-1).command,'cancel'); assert.equal(f.calls.at(-1).argument,7);
    assert.equal(f.tasks.size,0); assert.equal(f.cancelled.length,3);
    assert.equal(f.unloaded,f.calls.length);
});
test('failed scheduling cancels the native request and restores controls',()=>{
    const f=fixture({scheduleFail:true}); f.button('保存当前合成快照').onClick();
    assert.equal(f.calls.at(-1).command,'cancel'); assert.equal(f.tasks.size,0);
    assert.equal(f.button('保存当前合成快照').enabled,true); assert(f.alerts.length);
});
test('polling and rescheduling errors cancel the accepted request before restoring controls',()=>{
    for (const failure of ['requeststatus','schedule']) {
        const options={pending:true}; const f=fixture(options);
        f.button('保存当前合成快照').onClick();
        if (failure === 'requeststatus') options.throwOn='requeststatus';
        else options.scheduleFail=true;
        f.runTask();
        assert.equal(f.calls.at(-1).command,'cancel');
        assert.equal(f.calls.at(-1).argument,7);
        assert.equal(f.tasks.size,0);
        assert.equal(f.button('保存当前合成快照').enabled,true);
        assert(f.alerts.length);
        assert.equal(f.unloaded,f.calls.length);
    }
});
test('destroyed controls cannot leak a poll exception or a registered client',()=>{
    for (const failedPoll of [false,true]) {
        const options={}; const f=fixture(options);
        f.button('保存当前合成快照').onClick();
        const button=f.button('保存当前合成快照');
        Object.defineProperty(button,'enabled',{get(){return false;},set(){throw Error('Object is invalid');}});
        if (failedPoll) options.throwOn='requeststatus';
        assert.doesNotThrow(()=>f.runTask());
        assert.equal(Object.keys(f.context.$.global.__XFXConsoleCNPanels.clients).length,0);
        assert.equal(f.tasks.size,0);
        assert.equal(f.alerts.length,0);
        if (failedPoll) assert.equal(f.calls.at(-1).command,'cancel');
    }
});
test('destroying a control before submission cancels without scheduling',()=>{
    const f=fixture(); const button=f.button('保存当前合成快照');
    Object.defineProperty(button,'enabled',{get(){return false;},set(){throw Error('Object is invalid');}});
    assert.doesNotThrow(()=>button.onClick());
    assert.equal(f.calls.at(-1).command,'cancel');
    assert.equal(f.tasks.size,0);
    assert.equal(Object.keys(f.context.$.global.__XFXConsoleCNPanels.clients).length,0);
});
test('a queued request that exceeds the UI wait is cancelled',()=>{
    const options={pending:true,time:0}; const f=fixture(options);
    f.button('保存当前合成快照').onClick(); options.time=31001; f.runTask();
    assert.equal(f.calls.at(-1).command,'cancel'); assert.equal(f.tasks.size,0);
    assert.equal(f.button('保存当前合成快照').enabled,true);
});
test('multiple palettes retain independent polling registrations',()=>{
    const f=fixture(); const first=f.button('保存当前合成快照');
    vm.runInContext(`(function(){${source}\n}).call(this);`,f.context);
    const second=f.objects.filter(item=>item.kind==='iconbutton').at(-4);
    first.onClick(); second.onClick(); assert.equal(f.tasks.size,2);
    f.objects.find(item=>item.kind==='palette').close(); assert.equal(f.tasks.size,1);
    f.runTask(); assert.equal(second.enabled,true); assert.equal(f.tasks.size,0);
});
test('native appearance colors paint the toolbar without adding tasks or host actions',()=>{
    for (const parentOnly of [false,true]) {
        const options={lightTheme:true,time:0}; const f=fixture(options);
        const camera=parentOnly ? f.objects.find(item=>item.kind==='palette') : f.button('保存当前合成快照');
        camera.onDraw({});
        assert(camera.graphics.pens.some(c=>JSON.stringify(c)===JSON.stringify([80/255,80/255,80/255,1])));
        assert(camera.graphics.brushes.length>0);
        options.lightTheme=false; options.time=1001; camera.onDraw({});
        assert(camera.graphics.pens.some(c=>JSON.stringify(c)===JSON.stringify([200/255,200/255,200/255,1])));
        assert.equal(f.tasks.size,0);
        assert(f.calls.every(call=>call.command==='requeststatus' || call.command==='appearance' || call.command==='motion'));
        assert.equal(f.unloaded,f.calls.length);
    }
});
test('an old native version or malformed appearance cannot enable the toolbar',()=>{
    for(const options of [{version:10002},{appearanceReply:'XFX1\tTHEME\tlight\tZZZZZZ\t505050\t0066B8\tCFCFCF'}]) {
        const f=fixture(options);
        assert(f.alerts.length);
        assert(f.objects.filter(item=>item.kind==='iconbutton').every(item=>!item.enabled));
    }
});
test('appearance repaint failures are throttled and retain the last valid palette',()=>{
    for (const parentOnly of [false,true]) {
        const options={lightTheme:true,time:0}; const f=fixture(options);
        options.time=1001; options.throwOn='appearance';
        const before=f.calls.filter(call=>call.command==='appearance').length;
        const camera=parentOnly ? f.objects.find(item=>item.kind==='palette') : f.button('保存当前合成快照');
        camera.onDraw({}); camera.onDraw({});
        assert.equal(f.calls.filter(call=>call.command==='appearance').length-before,1);
        assert(camera.graphics.pens.some(c=>JSON.stringify(c)===JSON.stringify([80/255,80/255,80/255,1])));
        assert.equal(f.alerts.length,0); assert.equal(f.tasks.size,0);
    }
});
test('pointer ripple starts at click location, remains clipped and cleans up',()=>{
    const options={time:0};const f=fixture(options);const button=f.button('保存当前合成快照');
    button.events.mousedown({clientX:4,clientY:9});
    options.time=180;button.graphics.geometry.length=0;button.onDraw({});
    const spans=button.graphics.geometry.filter(x=>x[0]==='rectPath' && x[4]===1);
    assert(spans.length>5);assert(spans.every(x=>x[1]>=1 && x[1]+x[3]<=39 && x[2]>=1 && x[2]<39));
    const atPointer=spans.find(x=>x[2]===9); assert(atPointer && atPointer[1]===1);
    options.time=701;f.runMotion();assert.equal(f.motionTasks.size,0);
    button.enabled=false;button.events.mousedown({clientX:4,clientY:9});assert.equal(f.motionTasks.size,0);
});
test('keyboard ripple is centered and rapid clicks have one scheduler',()=>{
    const options={time:0};const f=fixture(options);const button=f.button('设置');
    for(let i=0;i<20;i++) button.events.keydown({keyName:'Enter'});
    assert.equal(f.motionTasks.size,1);options.time=100;button.graphics.geometry.length=0;button.onDraw({});
    const span=button.graphics.geometry.find(x=>x[0]==='rectPath' && x[2]===20 && x[4]===1);
    assert(span && Math.abs(span[1]+span[3]/2-20)<.01);
    options.time=800;f.runMotion();assert.equal(f.motionTasks.size,0);
});
test('loading spinner and indeterminate bar actually change geometry then stop',()=>{
    const options={pending:true,time:0};const f=fixture(options);const parent=f.objects.find(x=>x.kind==='palette');
    f.button('保存当前合成快照').onClick();
    function geometry(){parent.graphics.geometry.length=0;parent.onDraw();return JSON.stringify(parent.graphics.geometry);}
    const first=geometry();options.time=250;const second=geometry();assert.notEqual(first,second);
    assert(f.objects.find(x=>x.kind==='statictext').text.includes('正在处理'));
    options.pending=false;f.runTask();assert.equal(f.tasks.size,0);assert(f.objects.filter(x=>x.kind==='iconbutton').every(x=>x.enabled));
    assert.equal(f.toastTasks.size,1);const toast=f.toastTasks.entries().next().value;f.toastTasks.delete(toast[0]);vm.runInContext(toast[1].script,f.context);
    assert.equal(f.objects.find(x=>x.kind==='statictext').text,'就绪');
    options.time=801;f.runMotion();const done=geometry();options.time=1100;assert.equal(geometry(),done);
});
test('errors persist and closing cleans all visual and request timers',()=>{
    const options={failure:true,time:0};const f=fixture(options);f.button('保存当前合成快照').onClick();f.runTask();
    assert(f.objects.find(x=>x.kind==='statictext').text.includes('没有活动合成'));assert.equal(f.toastTasks.size,0);
    f.objects.find(x=>x.kind==='palette').close();assert.equal(f.motionTasks.size+f.toastTasks.size+f.tasks.size+f.appearanceTasks.size,0);
});
test('reduced motion retains task text while suppressing ripples',()=>{
    const f=fixture({reducedMotion:true,pending:true});const button=f.button('设置');button.events.mousedown({clientX:3,clientY:4});button.onClick();
    assert.equal(f.motionTasks.size,0);assert(f.objects.find(x=>x.kind==='statictext').text.includes('正在处理'));
});
test('idle appearance changes repaint without submitting a host operation',()=>{
    const options={time:0,lightTheme:true};const f=fixture(options);const parent=f.objects.find(x=>x.kind==='palette');
    assert.equal(f.appearanceTasks.size,1);options.time=501;options.lightTheme=false;
    const tick=f.appearanceTasks.entries().next().value;f.appearanceTasks.delete(tick[0]);vm.runInContext(tick[1].script,f.context);
    assert(f.calls.every(x=>['motion','appearance','requeststatus'].includes(x.command)));
    assert.equal(f.appearanceTasks.size,1);assert(parent.graphics.brushes.length>0);
    parent.close();assert.equal(f.appearanceTasks.size,0);
});
console.log('ScriptUI toolbar VM tests passed: '+checks);
