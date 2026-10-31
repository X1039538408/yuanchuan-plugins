const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const path = require('path');

const jsxPath = path.resolve(__dirname, '../../../zexp_image_split_portable.jsx');
const source = fs.readFileSync(jsxPath, 'utf8').replace(/^\uFEFF/, '');
const sourceFor = (kinds, token, protocol = 'AE_PARTS_V2', badKindAt = -1, boxes, pointsPerPart = 4, loopsPerPart = 1, width = 1280, height = 1280) => {
    const lines = [protocol, token, 'OK', `${width} ${height}`, String(kinds.length)];
    for (let i = 0; i < kinds.length; i++) {
        const { x, y, w = 40, h = 40 } = boxes ? boxes[i] : { x: 100 + (i % 10) * 100, y: 120 + Math.floor(i / 10) * 100 };
        lines.push((i === badKindAt ? 'OTHER' : kinds[i]) + ' 1 0 ' + loopsPerPart);
        for (let loop = 0; loop < loopsPerPart; loop++) {
            lines.push(String(pointsPerPart));
            for (let p = 0; p < pointsPerPart; p++) {
                const corner = p % 4;
                lines.push([[x,y],[x+w,y],[x+w,y+h],[x,y+h]][corner].join(' '));
            }
        }
    }
    return lines.join('\n');
};

