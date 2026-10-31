/* AE 智能分层：自包含、可直接运行、无需重启。Windows/AE25。
最多20个图片/预合成/原生文字源；自动显示可取消进度，整批成功后保留隐藏原层并选中全部输出。
原生点文字逐字生成可编辑文字层；图片/预合成当前帧识别文字及花瓣等非文字，输出带蒙版的图像层。
每源按左到右、下到上编号与堆叠；锚点居中且补偿画面位置。
要求无父级、无基本变换动画/表达式的静态二维及非分离Position；不支持的文字排版/依赖安全拒绝。
图像识别需脚本文件操作及Windows Script Host/PowerShell；不自动修改偏好、不保存工程。 */
(function () {

    function describe(e,where) {
        var message = '', line = '';
        try { if (e && e.message) message = String(e.message); } catch(ignoreMessage) {}
        try { if (!message && e) message = String(e); } catch(ignoreString) {}
        try { if (e && e.line) line = String(e.line); else if (e && e.lineNumber) line = String(e.lineNumber); } catch(ignoreLine) {}
        if (!message) message = 'AE 返回了空错误信息。';
        return '阶段：' + where + '\n错误：' + message + (line ? '\n行号：' + line : '');
    }
    function stamp(f) {
        try { var d = f.modified; if (d && typeof d.getTime === 'function') return d.getTime(); } catch(ignoreStamp) {}
        return -1;
    }
    function findPowershell() {
        var systemFolder = '';
        try { if (Folder.system && Folder.system.fsName) systemFolder = Folder.system.fsName; } catch(ignoreSystem) {}
        if (systemFolder) { var f = new File(systemFolder + '/WindowsPowerShell/v1.0/powershell.exe'); if (f.exists) return f; }
        var windows = ''; try { windows = $.getenv('SystemRoot') || $.getenv('WINDIR') || ''; } catch(ignoreEnv) {}
        return new File((windows || 'C:/Windows') + '/System32/WindowsPowerShell/v1.0/powershell.exe');
    }

    var stage = '初始化';
    try {
    var G = $.global;
    var BUILD = '2026.10.29-portable14-native-text';
    if(G.__ZExpSplitPortable&&(G.__ZExpSplitPortable.mutating||(G.__ZExpSplitPortable.active&&G.__ZExpSplitPortable.phase==='APPLYING'))){alert('正在生成图层，请等待完成或在进度窗口取消后重试。');return;}
    function retireKnown(slotName) {
        var old = null;
        try { old = G[slotName]; } catch(ignoreSlot) { return; }
        if (!old) return;
        var wasActive = false, task = null, jobPath = '';
        try { wasActive = !!old.active; } catch(ignoreActiveRead) {}
        try { old.active = false; old.phase = 'RETIRED'; } catch(ignoreDisable) {}
        try { task = old.task; old.task = null; } catch(ignoreTaskRead) {}
        try { if (typeof task === 'number') app.cancelTask(task); } catch(ignoreTaskCancel) {}
        if (wasActive) {
            try {if(old.progress&&old.progress.window){old.progress.closing=true;old.progress.window.close();}}catch(ignoreOldUI){}
            try {
                if (old.folder && old.folder.fsName) jobPath = String(old.folder.fsName);
                var base = jobPath.replace(/\\/g,'/').split('/').pop();
                if (/^ae_parts_[a-z0-9_]+$/i.test(base)) {
                    writeFile(new File(jobPath + '/cancel.txt'),'superseded');
                    var oldRecords=old.records||[];
                    for(var oi=0;oi<oldRecords.length;oi++)if(oldRecords[oi].folder)writeFile(new File(oldRecords[oi].folder.fsName+'/cancel.txt'),'superseded');
                }
            } catch(ignoreOldMarker) {}
        }
        // Do not execute old.cancel()/old.poll(); they may contain obsolete code.
    }
    stage = '替换旧版后台任务';
    retireKnown('__AEPartsBG');
    retireKnown('__ZExpImageSplitV3');
    retireKnown('__ZExpSplitPortable');
    if ($.os.toLowerCase().indexOf('windows') < 0) { alert('此版仅支持 Windows。'); return; }
    stage = '读取当前合成';
    var comp = app.project.activeItem;
    if (!(comp instanceof CompItem) || !comp.selectedLayers || comp.selectedLayers.length < 1) {
        alert('AE 智能分层：请在合成里选择图片、预合成或原生文字图层，可多选。'); return;
    }
    if(app.project.renderQueue&&app.project.renderQueue.rendering){alert('AE 正在渲染，请渲染结束后再运行 AE 智能分层。');return;}
    stage = '读取选中图层';
    var sources=[],selected=comp.selectedLayers;for(var ss=0;ss<selected.length;ss++)sources.push(selected[ss]);
    if(sources.length>20){alert('一次最多处理20个源图层，请减少选择后重试。');return;}
    var src=sources[0];
    function isStatic2D(L){
        if(L.threeDLayer||L.parent)return false;
        var position=L.property('ADBE Transform Group').property('ADBE Position');
        if(!position||position.dimensionsSeparated)return false;
        var names=['ADBE Anchor Point','ADBE Position','ADBE Scale','ADBE Rotate Z'];
        for(var ti=0;ti<names.length;ti++){var prop=L.property('ADBE Transform Group').property(names[ti]);if(!prop||prop.expressionEnabled||prop.numKeys>0)return false;}
        return true;
    }
    // BEGIN_NATIVE_TEXT
    // Native text stays native: trim rich TextDocuments, never mask a hidden full string.
    function isNativeText(L){return !!L&&L.matchName==='ADBE Text Layer';}
    function nativeProperty(L){return L.property('ADBE Text Properties').property('ADBE Text Document');}
    function nativeUnits(text){
        var units=[],i=0;
        while(i<text.length){
            var start=i,code=text.charCodeAt(i++);
            if(code>=0xD800&&code<=0xDBFF){var low=text.charCodeAt(i);if(!(low>=0xDC00&&low<=0xDFFF))throw Error('文字包含无效Unicode代理项。');i++;}
            else if(code>=0xDC00&&code<=0xDFFF)throw Error('文字包含无效Unicode代理项。');
            if(code===13&&text.charCodeAt(i)===10)i++;
            // Keep surrogate pairs, combining accents, selectors and emoji skin-tone modifiers with their base.
            if(code===0xD83C&&text.charCodeAt(start+1)>=0xDDE6&&text.charCodeAt(start+1)<=0xDDFF&&text.charCodeAt(i)===0xD83C&&text.charCodeAt(i+1)>=0xDDE6&&text.charCodeAt(i+1)<=0xDDFF)i+=2;
            while(i<text.length){var mark=text.charCodeAt(i),tail=text.charCodeAt(i+1);if((mark>=0x0300&&mark<=0x036F)||(mark>=0x1AB0&&mark<=0x1AFF)||(mark>=0x1DC0&&mark<=0x1DFF)||(mark>=0x20D0&&mark<=0x20FF)||(mark>=0xFE00&&mark<=0xFE0F)||(mark>=0xFE20&&mark<=0xFE2F))i++;else if((mark===0xD83C&&tail>=0xDFFB&&tail<=0xDFFF)||(mark===0xDB40&&tail>=0xDD00&&tail<=0xDDEF))i+=2;else break;}
            units.push({start:start,end:i,text:text.substring(start,i)});
        }
        return units;
    }
    function nativeValueString(value){
        if(value===undefined)return '?';if(value===null)return 'null';
        if(value instanceof Array){var a=[];for(var i=0;i<value.length;i++)a.push(nativeValueString(value[i]));return '['+a.join(',')+']';}
        return String(value);
    }
    function nativeStyleSignature(doc,start,end){
        var range=doc.characterRange(start,end),fields=['font','fontSize','tracking','horizontalScale','verticalScale','baselineShift','applyFill','fillColor','applyStroke','strokeColor','strokeWidth','strokeOverFill','fauxBold','fauxItalic','fontCapsOption','fontBaselineOption','ligature','tsume'],out=[];
        for(var i=0;i<fields.length;i++)try{out.push(fields[i]+':'+nativeValueString(range[fields[i]]));}catch(unsupportedStyle){out.push(fields[i]+':!');}
        return out.join('|');
    }
    function nativeSignature(L){
        var doc=nativeProperty(L).value,out=[doc.text],fields=['font','fontSize','tracking','kerning','autoKernType','horizontalScale','verticalScale','baselineShift','applyFill','fillColor','applyStroke','strokeColor','strokeWidth','strokeOverFill','fauxBold','fauxItalic','allCaps','smallCaps','fontCapsOption','fontBaselineOption','ligature','tsume','leading','autoLeading','justification','direction','lineOrientation','firstLineIndent','startIndent','endIndent','spaceBefore','spaceAfter','composerEngine','everyLineComposer','noBreak'];
        out.push(nativeValueString(doc.baselineLocs));
        for(var i=0;i<fields.length;i++){try{out.push(fields[i]+':'+nativeValueString(doc[fields[i]]));}catch(ignoreField){out.push(fields[i]+':!');}}
        // A Source Text string alone does not detect same-text styling edits.
        for(var n=0;n<doc.text.length;n++){
            var range=doc.characterRange(n,n+1);
            for(var f=0;f<fields.length;f++)try{out.push(nativeValueString(range[fields[f]]));}catch(ignoreRange){out.push('!');}
        }
        return out.join('|');
    }
    function nativeTextPreflight(L){
        var prop=nativeProperty(L),doc=prop.value,text=doc.text;
        if(prop.expressionEnabled||prop.numKeys>0)throw Error('文字层暂不支持来源文字关键帧或表达式：'+L.name);
        if(!doc.pointText||doc.boxText)throw Error('文字层请先转换为点文字；暂不支持段落文本框：'+L.name);
        if(typeof doc.characterRange!=='function')throw Error('原生文字拆分需要AE24.3以上的CharacterRange API。');
        if(!text||text.length>2000)throw Error('文字层为空或超过2000个UTF-16字符：'+L.name);
        if(/[\u0590-\u1CFF\u200C-\u200F\u202A-\u202E\u2066-\u2069\uA960-\uA97F\uD7B0-\uD7FF]/.test(text))throw Error('暂不支持双向、连接书写或复杂塑形文字，请改用预合成画面拆分：'+L.name);
        if(doc.lineOrientation!==undefined&&(typeof LineOrientation==='undefined'||doc.lineOrientation!==LineOrientation.HORIZONTAL))throw Error('暂不支持竖排文字：'+L.name);
        if(doc.direction!==undefined&&(typeof ParagraphDirection==='undefined'||doc.direction!==ParagraphDirection.DIRECTION_LEFT_TO_RIGHT))throw Error('暂不支持非从左到右排版：'+L.name);
        var group=L.property('ADBE Text Properties'),animators=group.property('ADBE Text Animators');
        if(animators&&animators.numProperties>0)throw Error('文字动画器会改变逐字排版，请先移除或预合成：'+L.name);
        // AE text paths use a mask on this text layer. Reject all masks rather than guessing a localized/NO_VALUE Path=None sentinel.
        if(L.property('ADBE Mask Parade').numProperties||L.property('ADBE Effect Parade').numProperties||L.hasTrackMatte)throw Error('原生文字含蒙版/文字路径、效果或轨道遮罩，请预合成后按画面拆分：'+L.name);
        var units=nativeUnits(text),visible=0;for(var i=0;i<units.length;i++)if(!/^\s+$/.test(units[i].text))visible++;
        if(!visible||visible>120)throw Error('单个文字层需要1–120个可见字符：'+L.name);
        return nativeSignature(L);
    }
    function nativeSlice(L,start,end){
        var doc=nativeProperty(L).value,len=doc.text.length;
        if(end<len)doc.characterRange(end,len).text='';
        if(start>0)doc.characterRange(0,start).text='';
        doc.justification=ParagraphJustification.LEFT_JUSTIFY;
        return doc;
    }
    function prepareNativeText(rec){
        var scratch=null,measure=null,undo=false,oldMutating=state.mutating,failure=null,cleanupErrors='',parts=[],lineMetrics={},doc=nativeProperty(rec.layer).value,text=doc.text,baselines=doc.baselineLocs,units=nativeUnits(text),line=0,lineStart=0;
        state.mutating=true;var measureStarted=new Date().getTime();
        function metric(document){
            checkCurrent();if(new Date().getTime()-measureStarted>45000)throw Error('原生文字测量超时，请减少字数后重试。');nativeProperty(measure).setValue(document);var evaluated=nativeProperty(measure).value,b=evaluated.baselineLocs,rect=measure.sourceRectAtTime(0,false);
            if(!b||b.length!==4||!isFinite(b[0])||!isFinite(b[1])||!isFinite(b[2])||!isFinite(b[3])||Math.abs(b[0])>1000000||Math.abs(b[1])>1000000||Math.abs(b[2])>1000000||Math.abs(b[3])>1000000||Math.abs(b[3]-b[1])>0.35)throw Error('AE未提供可用的水平文字基线，无法安全定位单字。');
            if(!rect||!isFinite(rect.left)||!isFinite(rect.top)||!isFinite(rect.width)||!isFinite(rect.height)||rect.width<0||rect.height<0)throw Error('AE返回了无效文字边界。');
            return {document:evaluated,width:b[2]-b[0],x:b[0],y:b[1],rect:rect};
        }
        try{
            app.beginUndoGroup('AE 智能分层 · 测量原生文字');undo=true;
            scratch=app.project.items.addComp('__AE智能分层_文字测量_'+token,Math.max(16,comp.width),Math.max(16,comp.height),1,1,comp.frameRate||25);
            measure=scratch.layers.addText('');
            for(var i=0;i<units.length;i++){
                var unit=units[i];
                if(/^[\r\n]+$/.test(unit.text)){line++;lineStart=unit.end;continue;}
                if(/^\s+$/.test(unit.text))continue;
                var lineEnd=text.length;for(var e=unit.end;e<text.length;e++)if(text.charAt(e)==='\r'||text.charAt(e)==='\n'){lineEnd=e;break;}
                var bi=line*4;if(!baselines||baselines.length<bi+4||!isFinite(baselines[bi])||!isFinite(baselines[bi+1])||Math.abs(baselines[bi])>1000000||Math.abs(baselines[bi+1])>1000000)throw Error('源文字行基线无效：'+rec.layer.name);
                var full=lineMetrics['$'+line];
                if(!full){
                    full=metric(nativeSlice(rec.layer,lineStart,lineEnd));lineMetrics['$'+line]=full;
                    // Compare a no-ligature line probe. This is a conservative geometry screen, not a pixel-equivalence proof.
                    var noLig=nativeSlice(rec.layer,lineStart,lineEnd);if(noLig.ligature!==undefined){noLig.ligature=false;var plain=metric(noLig);if(Math.abs(plain.width-full.width)>0.1||Math.abs(plain.rect.left-full.rect.left)>0.1||Math.abs(plain.rect.top-full.rect.top)>0.1||Math.abs(plain.rect.width-full.rect.width)>0.1||Math.abs(plain.rect.height-full.rect.height)>0.1)throw Error('检测到连字或上下文替换，请预合成后按画面拆分：'+rec.layer.name);}
                }
                var single=metric(nativeSlice(rec.layer,unit.start,unit.end)),prefix=unit.start===lineStart?single:metric(nativeSlice(rec.layer,lineStart,unit.end)),suffix=unit.end===lineEnd?single:metric(nativeSlice(rec.layer,unit.start,lineEnd));
                var a=prefix.width-single.width,b=full.width-suffix.width;
                if(Math.abs(a-b)>0.35||single.width<0||!isFinite(baselines[bi+2])||Math.abs((baselines[bi+2]-baselines[bi])-full.width)>0.35)throw Error('文字上下文排版不一致，不能可靠逐字定位：'+unit.text+'（'+rec.layer.name+'）');
                var style=nativeStyleSignature(doc,unit.start,unit.end);
                if(single.document.text!==unit.text||nativeStyleSignature(single.document,0,unit.text.length)!==style)throw Error('AE 剪裁文字时改变了字符内容或样式，已停止：'+unit.text);
                var offset=[baselines[bi]+a-single.x,baselines[bi+1]-single.y],r=single.rect,x=r.left+offset[0],y=r.top+offset[1];
                if(r.width<=0||r.height<=0)throw Error('字符无可见边界：'+unit.text+'（'+rec.layer.name+'）');
                parts.push({kind:'TEXT',nativeText:true,text:unit.text,document:single.document,nativeStyle:style,nativeOffset:offset,nativeRect:r,feather:0,padding:0,loops:[[[x,y],[x+r.width,y],[x+r.width,y+r.height],[x,y+r.height]]]});
                progress('测量文字 '+(state.index+1)+'/'+records.length,state.index,records.length,rec.layer.name+'：已测量 '+parts.length+' 个可见字符；空白保留间距但不生成空层。');
                if(nativeSignature(rec.layer)!==rec.textSignature)throw Error('测量期间源文字或样式已改变：'+rec.layer.name);
            }
        }catch(e){failure=e;}finally{
            try{if(scratch)scratch.remove();}catch(cleanup){cleanupErrors+='\n恢复文字测量临时合成失败：'+cleanup;}
            try{if(undo)app.endUndoGroup();}catch(closeUndo){cleanupErrors+='\n关闭撤销组失败：'+closeUndo;}
            state.mutating=oldMutating;
        }
        if(failure||cleanupErrors)throw Error((failure?String(failure):'')+cleanupErrors);
        return parts;
    }
    function configureNativeCopy(copy,part,rec){
        nativeProperty(copy).setValue(part.document);
        var actual=nativeProperty(copy).value;
        if(actual.text!==part.text||nativeStyleSignature(actual,0,part.text.length)!==part.nativeStyle)throw Error('AE 未保留真实单字符文字内容或样式，已停止本批次。');
        var r=copy.sourceRectAtTime(comp.time,false),expected=part.nativeRect;
        if(!r||!isFinite(r.left)||!isFinite(r.top)||!isFinite(r.width)||!isFinite(r.height)||Math.abs(r.left-expected.left)>0.35||Math.abs(r.top-expected.top)>0.35||Math.abs(r.width-expected.width)>0.35||Math.abs(r.height-expected.height)>0.35)throw Error('输出单字边界与测量不符，已停止本批次。');
        var anchor=[r.left+r.width/2,r.top+r.height/2],dx=part.nativeOffset[0]+anchor[0]-rec.anchor[0],dy=part.nativeOffset[1]+anchor[1]-rec.anchor[1],rad=rec.rotation*Math.PI/180,sx=rec.scale[0]/100,sy=rec.scale[1]/100,tr=copy.property('ADBE Transform Group');
        tr.property('ADBE Anchor Point').setValue(anchor);tr.property('ADBE Position').setValue([rec.position[0]+Math.cos(rad)*sx*dx-Math.sin(rad)*sy*dy,rec.position[1]+Math.sin(rad)*sx*dx+Math.cos(rad)*sy*dy]);
    }
    // END_NATIVE_TEXT
    function supported(L) {
        if(isNativeText(L))return true;
        if (!L || !L.source) return false;
        if (L.source instanceof CompItem) return true;
        return L.source instanceof FootageItem && L.source.mainSource.isStill && L.source.file;
    }
    function sourceDescription(L) {
        var text = '图层：' + L.name;
        try { text += '\n来源类型：' + (L.source ? L.source.typeName : L.matchName); } catch(ignoreType) {}
        try { if (L.source instanceof FootageItem) text += '\n素材文件：' + (L.source.file ? L.source.file.fsName : '无外部文件') + '\n静态素材：' + L.source.mainSource.isStill; } catch(ignoreSource) {}
        return text;
    }
    for(var sf=0;sf<sources.length;sf++){
        var candidate=sources[sf];
        if(!supported(candidate)){alert('选择中含不支持的图片/预合成/文字层，本批未处理。\n'+sourceDescription(candidate));return;}
        if(isNativeText(candidate))nativeTextPreflight(candidate);
        var initialFile=candidate.source instanceof FootageItem?candidate.source.file:null;
        if(initialFile&&!initialFile.exists){alert('图片素材离线：'+initialFile.fsName);return;}
        if(!isStatic2D(candidate)){alert('图层 '+candidate.name+' 不支持锚点居中（需要无父级、无关键帧/表达式的静态二维图层，且位置维度未分离）。本批未处理。');return;}
        for(var li=1;li<=comp.numLayers;li++){var dependent=comp.layer(li);if(dependent!==candidate&&dependent.trackMatteLayer===candidate){alert('图层 '+candidate.name+' 正在为 '+dependent.name+' 提供轨道遮罩，不能隐藏源图层。本批未处理。');return;}}
    }
    stage = '查找 Windows PowerShell';
    var needsWorker=false;for(var wi=0;wi<sources.length;wi++)if(!isNativeText(sources[wi]))needsWorker=true;
    var ps = needsWorker?findPowershell():null;
    if (needsWorker&&!ps.exists) { alert('未找到系统自带 Windows PowerShell。'); return; }
    function writeFile(f, text) {
        f.encoding = 'UTF-8';
        if (!f.open('w')) throw Error('无法写入：' + f.fsName);
        try { if (!f.write(text)) throw Error('写入失败：' + f.fsName); }
        finally { f.close(); }
    }
    function readFile(f, max, encoding) {
        if (!f.exists) throw Error('后台文件尚未出现：' + f.fsName);
        if (f.length > max) throw Error('后台数据超过大小限制。');
        f.encoding = encoding || 'UTF-8';
        if (!f.open('r')) throw Error('无法读取：' + f.fsName);
        try {
            var lines = [], chars = 0, count = 0;
            while (!f.eof) {
                if (++count > 30000) throw Error('后台文本行数超过限制。');
                var line = f.readln();
                if (typeof line !== 'string') throw Error('AE readln 返回了无效内容。');
                chars += line.length + 1;
                if (chars > max) throw Error('后台文本内容超过大小限制。');
                lines.push(line);
                if (f.error) throw Error('读取后台文本失败：' + f.error);
            }
            var text = lines.join('\n');
            if (text.length && text.charCodeAt(0) === 65279) text = text.substring(1);
            return text;
        } finally { f.close(); }
    }
    function renderAnalysisSnapshot(L, destination) {
        stage = '生成当前帧分析快照';
        var source = L.source, scratch = null, analysisComp = null, time = 0, opened = false;
        var previousRendering, restoreRendering = false;
        try {
            try { previousRendering = app.disableRendering; if (typeof previousRendering === 'boolean') { app.disableRendering = false; restoreRendering = true; } } catch(ignoreRendering) {}
            if (source instanceof CompItem) {
                analysisComp = source;
                if (L.timeRemapEnabled) time = L.property('ADBE Time Remapping').valueAtTime(comp.time,false);
                else time = (comp.time - L.startTime) * 100 / (L.stretch || 100);
                var frame = source.frameDuration || (1/25);
                time = Math.max(0,Math.min(time,Math.max(0,source.duration-frame*0.1)));
            } else {
                app.beginUndoGroup('准备临时分析快照'); opened = true;
                scratch = app.project.items.addComp('__ZX_临时分析_' + token, source.width, source.height, source.pixelAspect || 1, 1, comp.frameRate || 25);
                scratch.layers.add(source); analysisComp = scratch;
            }
            analysisComp.saveFrameToPng(time,destination);
            // PNG 完整性由后台检查，AE 端不读取二进制、不忙等。
        } finally {
            try { if (scratch) scratch.remove(); }
            finally { try { if (opened) app.endUndoGroup(); } finally { if (restoreRendering) app.disableRendering = previousRendering; } }
        }
    }
    function parseResult(text, token, width, height) {
        var lines = text.replace(/\r/g, '').split('\n'), at = 0, total = 0, paths = 0;
        while (lines.length && lines[lines.length - 1] === '') lines.pop();
        function take() { if (at >= lines.length) throw Error('后台结果不完整。'); return lines[at++]; }
        function nums(line, count) {
            var pieces = line.split(' '), out = [];
            if (pieces.length !== count) throw Error('后台数据格式错误。');
            for (var i = 0; i < pieces.length; i++) {
                if (!/^-?\d+(\.\d+)?$/.test(pieces[i])) throw Error('后台数据包含非法数字。');
                var n = Number(pieces[i]); if (!isFinite(n)) throw Error('非法数字。'); out.push(n);
            }
            return out;
        }
        function integer(n, min, max) { if (n !== Math.floor(n) || n < min || n > max) throw Error('后台数据超出限制。'); return n; }
        if (take() !== 'AE_PARTS_V2' || take() !== token) throw Error('后台协议版本或任务标识不匹配。');
        var status = take();
        if (status !== 'OK') throw Error(lines.slice(at).join('\n') || '后台分析失败。');
        var size = nums(take(), 2);
        if (size[0] !== width || size[1] !== height) throw Error('文件尺寸与 AE 素材尺寸不同，不能安全套用蒙版。');
        var count = integer(nums(take(), 1)[0], 1, 40), parts = [];
        for (var i = 0; i < count; i++) {
            var header=take().split(' ');
            if(header.length!==4||(header[0]!=='TEXT'&&header[0]!=='OBJECT'&&header[0]!=='UNKNOWN'))throw Error('后台区域类型无效。');
            var settings=nums(header.slice(1).join(' '),3);
            if (settings[0] < 0 || settings[0] > 64 || settings[1] < 0 || settings[1] > 256) throw Error('羽化或留边超出安全范围。');
            var number = integer(settings[2], 1, 8), loops = [];
            for (var j = 0; j < number; j++) {
                var n = integer(nums(take(), 1)[0], 3, 256), points = [];
                total += n; paths++;
                if (total > 12000 || paths > 120) throw Error('轮廓过于复杂，已停止生成。');
                for (var k = 0; k < n; k++) {
                    var xy = nums(take(), 2);
                    if (xy[0] < 0 || xy[0] > width || xy[1] < 0 || xy[1] > height) throw Error('轮廓坐标越界。');
                    points.push(xy);
                }
                loops.push(points);
            }
            parts.push({kind:header[0],feather:settings[0],padding:settings[1],loops:loops});
        }
        if (at !== lines.length) throw Error('后台结果包含额外数据。');
        return parts;
    }
    // Sort the complete part objects, preserving kind/masks/feather associations.
    // Coordinates are composition-space, so mirrored/rotated sources follow what is visible.
    function spatialOrder(parts,rec){
        var items=[],rows=[],rad=rec.rotation*Math.PI/180,c=Math.cos(rad),s=Math.sin(rad),sx=rec.scale[0]/100,sy=rec.scale[1]/100;
        for(var i=0;i<parts.length;i++){
            var left=Infinity,right=-Infinity,top=Infinity,bottom=-Infinity;
            for(var j=0;j<parts[i].loops.length;j++)for(var k=0;k<parts[i].loops[j].length;k++){
                var p=parts[i].loops[j][k],dx=(p[0]-rec.anchor[0])*sx,dy=(p[1]-rec.anchor[1])*sy,x=rec.position[0]+c*dx-s*dy,y=rec.position[1]+s*dx+c*dy;
                left=Math.min(left,x);right=Math.max(right,x);top=Math.min(top,y);bottom=Math.max(bottom,y);
            }
            items.push({part:parts[i],index:i,x:(left+right)/2,y:(top+bottom)/2,top:top,bottom:bottom,height:Math.max(1,bottom-top)});
        }
        // Anchor each row to its first item rather than chaining overlapping neighbors.
        items.sort(function(a,b){return b.y-a.y||a.x-b.x||a.index-b.index;});
        for(var n=0;n<items.length;n++){
            var item=items[n],winner=-1,best=Infinity;
            for(var r=0;r<rows.length;r++){
                var row=rows[r],overlap=Math.min(item.bottom,row.bottom)-Math.max(item.top,row.top),distance=Math.abs(item.y-row.y);
                if(overlap>=Math.min(item.height,row.height)*0.5&&distance<=Math.max(item.height,row.height)*0.5&&distance<best){best=distance;winner=r;}
            }
            if(winner<0)rows.push({y:item.y,top:item.top,bottom:item.bottom,height:item.height,items:[item]});else rows[winner].items.push(item);
        }
        rows.sort(function(a,b){return b.y-a.y;});
        var ordered=[];
        for(var ri=0;ri<rows.length;ri++){
            rows[ri].items.sort(function(a,b){return a.x-b.x||b.y-a.y||a.index-b.index;});
            for(var pi=0;pi<rows[ri].items.length;pi++)ordered.push(rows[ri].items[pi].part);
        }
        return ordered;
    }
    var token = 'ae_parts_' + new Date().getTime().toString(36) + '_' + Math.floor(Math.random() * 1000000000).toString(36);
    var folder = new Folder(Folder.temp.fsName + '/' + token);
    var state;
    try {
        stage = '创建临时目录';
        if (folder.exists || !folder.create()) throw Error('无法创建临时目录。');
        var worker = new File(folder.fsName + '/worker.ps1');
        stage = '写入后台脚本';
        var WORKER = [
            "$ErrorActionPreference = 'Stop'",
            "$job = $PSScriptRoot",
            "[IO.File]::WriteAllText((Join-Path $job 'started.txt'), 'started')",
            "try {",
            "Add-Type -AssemblyName System.Drawing",
            "$code = @'",
            "using System;",
            "using System.IO;",
            "using System.Text;",
            "using System.Drawing;",
            "using System.Drawing.Imaging;",
            "using System.Drawing.Drawing2D;",
            "using System.Runtime.InteropServices;",
            "using System.Collections.Generic;",
            "using System.Globalization;",
            "using System.Diagnostics;",
            "public static class SplitWorker {",
            " const int MAXSIDE=1024, MAXPARTS=40, MAXPOINTS=128;",
            " static int W,H,OW,OH; static double DX,DY; static string Job; static Stopwatch Clock;",
            " struct P { public int x,y; public P(int a,int b){x=a;y=b;} }",
            " class E { public int x,y,u,v,d; public bool used; public E(int a,int b,int c,int e,int f){x=a;y=b;u=c;v=e;d=f;} }",
            " class R { public List<int> cells=new List<int>(); public int x0=int.MaxValue,y0=int.MaxValue,x1,y1;public string kind=\"UNKNOWN\"; }",
            " class Part { public List<List<P>> paths; public double feather,padding;public string kind; }",
            " static void Check(){if(Clock.ElapsedMilliseconds>35000||File.Exists(Path.Combine(Job,\"cancel.txt\")))throw new Exception(\"Cancelled or analysis timed out.\");}",
            " static string F(double n){return n.ToString(\"0.###\",CultureInfo.InvariantCulture);}",
            " static void Save(string body){string p=Path.Combine(Job,\"result.part\");File.WriteAllText(p,\"AE_PARTS_V2\\n\"+new DirectoryInfo(Job).Name+\"\\n\"+body,new UTF8Encoding(false));File.Move(p,Path.Combine(Job,\"result.txt\"));}",
            " public static void Run(string job){Job=job;Clock=Stopwatch.StartNew();try{Analyze();}catch(Exception e){Save(\"ERROR\\n\"+e.Message);}}",
            " static double BackgroundAt(double[] model,int channel,int x,int y){double u=(double)x/Math.Max(1,W-1),v=(double)y/Math.Max(1,H-1);return model[channel]*(1-u)*(1-v)+model[3+channel]*u*(1-v)+model[6+channel]*(1-u)*v+model[9+channel]*u*v;}",
            " static double[] SmoothBackground(byte[] pixels,List<int> border,out double noise){",
            "  noise=0;if(W<4||H<4)return null;int[] corners={0,W-1,(H-1)*W,H*W-1};double[] model=new double[12];double spread=0;",
            "  for(int c=0;c<3;c++){double min=255,max=0;for(int j=0;j<4;j++){int at=corners[j]*4;if(pixels[at+3]<250)return null;model[j*3+c]=pixels[at+c];min=Math.Min(min,pixels[at+c]);max=Math.Max(max,pixels[at+c]);}spread=Math.Max(spread,max-min);}if(spread<20)return null;",
            "  List<double> errors=new List<double>();foreach(int index in border){int at=index*4;if(pixels[at+3]<250)return null;int x=index%W,y=index/W;double error=0;for(int c=0;c<3;c++)error=Math.Max(error,Math.Abs(pixels[at+c]-BackgroundAt(model,c,x,y)));errors.Add(error);}errors.Sort();",
            "  double q95=errors[(int)((errors.Count-1)*0.95)],q99=errors[(int)((errors.Count-1)*0.99)];if(q95>4||q99>8)return null;noise=q99+1;return model;",
            " }",
            " static void Analyze(){",
            "  Stage(\"LOAD_IMAGE\");",
            "  string path=File.ReadAllText(Path.Combine(Job,\"source.txt\"),Encoding.UTF8).TrimEnd('\\r','\\n');",
            "  long length;DateTime modified;",
            "  float[] score;",
            "  using(Image image=OpenReadyImage(path)){",
            "   FileInfo before=new FileInfo(path);length=before.Length;modified=before.LastWriteTimeUtc;",
            "   OW=image.Width;OH=image.Height;if((long)OW*OH>100000000)throw new Exception(\"Image exceeds 100 megapixels.\");",
            "   double scale=Math.Min(1.0,(double)MAXSIDE/Math.Max(OW,OH));if(OW>OH*2.5)scale=Math.Min(scale,120.0/OH);W=Math.Max(1,(int)Math.Ceiling(OW*scale));H=Math.Max(1,(int)Math.Ceiling(OH*scale));DX=(double)OW/W;DY=(double)OH/H;",
            "   using(Bitmap bitmap=new Bitmap(W,H,PixelFormat.Format32bppArgb)){",
            "    using(Graphics g=Graphics.FromImage(bitmap)){g.CompositingMode=CompositingMode.SourceCopy;g.InterpolationMode=InterpolationMode.HighQualityBilinear;g.DrawImage(image,new Rectangle(0,0,W,H),0,0,OW,OH,GraphicsUnit.Pixel);}",
            "    BitmapData data=bitmap.LockBits(new Rectangle(0,0,W,H),ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);",
            "    byte[] pixels=new byte[W*H*4];try{byte[] row=new byte[W*4];for(int y=0;y<H;y++){Marshal.Copy(IntPtr.Add(data.Scan0,y*data.Stride),row,0,row.Length);Buffer.BlockCopy(row,0,pixels,y*W*4,row.Length);}}finally{bitmap.UnlockBits(data);}",
            "    List<int> border=new List<int>();for(int x=0;x<W;x++){border.Add(x);if(H>1)border.Add((H-1)*W+x);}for(int y=1;y<H-1;y++){border.Add(y*W);if(W>1)border.Add(y*W+W-1);}",
            "    int transparent=0,bestKey=0,bestCount=0;Dictionary<int,int> counts=new Dictionary<int,int>();",
            "    foreach(int at in border){int k=at*4;if(pixels[k+3]<8){transparent++;continue;}int key=(pixels[k]>>3)|((pixels[k+1]>>3)<<5)|((pixels[k+2]>>3)<<10);int count;counts.TryGetValue(key,out count);counts[key]=++count;if(count>bestCount){bestCount=count;bestKey=key;}}",
            "    double bb=0,bg=0,br=0,n=0;foreach(int at in border){int k=at*4;int key=(pixels[k]>>3)|((pixels[k+1]>>3)<<5)|((pixels[k+2]>>3)<<10);if(pixels[k+3]>=8&&key==bestKey){bb+=pixels[k];bg+=pixels[k+1];br+=pixels[k+2];n++;}}",
            "    if(n>0){bb/=n;bg/=n;br/=n;}bool alpha=transparent>=Math.Max(1,border.Count/4);score=new float[W*H];double noise=0;double[] gradient=alpha?null:SmoothBackground(pixels,border,out noise);",
            "    for(int y=0;y<H;y++){if((y&15)==0)Check();for(int x=0;x<W;x++){int at=y*W+x,k=at*4;double a=pixels[k+3]/255.0,contrast=gradient==null?Math.Max(Math.Abs(pixels[k]-bb),Math.Max(Math.Abs(pixels[k+1]-bg),Math.Abs(pixels[k+2]-br))):Math.Max(0,Math.Max(Math.Abs(pixels[k]-BackgroundAt(gradient,0,x,y)),Math.Max(Math.Abs(pixels[k+1]-BackgroundAt(gradient,1,x,y)),Math.Abs(pixels[k+2]-BackgroundAt(gradient,2,x,y))))-noise);score[at]=(float)(alpha?a:contrast/255.0*a);}}",
            "   }",
            "  }",
            "  FileInfo after=new FileInfo(path);if(after.Length!=length||after.LastWriteTimeUtc!=modified)throw new Exception(\"Source image changed during analysis.\");Check();",
            "  Stage(\"OBJECT_SEGMENTATION\");",
            "  float[] smooth=Blur(Blur(score));float peak=0;foreach(float v in smooth)if(v>peak)peak=v;if(peak<0.008)throw new Exception(\"Insufficient foreground contrast.\");",
            "  double threshold=ForegroundThreshold(smooth,peak);",
            "  double supportLevel=Math.Max(0.001,threshold*0.10);bool[] support=new bool[score.Length];for(int i=0;i<score.Length;i++)support[i]=score[i]>=supportLevel;",
            "  // Use low-threshold candidates so a bright subject cannot erase a separate faint one.",
            "  // Never force a split of candidates connected by blur or a weak real stroke.",
            "  List<R> all=Components(support),regions=new List<R>();foreach(R r in all){double localPeak=0;int localCore=0;foreach(int at in r.cells)localPeak=Math.Max(localPeak,smooth[at]);foreach(int at in r.cells)if(smooth[at]>=localPeak*0.5)localCore++;if(r.cells.Count>=4&&localCore>=3&&localPeak>=0.008)regions.Add(r);}",
            "  int retainedCells=0;foreach(R region in regions)retainedCells+=region.cells.Count;",
            "  regions=CharacterRegions(GroupAlignedFragments(regions,smooth),smooth);",
            "  for(int pass=0;pass<3;pass++){int before=regions.Count;regions=RefineCharacterGaps(regions,smooth);if(regions.Count==before)break;}",
            "  regions.Sort(delegate(R a,R b){int d=a.y0-b.y0;return d==0?a.x0-b.x0:d;});",
            "  if(regions.Count==0||regions.Count>MAXPARTS)throw new Exception(\"Detected \"+regions.Count+\" subjects; expected 1..\"+MAXPARTS+\".\");",
            "  ClassifyRegions(regions,smooth,false);",
            "  regions=MergeUncertainFragments(regions,smooth);",
            "  regions=RefitUncertainNeighborhoods(regions,smooth);",
            "  ClassifyRegions(regions,smooth,true);",
            "  File.WriteAllText(Path.Combine(Job,\"route.txt\"),\"ADAPTIVE_CHARACTERS\");",
            "  File.WriteAllText(Path.Combine(Job,\"threshold.txt\"),\"otsu=\"+F(threshold)+\" support=\"+F(supportLevel));",
            "  if(regions.Count==0||regions.Count>MAXPARTS)throw new Exception(\"Detected \"+regions.Count+\" subjects; expected 1..\"+MAXPARTS+\".\");",
            "  int[] ids=new int[score.Length];List<int>[] cells=new List<int>[regions.Count];int assignedCells=0;for(int r=0;r<regions.Count;r++){cells[r]=regions[r].cells;foreach(int at in cells[r]){if(ids[at]!=0)throw new Exception(\"Overlapping region assignment.\");ids[at]=r+1;assignedCells++;}}",
            "  if(assignedCells!=retainedCells)throw new Exception(\"Region coverage changed during segmentation.\");File.WriteAllText(Path.Combine(Job,\"coverage.txt\"),retainedCells+\" \"+assignedCells);",
            "  List<Part> parts=new List<Part>();int totalPoints=0,totalPaths=0;",
            "  for(int r=0;r<regions.Count;r++){Check();Part part=new Part();part.kind=regions[r].kind;part.paths=Trace(cells[r],ids,r+1);if(part.paths.Count==0||part.paths.Count>8)throw new Exception(\"Outline too fragmented.\");",
            "   part.feather=Math.Max(2,Math.Min(18,Math.Max(OW,OH)*0.008));part.padding=part.feather*1.5+Math.Max(DX,DY);R a=regions[r];double nearest=1e30;",
            "   for(int j=0;j<regions.Count;j++){if(r==j)continue;R b=regions[j];double gx=Math.Max(0,Math.Max(a.x0-b.x1-1,b.x0-a.x1-1))*DX,gy=Math.Max(0,Math.Max(a.y0-b.y1-1,b.y0-a.y1-1))*DY;nearest=Math.Min(nearest,Math.Sqrt(gx*gx+gy*gy));}",
            "   part.feather=Math.Min(part.feather,Math.Max(1,nearest/2));part.padding=Math.Min(part.padding,Math.Max(0,nearest/4));",
            "   foreach(List<P> poly in part.paths)totalPoints+=poly.Count;totalPaths+=part.paths.Count;parts.Add(part);",
            "  }",
            "  if(totalPoints>12000||totalPaths>120)throw new Exception(\"Outline budget exceeded.\");Check();",
            "  StringBuilder output=new StringBuilder();output.Append(\"OK\\n\").Append(OW).Append(' ').Append(OH).Append('\\n').Append(parts.Count).Append('\\n');",
            "  foreach(Part p in parts){output.Append(p.kind).Append(' ').Append(F(p.feather)).Append(' ').Append(F(p.padding)).Append(' ').Append(p.paths.Count).Append('\\n');foreach(List<P> poly in p.paths){output.Append(poly.Count).Append('\\n');foreach(P pt in poly)output.Append(F(pt.x*DX)).Append(' ').Append(F(pt.y*DY)).Append('\\n');}}",
            "  Save(output.ToString());",
            " }",
            "",
            "",
            "",
            " static bool PngComplete(string path){",
            "  if(!Path.GetExtension(path).Equals(\".png\",StringComparison.OrdinalIgnoreCase))return true;",
            "  using(FileStream f=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)){",
            "   if(f.Length<45)return false;",
            "   byte[] h=new byte[8],t=new byte[12];if(f.Read(h,0,8)!=8)return false;",
            "   f.Seek(-12,SeekOrigin.End);if(f.Read(t,0,12)!=12)return false;",
            "   return h[0]==137&&h[1]==80&&h[2]==78&&h[3]==71&&h[4]==13&&h[5]==10&&h[6]==26&&h[7]==10&&t[4]==73&&t[5]==69&&t[6]==78&&t[7]==68;",
            "  }",
            " }",
            " static Image OpenReadyImage(string path){",
            "  Stage(\"WAIT_INPUT\");Stopwatch wait=Stopwatch.StartNew();string last=\"input file has not appeared\";",
            "  while(wait.ElapsedMilliseconds<10000){",
            "   Check();",
            "   try{",
            "    if(File.Exists(path)){",
            "     if(PngComplete(path))return Image.FromFile(path);",
            "     last=\"PNG is still being written\";",
            "    }",
            "   }catch(OutOfMemoryException){throw;}",
            "   catch(Exception e){last=e.Message;}",
            "   System.Threading.Thread.Sleep(75);",
            "  }",
            "  throw new Exception(\"Input image was not ready within 10 seconds: \"+last+\" | \"+path);",
            " }",
            "",
            " // Raster content classification, not conversion into editable AE text.",
            " static void ClassifyRegions(List<R> regions,float[] soft,bool finalPass){",
            "  double[] fits=new double[regions.Count],tolerant=new double[regions.Count],compactness=new double[regions.Count];bool[] solid=new bool[regions.Count];StringBuilder report=new StringBuilder();",
            "  for(int i=0;i<regions.Count;i++){",
            "   Check();R r=regions[i];float[] isolated=new float[soft.Length];double peak=0;foreach(int at in r.cells){isolated[at]=soft[at];peak=Math.Max(peak,soft[at]);}",
            "   fits[i]=GlyphTemplates.Match(isolated,W,r.x0,r.y0,r.x1,r.y1,peak);tolerant[i]=GlyphTemplates.MatchTolerant(isolated,W,r.x0,r.y0,r.x1,r.y1,peak);",
            "   int left=W,right=0,top=H,bottom=0,area=0;foreach(int at in r.cells)if(soft[at]>=peak*0.5){int x=at%W,y=at/W;left=Math.Min(left,x);right=Math.Max(right,x);top=Math.Min(top,y);bottom=Math.Max(bottom,y);area++;}",
            "   double hull=ConvexArea(isolated,peak*0.5,r);compactness[i]=hull>0?area/hull:0;solid[i]=compactness[i]>0.94;",
            "  }",
            "  for(int i=0;i<regions.Count;i++){",
            "   R r=regions[i];int row=0;double rowFit=0;",
            "   for(int j=0;j<regions.Count;j++){",
            "    R b=regions[j];double h=Math.Max(r.y1-r.y0+1,b.y1-b.y0+1),centers=Math.Abs((r.y0+r.y1-b.y0-b.y1)*0.5),heightRatio=(double)Math.Min(r.y1-r.y0+1,b.y1-b.y0+1)/h;",
            "    if(centers<h*0.25&&heightRatio>0.60&&!solid[j]&&fits[j]>0.40){row++;rowFit+=tolerant[j];}",
            "   }",
            "   double aspect=(double)(r.x1-r.x0+1)/(r.y1-r.y0+1);",
            "   bool text=(!solid[i]&&((row>=2&&rowFit/row>0.78&&fits[i]>0.45&&tolerant[i]>0.76)||(fits[i]>0.68&&tolerant[i]>0.82)))||(row>=3&&rowFit/row>0.80&&solid[i]&&aspect<0.5&&tolerant[i]>0.75);",
            "   // A lone bar or ring can also be an icon: preserve an explicit uncertain state.",
            "   if(text&&row<2&&(solid[i]||aspect<0.18))text=false;",
            "   r.kind=text?\"TEXT\":(solid[i]||(finalPass&&compactness[i]>0.85))?\"OBJECT\":\"UNKNOWN\";",
            "   report.Append(i+1).Append(' ').Append(r.kind).Append(\" box=\").Append(r.x0).Append(',').Append(r.y0).Append(',').Append(r.x1).Append(',').Append(r.y1).Append(\" fit=\").Append(F(fits[i])).Append(\" tolerant=\").Append(F(tolerant[i])).Append(\" row=\").Append(row).Append('\\n');",
            "  }",
            "  File.WriteAllText(Path.Combine(Job,\"classification.txt\"),report.ToString());",
            " }",
            " static double RegionQuality(List<R> regions,float[] soft){double sum=0,weight=0;foreach(R r in regions){Check();float[] isolated=new float[soft.Length];double peak=0;foreach(int at in r.cells){isolated[at]=soft[at];peak=Math.Max(peak,soft[at]);}double w=r.x1-r.x0+1;sum+=w*GlyphTemplates.Match(isolated,W,r.x0,r.y0,r.x1,r.y1,peak);weight+=w;}return sum/Math.Max(1,weight);}",
            " static List<R> RefitUncertainNeighborhoods(List<R> input,float[] soft){",
            "  int confidentText=0;foreach(R part in input)if(part.kind==\"TEXT\")confidentText++;if(confidentText<2)return input;",
            "  List<R> regions=new List<R>(input);regions.Sort(delegate(R a,R b){return a.x0-b.x0;});",
            "  for(int end=regions.Count-1;end>=1;end--){",
            "   if(regions[end].kind!=\"UNKNOWN\")continue;",
            "   for(int size=2;size<=4&&end-size+1>=0;size++){",
            "    Check();int start=end-size+1;List<R> old=new List<R>();R joined=new R();bool valid=true;int maxHeight=0;",
            "    for(int j=start;j<=end;j++){R part=regions[j];old.Add(part);foreach(int at in part.cells)AddCell(joined,at);maxHeight=Math.Max(maxHeight,part.y1-part.y0+1);}",
            "    for(int j=start+1;j<=end;j++)if(regions[j].x0-regions[j-1].x1>maxHeight*0.2||Math.Abs(regions[j].y1-regions[j-1].y1)>maxHeight*0.2)valid=false;",
            "    if(!valid)continue;List<R> context=new List<R>();for(int j=0;j<regions.Count;j++)if(j<start||j>end)context.Add(regions[j]);context.Add(joined);",
            "    List<R> candidate=RefineCharacterGaps(context,soft),replacement=new List<R>();HashSet<int> allowed=new HashSet<int>(joined.cells);foreach(R part in candidate){bool inside=part.cells.Count>0;foreach(int at in part.cells)if(!allowed.Contains(at)){inside=false;break;}if(inside)replacement.Add(part);}",
            "    if(replacement.Count<2||replacement.Count>=old.Count||RegionQuality(replacement,soft)<RegionQuality(old,soft)+0.05)continue;",
            "    regions.RemoveRange(start,size);regions.InsertRange(start,replacement);end=start;break;",
            "   }",
            "  }",
            "  return regions;",
            " }",
            " static List<R> MergeUncertainFragments(List<R> regions,float[] soft){",
            "  StringBuilder log=new StringBuilder();bool changed=true;while(changed){changed=false;for(int i=0;i<regions.Count&&!changed;i++)for(int j=i+1;j<regions.Count;j++){",
            "   Check();R a=regions[i],b=regions[j];if(a.kind!=\"UNKNOWN\"&&b.kind!=\"UNKNOWN\")continue;",
            "   int height=Math.Max(a.y1-a.y0+1,b.y1-b.y0+1),gap=Math.Max(a.x0-b.x1-1,b.x0-a.x1-1);",
            "   if(gap>height*0.12||Math.Abs(a.y1-b.y1)>height*0.25||Math.Max(a.x1,b.x1)-Math.Min(a.x0,b.x0)>height*1.8)continue;",
            "   float[] va=new float[soft.Length],vb=new float[soft.Length],paired=new float[soft.Length];double peak=0;foreach(int at in a.cells){va[at]=paired[at]=soft[at];peak=Math.Max(peak,soft[at]);}foreach(int at in b.cells){vb[at]=paired[at]=soft[at];peak=Math.Max(peak,soft[at]);}",
            "   double fa=GlyphTemplates.Match(va,W,a.x0,a.y0,a.x1,a.y1,peak),fb=GlyphTemplates.Match(vb,W,b.x0,b.y0,b.x1,b.y1,peak),fit=GlyphTemplates.Match(paired,W,Math.Min(a.x0,b.x0),Math.Min(a.y0,b.y0),Math.Max(a.x1,b.x1),Math.Max(a.y1,b.y1),peak);",
            "   log.Append(a.x0).Append('+').Append(b.x0).Append(\" fa=\").Append(F(fa)).Append(\" fb=\").Append(F(fb)).Append(\" pair=\").Append(F(fit)).Append('\\n');",
            "   if(fit<0.55||fit<Math.Max(fa,fb)+0.04)continue;",
            "   foreach(int at in b.cells)AddCell(a,at);a.kind=\"UNKNOWN\";regions.RemoveAt(j);changed=true;break;",
            "  }}File.WriteAllText(Path.Combine(Job,\"fragment-merge.txt\"),log.ToString());return regions;",
            " }",
            " static double Cross(P a,P b,P c){return (double)(b.x-a.x)*(c.y-a.y)-(double)(b.y-a.y)*(c.x-a.x);}",
            " static double ConvexArea(float[] values,double cutoff,R region){",
            "  List<P> points=new List<P>();for(int y=region.y0;y<=region.y1;y++){int left=W,right=-1;for(int x=region.x0;x<=region.x1;x++)if(values[y*W+x]>=cutoff){left=Math.Min(left,x);right=Math.Max(right,x);}if(right>=left){points.Add(new P(left,y));points.Add(new P(left,y+1));points.Add(new P(right+1,y));points.Add(new P(right+1,y+1));}}",
            "  if(points.Count<3)return 0;points.Sort(delegate(P a,P b){return a.x==b.x?a.y-b.y:a.x-b.x;});List<P> hull=new List<P>();",
            "  foreach(P point in points){while(hull.Count>=2&&Cross(hull[hull.Count-2],hull[hull.Count-1],point)<=0)hull.RemoveAt(hull.Count-1);hull.Add(point);}int lower=hull.Count;",
            "  for(int i=points.Count-2;i>=0;i--){P point=points[i];while(hull.Count>lower&&Cross(hull[hull.Count-2],hull[hull.Count-1],point)<=0)hull.RemoveAt(hull.Count-1);hull.Add(point);}double area=0;for(int i=1;i<hull.Count;i++)area+=(double)hull[i-1].x*hull[i].y-(double)hull[i].x*hull[i-1].y;return Math.Abs(area)*0.5;",
            " }",
            " // Character candidates must persist over several foreground levels. Restore halos",
            " // after character identities are established; low alpha contact is not glyph identity.",
            " static List<R> CharacterRegions(List<R> input,float[] soft){",
            "  List<R> output=new List<R>();StringBuilder diagnostic=new StringBuilder();",
            "  foreach(R source in input){",
            "   Check();int rw=source.x1-source.x0+1,rh=source.y1-source.y0+1;",
            "   diagnostic.Append(\"box \").Append(source.x0).Append(',').Append(source.y0).Append(' ').Append(rw).Append('x').Append(rh).Append(' ');",
            "   if(rw<rh*0.75||rh<8){output.Add(source);continue;}",
            "   double peak=0;foreach(int at in source.cells)peak=Math.Max(peak,soft[at]);",
            "   List<List<R>> levels=new List<List<R>>();",
            "   for(int step=0;step<8;step++){",
            "    double cutoff=peak*(0.28+step*0.08);bool[] mask=new bool[soft.Length];",
            "    foreach(int at in source.cells)if(soft[at]>=cutoff)mask[at]=true;",
            "    List<R> candidates=Components(mask),accepted=new List<R>();",
            "    foreach(R candidate in candidates)if(candidate.cells.Count>=Math.Max(6,source.cells.Count*0.012)&&candidate.y1-candidate.y0+1>=rh*0.52)accepted.Add(candidate);",
            "    accepted.Sort(delegate(R a,R b){return a.x0-b.x0;});",
            "    bool valid=accepted.Count>=2&&accepted.Count<=MAXPARTS;",
            "    for(int j=1;j<accepted.Count;j++)if(accepted[j].x0<accepted[j-1].x1-rh*0.12)valid=false;",
            "    levels.Add(valid?accepted:new List<R>());",
            "    diagnostic.Append(step).Append(':').Append(valid?accepted.Count:0).Append(' ');",
            "   }",
            "   int best=-1,bestCount=1,bestRun=0;",
            "   for(int i=0;i<levels.Count;i++){",
            "    int count=levels[i].Count;if(count<2)continue;int end=i+1;",
            "    while(end<levels.Count&&CompatibleSeeds(levels[end-1],levels[end],rh))end++;",
            "    int run=end-i;",
            "    if(run>=2&&(run>bestRun||(run==bestRun&&count<bestCount))){best=i+(run-1)/2;bestCount=count;bestRun=run;}",
            "    i=end-1;",
            "   }",
            "   if(best<0){output.Add(source);diagnostic.Append(\" -> keep\\n\");continue;}",
            "   double bestQuality=SeedQuality(levels[best],soft,peak);",
            "   for(int i=0;i<levels.Count-1;i++)if(CompatibleSeeds(levels[i],levels[i+1],rh)){",
            "    double quality=SeedQuality(levels[i],soft,peak);if(quality>bestQuality+0.06){best=i;bestQuality=quality;}",
            "   }",
            "   diagnostic.Append(\" quality=\").Append(F(bestQuality));",
            "   double singleFit=GlyphTemplates.Match(soft,W,source.x0,source.y0,source.x1,source.y1,peak);",
            "   diagnostic.Append(\" single=\").Append(F(singleFit));",
            "   if(singleFit>=0.70){output.Add(source);diagnostic.Append(\" -> single glyph\\n\");continue;}",
            "   List<R> seeds=levels[best];int[] labels=new int[soft.Length];bool[] allowed=new bool[soft.Length];Queue<int> queue=new Queue<int>();",
            "   foreach(int at in source.cells)allowed[at]=true;",
            "   for(int j=0;j<seeds.Count;j++)foreach(int at in seeds[j].cells){labels[at]=j+1;queue.Enqueue(at);}",
            "   while(queue.Count>0){int at=queue.Dequeue(),x=at%W,y=at/W;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int nx=x+dx,ny=y+dy;if(nx<0||ny<0||nx>=W||ny>=H)continue;int next=ny*W+nx;if(allowed[next]&&labels[next]==0){labels[next]=labels[at];queue.Enqueue(next);}}}",
            "   CompleteLabels(source.cells,labels,seeds.Count,true);",
            "   // Do not sever a real interior crossbar. Edge contacts from kerning may separate.",
            "   int[] parent=new int[seeds.Count+1];for(int j=1;j<parent.Length;j++)parent[j]=j;",
            "   int[] crossings=new int[parent.Length*parent.Length];double[] boundaryMax=new double[crossings.Length],boundaryMass=new double[crossings.Length];",
            "   foreach(int at in source.cells){int x=at%W;for(int d=0;d<2;d++){int next=d==0?at+1:at+W;if((d==0&&x+1>=W)||next>=labels.Length)continue;int a=labels[at],b=labels[next];if(a<=0||b<=0||a==b)continue;int key=Math.Min(a,b)*parent.Length+Math.Max(a,b);double value=Math.Min(soft[at],soft[next])/peak;boundaryMax[key]=Math.Max(boundaryMax[key],value);boundaryMass[key]+=value;}}",
            "   for(int a=1;a<parent.Length;a++)for(int b=a+1;b<parent.Length;b++)diagnostic.Append(\" B\").Append(a).Append(',').Append(b).Append(\" max=\").Append(F(boundaryMax[a*parent.Length+b])).Append(\" mass=\").Append(F(boundaryMass[a*parent.Length+b]));",
            "   foreach(int at in source.cells){int x=at%W,y=at/W;if(y<source.y0+rh*0.20||y>source.y1-rh*0.20)continue;for(int d=0;d<2;d++){int next=d==0?at+1:at+W;if((d==0&&x+1>=W)||next>=labels.Length)continue;int a=labels[at],b=labels[next];if(a>0&&b>0&&a!=b&&Math.Min(soft[at],soft[next])>peak*0.30)crossings[Math.Min(a,b)*parent.Length+Math.Max(a,b)]++;}}",
            "   double[] partFit=new double[parent.Length];R[] grown=new R[parent.Length];for(int j=1;j<grown.Length;j++)grown[j]=new R();",
            "   foreach(int at in source.cells)if(labels[at]>0)AddCell(grown[labels[at]],at);",
            "   for(int j=1;j<grown.Length;j++){Check();R part=grown[j];float[] isolated=new float[soft.Length];foreach(int at in part.cells)isolated[at]=soft[at];partFit[j]=GlyphTemplates.Match(isolated,W,part.x0,part.y0,part.x1,part.y1,peak);diagnostic.Append(\" fit\").Append(j).Append('=').Append(F(partFit[j]));}",
            "   for(int a=1;a<parent.Length;a++)for(int b=a+1;b<parent.Length;b++)if(crossings[a*parent.Length+b]>=2&&!(partFit[a]>=0.68&&partFit[b]>=0.68)){",
            "    Check();float[] paired=new float[soft.Length];foreach(int at in grown[a].cells)paired[at]=soft[at];foreach(int at in grown[b].cells)paired[at]=soft[at];",
            "    double combined=GlyphTemplates.Match(paired,W,Math.Min(grown[a].x0,grown[b].x0),Math.Min(grown[a].y0,grown[b].y0),Math.Max(grown[a].x1,grown[b].x1),Math.Max(grown[a].y1,grown[b].y1),peak);",
            "    bool merge=combined>=(partFit[a]+partFit[b])*0.5-0.02||(seeds.Count==2&&rw<rh*1.8&&boundaryMax[a*parent.Length+b]>0.5);",
            "    diagnostic.Append(\" pair \").Append(a).Append(',').Append(b).Append(\" combined=\").Append(F(combined)).Append(merge?\" merge\":\" retain\");if(merge)parent[Root(parent,b)]=Root(parent,a);",
            "   }",
            "   Dictionary<int,R> pieces=new Dictionary<int,R>();",
            "   foreach(int at in source.cells){int label=labels[at];if(label==0)continue;int key=Root(parent,label);R part;if(!pieces.TryGetValue(key,out part)){part=new R();pieces[key]=part;}AddCell(part,at);}",
            "   if(pieces.Count<2)output.Add(source);else foreach(R part in pieces.Values)output.Add(part);",
            "   diagnostic.Append(\" -> \").Append(pieces.Count).Append(\"\\n\");",
            "  }",
            "  File.WriteAllText(Path.Combine(Job,\"characters.txt\"),diagnostic.ToString());return output;",
            " }",
            " // A joined serif can survive every threshold. Compare complete single-glyph shapes",
            " // across image-derived gap candidates instead of treating the number of CC seeds as truth.",
            " static List<R> RefineCharacterGaps(List<R> glyphs,float[] soft){",
            "  int gx0=W,gy0=H,gx1=0,gy1=0;foreach(R r in glyphs){gx0=Math.Min(gx0,r.x0);gy0=Math.Min(gy0,r.y0);gx1=Math.Max(gx1,r.x1);gy1=Math.Max(gy1,r.y1);}",
            "  if(gx1-gx0+1<(gy1-gy0+1)*2.2)return glyphs;",
            "  List<R> result=new List<R>();StringBuilder log=new StringBuilder();",
            "  foreach(R source in glyphs){",
            "   Check();int width=source.x1-source.x0+1,height=source.y1-source.y0+1;",
            "   if(width<height*1.5){result.Add(source);continue;}",
            "   float[] isolated=new float[soft.Length];double peak=0;double[] projection=new double[width];",
            "   foreach(int at in source.cells){isolated[at]=soft[at];peak=Math.Max(peak,soft[at]);projection[at%W-source.x0]+=soft[at];}",
            "   double whole=GlyphTemplates.Match(isolated,W,source.x0,source.y0,source.x1,source.y1,peak);",
            "   log.Append(\"box \").Append(source.x0).Append(' ').Append(width).Append('x').Append(height).Append(\" single=\").Append(F(whole)).Append('\\n');",
            "   if(whole>=0.70){result.Add(source);continue;}",
            "   double[] filtered=new double[width];for(int x=0;x<width;x++){double sum=0,weight=0;for(int d=-2;d<=2;d++){int xx=x+d;if(xx>=0&&xx<width){int w=d==0?3:Math.Abs(d)==1?2:1;sum+=projection[xx]*w;weight+=w;}}filtered[x]=sum/weight;}",
            "   List<int> boundaries=new List<int>();boundaries.Add(0);int radius=Math.Max(4,(int)(height*0.65)),minWidth=Math.Max(3,(int)(height*0.10));",
            "   for(int x=minWidth;x<width-minWidth;x++){",
            "    if(filtered[x]>filtered[x-1]||filtered[x]>=filtered[x+1])continue;",
            "    double a=0,b=0;for(int j=Math.Max(0,x-radius);j<x;j++)a=Math.Max(a,filtered[j]);for(int j=x+1;j<Math.Min(width,x+radius);j++)b=Math.Max(b,filtered[j]);",
            "    if(filtered[x]>=Math.Min(a,b)*0.85)continue;",
            "    if(x-boundaries[boundaries.Count-1]<minWidth){if(boundaries.Count>1&&filtered[x]<filtered[boundaries[boundaries.Count-1]])boundaries[boundaries.Count-1]=x;continue;}",
            "    boundaries.Add(x);",
            "   }",
            "   boundaries.Add(width);int n=boundaries.Count;log.Append(\" boundaries:\");foreach(int cut in boundaries)log.Append(cut).Append(',');log.Append('\\n');",
            "   if(n<3||n>24){result.Add(source);continue;}",
            "   double[,] fits=new double[n,n];double[] best=new double[n];int[] previous=new int[n],counts=new int[n];for(int j=1;j<n;j++)best[j]=double.NegativeInfinity;",
            "   for(int end=1;end<n;end++)for(int begin=0;begin<end;begin++){",
            "    Check();int span=boundaries[end]-boundaries[begin];if(span<minWidth||span>height*1.9||double.IsNegativeInfinity(best[begin]))continue;",
            "    double fit=GlyphTemplates.MatchTolerant(isolated,W,source.x0+boundaries[begin],source.y0,source.x0+boundaries[end]-1,source.y1,peak);fits[begin,end]=fit;",
            "    if(fit<0.72)continue;double value=best[begin]+(fit-0.75)*span-(begin>0?2:0);",
            "    if(value>best[end]){best[end]=value;previous[end]=begin;counts[end]=counts[begin]+1;}",
            "   }",
            "   log.Append(\" result=\").Append(counts[n-1]).Append(\" score=\").Append(best[n-1]).Append('\\n');",
            "   if(counts[n-1]<2||best[n-1]<(Math.Max(whole,0.75)-0.75)*width+width*0.04){result.Add(source);continue;}",
            "   List<int> chosen=new List<int>();List<double> chosenFits=new List<double>();for(int at=n-1;at>0;at=previous[at]){chosen.Add(boundaries[at]);chosenFits.Add(fits[previous[at],at]);}chosen.Add(0);chosen.Reverse();chosenFits.Reverse();",
            "   List<int[]> seams=new List<int[]>();",
            "   for(int i=1;i<chosen.Count-1;i++){",
            "    int center=source.x0+chosen[i],lo=Math.Max(source.x0+(chosen[i-1]+chosen[i])/2+1,center-Math.Max(3,height/8)),hi=Math.Min(source.x0+(chosen[i]+chosen[i+1])/2-1,center+Math.Max(3,height/8));",
            "    int[] seam=FindCharacterSeam(isolated,center,lo,hi,peak);int crossings=0;",
            "    for(int y=source.y0+height/5;y<source.y1-height/5;y++)if(Math.Min(isolated[y*W+seam[y]-1],isolated[y*W+seam[y]])>peak*0.30)crossings++;",
            "    double pairFit=(chosenFits[i-1]+chosenFits[i])*0.5;",
            "    if(crossings>=2&&(chosenFits[i-1]<0.75||chosenFits[i]<0.75||pairFit<whole+0.12))continue;seams.Add(seam);",
            "   }",
            "   if(seams.Count==0){result.Add(source);continue;}",
            "   R[] pieces=new R[seams.Count+1];for(int i=0;i<pieces.Length;i++)pieces[i]=new R();",
            "   foreach(int at in source.cells){int id=0;while(id<seams.Count&&at%W>=seams[id][at/W])id++;AddCell(pieces[id],at);}",
            "   foreach(R part in pieces)if(part.cells.Count>0)result.Add(part);",
            "   log.Append(source.x0).Append(':').Append(pieces.Length).Append(\" fit=\").Append(F(best[n-1])).Append('\\n');",
            "  }",
            "  File.WriteAllText(Path.Combine(Job,\"gap-refinement.txt\"),log.ToString());return result;",
            " }",
            " static int[] FindCharacterSeam(float[] soft,int center,int lo,int hi,double peak){",
            "  lo=Math.Max(1,Math.Min(lo,center));hi=Math.Min(W-1,Math.Max(hi,center));int span=hi-lo+1;",
            "  double[] previous=new double[span],next=new double[span];short[] back=new short[span*H];",
            "  for(int y=0;y<H;y++){if((y&31)==0)Check();for(int j=0;j<span;j++){",
            "   int x=lo+j;double ink=(soft[y*W+x-1]+soft[y*W+x])/Math.Max(0.001,2*peak),cost=ink*ink*20+Math.Abs(x-center)*0.01;",
            "   if(y==0){next[j]=cost;continue;}int winner=j;double best=previous[j];for(int d=-1;d<=1;d++){int k=j+d;if(k>=0&&k<span&&previous[k]+Math.Abs(d)*0.05<best){winner=k;best=previous[k]+Math.Abs(d)*0.05;}}next[j]=best+cost;back[y*span+j]=(short)(winner-j);",
            "  }double[] swap=previous;previous=next;next=swap;}",
            "  int last=0;for(int j=1;j<span;j++)if(previous[j]<previous[last])last=j;int[] path=new int[H];for(int y=H-1;y>=0;y--){path[y]=lo+last;if(y>0)last+=back[y*span+last];}return path;",
            " }",
            " // Disconnected accents/dots without tall seeds still belong to a character; never drop them.",
            " static void CompleteLabels(List<int> cells,int[] labels,int count,bool favorVerticalAlignment){",
            "  double[] xs=new double[count+1],ys=new double[count+1];int[] sizes=new int[count+1];",
            "  foreach(int at in cells){int id=labels[at];if(id>0){xs[id]+=at%W;ys[id]+=at/W;sizes[id]++;}}",
            "  for(int i=1;i<=count;i++)if(sizes[i]>0){xs[i]/=sizes[i];ys[i]/=sizes[i];}",
            "  foreach(int at in cells)if(labels[at]==0){int winner=0;double best=double.MaxValue,x=at%W,y=at/W;for(int i=1;i<=count;i++)if(sizes[i]>0){double dx=x-xs[i],dy=y-ys[i],distance=dx*dx*(favorVerticalAlignment?4:1)+dy*dy;if(distance<best){best=distance;winner=i;}}if(winner==0)throw new Exception(\"No valid region seeds.\");labels[at]=winner;}",
            " }",
            " static double SeedQuality(List<R> seeds,float[] soft,double peak){double sum=0,weight=0;foreach(R seed in seeds){Check();double w=seed.x1-seed.x0+1;sum+=GlyphTemplates.Match(soft,W,seed.x0,seed.y0,seed.x1,seed.y1,peak)*w;weight+=w;}return sum/Math.Max(1,weight);}",
            " static bool CompatibleSeeds(List<R> a,List<R> b,int height){",
            "  if(a.Count<2||a.Count!=b.Count)return false;",
            "  for(int i=0;i<a.Count;i++){double ca=(a[i].x0+a[i].x1)*0.5,cb=(b[i].x0+b[i].x1)*0.5;if(Math.Abs(ca-cb)>height*0.12)return false;}",
            "  return true;",
            " }",
            " static int Root(int[] parents,int at){while(parents[at]!=at){parents[at]=parents[parents[at]];at=parents[at];}return at;}",
            " static void AddCell(R region,int at){int x=at%W,y=at/W;region.cells.Add(at);region.x0=Math.Min(region.x0,x);region.x1=Math.Max(region.x1,x);region.y0=Math.Min(region.y0,y);region.y1=Math.Max(region.y1,y);}",
            " static double ForegroundThreshold(float[] values,float peak){",
            "  // Otsu between-class variance on this image, not a screenshot-specific contrast cutoff.",
            "  long[] histogram=new long[256];double sum=0;foreach(float value in values){int bin=Math.Max(0,Math.Min(255,(int)(value/peak*255)));histogram[bin]++;sum+=bin;}",
            "  long count=0;double partial=0,best=-1;int first=0,last=0;",
            "  for(int i=0;i<255;i++){count+=histogram[i];partial+=i*histogram[i];long rest=values.Length-count;if(count==0||rest==0)continue;double delta=partial/count-(sum-partial)/rest,variance=(double)count*rest*delta*delta;if(variance>best+0.0001){best=variance;first=last=i;}else if(Math.Abs(variance-best)<=0.0001)last=i;}",
            "  return peak*Math.Max(0.08,Math.Min(0.60,((first+last)*0.5+1)/255.0));",
            " }",
            " static double Compactness(R region,float[] soft){",
            "  float[] isolated=new float[soft.Length];double peak=0;foreach(int at in region.cells){isolated[at]=soft[at];peak=Math.Max(peak,soft[at]);}int area=0;foreach(int at in region.cells)if(soft[at]>=peak*0.5)area++;double hull=ConvexArea(isolated,peak*0.5,region);return hull>0?area/hull:0;",
            " }",
            " static List<R> GroupAlignedFragments(List<R> input,float[] soft){",
            "  // Similar-sized, complete compact subjects are not detached glyph strokes merely because their x projections overlap.",
            "  // Retain the small-dot/large-stem route for i/j and asymmetric detached character fragments.",
            "  if(input.Count>400)throw new Exception(\"Too many foreground fragments for safe grouping.\");",
            "  List<R> result=new List<R>(input);Dictionary<R,double> compact=new Dictionary<R,double>();bool changed=true;",
            "  while(changed){Check();changed=false;for(int i=0;i<result.Count&&!changed;i++)for(int j=i+1;j<result.Count;j++){",
            "   if((j&31)==0)Check();R a=result[i],b=result[j];int overlap=Math.Min(a.x1,b.x1)-Math.Max(a.x0,b.x0)+1,minWidth=Math.Min(a.x1-a.x0+1,b.x1-b.x0+1),gap=Math.Max(0,Math.Max(a.y0-b.y1-1,b.y0-a.y1-1));",
            "   int extent=Math.Max(Math.Max(a.x1-a.x0+1,b.x1-b.x0+1),Math.Max(a.y1-a.y0+1,b.y1-b.y0+1));if(overlap<Math.Max(1,minWidth*0.35)||gap>extent*1.25)continue;",
            "   double ratio=(double)Math.Min(a.cells.Count,b.cells.Count)/Math.Max(a.cells.Count,b.cells.Count);",
            "   if(ratio>0.35){if(!compact.ContainsKey(a))compact[a]=Compactness(a,soft);if(!compact.ContainsKey(b))compact[b]=Compactness(b,soft);if(compact[a]>0.85&&compact[b]>0.85)continue;}",
            "   else{R large=a.cells.Count>b.cells.Count?a:b;if(!compact.ContainsKey(large))compact[large]=Compactness(large,soft);if(compact[large]>0.85&&large.x1-large.x0+1>(large.y1-large.y0+1)*0.4)continue;}",
            "   a.cells.AddRange(b.cells);a.x0=Math.Min(a.x0,b.x0);a.y0=Math.Min(a.y0,b.y0);a.x1=Math.Max(a.x1,b.x1);a.y1=Math.Max(a.y1,b.y1);compact.Remove(a);result.RemoveAt(j);changed=true;break;",
            "  }}return result;",
            " }",
            " static void Stage(string text){File.WriteAllText(Path.Combine(Job,\"phase.txt\"),text,new UTF8Encoding(false));}",
            "",
            " static float[] Blur(float[] a){float[] b=new float[a.Length];for(int y=0;y<H;y++){if((y&31)==0)Check();for(int x=0;x<W;x++){double sum=0,weight=0;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int nx=x+dx,ny=y+dy;if(nx<0||ny<0||nx>=W||ny>=H)continue;int w=(dx==0?2:1)*(dy==0?2:1);sum+=a[ny*W+nx]*w;weight+=w;}b[y*W+x]=(float)(sum/weight);}}return b;}",
            " static List<R> Components(bool[] mask){bool[] seen=new bool[mask.Length];List<R> list=new List<R>();for(int i=0;i<mask.Length;i++){if((i&8191)==0)Check();if(!mask[i]||seen[i])continue;R r=new R();Queue<int> q=new Queue<int>();seen[i]=true;q.Enqueue(i);while(q.Count>0){int at=q.Dequeue(),x=at%W,y=at/W;r.cells.Add(at);r.x0=Math.Min(r.x0,x);r.y0=Math.Min(r.y0,y);r.x1=Math.Max(r.x1,x);r.y1=Math.Max(r.y1,y);for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int nx=x+dx,ny=y+dy;if(nx<0||ny<0||nx>=W||ny>=H)continue;int ni=ny*W+nx;if(mask[ni]&&!seen[ni]){seen[ni]=true;q.Enqueue(ni);}}}list.Add(r);}return list;}",
            " static bool Same(int[] ids,int x,int y,int id){return x>=0&&y>=0&&x<W&&y<H&&ids[y*W+x]==id;}",
            " static void Edge(List<E> edges,Dictionary<int,List<int>> starts,int x,int y,int u,int v,int d){int key=y*(W+1)+x;List<int> list;if(!starts.TryGetValue(key,out list)){list=new List<int>();starts[key]=list;}list.Add(edges.Count);edges.Add(new E(x,y,u,v,d));}",
            " static List<List<P>> Trace(List<int> cells,int[] ids,int id){List<E> edges=new List<E>();Dictionary<int,List<int>> starts=new Dictionary<int,List<int>>();int visitedCells=0;foreach(int at in cells){if((visitedCells++&8191)==0)Check();int x=at%W,y=at/W;if(!Same(ids,x,y-1,id))Edge(edges,starts,x,y,x+1,y,0);if(!Same(ids,x+1,y,id))Edge(edges,starts,x+1,y,x+1,y+1,1);if(!Same(ids,x,y+1,id))Edge(edges,starts,x+1,y+1,x,y+1,2);if(!Same(ids,x-1,y,id))Edge(edges,starts,x,y+1,x,y,3);}",
            "  List<List<P>> loops=new List<List<P>>();List<double> areas=new List<double>();double largest=0;",
            "  for(int s=0;s<edges.Count;s++){if((s&8191)==0)Check();if(edges[s].used)continue;E first=edges[s];int cur=s,guard=0;bool closed=false;List<P> points=new List<P>();while(guard++<=edges.Count){E e=edges[cur];if(e.used)break;e.used=true;points.Add(new P(e.x,e.y));if(e.u==first.x&&e.v==first.y){closed=true;break;}List<int> choices;int next=-1,best=99;if(starts.TryGetValue(e.v*(W+1)+e.u,out choices))foreach(int ei in choices){E c=edges[ei];if(c.used)continue;int turn=(c.d-e.d+4)%4,rank=turn==1?0:turn==0?1:turn==3?2:3;if(rank<best){best=rank;next=ei;}}if(next<0)break;cur=next;}if(!closed)throw new Exception(\"Unclosed contour.\");",
            "   double area=0;List<P> simple=new List<P>();for(int k=0;k<points.Count;k++){P a=points[(k+points.Count-1)%points.Count],b=points[k],c=points[(k+1)%points.Count];if((b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x)!=0)simple.Add(b);}for(int k=0;k<simple.Count;k++){P a=simple[k],b=simple[(k+1)%simple.Count];area+=(double)a.x*b.y-(double)b.x*a.y;}",
            "   if(area>0&&simple.Count>=3){loops.Add(simple);areas.Add(area/2);largest=Math.Max(largest,area/2);}",
            "  }",
            "  List<List<P>> result=new List<List<P>>();for(int i=0;i<loops.Count;i++){if(areas[i]<Math.Max(2,largest*0.002))continue;double eps=0.35;List<P> simple=Simplify(loops[i],eps);while(simple.Count>MAXPOINTS&&eps<8){eps*=1.5;simple=Simplify(loops[i],eps);}if(simple.Count>MAXPOINTS||simple.Count<3)throw new Exception(\"Contour complexity limit reached.\");result.Add(simple);}return result;",
            " }",
            " static List<P> Simplify(List<P> p,double eps){int split=1;double far=-1;for(int i=1;i<p.Count;i++){double dx=p[i].x-p[0].x,dy=p[i].y-p[0].y,d=dx*dx+dy*dy;if(d>far){far=d;split=i;}}List<P> open=new List<P>(p);open.Add(p[0]);bool[] keep=new bool[open.Count];keep[0]=keep[split]=keep[open.Count-1]=true;Reduce(open,0,split,eps*eps,keep);Reduce(open,split,open.Count-1,eps*eps,keep);List<P> result=new List<P>();for(int i=0;i<p.Count;i++)if(keep[i])result.Add(p[i]);return result;}",
            " static void Reduce(List<P> p,int first,int last,double eps2,bool[] keep){Stack<int[]> stack=new Stack<int[]>();stack.Push(new int[]{first,last});while(stack.Count>0){int[] range=stack.Pop();int a=range[0],b=range[1],at=-1;double max=eps2;double vx=p[b].x-p[a].x,vy=p[b].y-p[a].y,len=vx*vx+vy*vy;for(int i=a+1;i<b;i++){double wx=p[i].x-p[a].x,wy=p[i].y-p[a].y,t=len==0?0:Math.Max(0,Math.Min(1,(wx*vx+wy*vy)/len));double dx=wx-t*vx,dy=wy-t*vy,d=dx*dx+dy*dy;if(d>max){max=d;at=i;}}if(at>=0){keep[at]=true;stack.Push(new int[]{a,at});stack.Push(new int[]{at,b});}}}",
            "}",
            "// Lightweight single-glyph shape veto using Windows' installed generic font families.",
            "// This is not full OCR: it prevents a convincing existing glyph from being split by weak necks.",
            "public static class GlyphTemplates {",
            " const int NW=36,NH=48;",
            " class Template { public ulong[] bits,expanded;public int area;public double aspect; }",
            " static List<Template> templates;",
            " static object gate=new object();",
            " static List<Template> Templates(){",
            "  lock(gate){",
            "   if(templates!=null)return templates;",
            "   templates=new List<Template>();",
            "   FontFamily[] families={FontFamily.GenericSansSerif,FontFamily.GenericSerif,FontFamily.GenericMonospace};",
            "   FontStyle[] styles={FontStyle.Regular,FontStyle.Bold,FontStyle.Italic};",
            "   string alphabet=\"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz\";",
            "   foreach(FontFamily family in families)foreach(FontStyle style in styles){",
            "    if(!family.IsStyleAvailable(style))continue;",
            "    using(Font font=new Font(family,72,style,GraphicsUnit.Pixel))foreach(char letter in alphabet){",
            "     using(Bitmap bitmap=new Bitmap(160,130,PixelFormat.Format32bppArgb)){",
            "      using(Graphics graphics=Graphics.FromImage(bitmap)){",
            "       graphics.Clear(Color.Transparent);graphics.TextRenderingHint=System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;",
            "       graphics.DrawString(letter.ToString(),font,Brushes.White,new PointF(8,8),StringFormat.GenericTypographic);",
            "      }",
            "      BitmapData data=bitmap.LockBits(new Rectangle(0,0,bitmap.Width,bitmap.Height),ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);",
            "      float[] values=new float[bitmap.Width*bitmap.Height];",
            "      try{byte[] row=new byte[bitmap.Width*4];for(int y=0;y<bitmap.Height;y++){Marshal.Copy(IntPtr.Add(data.Scan0,y*data.Stride),row,0,row.Length);for(int x=0;x<bitmap.Width;x++)values[y*bitmap.Width+x]=row[x*4+3]/255.0f;}}finally{bitmap.UnlockBits(data);}",
            "      double aspect;bool[] mask=Normalize(values,bitmap.Width,0,0,bitmap.Width-1,bitmap.Height-1,0.5,out aspect);",
            "      if(mask!=null){int area=0;foreach(bool value in mask)if(value)area++;templates.Add(new Template{bits=Pack(mask),expanded=Pack(Expand(mask)),area=area,aspect=aspect});}",
            "     }",
            "    }",
            "   }",
            "   return templates;",
            "  }",
            " }",
            " static bool[] Normalize(float[] values,int width,int x0,int y0,int x1,int y1,double level,out double aspect){",
            "  int l=x1,r=x0,t=y1,b=y0;",
            "  for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++)if(values[y*width+x]>=level){l=Math.Min(l,x);r=Math.Max(r,x);t=Math.Min(t,y);b=Math.Max(b,y);}",
            "  if(l>r||t>b){aspect=0;return null;}",
            "  int w=r-l+1,h=b-t+1;aspect=(double)w/h;bool[] normalized=new bool[NW*NH];",
            "  for(int y=0;y<NH;y++)for(int x=0;x<NW;x++){",
            "   int xx=l+Math.Min(w-1,(int)((x+0.5)*w/NW)),yy=t+Math.Min(h-1,(int)((y+0.5)*h/NH));",
            "   normalized[y*NW+x]=values[yy*width+xx]>=level;",
            "  }",
            "  return normalized;",
            " }",
            " static bool[] Expand(bool[] mask){bool[] result=new bool[mask.Length];for(int y=0;y<NH;y++)for(int x=0;x<NW;x++){bool value=false;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int xx=x+dx,yy=y+dy;if(xx>=0&&xx<NW&&yy>=0&&yy<NH)value|=mask[yy*NW+xx];}result[y*NW+x]=value;}return result;}",
            " static ulong[] Pack(bool[] mask){ulong[] words=new ulong[(mask.Length+63)/64];for(int i=0;i<mask.Length;i++)if(mask[i])words[i>>6]|=1UL<<(i&63);return words;}",
            " static int PopCount(ulong value){value-=(value>>1)&0x5555555555555555UL;value=(value&0x3333333333333333UL)+((value>>2)&0x3333333333333333UL);value=(value+(value>>4))&0x0F0F0F0F0F0F0F0FUL;return (int)((value*0x0101010101010101UL)>>56);}",
            " public static double Match(float[] values,int width,int x0,int y0,int x1,int y1,double peak){return MatchCore(values,width,x0,y0,x1,y1,peak,false);}",
            " public static double MatchTolerant(float[] values,int width,int x0,int y0,int x1,int y1,double peak){return MatchCore(values,width,x0,y0,x1,y1,peak,true);}",
            " static double MatchCore(float[] values,int width,int x0,int y0,int x1,int y1,double peak,bool tolerant){",
            "  double best=0;List<Template> models=Templates();",
            "  foreach(double ratio in new double[]{0.25,0.4,0.55}){",
            "   double aspect;bool[] sample=Normalize(values,width,x0,y0,x1,y1,peak*ratio,out aspect);if(sample==null)continue;",
            "   ulong[] sampleBits=Pack(sample),sampleExpanded=tolerant?Pack(Expand(sample)):null;int sampleArea=0;foreach(bool value in sample)if(value)sampleArea++;",
            "   foreach(Template model in models){",
            "    double proportion=Math.Min(aspect,model.aspect)/Math.Max(aspect,model.aspect);if(proportion<0.80)continue;",
            "    int intersection=0,union=0;",
            "    for(int i=0;i<sampleBits.Length;i++){if(tolerant){intersection+=PopCount(sampleBits[i]&model.expanded[i])+PopCount(model.bits[i]&sampleExpanded[i]);}else{union+=PopCount(sampleBits[i]|model.bits[i]);intersection+=PopCount(sampleBits[i]&model.bits[i]);}}",
            "    double fit=(double)intersection/Math.Max(1,tolerant?sampleArea+model.area:union)*proportion;if(fit>best)best=fit;",
            "   }",
            "  }",
            "  return best;",
            " }",
            "}",
            "'@",
            "Add-Type -TypeDefinition $code -ReferencedAssemblies System.Drawing",
            "[SplitWorker]::Run($job)",
            "} catch {",
            " [IO.File]::WriteAllText((Join-Path $job 'error.txt'), $_.Exception.ToString(), (New-Object Text.UTF8Encoding($false)))",
            " exit 1",
            "}"
        ].join("\n");
        // BEGIN_BATCH_HOST
        var records=[];
        for(var ri=0;ri<sources.length;ri++){
            var L=sources[ri],nativeText=isNativeText(L),foot=L.source,orig=foot instanceof FootageItem?foot.file:null,tr=L.property('ADBE Transform Group');
            var rec={layer:L,id:L.id,nativeText:nativeText,textSignature:nativeText?nativeTextPreflight(L):null,footage:foot,width:nativeText?comp.width:foot.width,height:nativeText?comp.height:foot.height,original:orig,path:orig?orig.fsName:null,length:orig?orig.length:0,modified:orig?stamp(orig):-1,proxy:foot?!!foot.useProxy:false,enabled:L.enabled,locked:L.locked,anchor:tr.property('ADBE Anchor Point').value.slice(0),position:tr.property('ADBE Position').value.slice(0),scale:tr.property('ADBE Scale').value.slice(0),rotation:tr.property('ADBE Rotate Z').value,parts:null};
            rec.matte=L.trackMatteLayer||null;rec.matteType=L.trackMatteType;
            rec.startTime=L.startTime;rec.stretch=L.stretch;rec.timeRemapEnabled=!!L.timeRemapEnabled;rec.remapTime=foot instanceof CompItem&&rec.timeRemapEnabled?L.property('ADBE Time Remapping').valueAtTime(comp.time,false):null;
            rec.maskCount=L.property('ADBE Mask Parade').numProperties;rec.effectCount=L.property('ADBE Effect Parade').numProperties;
            rec.token=token+'_'+ri;rec.folder=new Folder(folder.fsName+'/'+rec.token);
            rec.image=orig;rec.snapshot=!nativeText&&(foot instanceof CompItem||rec.proxy||!orig||! /\.(png|jpe?g|bmp|tiff?)$/i.test(orig.name));
            records.push(rec);
        }
        state={token:token,build:BUILD,active:true,mutating:false,cancelRequested:false,task:null,started:new Date().getTime(),project:app.project,comp:comp,compId:comp.id,records:records,index:0,record:records[0],phase:'PREPARING',snapshotTime:comp.time,folder:folder,progress:null};
        function current(){return G.__ZExpSplitPortable===state&&state.active&&!state.cancelRequested;}
        function checkCurrent(){if(!current())throw Error('任务已取消或被替换。');if(app.project!==state.project)throw Error('工程已切换，已停止本批操作。');}
        function closeProgress(){
            if(!state||!state.progress)return;
            var p=state.progress;if(p.closing)return;
            if(p.userClosing){if(!state.mutating)p.closing=true;return;}p.closing=true;
            try{p.window.close();}catch(e){try{p.window.hide();}catch(ignoreHide){}state.uiCleanupError=String(e);}
        }
        function stop(cancelWorker){
            state.active=false;
            if(state.task!==null){try{app.cancelTask(state.task);}catch(ignoreTask){}state.task=null;}
            if(cancelWorker)for(var ci=0;ci<state.records.length;ci++)try{if(state.records[ci].folder.exists)writeFile(new File(state.records[ci].folder.fsName+'/cancel.txt'),'cancel');}catch(ignoreCancel){}
            closeProgress();
        }
        function requestCancel(){
            if(G.__ZExpSplitPortable!==state||!state.active||state.cancelRequested)return;
            state.cancelRequested=true;
            try{if(state.progress){state.progress.cancelButton.enabled=false;state.progress.status.text='正在取消…';state.progress.detail.text='等待当前 AE 操作返回；生成阶段会恢复本批次全部输出。';}}catch(ignoreCancelUI){}
            if(state.mutating||state.phase==='APPLYING')return;
            state.phase='CANCELLED';stop(true);
        }
        function makeProgress(){
            stage='创建进度窗口';var window=new Window('palette','AE 智能分层 · 进度',undefined,{closeButton:true});
            window.orientation='column';window.alignChildren=['fill','top'];window.margins=14;window.spacing=8;
            var status=window.add('statictext',undefined,'准备分析图层…',{multiline:true});status.preferredSize=[420,42];
            var bar=window.add('progressbar',undefined,0,0,Math.max(1,records.length));bar.preferredSize=[420,18];
            var detail=window.add('statictext',undefined,'',{multiline:true});detail.preferredSize=[420,42];
            var note=window.add('statictext',undefined,'按当前阶段显示已完成数量；后台分析单层时进度会等待结果。',{multiline:true});note.preferredSize=[420,34];
            var cancelButton=window.add('button',undefined,'取消本批次');cancelButton.alignment='right';cancelButton.helpTip='取消会恢复本批次输出；当前同步 AE 快照或图层操作需返回后才能响应。';
            var p={window:window,bar:bar,status:status,detail:detail,cancelButton:cancelButton,closing:false};state.progress=p;
            cancelButton.onClick=requestCancel;
            window.onClose=function(){
                if(!p.closing&&G.__ZExpSplitPortable===state&&state.active){p.userClosing=true;try{requestCancel();}finally{p.userClosing=false;}return !state.mutating;}
                return true;
            };
            window.center();window.show();checkCurrent();
        }
        function progress(statusText,done,total,detailText){
            checkCurrent();var p=state.progress;
            if(p){p.status.text=statusText;p.status.helpTip=statusText;p.detail.text=detailText||'';p.bar.minvalue=0;p.bar.maxvalue=Math.max(1,total);p.bar.value=Math.max(0,Math.min(done,total));p.window.update();}
            checkCurrent();
        }
        function phaseLabel(rec){
            try{
                var f=new File(rec.folder.fsName+'/phase.txt');if(!f.exists)return '等待后台启动或返回结果';
                var text=readFile(f,4096),map={LOAD_IMAGE:'读取图片',WAIT_INPUT:'等待分析快照就绪',WAITING_IMAGE_READY:'等待快照就绪',OBJECT_SEGMENTATION:'识别前景区域与分类',TRACE:'整理轮廓'};
                return map[text]||'识别字符、分类并整理区域';
            }catch(ignorePhaseRead){return '后台正在处理，等待阶段信息';}
        }
        function schedule(){
            if(!current()||state.phase!=='ANALYSIS')return;
            state.task=app.scheduleTask("if($.global.__ZExpSplitPortable&&$.global.__ZExpSplitPortable.active&&$.global.__ZExpSplitPortable.token==='"+token+"')$.global.__ZExpSplitPortable.poll();",500,false);
        }
        function sameArray(a,b){if(!a||!b||a.length!==b.length)return false;for(var i=0;i<a.length;i++)if(Math.abs(a[i]-b[i])>0.0001)return false;return true;}
        function validateSources(){
            checkCurrent();var foundComp=null;
            for(var n=1;n<=app.project.numItems;n++){var item=app.project.item(n);if(item.id===state.compId)foundComp=item;}
            if(!foundComp||foundComp!==state.comp)throw Error('原合成已改变，结果已丢弃。');
            var layers=[];
            for(var ri=0;ri<records.length;ri++){
                var rec=records[ri],found=null;
                for(var n=1;n<=foundComp.numLayers;n++)if(foundComp.layer(n).id===rec.id)found=foundComp.layer(n);
                if(!found||found!==rec.layer||!supported(found)||found.source!==rec.footage||!isStatic2D(found))throw Error('源图层已删除、改变或不支持锚点居中：'+rec.layer.name);
                if(found.property('ADBE Mask Parade').numProperties!==rec.maskCount||found.property('ADBE Effect Parade').numProperties!==rec.effectCount)throw Error('分析期间源蒙版或效果数量已改变：'+rec.layer.name);
                if((found.trackMatteLayer||null)!==rec.matte||found.trackMatteType!==rec.matteType)throw Error('分析期间轨道遮罩关系已改变：'+rec.layer.name);
                for(var di=1;di<=foundComp.numLayers;di++){var dependent=foundComp.layer(di);if(dependent!==found&&dependent.trackMatteLayer===found)throw Error('源图层正在提供轨道遮罩，不能隐藏：'+found.name);}
                if(rec.snapshot&&foundComp.time!==state.snapshotTime)throw Error('分析快照后时间指针已改变。');
                if(rec.footage instanceof CompItem&&(found.startTime!==rec.startTime||found.stretch!==rec.stretch||!!found.timeRemapEnabled!==rec.timeRemapEnabled||(rec.timeRemapEnabled&&!(Math.abs(found.property('ADBE Time Remapping').valueAtTime(state.snapshotTime,false)-rec.remapTime)<=0.000001))))throw Error('分析期间预合成时间映射已改变：'+rec.layer.name);
                var f=found.source instanceof FootageItem?found.source.file:null,tr=found.property('ADBE Transform Group');
                if(!sameArray(tr.property('ADBE Anchor Point').value,rec.anchor)||!sameArray(tr.property('ADBE Position').value,rec.position)||!sameArray(tr.property('ADBE Scale').value,rec.scale)||Math.abs(tr.property('ADBE Rotate Z').value-rec.rotation)>0.0001)throw Error('分析期间变换已改变：'+rec.layer.name);
                if(rec.nativeText){if(!isNativeText(found)||nativeTextPreflight(found)!==rec.textSignature)throw Error('源文字或样式已改变：'+rec.layer.name);}
                else if(found.source.width!==rec.width||found.source.height!==rec.height||!!found.source.useProxy!==rec.proxy)throw Error('源素材尺寸或代理已改变：'+rec.layer.name);
                if(rec.path!==null&&(!f||!f.exists||f.fsName!==rec.path||f.length!==rec.length||stamp(f)!==rec.modified))throw Error('源图片文件已改变：'+rec.layer.name);
                layers.push(found);
            }
            return layers;
        }
        function apply(){
            if(!current()||state.phase!=='APPLYING')return;
            if(app.project.renderQueue&&app.project.renderQueue.rendering)throw Error('AE 已开始渲染，本批未提交，请渲染结束后重新运行。');
            stage='批次提交前验证';var layers=validateSources(),allParts=[],selection=[],oldStates=[],copies=[],outputs=[],total=0,points=0,completed=0,nativeCount=0,counts={TEXT:0,OBJECT:0,UNKNOWN:0};
            for(var ri=0;ri<records.length;ri++){
                var parts=spatialOrder(records[ri].parts,records[ri]);allParts.push(parts);total+=parts.length;
                for(var pi=0;pi<parts.length;pi++){counts[parts[pi].kind]++;for(var li=0;li<parts[pi].loops.length;li++)points+=parts[pi].loops[li].length;}
                var hadMasks=layers[ri].property('ADBE Mask Parade').numProperties>0;
                if(hadMasks)for(var pi=0;pi<parts.length;pi++)if(parts[pi].loops.length>1)throw Error('已有蒙版的源层含多轮廓区域，无法安全求交：'+layers[ri].name);
                oldStates.push({layer:layers[ri],enabled:layers[ri].enabled,locked:layers[ri].locked});
            }
            if(total>120||points>12000)throw Error('本批结果过大（最多120个输出区域/12000个轮廓点），未修改合成。');
            for(var si=1;si<=comp.numLayers;si++){var layer=comp.layer(si);selection.push({layer:layer,value:layer.selected});}
            var undo=false,renderCaptured=false,oldRendering,committed=false,errors='',start=new Date().getTime();state.mutating=true;
            try{
                progress('生成输出图层',0,total,'已完成 0/'+total+' 个区域；整批统一提交。');validateSources();
                try{oldRendering=app.disableRendering;if(typeof oldRendering==='boolean'){app.disableRendering=true;renderCaptured=true;}}catch(ignorePreview){}
                app.beginUndoGroup('AE 智能分层 · 多图层批次');undo=true;
                for(var ri=0;ri<records.length;ri++){
                    var rec=records[ri],L=layers[ri],parts=allParts[ri],sourceOutputs=[],hadMasks=L.property('ADBE Mask Parade').numProperties>0;
                    L.locked=false;
                    for(var i=0;i<parts.length;i++){
                        checkCurrent();if(new Date().getTime()-start>45000)throw Error('本批 AE 输出超时，已请求整体回滚。');
                        stage='生成 '+L.name+' 区域 '+(i+1)+'/'+parts.length;
                        var copy=L.duplicate();copies.push(copy);outputs.push(copy);sourceOutputs.push(copy);
                        if(L.hasTrackMatte&&L.trackMatteLayer&&typeof copy.setTrackMatte==='function')copy.setTrackMatte(L.trackMatteLayer,L.trackMatteType);
                        copy.locked=false;copy.enabled=true;copy.moveBefore(i>0?sourceOutputs[i-1]:L);
                        var kindName=parts[i].kind==='TEXT'?'文字':parts[i].kind==='OBJECT'?'非文字':'类型待确认';copy.name=L.name+' ['+(rec.nativeText?'可编辑文字':kindName)+'] · 区域_'+(i+1)+(rec.nativeText?' · '+parts[i].text:'');
                        if(rec.nativeText){configureNativeCopy(copy,parts[i],rec);nativeCount++;}
                        else{
                        var minX=rec.width,minY=rec.height,maxX=0,maxY=0;
                        for(var j=0;j<parts[i].loops.length;j++){
                            var vertices=parts[i].loops[j],tangents=[];
                            for(var k=0;k<vertices.length;k++){minX=Math.min(minX,vertices[k][0]);minY=Math.min(minY,vertices[k][1]);maxX=Math.max(maxX,vertices[k][0]);maxY=Math.max(maxY,vertices[k][1]);tangents.push([0,0]);}
                            var mask=copy.property('ADBE Mask Parade').addProperty('ADBE Mask Atom');mask.name='主体与模糊边缘';mask.maskMode=hadMasks?MaskMode.INTERSECT:MaskMode.ADD;mask.inverted=false;
                            var shape=new Shape();shape.vertices=vertices;shape.inTangents=tangents;shape.outTangents=tangents;shape.closed=true;mask.property('ADBE Mask Shape').setValue(shape);mask.property('ADBE Mask Opacity').setValue(100);mask.property('ADBE Mask Feather').setValue([parts[i].feather,parts[i].feather]);mask.property('ADBE Mask Offset').setValue(parts[i].padding);
                        }
                        var anchor=[(minX+maxX)/2,(minY+maxY)/2],dx=anchor[0]-rec.anchor[0],dy=anchor[1]-rec.anchor[1],rad=rec.rotation*Math.PI/180,sx=rec.scale[0]/100,sy=rec.scale[1]/100,tr=copy.property('ADBE Transform Group');
                        tr.property('ADBE Anchor Point').setValue(anchor);tr.property('ADBE Position').setValue([rec.position[0]+Math.cos(rad)*sx*dx-Math.sin(rad)*sy*dy,rec.position[1]+Math.sin(rad)*sx*dx+Math.cos(rad)*sy*dy]);
                        }
                        completed++;
                        progress('生成输出图层',completed,total,'源 '+(ri+1)+'/'+records.length+'：'+L.name+'；已完成 '+completed+'/'+total+' 个区域。');
                        validateSources();
                    }
                }
                checkCurrent();validateSources();
                for(var ri=0;ri<layers.length;ri++)layers[ri].enabled=false;
                for(var d=1;d<=comp.numLayers;d++)comp.layer(d).selected=false;
                for(var o=0;o<outputs.length;o++)outputs[o].selected=true;
                for(var ri=0;ri<oldStates.length;ri++)oldStates[ri].layer.locked=oldStates[ri].locked;
                checkCurrent();committed=true;
            }catch(e){
                errors=describe(e,stage);try{if(state.progress){state.progress.status.text='正在恢复本批次…';state.progress.cancelButton.enabled=false;}}catch(ignoreRollbackUI){}
                for(var ci=copies.length-1;ci>=0;ci--)try{copies[ci].locked=false;copies[ci].remove();}catch(clean){errors+='\n清理输出失败：'+clean;}
                for(var oi=0;oi<oldStates.length;oi++)try{oldStates[oi].layer.enabled=oldStates[oi].enabled;oldStates[oi].layer.locked=oldStates[oi].locked;}catch(restore){errors+='\n恢复源图层失败：'+restore;}
                for(var qi=0;qi<selection.length;qi++)try{selection[qi].layer.selected=selection[qi].value;}catch(restoreSelection){errors+='\n恢复选择失败：'+restoreSelection;}
            }finally{
                try{if(undo)app.endUndoGroup();}catch(undoError){errors+='\n关闭撤销组失败：'+undoError;}
                try{if(renderCaptured)app.disableRendering=oldRendering;}catch(renderError){errors+='\n恢复预览失败：'+renderError;}
                state.mutating=false;
            }
            if(errors)throw Error((committed?'分层已提交但收尾异常，请检查工程。\n':'')+errors);
            state.phase='DONE';stop(false);
            alert('AE 智能分层完成（'+BUILD+'）：处理 '+records.length+' 个源图层，生成 '+outputs.length+' 个输出（可编辑文字 '+nativeCount+'，图片文字 '+(counts.TEXT-nativeCount)+'，非文字 '+counts.OBJECT+'，类型待确认 '+counts.UNKNOWN+'）。\n原图层已保留并隐藏；锚点已居中；所有输出已选中。各源内部按左到右、下到上编号与堆叠。图片/预合成画面结果不是可编辑文字；原生文字结果可编辑。'+(state.uiCleanupError?'\n进度窗口收尾异常：'+state.uiCleanupError:''));
        }
        function advanceAnalysis(){
            checkCurrent();
            while(state.index<records.length&&records[state.index].nativeText){
                var rec=records[state.index];state.record=rec;
                progress('分析图层 '+(state.index+1)+'/'+records.length,state.index+1,records.length,rec.layer.name+'：原生文字测量完成，无需图像识别。');state.index++;
            }
            if(state.index>=records.length){state.phase='APPLYING';apply();return;}
            state.record=records[state.index];launchWorker();schedule();
        }
        function launchWorker(){
            checkCurrent();stage='异步启动 Windows 后台（无 File.execute）';var rec=state.record,job=rec.folder;rec.started=new Date().getTime();
            progress('分析图层 '+(state.index+1)+'/'+records.length,state.index,records.length,rec.layer.name+'；已完成 '+state.index+'/'+records.length+' 个源图层。');
            var vbs=new File(job.fsName+'/launch.vbs');
            var code=[
                'Option Explicit',
                'On Error Resume Next',
                'Dim fso, sh, job, exe, command, status, failure, number, message, marker',
                'Set fso = CreateObject("Scripting.FileSystemObject")',
                'Set sh = CreateObject("WScript.Shell")',
                'job = fso.GetParentFolderName(WScript.ScriptFullName)',
                'exe = sh.ExpandEnvironmentStrings("%SystemRoot%") & "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"',
                'command = Chr(34) & exe & Chr(34) & " -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File " & Chr(34) & job & "\\worker.ps1" & Chr(34)',
                'Err.Clear',
                'status = sh.Run(command, 0, False)',
                'number = Err.Number',
                'message = Err.Description',
                'If number <> 0 Then',
                '  Set failure = fso.CreateTextFile(job & "\\launch_error.txt", True, True)',
                '  failure.WriteLine "WSH launch failed: " & CStr(number) & " | " & message',
                '  failure.Close',
                'Else',
                '  Set marker = fso.CreateTextFile(job & "\\launch_ok.txt", True, False)',
                '  marker.WriteLine "submitted"',
                '  marker.Close',
                'End If'
            ].join('\r\n');
            vbs.encoding='BINARY';if(!vbs.open('w'))throw Error('无法写入后台启动文件。');try{if(!vbs.write(code))throw Error('写入后台启动文件失败。');}finally{vbs.close();}
            var systemDir=ps.parent.parent.parent,commandHost=new File(systemDir.fsName+'/cmd.exe'),scriptHost=new File(systemDir.fsName+'/wscript.exe');
            if(!commandHost.exists||!scriptHost.exists)throw Error('未找到 cmd.exe / wscript.exe。');
            var paths=[commandHost.fsName,scriptHost.fsName,vbs.fsName];for(var pi=0;pi<paths.length;pi++)if(/[\x00-\x1f"%]/.test(paths[pi]))throw Error('后台路径包含不支持的字符。');
            checkCurrent();var command='"'+commandHost.fsName+'" /d /s /v:off /c ""'+scriptHost.fsName+'" //B //NoLogo "'+vbs.fsName+'" >nul 2>&1"';system.callSystem(command);checkCurrent();
        }
        function fail(e){
            if(G.__ZExpSplitPortable!==state||state.phase==='RETIRED')return;
            var cancelled=state.cancelRequested;state.phase=cancelled?'CANCELLED':'FAILED';stop(true);
            if(!cancelled)alert('AE 智能分层报告：\n'+describe(e,stage)+'\n\n诊断目录：'+folder.fsName);
            else if(String(e).indexOf('清理输出失败')>=0||String(e).indexOf('恢复')>=0||String(e).indexOf('关闭撤销组失败')>=0)alert('取消后的清理报告：\n'+describe(e,stage));
        }
        state.cancel=requestCancel;
        state.poll=function(){
            if(!current()||state.phase!=='ANALYSIS')return;state.task=null;
            try{
                var rec=state.record;stage='等待或读取后台结果';
                if(!rec||new Date().getTime()-rec.started>45000)throw Error('当前源图层后台分析超时。');
                var launchError=new File(rec.folder.fsName+'/launch_error.txt'),failure=new File(rec.folder.fsName+'/error.txt'),result=new File(rec.folder.fsName+'/result.txt');
                if(launchError.exists)throw Error(readFile(launchError,65536,'UTF-16'));if(failure.exists)throw Error(readFile(failure,65536));
                if(result.exists){
                    if(app.project&&app.project.renderQueue.rendering){progress('等待 AE 渲染结束',state.index,records.length,'已完成 '+state.index+'/'+records.length+' 个源图层；暂不写入。');schedule();return;}
                    stage='读取并验证分区结果';rec.parts=parseResult(readFile(result,2000000),rec.token,rec.width,rec.height);checkCurrent();
                    var done=state.index+1;progress('分析图层 '+done+'/'+records.length,done,records.length,rec.layer.name+' 分析完成；已完成 '+done+'/'+records.length+'。');
                    state.index=done;advanceAnalysis();return;
                }
                var elapsed=new Date().getTime()-rec.started;
                if(elapsed>20000&&!(new File(rec.folder.fsName+'/started.txt')).exists)throw Error('后台未返回 started 握手，请检查 Windows Script Host / PowerShell 是否被系统策略拦截。');
                progress('分析图层 '+(state.index+1)+'/'+records.length,state.index,records.length,rec.layer.name+' · '+phaseLabel(rec)+'；已完成 '+state.index+'/'+records.length+' 个源图层。');schedule();
            }catch(e){fail(e);}
        };
        G.__ZExpSplitPortable=state;
        makeProgress();
        for(var ri=0;ri<records.length;ri++){
            var rec=records[ri];state.record=rec;state.index=ri;
            progress('准备图层 '+(ri+1)+'/'+records.length,ri,records.length,rec.layer.name+'；同步 AE 操作需返回后才能取消。');
            if(rec.nativeText)rec.parts=prepareNativeText(rec);
            else{
                if(!rec.folder.create())throw Error('无法创建源图层临时目录：'+rec.layer.name);
                writeFile(new File(rec.folder.fsName+'/worker.ps1'),WORKER);checkCurrent();
                if(rec.snapshot){state.mutating=true;try{rec.image=new File(rec.folder.fsName+'/analysis.png');renderAnalysisSnapshot(rec.layer,rec.image);}finally{state.mutating=false;}checkCurrent();}
                writeFile(new File(rec.folder.fsName+'/source.txt'),rec.image.fsName);
            }
            progress('准备图层 '+(ri+1)+'/'+records.length,ri+1,records.length,rec.layer.name+' 准备完成。');
        }
        validateSources();state.index=0;state.record=records[0];state.phase='ANALYSIS';advanceAnalysis();
    } catch(e){
        if(state){if(G.__ZExpSplitPortable===state)fail(e);else{try{stop(true);}catch(ignoreStaleStop){}}}
        else alert('AE 智能分层准备报告：\n'+describe(e,stage)+'\n诊断目录：'+folder.fsName);
    }
    // END_BATCH_HOST

    } catch (fatal) {
        alert('AE 智能分层入口错误：\n' + describe(fatal,stage));
    }
})();

"ZEXP_PORTABLE14_SUBMITTED";