function fixture(opts = {}) {
    const files = {}, folders = {}, alerts = [], tasks = [], undo = [], copies = [], windows = [];
    let serial = 1, applied = 0, moved = 0, globalRef = null;
    function File(p) { this.fsName = p; this.name = p.split('/').pop(); this.position = 0; }
    Object.defineProperties(File.prototype, {
        exists: { get() { return !!files[this.fsName]; } },
        length: { get() { return files[this.fsName] ? files[this.fsName].data.length : 0; } },
        modified: { get() { return new Date(files[this.fsName] ? files[this.fsName].mtime : 1000); } },
        parent: { get() { return new Folder(this.fsName.replace(/\/[^/]+$/, '')); } }
    });
    File.prototype.open = function (mode) { this.position = 0; if (mode === 'w') files[this.fsName] = { data: '', mtime: 1000 }; return true; };
    File.prototype.write = function (s) { files[this.fsName].data += s; return true; };
    File.prototype.readln = function () {
        const text = files[this.fsName].data; let end = this.position;
        while (end < text.length && text[end] !== '\n' && text[end] !== '\r') end++;
        const line = text.slice(this.position, end);
        if (end < text.length) end += text[end] === '\r' && text[end + 1] === '\n' ? 2 : 1;
        this.position = end; return line;
    };
    Object.defineProperty(File.prototype, 'eof', { get() { return this.position >= files[this.fsName].data.length; } });
    File.prototype.close = function () {};
    function Folder(p) { this.fsName = p; }
    Object.defineProperties(Folder.prototype, {
        exists: { get() { return !!folders[this.fsName]; } },
        parent: { get() { return new Folder(this.fsName.replace(/\/[^/]+$/, '')); } }
    });
    Folder.prototype.create = function () { folders[this.fsName] = true; return true; };
    Folder.temp = new Folder('C:/Temp'); Folder.system = new Folder('C:/Windows/System32');
    for (const p of ['cmd.exe', 'wscript.exe']) files['C:/Windows/System32/' + p] = { data: 'x', mtime: 1 };
    if (!opts.noPowerShell) files['C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe'] = { data: 'x', mtime: 1 };
    function Window(type, title) {
        assert.equal(type, 'palette', 'progress UI must be a non-modal palette');
        const window = { type, title, controls: [], visible: false, closed: false, onClose: null, updateHook: opts.windowUpdateHook || null,
            add(kind, bounds, text) {
                if (kind === 'checkbox' || kind === 'button' && /start|开始/i.test(text || '')) throw Error('progress palette must not contain option/start controls');
                const control = { kind, _value: kind === 'statictext' ? String(text || '') : 0, minvalue: 0, maxvalue: 100, history: [], onClick: null };
                Object.defineProperty(control, 'value', { get() { return this._value; }, set(value) { this._value = value; this.history.push(value); } });
                if (kind === 'statictext') Object.defineProperty(control, 'text', { get() { return this._value; }, set(value) { if (opts.throwStatusSetter || opts.throwStatusAfterOutput && globalRef && globalRef.__ZExpSplitPortable && globalRef.__ZExpSplitPortable.phase === 'APPLYING' && copies.length > 0) throw Error('injected UI status update failure'); this._value = value; this.history.push(value); } });
                this.controls.push(control); return control;
            },
            center() {},
            show() { this.visible = true; windows.push(this); if (this.updateHook) this.updateHook(globalRef && globalRef.__ZExpSplitPortable, this, 'show', context); return this; },
            close() { this.closed = true; this.visible = false; },
            update() { if (this.updateHook) this.updateHook(globalRef && globalRef.__ZExpSplitPortable, this, 'update'); if (opts.onProgressUpdate) opts.onProgressUpdate(globalRef && globalRef.__ZExpSplitPortable, this); if (opts.throwProgressUpdate && globalRef && globalRef.__ZExpSplitPortable.phase === 'APPLYING' && copies.length > 0) throw Error('injected palette update failure'); },
            onClose: function () { const current = globalRef && globalRef.__ZExpSplitPortable; if (current && current.progress && current.progress.window === this && current.active) current.cancel(); return true; },
            triggerClose() { const shouldClose = this.onClose ? this.onClose() : true; if (shouldClose) this.close(); return shouldClose; },
            clickCancel() { const button = this.controls.find(control => control.kind === 'button'); return button && button.onClick ? button.onClick() : undefined; }
        };
        return window;
    }

    class FootageItem {
        constructor(id) { this.width = 1280; this.height = 1280; this.mainSource = { isStill: true }; this.file = new File(`C:/Assets/${id}.png`); files[this.file.fsName] = { data: 'image', mtime: 1000 }; this.useProxy = false; }
    }
    class TextDocument {
        constructor(text = '', style = {}) { this.text = text; Object.assign(this, style); this.pointText = style.pointText !== false; this.boxText = !!style.boxText; this.justification = style.justification || 'LEFT_JUSTIFY'; this.direction = style.direction || 'LTR'; this._ligature = style.ligature !== false; this.charStyles = style.charStyles || Array.from({ length: text.length }, (_, index) => Object.assign({}, style.characterStyles && style.characterStyles[String(text).charAt(index)] || {})); this.baselineLocs = style.baselineLocs || baselineFor(text, this); }
        get ligature() { return this._ligature; }
        set ligature(value) { this._ligature = value; if (this.text !== undefined) this.baselineLocs = baselineFor(this.text, this); }
        characterRange(start, end) {
            const owner = this;
            const range = { get text() { return owner.text.slice(start, end); }, set text(value) { const nextText = owner.text.slice(0, start) + value + owner.text.slice(end); const nextStyles = (owner.charStyles || []).slice(0, start).concat(Array.from({ length: String(value).length }, () => Object.assign({}, (owner.charStyles || [])[start] || {})), (owner.charStyles || []).slice(end)); owner.text = nextText; owner.charStyles = nextStyles; owner.baselineLocs = baselineFor(owner.text, owner); } };
            const fields = ['font','fontSize','tracking','horizontalScale','verticalScale','baselineShift','applyFill','fillColor','applyStroke','strokeColor','strokeWidth','strokeOverFill','fauxBold','fauxItalic','fontCapsOption','fontBaselineOption','ligature','tsume'];
            for (const field of fields) Object.defineProperty(range, field, { get() { const style = (owner.charStyles || [])[start] || owner; if (style[field] !== undefined) return style[field]; return owner[field]; } });
            return range;
        }
    }
    function cloneDocument(doc) { return new TextDocument(doc.text, Object.assign({}, doc, { baselineLocs: doc.baselineLocs.slice() })); }
    function baselineFor(text, style = {}) {
        const lines = String(text).split(/\r\n|\r|\n/), width = s => { const chars = Array.from(s); let value = chars.reduce((sum, ch, i) => sum + ((style.charWidths && style.charWidths[ch]) || (ch === ' ' ? 12 : ch.charCodeAt(0) > 255 ? 40 : 24)) + (style.tracking || 0) + (i ? ((style.kerningPairs && style.kerningPairs[chars[i - 1] + ch]) || 0) : 0), 0); if (style.ligature !== false && style.ligaturePairs) for (const pair of Object.keys(style.ligaturePairs)) if (s.includes(pair)) value += style.ligaturePairs[pair]; return value; };
        const result = []; let y = 0;
        for (const line of lines) { if (!line.length) result.push(0, 3.402823466e38, 0, 3.402823466e38); else result.push(0, y, width(line), y); y += 48; }
        return result;
    }
    class CompItem {
        constructor(id = 20, width = 1280, height = 1280, name = 'comp') { this.id = id; this.width = width; this.height = height; this.name = name; this.time = 1; this.frameRate = 25; this.duration = 4; this.pixelAspect = 1; this._layers = []; this.numLayers = 0; this.renderQueue = { rendering: false }; }
        get layers() { const list = this._layers; if (!Object.prototype.hasOwnProperty.call(list, 'addText')) Object.defineProperty(list, 'addText', { enumerable: false, value: doc => { const layer = new Layer(1000 + copies.length); layer.makeText(doc); layer.comp = this; layer.isScratchMeasurement = this.id >= 10000; if (opts.measurementDropsStyle && layer.isScratchMeasurement) layer.textDocument.charStyles = layer.textDocument.charStyles.map(() => ({})); list.unshift(layer); this.numLayers++; return layer; } }); return list; }
        set layers(value) { this._layers = value; }
        layer(i) { const L = this._layers[i - 1]; if (L) L.comp = this; return L; }
        saveFrameToPng(time, destination) { this.lastSavedTime = time; this.lastSavedPath = destination.fsName; files[destination.fsName] = { data: 'snapshot', mtime: 1000 }; this.saveCount = (this.saveCount || 0) + 1; return true; }
        remove() { this.removed = true; }
    }
    class Layer {
        constructor(id) {
            this.id = id; this.name = `img${id}`; this.matchName = 'ADBE AV Layer'; this.source = new FootageItem(id); this.enabled = true; this.locked = false; this.selected = false;
            this.threeDLayer = !!opts.threeD; this.parent = opts.parent ? {} : null; this.startTime = opts.layerStartTime || 0; this.stretch = opts.layerStretch || 100; this.timeRemapEnabled = !!opts.timeRemapEnabled; this.remapValue = opts.remapValue === undefined ? 0 : opts.remapValue; this.trackMatteLayer = null; this.trackMatteType = 0;
            this.anchor = [0, 0]; this.position = [300, 200]; this.scale = opts.scale || [100, 100]; this.rotation = opts.rotation === undefined ? 30 : opts.rotation; this.groups = {}; this.textDocument = null; this.sourceRect = null;
        }
        makeText(value) { this.matchName = 'ADBE Text Layer'; this.source = null; this.textDocument = value instanceof TextDocument ? cloneDocument(value) : new TextDocument(String(value)); this.position = [300, 200]; this.anchor = [0, 0]; return this; }
        sourceRectAtTime() { if (this.textDocument) { const b = this.textDocument.baselineLocs; return { left: 0, top: -36, width: Math.max(0, ...Array.from({length: Math.floor(b.length / 4)}, (_,i) => b[i*4+2] - b[i*4])), height: Math.max(48, Math.ceil(b.length / 4) * 48) }; } return this.sourceRect || { left: 0, top: 0, width: this.source.width, height: this.source.height }; }
        property(name) {
            const thisLayer = this;
            if (name === 'ADBE Transform Group') return { property: key => ({ 'ADBE Anchor Point': prop(this, 'anchor'), 'ADBE Position': prop(this, 'position'), 'ADBE Scale': prop(this, 'scale'), 'ADBE Rotate Z': prop(this, 'rotation') }[key]) };
            if (name === 'ADBE Time Remapping') return { valueAtTime: (time, preExpression) => opts.remapValue === 'NaN' ? NaN : opts.remapValue === undefined ? this.remapValue : opts.remapValue };
            if (name === 'ADBE Text Properties') return { property: key => {
                if (key === 'ADBE Text Document') return { get value() { return cloneDocument(thisLayer.textDocument); }, setValue(value) { const next = value instanceof TextDocument ? cloneDocument(value) : new TextDocument(String(value)); if (thisLayer.textDocument && thisLayer.textDocument.ligature !== next.ligature) next.baselineLocs = baselineFor(next.text, next); if (opts.measurementDropsStyle && thisLayer.isScratchMeasurement && next.text) next.charStyles = next.charStyles.map(() => ({})); thisLayer.textDocument = next; }, get numKeys() { return opts.nativeTextKeys || 0; }, get expressionEnabled() { return !!opts.nativeTextExpression; } };
                if (key === 'ADBE Text Animators') return { numProperties: opts.nativeTextAnimators || 0 };
                if (key === 'ADBE Text Path Options') return { property: subkey => subkey === 'ADBE Text Path' ? { value: opts.nativeTextPath || 0 } : null };
                return null;
            } };
            if (!this.groups[name]) this.groups[name] = { numProperties: 0, addProperty: () => { this.groups[name].numProperties++; return { name: '', maskMode: 0, property: () => ({ setValue: () => { applied++; if (opts.applyCancelAt === applied && globalRef && globalRef.__ZExpSplitPortable) globalRef.__ZExpSplitPortable.cancel(); if (opts.applyCloseAt === applied && globalRef && globalRef.__ZExpSplitPortable && globalRef.__ZExpSplitPortable.progress.window.onClose) globalRef.__ZExpSplitPortable.progress.window.onClose(); if (opts.failAt === applied) throw Error('injected apply failure'); } }) }; } };
            return this.groups[name];
        }
        duplicate() {
            const copy = new Layer(100 + copies.length); copy.source = this.source; copy.matchName = this.matchName; copy.textDocument = this.textDocument ? cloneDocument(this.textDocument) : null; copy.anchor = this.anchor.slice(); copy.position = this.position.slice(); copy.scale = this.scale.slice(); copy.rotation = this.rotation; copy.comp = this.comp;
            this.comp.layers.splice(this.comp.layers.indexOf(this), 0, copy); this.comp.numLayers++; copies.push(copy); return copy;
        }
        moveBefore(target) {
            moved++; if (opts.failMoveAt === moved) throw Error('injected stack move failure');
            assert(target !== this && target.comp === this.comp);
            const layers = this.comp.layers;
            layers.splice(layers.indexOf(this), 1); layers.splice(layers.indexOf(target), 0, this);
        }
        remove() { this.removed = true; if (this.comp) { const i = this.comp.layers.indexOf(this); if (i >= 0) this.comp.layers.splice(i, 1); this.comp.numLayers--; } }
        get textDocument() { return this._textDocument; }
        set textDocument(value) { this._textDocument = value; }
        getTextProperty() { const layer = this; return { get value() { return cloneDocument(layer.textDocument); }, get numKeys() { return opts.nativeTextKeys || 0; }, get expressionEnabled() { return !!opts.nativeTextExpression; }, setValue(value) { layer.textDocument = value instanceof TextDocument ? cloneDocument(value) : new TextDocument(String(value)); } }; }
    }
    function prop(layer, key) { return { get value() { return layer[key]; }, expressionEnabled: false, numKeys: 0, dimensionsSeparated: !!layer.positionSeparated, setValue(v) { layer[key] = v; } }; }
    function ensureGroups(layer) { for (const name of ['ADBE Mask Parade', 'ADBE Effect Parade']) if (!layer.groups[name]) layer.groups[name] = { numProperties: 0, addProperty() { this.numProperties++; return { property: () => ({ setValue() {} }) }; } }; }


    const comp = new CompItem(), originals = [];
    const sourceCount = opts.sourceCount || (opts.sourceSpecs ? opts.sourceSpecs.length : (opts.multi ? 2 : 1));
    const sourceSpecs = opts.sourceSpecs || [];
    for (let i = 0; i < Math.max(2, sourceCount); i++) {
        const spec = sourceSpecs[i] || {};
        const L = new Layer(i + 1); L.comp = comp; L.selected = i < sourceCount;
        if (spec.kind === 'text') { L.makeText(new TextDocument(spec.text || 'Ab', spec.style || {})); L.position = spec.position || [300, 200]; if (spec.maskCount) L.property('ADBE Mask Parade').numProperties = spec.maskCount; }
        else if (spec.kind === 'precomp') { L.source = new CompItem(200 + i, spec.width || 500, spec.height || 400, `precomp${i}`); L.source.frameRate = spec.frameRate || 25; L.startTime = spec.startTime || 0; L.stretch = spec.stretch || 100; L.timeRemapEnabled = !!spec.timeRemapEnabled; L.remapValue = spec.remapValue || 0; }
        originals.push(L);
    }
    const original = originals[0], second = originals[1];
    comp.layers = originals.slice(); comp.numLayers = originals.length; comp.selectedLayers = originals.filter(L => L.selected);
    if (opts.initialMatte) { original.trackMatteLayer = { id: 'matte0' }; original.trackMatteType = 1; }
    const projectItems = [comp];
    const project = { get numItems() { return projectItems.filter(item => !item.removed).length; }, item(index) { return projectItems.filter(item => !item.removed)[index - 1]; }, items: { addComp(name, width, height, pixelAspect, duration, frameRate) { const scratch = new CompItem(10000 + projectItems.length, width, height, name); scratch.duration = duration; scratch.frameRate = frameRate; projectItems.push(scratch); return scratch; } }, activeItem: comp, renderQueue: { rendering: false } };
    const app = { project, disableRendering: false, scheduleTask() { tasks.push(++serial); return serial; }, cancelTask() {}, beginUndoGroup() { undo.push('begin'); }, endUndoGroup() { undo.push('end'); } };
    Object.defineProperty(app, 'settings', { get() { throw Error('portable11 must not access app.settings'); } });
    const context = { File, Folder, FootageItem, CompItem, AVLayer: Layer, TextDocument, Window, ParagraphJustification: { LEFT_JUSTIFY: 'LEFT_JUSTIFY' }, LineOrientation: { HORIZONTAL: 'HORIZONTAL' }, ParagraphDirection: { DIRECTION_LEFT_TO_RIGHT: 'LTR' }, Window, app, Shape: function () {}, MaskMode: { ADD: 1, INTERSECT: 2 }, system: { callSystem() { return ''; } }, $: { os: 'Windows', global: {} }, alert(s) { alerts.push(String(s)); }, Error, Math, Date, isFinite, Number };
    globalRef = context.$.global;
    vm.createContext(context); vm.runInContext(source, context);
    const state = context.$.global.__ZExpSplitPortable;
    activeState = state;
    function writeResult(recordIndex, { kinds = Array(9).fill('UNKNOWN'), protocol = 'AE_PARTS_V2', badKindAt = -1, token, boxes, pointsPerPart = 4, loopsPerPart = 1, error } = {}) {
        const rec = state.records ? state.records[recordIndex] : state.record;
        const body = error ? `AE_PARTS_V2\n${rec.token}\nERROR\n${error}` : sourceFor(kinds, token || rec.token, protocol, badKindAt, boxes, pointsPerPart, loopsPerPart, rec.width, rec.height);
        files[rec.folder.fsName + '/result.txt'] = { data: body, mtime: 1000 };
    }
    function pollResult(options, recordIndex = state.index || 0) { writeResult(recordIndex, options); state.poll(); }
    function finishAll(resultOptions = []) {
        while (state.phase === 'ANALYSIS') {
            const current = state.index || 0;
            writeResult(current, resultOptions[current] || {});
            state.poll();
        }
    }
    function findProgress(label) { return windows[0] && windows[0].controls.find(control => String(control.text || '').includes(label)); }
    return { comp, originals, original, second, copies, alerts, files, state, undo, tasks, windows, context, writeResult, pollResult, finishAll, findProgress };
}

const cleanHost = source.slice(0, source.indexOf('        var WORKER = [')) + source.slice(source.indexOf('        ].join("\\n");'));
assert(!cleanHost.includes('app.settings'), 'host must not access persistent settings');
assert(!cleanHost.includes('mode.txt'), 'host must not write a mode file');
assert(source.includes("var BUILD = '2026.10.29-portable14-native-text';"));
assert(source.includes("new Window('palette'"), 'host should create a progress palette');
assert(!cleanHost.includes("new Window('dialog'"), 'host must not create a modal dialog');

const prepareCancel = fixture({ multi: true, windowUpdateHook(state, window, where) { if (where === 'show' && state && state.phase === 'PREPARING') state.cancel(); } });
assert.equal(prepareCancel.state.phase, 'CANCELLED');
assert.equal(prepareCancel.tasks.length, 0, 'cancel during palette show stops before launching any worker');
assert(prepareCancel.windows[0].closed);

const multi = fixture({ multi: true });
assert(multi.state, 'multiple selected layers should be accepted up to the batch cap');
assert.equal(multi.state.records.length, 2);
assert.notEqual(multi.state.records[0].folder.fsName, multi.state.records[1].folder.fsName);
assert.notEqual(multi.state.records[0].token, multi.state.records[1].token);
assert.equal(multi.state.phase, 'ANALYSIS', multi.alerts.join(' | '));
assert(multi.state.progress.window.visible);
assert(multi.state.progress.bar && multi.state.progress.status && multi.state.progress.detail && multi.state.progress.cancelButton);
assert(!multi.state.progress.window.controls.some(c => c.kind === 'checkbox' || /开始/.test(c.text || '')));
const tooMany = fixture({ sourceCount: 21 });
assert.equal(tooMany.state, undefined, 'more than twenty selected sources must be rejected before preparation');
assert.equal(tooMany.tasks.length, 0);
const maxSources = fixture({ sourceCount: 20 });
for (let sourceIndex = 0; sourceIndex < 20; sourceIndex++) maxSources.pollResult({ kinds: ['UNKNOWN'] }, sourceIndex);
assert.equal(maxSources.state.phase, 'DONE', 'twenty sources at one output each must fit the source-count limit');
assert.equal(maxSources.copies.length, 20);
assert(maxSources.originals.every(layer => !layer.enabled));

const run = fixture({ threeD: true });
assert.equal(run.state, undefined, 'fixed centering must preflight 3D layers before worker setup');
assert.equal(run.tasks.length, 0);
assert(run.alerts.some(s => s.includes('锚点居中')));

const good = fixture({rotation:0});
assert(good.state, 'single supported source starts with a progress palette');
assert.equal(good.tasks.length, 1);
assert.equal(good.state.records.length, 1);
assert.equal(good.files[good.state.records[0].folder.fsName + '/mode.txt'], undefined);
good.pollResult({ kinds: ['TEXT', 'OBJECT', 'UNKNOWN', 'TEXT', 'OBJECT', 'UNKNOWN', 'TEXT', 'OBJECT', 'UNKNOWN'] });
assert.equal(good.state.phase, 'DONE');
assert.equal(good.copies.length, 9);
assert(good.state.progress.window.closed, 'terminal state closes progress palette');
assert(good.state.progress.bar.maxvalue >= good.state.progress.bar.value);
assert.deepEqual(good.copies.map(L => L.name.match(/\[(.*?)\]/)[1]), ['文字', '非文字', '类型待确认', '文字', '非文字', '类型待确认', '文字', '非文字', '类型待确认']);
assert(good.copies.every(L => L.selected));
assert(!good.original.selected && !good.original.enabled);
assert(good.copies.every(L => L.anchor[0] > 0 && L.anchor[1] > 0));
assert(good.alerts.some(s => s.includes('文字 3，非文字 3，类型待确认 3')));
assert(good.alerts.some(s => s.includes('不是可编辑文字')));

const batch = fixture({ sourceCount: 2, rotation: 0 });
assert.equal(batch.state.records.length, 2);
assert.equal(batch.state.records[0].folder.fsName === batch.state.records[1].folder.fsName, false);
assert.equal(batch.state.records[0].token === batch.state.records[1].token, false);
assert.equal(batch.state.index, 0);
batch.pollResult({ kinds: ['TEXT', 'OBJECT', 'UNKNOWN', 'TEXT', 'OBJECT', 'UNKNOWN', 'TEXT', 'OBJECT', 'UNKNOWN'] }, 0);
assert.equal(batch.state.index, 1, 'analysis advances serially after first source');
assert.equal(batch.copies.length, 0, 'analysis results are not partially applied');
batch.pollResult({ kinds: ['OBJECT', 'TEXT', 'UNKNOWN', 'OBJECT', 'TEXT', 'UNKNOWN', 'OBJECT', 'TEXT', 'UNKNOWN'] }, 1);
assert.equal(batch.state.phase, 'DONE');
assert.equal(batch.copies.length, 18);
assert(batch.originals.every(L => !L.enabled));

const native = fixture({ sourceSpecs: [{ kind: 'text', text: 'Ab C' }] });
assert(native.state, 'native point text should be accepted');
assert.equal(native.state.phase, 'DONE', native.alerts.join(' | '));
assert.equal(native.tasks.length, 0, 'native-only batches must not launch or schedule a worker');
assert.equal(native.copies.length, 3, 'spaces preserve layout but do not produce empty text layers');
assert.deepEqual(native.copies.slice().sort((a, b) => a.position[0] - b.position[0]).map(layer => layer.textDocument.text), ['A', 'b', 'C']);
assert(native.copies.every(layer => layer.matchName === 'ADBE Text Layer'));
assert(native.copies.every(layer => layer.position[0] !== 300 || layer.position[1] !== 200));
assert.equal(native.original.textDocument.text, 'Ab C', 'deep-cloned text ranges must not mutate the original document');
assert.equal(native.context.app.project.numItems, 1, 'scratch measurement comp is removed after native preparation');
assert(native.originals[0].selected === false && native.copies.every(layer => layer.selected));
const nativeChinese = fixture({ sourceSpecs: [{ kind: 'text', text: '你好\r\n世界 ' }] });
assert.equal(nativeChinese.state.phase, 'DONE', nativeChinese.alerts.join(' | '));
assert.deepEqual(nativeChinese.copies.slice().sort((a, b) => a.position[1] - b.position[1] || a.position[0] - b.position[0]).map(layer => layer.textDocument.text), ['你', '好', '世', '界']);
const styledNative = fixture({ sourceSpecs: [{ kind: 'text', text: 'AB', style: { font: 'MockFont', fontSize: 54, fillColor: [1, 0, 0], charWidths: { A: 31, B: 29 } } }] });
assert.equal(styledNative.state.phase, 'DONE', styledNative.alerts.join(' | '));
assert(styledNative.copies.every(layer => layer.textDocument.font === 'MockFont' && layer.textDocument.fontSize === 54 && layer.textDocument.fillColor[0] === 1));
const kernedNative = fixture({ sourceSpecs: [{ kind: 'text', text: 'AV', style: { kerningPairs: { AV: -4 } } }] });
assert.equal(kernedNative.state.phase, 'DONE', kernedNative.alerts.join(' | '));
assert.deepEqual(kernedNative.copies.map(layer => layer.textDocument.text), ['A', 'V']);
const boxedNative = fixture({ sourceSpecs: [{ kind: 'text', text: 'A B', style: { boxText: true, pointText: false } }] });
assert.equal(boxedNative.state, undefined); assert(boxedNative.alerts.some(message => /点文字|文本框/.test(message)));
const bidiNative = fixture({ sourceSpecs: [{ kind: 'text', text: 'שלום' }] });
assert.equal(bidiNative.state, undefined); assert(bidiNative.alerts.some(message => /复杂塑形|双向/.test(message)));
const animatorNative = fixture({ nativeTextAnimators: 1, sourceSpecs: [{ kind: 'text', text: 'Hi' }] });
assert.equal(animatorNative.state, undefined); assert(animatorNative.alerts.some(message => message.includes('文字动画器')));
const ligatureNative = fixture({ sourceSpecs: [{ kind: 'text', text: 'fi', style: { ligature: true, charWidths: { f: 24, i: 24 }, ligaturePairs: { fi: -7 } } }] });
assert(ligatureNative.state); assert.equal(ligatureNative.state.phase, 'FAILED'); assert(ligatureNative.alerts.some(message => /排版|连字/.test(message))); assert.equal(ligatureNative.context.app.project.numItems, 1, 'failed measurement removes its scratch comp');
const rangedStyles = fixture({ sourceSpecs: [{ kind: 'text', text: 'AB', style: { font: 'Base', fontSize: 40, characterStyles: { B: { font: 'Accent', fontSize: 64, fillColor: [0, 0, 1], baselineShift: 6 } } } }] });
assert.equal(rangedStyles.state.phase, 'DONE', rangedStyles.alerts.join(' | '));
const visualStyles = rangedStyles.copies.slice().sort((a, b) => a.position[0] - b.position[0]);
assert.deepEqual(visualStyles.map(layer => layer.textDocument.text), ['A', 'B']);
assert.equal(visualStyles[0].textDocument.characterRange(0, 1).font, 'Base');
assert.equal(visualStyles[1].textDocument.characterRange(0, 1).font, 'Accent'); assert.equal(visualStyles[1].textDocument.characterRange(0, 1).fontSize, 64); assert.deepEqual(visualStyles[1].textDocument.characterRange(0, 1).fillColor, [0, 0, 1]); assert.equal(visualStyles[1].textDocument.characterRange(0, 1).baselineShift, 6);
const lostRangeStyles = fixture({ measurementDropsStyle: true, sourceSpecs: [{ kind: 'text', text: 'AB', style: { characterStyles: { B: { font: 'Accent', fontSize: 64, fillColor: [0, 0, 1] } } } }] });
assert.equal(lostRangeStyles.state.phase, 'FAILED'); assert.equal(lostRangeStyles.copies.length, 0); assert(lostRangeStyles.alerts.some(message => /样式/.test(message)));
const plainNativePath = fixture({ sourceSpecs: [{ kind: 'text', text: 'A' }] });
assert.equal(plainNativePath.state.phase, 'DONE', plainNativePath.alerts.join(' | '));
const maskedNativePath = fixture({ sourceSpecs: [{ kind: 'text', text: 'A', maskCount: 1 }] });
assert.equal(maskedNativePath.state, undefined); assert(maskedNativePath.alerts.some(message => /蒙版|文字路径/.test(message)));
const fullNativeBudget = fixture({ sourceSpecs: [{ kind: 'text', text: 'A'.repeat(120) }] });
assert.equal(fullNativeBudget.state.phase, 'DONE', fullNativeBudget.alerts.join(' | ')); assert.equal(fullNativeBudget.copies.length, 120);
const overNativeBudget = fixture({ sourceSpecs: [{ kind: 'text', text: 'A'.repeat(121) }] });
assert.equal(overNativeBudget.state, undefined); assert(overNativeBudget.alerts.some(message => /1–120|120/.test(message)));
assert(batch.copies.every(L => L.selected));
assert(batch.state.progress.window.closed);
assert(batch.state.progress.bar.history.some(value => value === 2), 'analysis bar reaches source count, not a fabricated fraction');
assert(batch.state.progress.status.history.some(value => String(value).includes('分析')));
assert(batch.state.progress.status.history.some(value => String(value).includes('生成')));
assert(batch.state.progress.bar.history.some(value => value === 18), 'apply bar advances by generated output count');
assert.deepEqual(batch.comp.layers.filter(L => !batch.copies.includes(L)), batch.originals, 'old layer relative order is unchanged');
for (const src of batch.originals) {
    const output = batch.copies.filter(L => L.source === src.source);
    assert.equal(output.length, 9, 'each source contributes nine output layers');
    assert(output.every(L => output.indexOf(L) >= 0));
    const timeline = batch.comp.layers.filter(L => batch.copies.includes(L));
    assert(timeline.indexOf(output[0]) > timeline.indexOf(output[8]), 'region 1 remains below region 9 within each source block');
}

const secondSourceFailure = fixture({ sourceCount: 2 });
secondSourceFailure.pollResult({}, 0);
secondSourceFailure.originals[1].position = [11, 12];
secondSourceFailure.pollResult({}, 1);
assert.equal(secondSourceFailure.state.phase, 'FAILED');
assert.equal(secondSourceFailure.copies.length, 0, 'validate every source after analysis before applying any result');
assert(secondSourceFailure.originals.every(L => L.enabled));
const secondMaskFailure = fixture({ sourceCount: 2 }); secondMaskFailure.pollResult({}, 0); secondMaskFailure.originals[1].property('ADBE Mask Parade').numProperties = 1; secondMaskFailure.pollResult({}, 1);
assert.equal(secondMaskFailure.state.phase, 'FAILED'); assert.equal(secondMaskFailure.copies.length, 0, 'mask count changes after worker completion abort before commit'); assert(secondMaskFailure.originals.every(L => L.enabled));
const secondWorkerFailure = fixture({ sourceCount: 2 });
secondWorkerFailure.pollResult({}, 0); secondWorkerFailure.pollResult({ error: 'synthetic second worker error' }, 1);
assert.equal(secondWorkerFailure.state.phase, 'FAILED'); assert.equal(secondWorkerFailure.copies.length, 0, 'failed second worker leaves no partial outputs');
assert(secondWorkerFailure.originals.every(L => L.enabled));

const unknown = fixture(); unknown.pollResult({ kinds: ['UNKNOWN'] });
assert.equal(unknown.copies.length, 1); assert(unknown.copies[0].name.includes('[类型待确认]'));

for (const invalid of [
    { protocol: 'AE_PARTS_V1' },
    { badKindAt: 2 }
]) {
    const rejected = fixture(); rejected.pollResult(invalid);
    assert.equal(rejected.state.phase, 'FAILED'); assert.equal(rejected.copies.length, 0); assert(rejected.original.enabled);
}

const rollback = fixture({ failAt: 3 }); rollback.pollResult();
assert.equal(rollback.state.phase, 'FAILED'); assert(rollback.copies.every(L => L.removed)); assert(rollback.original.enabled); assert.deepEqual(rollback.undo, ['begin', 'end']);
const crossSourceRollback = fixture({ sourceCount: 2, failAt: 57 }); crossSourceRollback.pollResult({ kinds: Array(9).fill('UNKNOWN') }, 0); crossSourceRollback.pollResult({ kinds: Array(9).fill('UNKNOWN') }, 1);
assert.equal(crossSourceRollback.state.phase, 'FAILED'); assert(crossSourceRollback.copies.length > 9 && crossSourceRollback.copies.length < 18, 'injected setter fails after source two has begun'); assert(crossSourceRollback.copies.every(L => L.removed), 'failure on source two removes source one and source two outputs'); assert(crossSourceRollback.originals.every(L => L.enabled && L.selected)); assert.deepEqual(crossSourceRollback.comp.layers, crossSourceRollback.originals);

const mixedSourceStates=fixture({sourceCount:2,failAt:57});mixedSourceStates.original.locked=true;mixedSourceStates.second.enabled=false;mixedSourceStates.finishAll();
assert.equal(mixedSourceStates.state.phase,'FAILED');assert.equal(mixedSourceStates.copies.length,15);assert(mixedSourceStates.copies.every(L=>L.removed));
assert.equal(mixedSourceStates.original.locked,true);assert.equal(mixedSourceStates.original.enabled,true);assert.equal(mixedSourceStates.second.enabled,false);assert.equal(mixedSourceStates.second.locked,false);
assert(mixedSourceStates.originals.every(L=>L.selected));assert.deepEqual(mixedSourceStates.undo,['begin','end']);assert.equal(mixedSourceStates.context.app.disableRendering,false);
const changedSource = fixture(); changedSource.original.position = [12, 13]; changedSource.pollResult();
assert.equal(changedSource.copies.length, 0); assert(changedSource.alerts.some(s => s.includes('变换已改变')));
const changedMatte = fixture({ initialMatte: true }); changedMatte.original.trackMatteLayer = { id: 'different' }; changedMatte.pollResult();
assert.equal(changedMatte.copies.length, 0); assert(changedMatte.alerts.some(s => s.includes('轨道遮罩关系已改变')));
const newMatteDependent = fixture(); newMatteDependent.comp.layers.push({ trackMatteLayer: newMatteDependent.original }); newMatteDependent.comp.numLayers++; newMatteDependent.pollResult();
assert.equal(newMatteDependent.copies.length, 0); assert(newMatteDependent.alerts.some(s => s.includes('正在提供轨道遮罩')));
const secondMatteFailure = fixture({ sourceCount: 2 }); secondMatteFailure.pollResult({}, 0); secondMatteFailure.originals[1].trackMatteLayer = { id: 'changed' }; secondMatteFailure.pollResult({}, 1);
assert.equal(secondMatteFailure.state.phase, 'FAILED'); assert.equal(secondMatteFailure.copies.length, 0); assert(secondMatteFailure.originals.every(L => L.enabled));
const exactCap = fixture({ sourceCount: 3 }); exactCap.finishAll(Array.from({length:3},() => ({kinds:Array(40).fill('UNKNOWN')})));
assert.equal(exactCap.state.phase, 'DONE', exactCap.alerts.join(' | ')); assert.equal(exactCap.copies.length,120);
const capParts = fixture({ sourceCount: 4 }); capParts.finishAll(Array.from({length:4},() => ({kinds:Array(40).fill('UNKNOWN')})));
assert.equal(capParts.state.phase, 'FAILED'); assert.equal(capParts.copies.length, 0, 'aggregate output count must be capped across sources');
assert(capParts.state.records.every(rec => rec.parts && rec.parts.length===40), 'all individually valid worker results are parsed before aggregate cap');
assert(capParts.alerts.some(s => s.includes('本批结果过大')), 'rejection must be aggregate budget, not out-of-bounds fixture coordinates');
const capPoints = fixture({ sourceCount: 2 }); capPoints.pollResult({ kinds: Array(5).fill('UNKNOWN'), pointsPerPart: 256, loopsPerPart: 8 }, 0); capPoints.pollResult({ kinds: Array(5).fill('UNKNOWN'), pointsPerPart: 256, loopsPerPart: 8 }, 1);
assert.equal(capPoints.state.phase, 'FAILED'); assert.equal(capPoints.copies.length, 0, 'aggregate outline point count must be capped across sources');

const cancelled = fixture(); cancelled.state.progress.cancelButton.onClick(); assert.equal(cancelled.state.phase, 'CANCELLED'); cancelled.pollResult(); assert.equal(cancelled.copies.length, 0); assert(cancelled.files[cancelled.state.records[0].folder.fsName + '/cancel.txt']); assert(cancelled.state.progress.window.closed);
const closedCancel = fixture(); closedCancel.state.progress.window.triggerClose(); assert.equal(closedCancel.state.phase, 'CANCELLED'); assert.equal(closedCancel.tasks.length, 1); assert(closedCancel.state.progress.window.closed);
const applyCancel = fixture({ applyCancelAt: 1 }); applyCancel.pollResult({ kinds: ['TEXT', 'TEXT', 'TEXT'] });
assert.equal(applyCancel.state.phase, 'CANCELLED'); assert(applyCancel.copies.every(L => L.removed), 'cancel during apply rolls back all copies'); assert(applyCancel.original.enabled && applyCancel.original.selected); assert.deepEqual(applyCancel.comp.layers, applyCancel.originals); assert(applyCancel.state.progress.window.closed);
const applyClose = fixture({ applyCloseAt: 1 }); applyClose.pollResult({ kinds: ['TEXT', 'TEXT', 'TEXT'] });
assert.equal(applyClose.state.phase, 'CANCELLED'); assert(applyClose.copies.every(L => L.removed), 'closing palette during apply requests whole-run rollback');
const uiThrowsOnRollback = fixture({ failAt: 1, throwStatusAfterOutput: true }); uiThrowsOnRollback.pollResult({ kinds: ['TEXT', 'TEXT'] });
assert.equal(uiThrowsOnRollback.state.phase, 'FAILED'); assert(uiThrowsOnRollback.copies.length>0, 'rollback test must reach a real output before UI setter failure'); assert(uiThrowsOnRollback.copies.every(L => L.removed), 'rollback must proceed even if status control setter throws'); assert(uiThrowsOnRollback.original.enabled);
const uiUpdateThrows = fixture({ throwProgressUpdate: true }); uiUpdateThrows.pollResult({ kinds: ['TEXT', 'TEXT'] });
assert.equal(uiUpdateThrows.state.phase, 'FAILED'); assert(uiUpdateThrows.copies.length>0, 'update failure test must reach a real output, not fail during preparation'); assert(uiUpdateThrows.copies.every(L => L.removed), 'palette update exceptions must not bypass rollback'); assert(uiUpdateThrows.original.enabled);
const terminalCancel = fixture({ onProgressUpdate(state) { if (state.phase === 'APPLYING' && state.progress.bar.value === state.progress.bar.maxvalue) state.cancel(); } });
terminalCancel.pollResult({ kinds: ['TEXT', 'TEXT', 'TEXT'] });
assert.equal(terminalCancel.state.phase, 'CANCELLED'); assert(terminalCancel.copies.every(L => L.removed), 'cancel at final output update is still observed before commit');
const maskChangesDuringUpdate = fixture({ sourceCount: 2, onProgressUpdate(state) { if (state.phase === 'APPLYING' && state.progress.bar.value === 1) state.records[0].layer.property('ADBE Mask Parade').numProperties++; } });
maskChangesDuringUpdate.pollResult({ kinds: ['TEXT', 'TEXT'] }, 0); maskChangesDuringUpdate.pollResult({ kinds: ['TEXT', 'TEXT'] }, 1);
assert.equal(maskChangesDuringUpdate.state.phase, 'FAILED'); assert(maskChangesDuringUpdate.copies.every(L => L.removed), 'mask count change during output progress pump rolls back the whole batch'); assert(maskChangesDuringUpdate.originals.every(L => L.enabled));
const hotSwap = fixture(); const old = hotSwap.state, oldWindow = old.progress.window; vm.runInContext(source, hotSwap.context); const replacement = hotSwap.context.$.global.__ZExpSplitPortable; assert.equal(old.phase, 'RETIRED'); oldWindow.triggerClose(); assert.strictEqual(hotSwap.context.$.global.__ZExpSplitPortable, replacement, 'closing a retired palette cannot replace or cancel the current task'); assert.equal(old.phase, 'RETIRED'); hotSwap.pollResult(); assert.equal(hotSwap.copies.length, 0);
const cancelWithBrokenStatusOptions={};const cancelWithBrokenStatus=fixture(cancelWithBrokenStatusOptions);cancelWithBrokenStatusOptions.throwStatusSetter=true;
cancelWithBrokenStatus.state.cancel();assert.equal(cancelWithBrokenStatus.state.phase,'CANCELLED');assert(!cancelWithBrokenStatus.state.active&&cancelWithBrokenStatus.state.progress.window.closed);
let duringApply,attemptedApplyReentry=false;
duringApply=fixture({onProgressUpdate(state){if(state.phase==='APPLYING'&&state.progress.bar.value===1&&!attemptedApplyReentry){attemptedApplyReentry=true;vm.runInContext(source,duringApply.context);}}});
duringApply.pollResult({kinds:['TEXT','TEXT']});assert(attemptedApplyReentry);assert.equal(duringApply.state.phase,'DONE');assert.strictEqual(duringApply.context.$.global.__ZExpSplitPortable,duringApply.state);assert.equal(duringApply.windows.length,1);assert(duringApply.alerts.some(s=>s.includes('正在生成图层')));
let preempted=false,retiredPreparing;
const preparingReentry=fixture({windowUpdateHook(state,window,where,context){if(where==='show'&&state.phase==='PREPARING'&&!preempted){preempted=true;retiredPreparing=state;vm.runInContext(source,context);}}});
assert(preempted);assert.equal(retiredPreparing.phase,'RETIRED');assert(!retiredPreparing.active);assert(preparingReentry.windows[0].closed);assert.equal(preparingReentry.state.phase,'ANALYSIS');assert(!preparingReentry.windows[1].closed);assert.equal(preparingReentry.tasks.length,1,'only replacement preparation may launch/schedule');
preparingReentry.finishAll();assert.equal(preparingReentry.state.phase,'DONE');
const stale = fixture(); stale.pollResult({ token: 'wrong-token' }); assert.equal(stale.state.phase, 'FAILED'); assert.equal(stale.copies.length, 0);

// Input detection order is deliberately scrambled. Actual AE duplicate inserts above
// its source, not at the top of the composition; require explicit relative stacking.
function assertSpatial(run, boxes, expected) {
    run.pollResult({ kinds: boxes.map(() => 'TEXT'), boxes });
    assert.equal(run.state.phase, 'DONE', run.alerts.join(' | '));
    const centers = run.copies.map(L => [L.anchor[0], L.anchor[1]]);
    assert.deepEqual(centers, expected, 'bottom rows first, then left-to-right in each row');
    assert.deepEqual(run.copies.map(L => +L.name.match(/区域_(\d+)/)[1]), boxes.map((_, i) => i + 1));
    const timeline = run.comp.layers.filter(L => run.copies.includes(L));
    assert.deepEqual(timeline, run.copies.slice().reverse(), 'region 1 belongs at the bottom of output stack');
    assert.deepEqual(run.comp.layers.filter(L => !run.copies.includes(L)), [run.original, run.second], 'unrelated layers keep relative order');
    assert(run.copies.every(L => L.selected));
}
const jittered = [{x:300,y:110,w:40,h:80},{x:100,y:100,w:40,h:80},{x:200,y:90,w:40,h:80}];
assertSpatial(fixture({rotation:0}), jittered, [[120,140],[220,130],[320,150]]);
const associated = fixture({rotation:0});
associated.pollResult({kinds:['TEXT','OBJECT','UNKNOWN'],boxes:jittered});
assert.deepEqual(associated.copies.map(L => L.name.match(/\[(.*?)\]/)[1]), ['非文字','类型待确认','文字'], 'classification travels with its polygon when sorting');
const tied = fixture({rotation:0});
tied.pollResult({kinds:['TEXT','OBJECT'],boxes:[{x:100,y:100},{x:100,y:100}]});
assert.deepEqual(tied.copies.map(L => L.name.match(/\[(.*?)\]/)[1]), ['文字','非文字'], 'equal positions retain deterministic detection order');
const twoRows = [{x:300,y:100},{x:200,y:310},{x:100,y:105},{x:100,y:300},{x:200,y:95},{x:300,y:305}];
assertSpatial(fixture({rotation:0}), twoRows, [[120,320],[220,330],[320,325],[120,125],[220,115],[320,120]]);
const flat = [{x:100,y:100},{x:300,y:100},{x:200,y:100}];
assertSpatial(fixture({rotation:0,scale:[-100,100]}), flat, [[320,120],[220,120],[120,120]]);
assertSpatial(fixture({rotation:90}), flat, [[320,120],[220,120],[120,120]]);
const moveFailure = fixture({rotation:0, failMoveAt:2}); moveFailure.pollResult({kinds:['TEXT','TEXT','TEXT'],boxes:flat});
assert.equal(moveFailure.state.phase,'FAILED');
assert(moveFailure.copies.every(L => L.removed));
assert.deepEqual(moveFailure.comp.layers,[moveFailure.original,moveFailure.second]);
assert(moveFailure.original.enabled && moveFailure.original.selected);

const hybrid = fixture({ sourceSpecs: [{ kind: 'text', text: 'A ' }, { kind: 'precomp', startTime: 0.25, stretch: 50 }] });
assert(hybrid.state && hybrid.state.records.some(rec => rec.nativeText));
assert.equal(hybrid.tasks.length, 1, 'hybrid batch schedules one raster worker for the precomp only');
const precompSource = hybrid.originals[1].source;
assert.equal(precompSource.saveCount, 1); assert.equal(precompSource.lastSavedTime, 1.5, 'precomp time maps (comp.time-startTime)*100/stretch');
hybrid.pollResult({ kinds: ['OBJECT'] }, 1);
assert.equal(hybrid.state.phase, 'DONE', hybrid.alerts.join(' | '));
assert.equal(hybrid.copies.filter(layer => layer.matchName === 'ADBE Text Layer').length, 1);
assert.equal(hybrid.copies.length, 2);
assert(hybrid.originals.every(layer => !layer.enabled) && hybrid.copies.every(layer => layer.selected));
const changedPrecompTiming = fixture({ sourceSpecs: [{ kind: 'precomp', startTime: 0.25, stretch: 100 }] });
changedPrecompTiming.originals[0].startTime = 0.5; changedPrecompTiming.pollResult({ kinds: ['OBJECT'] }, 0);
assert.equal(changedPrecompTiming.state.phase, 'FAILED'); assert.equal(changedPrecompTiming.copies.length, 0); assert(changedPrecompTiming.originals[0].enabled);
const remappedPrecomp = fixture({ sourceSpecs: [{ kind: 'precomp', startTime: 0, stretch: 100, timeRemapEnabled: true, remapValue: 1.25 }] });
assert.equal(remappedPrecomp.originals[0].source.lastSavedTime, 1.25);
remappedPrecomp.originals[0].remapValue = 1.5; remappedPrecomp.pollResult({ kinds: ['OBJECT'] }, 0);
assert.equal(remappedPrecomp.state.phase, 'FAILED'); assert.equal(remappedPrecomp.copies.length, 0);
const nativeCancelDuringMeasure = fixture({ sourceSpecs: [{ kind: 'text', text: 'AB' }], onProgressUpdate(state) { if (state.phase === 'PREPARING' && state.progress && String(state.progress.status.text).includes('测量文字')) state.cancel(); } });
assert.equal(nativeCancelDuringMeasure.state.phase, 'CANCELLED'); assert.equal(nativeCancelDuringMeasure.copies.length, 0); assert.equal(nativeCancelDuringMeasure.tasks.length, 0); assert.equal(nativeCancelDuringMeasure.context.app.project.numItems, 1);
const nativeStyleChanged = fixture({ sourceSpecs: [{ kind: 'text', text: 'AB' }], onProgressUpdate(state) { if (state.phase === 'PREPARING' && state.progress && String(state.progress.status.text).includes('测量文字') && !state.records[0].layer._styleChanged) { state.records[0].layer._styleChanged = true; state.records[0].layer.textDocument.fontSize++; } } });
assert.equal(nativeStyleChanged.state.phase, 'FAILED'); assert.equal(nativeStyleChanged.copies.length, 0); assert.equal(nativeStyleChanged.context.app.project.numItems, 1);
const noShellNative = fixture({ noPowerShell: true, sourceSpecs: [{ kind: 'text', text: 'OK' }] });
assert.equal(noShellNative.state.phase, 'DONE', noShellNative.alerts.join(' | ')); assert.equal(noShellNative.tasks.length, 0);

// Independent transform invariants: compare a known original glyph origin, not only a changed Position.
function mapPoint(layer, point) {
    const rad=layer.rotation*Math.PI/180, dx=(point[0]-layer.anchor[0])*layer.scale[0]/100, dy=(point[1]-layer.anchor[1])*layer.scale[1]/100;
    return [layer.position[0]+Math.cos(rad)*dx-Math.sin(rad)*dy, layer.position[1]+Math.sin(rad)*dx+Math.cos(rad)*dy];
}
for(const rotation of [0,30,90]) for(const scale of [[100,100],[-80,125]]) {
    const geometry=fixture({rotation,scale,sourceSpecs:[{kind:'text',text:'AV C',style:{tracking:8,kerningPairs:{AV:-4}}}]});
    assert.equal(geometry.state.phase,'DONE',geometry.alerts.join(' | '));
    for(const layer of geometry.copies){
        const x={A:0,V:28,C:80}[layer.textDocument.text], actual=mapPoint(layer,[0,0]), expected=mapPoint(geometry.original,[x,0]);
        assert(Math.abs(actual[0]-expected[0])<1e-8&&Math.abs(actual[1]-expected[1])<1e-8,'centering preserves original glyph origin through rotation/scale/mirroring');
        assert.deepEqual(layer.anchor,[16,-12]);
    }
}
for(const [text,baselineY] of [['A\r\rB',96],['A\r\nB',48]]){
    const emptyLine=fixture({rotation:0,sourceSpecs:[{kind:'text',text}]});
    assert.equal(emptyLine.state.phase,'DONE',emptyLine.alerts.join(' | '));
    const B=emptyLine.copies.find(layer=>layer.textDocument.text==='B');
    assert.deepEqual(mapPoint(B,[0,0]),mapPoint(emptyLine.original,[0,baselineY]));
    assert.equal(emptyLine.context.app.project.numItems,1);
}
for(const [justification,startX] of [['CENTER_JUSTIFY',-36],['RIGHT_JUSTIFY',-72]]){
    const aligned=fixture({rotation:0,sourceSpecs:[{kind:'text',text:'ABC',style:{justification,baselineLocs:[startX,12,startX+72,12]}}]});
    assert.equal(aligned.state.phase,'DONE',aligned.alerts.join(' | '));
    for(const layer of aligned.copies){const index='ABC'.indexOf(layer.textDocument.text);assert.deepEqual(mapPoint(layer,[0,0]),mapPoint(aligned.original,[startX+24*index,12]));}
}
const nativeApplyCancel=fixture({sourceSpecs:[{kind:'text',text:'AB'}],onProgressUpdate(state){if(state.phase==='APPLYING'&&state.progress.bar.value===2)state.cancel();}});
assert.equal(nativeApplyCancel.state.phase,'CANCELLED');assert.equal(nativeApplyCancel.copies.length,2);assert(nativeApplyCancel.copies.every(layer=>layer.removed));assert(nativeApplyCancel.original.enabled&&nativeApplyCancel.original.selected);assert.equal(nativeApplyCancel.context.app.project.numItems,1);
const nativeUIFailure=fixture({throwProgressUpdate:true,sourceSpecs:[{kind:'text',text:'AB'}]});
assert.equal(nativeUIFailure.state.phase,'FAILED');assert.equal(nativeUIFailure.copies.length,1);assert(nativeUIFailure.copies.every(layer=>layer.removed));assert(nativeUIFailure.original.enabled);
const changedNative=fixture({sourceSpecs:[{kind:'text',text:'AB'},{kind:'image'}]});
changedNative.original.textDocument=new changedNative.context.TextDocument('AZ');changedNative.pollResult({kinds:['OBJECT']},1);
assert.equal(changedNative.state.phase,'FAILED');assert.equal(changedNative.copies.length,0);assert(changedNative.original.enabled);
const changedNativeColor=fixture({sourceSpecs:[{kind:'text',text:'AB'},{kind:'image'}]});
changedNativeColor.original.textDocument.charStyles[1].fillColor=[0,0,1];changedNativeColor.pollResult({kinds:['OBJECT']},1);
assert.equal(changedNativeColor.state.phase,'FAILED');assert.equal(changedNativeColor.copies.length,0);
const nativeCrossBudget=fixture({sourceSpecs:[{kind:'text',text:'A'.repeat(61)},{kind:'text',text:'B'.repeat(61)}]});
assert.equal(nativeCrossBudget.state.phase,'FAILED');assert.equal(nativeCrossBudget.copies.length,0);assert(nativeCrossBudget.alerts.some(message=>message.includes('本批结果过大')));
const hybridBudget=fixture({sourceSpecs:[{kind:'text',text:'A'.repeat(120)},{kind:'image'}]});hybridBudget.pollResult({kinds:['OBJECT']},1);
assert.equal(hybridBudget.state.phase,'FAILED');assert.equal(hybridBudget.copies.length,0);
const triple=fixture({sourceSpecs:[{kind:'text',text:'AB'},{kind:'image'},{kind:'precomp'}],onProgressUpdate(state){if(state.phase==='APPLYING'&&state.progress.bar.value===3)state.cancel();}});
triple.pollResult({kinds:['OBJECT']},1);assert.equal(triple.copies.length,0);triple.pollResult({kinds:['OBJECT']},2);
assert.equal(triple.state.phase,'CANCELLED');assert.equal(triple.copies.length,3);assert(triple.copies.every(layer=>layer.removed));assert(triple.originals.every(layer=>layer.enabled&&layer.selected));assert.deepEqual(triple.comp.layers,triple.originals);
const changedStretch=fixture({sourceSpecs:[{kind:'precomp'}]});changedStretch.original.stretch=50;changedStretch.pollResult({kinds:['OBJECT']});assert.equal(changedStretch.state.phase,'FAILED');assert.equal(changedStretch.copies.length,0);

const unicodeTextInclude = fs.readFileSync(path.resolve(__dirname, '../native_text.jsxinc'), 'utf8');
const unitsSource = unicodeTextInclude.slice(unicodeTextInclude.indexOf('function nativeUnits'), unicodeTextInclude.indexOf('function nativeValueString'));
const unitsContext = { Error }; vm.createContext(unitsContext); vm.runInContext(unitsSource + '\nthis.nativeUnits = nativeUnits;', unitsContext);
for (const sample of ['A', '你', '😀', 'e\u0301', '🇨🇳', '👍🏽', '葛\uDB40\uDD00']) assert.equal(unitsContext.nativeUnits(sample).length, 1, 'single grapheme unit: ' + sample);
assert.equal(unitsContext.nativeUnits('A\r\nB').length, 3);
assert.throws(() => unitsContext.nativeUnits('\uD800'), /无效Unicode代理项/);

console.log('PASS portable14: batch/progress, native editable text and styles, precomp timing, Unicode units, rejection, rollback, budgets and cancellation.');
