#include <fstream>
#include <iostream>
#include <string>

#include "../src/ae_script.h"
#include "../src/layer_styles.h"

namespace {

std::string quote_js(const std::string& value) {
    static const char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (const unsigned char c : value) {
        if (c == '\\' || c == '"') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 0x20) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else {
            result += static_cast<char>(c);
        }
    }
    return result + '"';
}

}  // namespace

int main() {
    // The emitted Node suite evaluates the actual C++-generated ExtendScript.
    // AE itself is an external host, so only its documented object boundary is faked.
    const std::wstring hostile_path =
        L"C:\\中文\\O'Brien\\\";injected=true;//\n\t\x01\u2028\U0001F680.png";
    const std::string hostile_match =
        "ADBE \";injected=true;//\n\t\x01中文\xe2\x80\xa8";
    std::ofstream out("ae_script_vm_tests.cjs", std::ios::binary);
    if (!out) {
        std::cerr << "Cannot write AE script VM tests\n";
        return 1;
    }
    out << "const styleScripts = [\n";
    for (const auto& style : xfx::layer_styles) {
        out << "{id:" << quote_js(style.match_name) << ",command:" << style.command_id
            << ",script:" << quote_js(xfx::script_apply_layer_style(style.match_name)) << "},\n";
    }
    std::ofstream style_export("layer_style_scripts.json", std::ios::binary);
    style_export << "[\n";
    for (size_t i = 0; i < xfx::layer_styles.size(); ++i) {
        const auto& style = xfx::layer_styles[i];
        style_export << (i ? ",\n" : "") << "{\"id\":" << quote_js(style.match_name)
            << ",\"command\":" << style.command_id << ",\"script\":"
            << quote_js(xfx::script_apply_layer_style(style.match_name)) << "}";
    }
    style_export << "\n]\n";
    out << "];\nconst unknownStyleScript = " << quote_js(xfx::script_apply_layer_style("unknown;injected=true")) << ";\n";
    out << "const scripts = {\n"
        << "effect: " << quote_js(xfx::script_apply_effect("ADBE Gaussian Blur 2")) << ",\n"
        << "effectEmpty: " << quote_js(xfx::script_apply_effect("")) << ",\n"
        << "effectHostile: " << quote_js(xfx::script_apply_effect(hostile_match)) << ",\n"
        << "preset: " << quote_js(xfx::script_apply_preset(L"C:\\预设\\O'Brien.ffx")) << ",\n"
        << "presetHostile: " << quote_js(xfx::script_apply_preset(hostile_path)) << ",\n"
        << "capture: " << quote_js(xfx::script_capture_png(L"C:\\截图\\合成.png")) << ",\n"
        << "captureHostile: " << quote_js(xfx::script_capture_png(hostile_path)) << ",\n"
        << "captureEmpty: " << quote_js(xfx::script_capture_png(L"")) << ",\n"
        << "importImage: " << quote_js(xfx::script_import_image(L"C:\\截图\\合成.png")) << ",\n"
        << "importHostile: " << quote_js(xfx::script_import_image(hostile_path)) << ",\n"
        << "importEmpty: " << quote_js(xfx::script_import_image(L"")) << "\n};\n";
    out << R"TESTJS(
'use strict';
const assert = require('node:assert/strict');
const vm = require('node:vm');
let passed = 0;
const expectedStyleCommands = [
 ['dropShadow/enabled',9000],['innerShadow/enabled',9001],['outerGlow/enabled',9002],
 ['innerGlow/enabled',9003],['bevelEmboss/enabled',9004],['chromeFX/enabled',9005],
 ['solidFill/enabled',9006],['gradientFill/enabled',9007],['frameFX/enabled',9008],
];
assert.deepEqual(styleScripts.map(style=>[style.id,style.command]),expectedStyleCommands,'native style command table drifted');

function test(name, body) {
    try {
        body();
        passed++;
        console.log('PASS ' + name);
    } catch (error) {
        console.error('FAIL ' + name + '\n' + error.stack);
        process.exitCode = 1;
    }
}

// applyPreset affects the comp's current selection, not just its receiver.
// Modeling that side effect catches an otherwise silent N-squared application bug.
function host(options = {}) {
    const state = {
        begin: 0, end: 0, effectArgs: [], presetSelections: [], styleCommands: [], viewer: 0, styleWrites: [],
        files: [], captures: [], imports: [], existing: new Set(), sleeps: [], pendingCapture: null,
    };
    class CompItem {
        constructor() {
            this.time = 3.75;
            this._layers = [];
        }
        get selectedLayers() { return this._layers.filter(layer => layer.selected); }
        get numLayers() { return this._layers.length; }
        layer(index) { return this._layers[index - 1]; }
        openInViewer() { state.viewer++; if (options.viewerError) throw Error('viewer failed');
            return options.undefinedViewerReturn ? undefined : {setActive(){}}; }
        saveFrameToPng(time, file) {
            state.captures.push({time, path: file.fsName});
            if (options.captureError) throw Error(options.captureError);
            if (!options.captureNoFile) {
                if (options.captureDelay) state.pendingCapture = file.fsName;
                else state.existing.add(file.fsName);
            }
        }
    }
    class File {
        constructor(path) {
            this.fsName = path;
            this.parent = {exists: options.parentExists !== false};
            this.cachedExists = options.fileExists !== false || state.existing.has(path);
            state.files.push(path);
        }
        get exists() {
            return options.cacheFileExists ? this.cachedExists :
                options.fileExists !== false || state.existing.has(this.fsName);
        }
    }
    class ImportOptions {
        constructor(file) { this.file = file; this.sequence = true; }
    }
    class AVLayer {}
    const comp = new CompItem();
    const styleGroups = new Map();
    function groupFor(index, id) {
        const key = index + ':' + id;
        if (!styleGroups.has(key)) {
            const definition = options.existingStyles?.[index]?.[id];
            const value = {canSetEnabled: definition !== undefined, _enabled: !!definition, parameter: 47};
            Object.defineProperty(value, 'enabled', {get(){return this._enabled;},set(v){
                if (!this.canSetEnabled) throw Error('style not exposed');
                state.styleWrites.push({index,id,value:v}); this._enabled=v;
            }});
            styleGroups.set(key, value);
        }
        return styleGroups.get(key);
    }
    const selection = options.selection || [true, true];
    comp._layers = selection.map((selected, index) => ({
        name: '图层 ' + (index + 1), index: index + 1, selected,
        locked: options.lockedLayer === index + 1,
        effects: [], presets: [],
        property(name) {
            if (name === 'ADBE Layer Styles') {
                if (options.noStyleGroup === this.index) return null;
                return {canSetEnabled: options.parentDisabled === this.index, enabled: options.parentDisabled !== this.index,
                    property: id => groupFor(this.index, id)};
            }
            assert.equal(name, 'ADBE Effect Parade');
            if (options.noEffectGroup === this.index) return null;
            return {
                canAddProperty: () => options.incompatibleLayer !== this.index,
                addProperty: matchName => {
                    state.effectArgs.push(matchName);
                    if (options.effectError === this.index) throw Error('cannot add\tbroken\nplugin');
                    if (options.nullEffect === this.index) return null;
                    this.effects.push(matchName);
                    if (options.changeSelection) comp._layers.forEach(layer => layer.selected = false);
                    return {matchName};
                },
            };
        },
        applyPreset(file) {
            const selectedLayers = comp.selectedLayers;
            state.presetSelections.push(selectedLayers.map(layer => layer.index));
            if (options.presetError === this.index) throw Error('preset failed\tbad\nfile');
            selectedLayers.forEach(layer => layer.presets.push(file.fsName));
            if (options.changeSelection) comp._layers.forEach(layer => layer.selected = true);
        },
    }));
    comp._layers.forEach(layer => { if (options.nonAVLayer !== layer.index) Object.setPrototypeOf(layer, AVLayer.prototype); });
    const project = {
        activeItem: options.noComp ? {} : comp,
        importFile(importOptions) {
            assert.ok(importOptions instanceof ImportOptions);
            if (options.importError) throw Error(options.importError);
            if (options.nullImport) return null;
            const item = {path: importOptions.file.fsName, sequence: importOptions.sequence};
            state.imports.push(item);
            return item;
        },
    };
    const context = {
        CompItem, AVLayer, File, ImportOptions, injected: false,
        ViewerType: {VIEWER_COMPOSITION: 1},
        $: {sleep(milliseconds) {
            state.sleeps.push(milliseconds);
            if (state.pendingCapture && state.sleeps.length >= options.captureDelay) {
                state.existing.add(state.pendingCapture);
            }
        }},
        app: {
            project: options.noProject ? null : project,
            activeViewer: options.noActiveViewer ? null : {type: options.wrongViewer ? 2 : 1, setActive(){}},
            effects: Object.hasOwn(options, 'registeredEffects') ? options.registeredEffects : [
                {matchName: 'ADBE Gaussian Blur 2', displayName: 'Gaussian Blur', category: 'Blur'},
                {matchName: 'ADBE ";injected=true;//\n\t\x01中文\u2028', displayName: 'Hostile quoted ID', category: 'Vendor'},
            ],
            executeCommand(command) {
                const selected = comp.selectedLayers;
                state.styleCommands.push({command, selection:selected.map(layer=>layer.index)});
                assert.equal(selected.length, 1, 'menu style action must isolate one target layer');
                if (options.styleCommandError === selected[0].index) throw Error('style command failed');
                const definition = styleScripts.find(style=>style.command===command);
                assert.ok(definition, 'unrecognized command');
                if (!options.styleCommandNoop) {
                    const group=groupFor(selected[0].index,definition.id);
                    group.canSetEnabled=true;group._enabled=true;
                }
                if (options.changeSelection) comp._layers.forEach(layer=>layer.selected=true);
            },
            beginUndoGroup(label) {
                if (options.beginError) throw Error('undo begin failed');
                assert.ok(label.length);
                state.begin++;
            },
            endUndoGroup() {
                state.end++;
                if (options.endError) throw Error('undo end failed');
            },
        },
    };
    return {state, comp, context, styleGroups};
}

function run(script, options) {
    const result = host(options);
    result.value = vm.runInNewContext(script, result.context, {timeout: 1000});
    assert.equal(typeof result.value, 'string', 'AEGP must receive a protocol string');
    return result;
}

function errorStatus(result) {
    assert.match(result.value, /^ERROR\t[^\t\r\n]+$/);
}

function balanced(result, expected = 1) {
    assert.equal(result.state.begin, expected);
    assert.equal(result.state.end, expected);
}

)TESTJS";
    out << R"TESTJS(
for (const style of styleScripts) {
    test('native layer style ' + style.id + ' applies once per selected layer and restores selection', () => {
        const selection=[true,false,true];
        const r=run(style.script,{selection,changeSelection:true});
        assert.equal(r.value,'OK\t2'); balanced(r);
        assert.deepEqual(r.state.styleCommands.map(call=>call.command),[style.command,style.command]);
        assert.deepEqual(r.state.styleCommands.map(call=>call.selection),[[1],[3]]);
        assert.deepEqual(r.comp._layers.map(layer=>layer.selected),selection);
        assert.equal(r.state.effectArgs.length,0,'styles must not use effect.addProperty');
    });
    test('existing enabled/disabled style ' + style.id + ' preserves parameters without menu toggle', () => {
        const r=run(style.script,{existingStyles:{1:{[style.id]:true},2:{[style.id]:false}}});
        assert.equal(r.value,'OK\t2');balanced(r);
        assert.equal(r.state.styleCommands.length,0);
        for(const group of r.styleGroups.values()){assert.equal(group.enabled,true);assert.equal(group.parameter,47);}
    });
}
for(const options of [{noProject:true},{noComp:true},{selection:[false,false]}]){
    test('layer style validates project/composition/selection before mutation '+JSON.stringify(options),()=>{
        const r=run(styleScripts[0].script,options);errorStatus(r);balanced(r,0);assert.equal(r.state.styleCommands.length,0);
    });
}
test('unknown style never invokes a menu command',()=>{
    const r=run(unknownStyleScript);errorStatus(r);balanced(r,0);assert.equal(r.state.styleCommands.length,0);
});
for(const options of [{lockedLayer:2},{nonAVLayer:2},{noStyleGroup:2},{styleCommandError:2},{parentDisabled:2}]){
    test('layer style reports partial and restores original selection '+JSON.stringify(options),()=>{
        const r=run(styleScripts[8].script,{...options,selection:[true,true,false]});
        assert.match(r.value,/^PARTIAL\t1\t/);balanced(r);assert.deepEqual(r.comp._layers.map(layer=>layer.selected),[true,true,false]);
    });
}
test('disabled parent style group refuses without touching menu or child parameters',()=>{
    const r=run(styleScripts[0].script,{selection:[true],parentDisabled:1,
        existingStyles:{1:{'dropShadow/enabled':false,'innerGlow/enabled':true}}});
    errorStatus(r);balanced(r);assert.equal(r.state.styleCommands.length,0);assert.equal(r.state.styleWrites.length,0);
    assert.deepEqual(r.comp._layers.map(layer=>layer.selected),[true]);
});
test('style command that silently does nothing is not reported as success',()=>{
    const r=run(styleScripts[0].script,{styleCommandNoop:true});errorStatus(r);balanced(r);assert.equal(r.value.startsWith('OK'),false);
});
for(const options of [{wrongViewer:true},{noActiveViewer:true},{viewerError:true}]) {
    test('style refuses unverified comp viewer before command '+JSON.stringify(options),()=>{
        const r=run(styleScripts[0].script,options);errorStatus(r);balanced(r);assert.equal(r.state.styleCommands.length,0);
        assert.deepEqual(r.comp._layers.map(layer=>layer.selected),[true,true]);
    });
}
test('undefined openInViewer return is accepted only with verified actual composition viewer',()=>{
    const r=run(styleScripts[0].script,{undefinedViewerReturn:true});assert.equal(r.value,'OK\t2');
});
test('style never changes selection if undo group cannot begin',()=>{
    const r=run(styleScripts[0].script,{beginError:true,selection:[true,false]});errorStatus(r);balanced(r,0);
    assert.equal(r.state.styleCommands.length,0);assert.deepEqual(r.comp._layers.map(layer=>layer.selected),[true,false]);
});

test('effect applies matchName once to both selected layers with one undo group', () => {
    const r = run(scripts.effect);
    assert.equal(r.value, 'OK\t2');
    assert.deepEqual(r.comp._layers.map(layer => layer.effects), [
        ['ADBE Gaussian Blur 2'], ['ADBE Gaussian Blur 2'],
    ]);
    balanced(r);
});

for (const [description, registeredEffects] of [
    ['hidden legacy registration', [{matchName: 'ADBE Gaussian Blur 2', displayName: 'Gaussian Blur', category: '_Obsolete'}]],
    ['internal registration', [{matchName: 'ADBE Gaussian Blur 2', displayName: 'Gaussian Blur', category: ''}]],
    ['unregistered/stale identity', [{matchName: 'Unrelated effect', category: 'Vendor'}]],
]) {
    test('effect rejects ' + description + ' before mutation or undo', () => {
        const r = run(scripts.effect, {registeredEffects});
        errorStatus(r);
        assert.match(r.value, /ADBE Gaussian Blur 2/);
        assert.equal(r.state.effectArgs.length, 0);
        balanced(r, 0);
    });
}
for (const category of ['BCC Obsolete', '过时', '_Vendor']) {
    test('effect retains valid visible deprecated/vendor category ' + category, () => {
        const r = run(scripts.effect, {registeredEffects: [{matchName: 'ADBE Gaussian Blur 2', displayName: 'Gaussian Blur', category}]});
        assert.equal(r.value, 'OK\t2');
        balanced(r);
    });
}

for (const registeredEffects of [undefined, null, {}, {length: 'bad'},
    [{matchName: 'ADBE Gaussian Blur 2', displayName: '  ', category: 'Vendor'}]]) {
    test('effect refuses missing/invalid registration metadata without undo or mutation', () => {
        const r = run(scripts.effect, {registeredEffects});
        errorStatus(r); assert.equal(r.state.effectArgs.length, 0); balanced(r, 0);
    });
}
test('same exact ID first hidden then visible follows native discovery selection', () => {
    const r = run(scripts.effect, {registeredEffects: [
        {matchName: 'ADBE Gaussian Blur 2', displayName: 'Old', category: '_Obsolete'},
        {matchName: 'ADBE Gaussian Blur 2', displayName: 'New', category: 'Vendor'},
    ]});
    assert.equal(r.value, 'OK\t2'); balanced(r);
});
test('ASCII category normalization does not invent a Unicode-whitespace hidden rule', () => {
    const r = run(scripts.effect, {registeredEffects: [
        {matchName: 'ADBE Gaussian Blur 2', displayName: 'Name', category: '\u00a0_Obsolete\u00a0'},
    ]});
    assert.equal(r.value, 'OK\t2');
});

test('effect snapshots selection before applying effects changes it', () => {
    const r = run(scripts.effect, {changeSelection: true});
    assert.equal(r.value, 'OK\t2');
    assert.deepEqual(r.comp._layers.map(layer => layer.effects.length), [1, 1]);
});

test('effect reports partial count and sanitizes host error protocol delimiters', () => {
    const r = run(scripts.effect, {effectError: 2});
    assert.match(r.value, /^PARTIAL\t1\t[^\t\r\n]+$/);
    assert.match(r.value, /cannot add/);
    balanced(r);
});

test('effect continues after one layer fails', () => {
    const r = run(scripts.effect, {selection: [true, true, true], effectError: 2});
    assert.match(r.value, /^PARTIAL\t2\t/);
    assert.deepEqual(r.comp._layers.map(layer => layer.effects.length), [1, 0, 1]);
});

for (const options of [{incompatibleLayer: 1}, {noEffectGroup: 1}, {nullEffect: 1}]) {
    test('effect rejects an unsupported or null effect result ' + JSON.stringify(options), () => {
        const r = run(scripts.effect, {...options, selection: [true]});
        errorStatus(r);
        balanced(r);
    });
}

for (const options of [{noComp: true}, {noProject: true}, {selection: [false, false]}]) {
    test('effect validates active composition and selected layers ' + JSON.stringify(options), () => {
        const r = run(scripts.effect, options);
        errorStatus(r);
        balanced(r, 0);
    });
}

test('empty effect matchName causes no mutation', () => {
    const r = run(scripts.effectEmpty);
    errorStatus(r);
    assert.equal(r.state.effectArgs.length, 0);
    balanced(r, 0);
});

test('effect matchName quotes, controls, Chinese and line separators cannot inject code', () => {
    const r = run(scripts.effectHostile);
    assert.equal(r.value, 'OK\t2');
    assert.equal(r.state.effectArgs[0], 'ADBE ";injected=true;//\n\t\x01中文\u2028');
    assert.equal(r.context.injected, false);
});

test('failed undo begin never calls end or applies an effect', () => {
    const r = run(scripts.effect, {beginError: true});
    errorStatus(r);
    assert.equal(r.state.effectArgs.length, 0);
    balanced(r, 0);
});

test('failed undo end preserves the successful layer count', () => {
    const r = run(scripts.effect, {endError: true});
    assert.match(r.value, /^PARTIAL\t2\t/);
    balanced(r);
});

)TESTJS";
    out << R"TESTJS(
test('preset isolates each selection so two layers receive one preset each', () => {
    const r = run(scripts.preset, {selection: [true, false, true], changeSelection: true});
    assert.equal(r.value, 'OK\t2');
    assert.deepEqual(r.comp._layers.map(layer => layer.presets), [
        ["C:\\预设\\O'Brien.ffx"], [], ["C:\\预设\\O'Brien.ffx"],
    ]);
    assert.deepEqual(r.state.presetSelections, [[1], [3]]);
    assert.deepEqual(r.comp._layers.map(layer => layer.selected), [true, false, true]);
    balanced(r);
});

test('preset failure reports partial count and restores original selection', () => {
    const r = run(scripts.preset, {
        selection: [true, true, true, false], presetError: 2, changeSelection: true,
    });
    assert.match(r.value, /^PARTIAL\t2\t[^\t\r\n]+$/);
    assert.deepEqual(r.comp._layers.map(layer => layer.presets.length), [1, 0, 1, 0]);
    assert.deepEqual(r.comp._layers.map(layer => layer.selected), [true, true, true, false]);
    balanced(r);
});

for (const options of [{fileExists: false}, {selection: [false, false]}, {noComp: true}]) {
    test('preset invalid context or file never applies ' + JSON.stringify(options), () => {
        const r = run(scripts.preset, options);
        errorStatus(r);
        assert.equal(r.state.presetSelections.length, 0);
        balanced(r, 0);
    });
}

const hostilePath = 'C:\\中文\\O\'Brien\\";injected=true;//\n\t\x01\u2028🚀.png';
for (const operation of ['presetHostile', 'captureHostile', 'importHostile']) {
    test(operation + ' preserves the complete Unicode path without executing it', () => {
        const r = run(scripts[operation]);
        assert.match(r.value, /^OK\t[12]$/);
        assert.ok(r.state.files.includes(hostilePath));
        assert.equal(r.context.injected, false);
    });
}

test('PNG capture renders the current comp time to the chosen file', () => {
    const r = run(scripts.capture, {fileExists: false});
    assert.equal(r.value, 'OK\t1');
    assert.deepEqual(r.state.captures, [{time: 3.75, path: 'C:\\截图\\合成.png'}]);
    assert.equal(r.state.imports.length, 0);
});

test('PNG capture refreshes a File object whose exists flag was cached before rendering', () => {
    const r = run(scripts.capture, {fileExists: false, cacheFileExists: true});
    assert.equal(r.value, 'OK\t1');
    assert.equal(r.state.captures.length, 1);
});

test('PNG capture tolerates a short delayed filesystem update', () => {
    const r = run(scripts.capture, {fileExists: false, cacheFileExists: true, captureDelay: 2});
    assert.equal(r.value, 'OK\t1');
    assert.equal(r.state.sleeps.length, 2);
    assert.ok(r.state.sleeps.reduce((total, delay) => total + delay, 0) <= 1000);
});

test('PNG capture bounds retries when no file is created', () => {
    const r = run(scripts.capture, {fileExists: false, cacheFileExists: true, captureNoFile: true});
    errorStatus(r);
    assert.equal(r.state.sleeps.length, 10);
    assert.ok(r.state.sleeps.reduce((total, delay) => total + delay, 0) <= 1000);
});

for (const options of [
    {noComp: true}, {parentExists: false}, {captureError: 'render failed\nblocked'},
    {fileExists: false, captureNoFile: true},
]) {
    test('PNG capture surfaces a context, filesystem or render failure ' + JSON.stringify(options), () => {
        errorStatus(run(scripts.capture, options));
    });
}

test('empty PNG path cannot render', () => {
    const r = run(scripts.captureEmpty);
    errorStatus(r);
    assert.equal(r.state.captures.length, 0);
});

test('image import uses ImportOptions and disables automatic image sequences', () => {
    const r = run(scripts.importImage, {noComp: true});
    assert.equal(r.value, 'OK\t1');
    assert.deepEqual(r.state.imports, [{path: 'C:\\截图\\合成.png', sequence: false}]);
    balanced(r);
});

for (const options of [
    {noProject: true}, {fileExists: false}, {importError: 'import failed\tbad\nimage'}, {nullImport: true},
]) {
    test('image import reports missing context, file or import failure ' + JSON.stringify(options), () => {
        const r = run(scripts.importImage, options);
        errorStatus(r);
        assert.equal(r.state.imports.length, 0);
        assert.equal(r.state.begin, r.state.end);
    });
}

test('empty import path cannot import a file', () => {
    const r = run(scripts.importEmpty);
    errorStatus(r);
    assert.equal(r.state.imports.length, 0);
    balanced(r, 0);
});

if (process.exitCode) {
    console.error('AE script VM tests failed; ' + passed + ' passed');
} else {
    console.log('AE script VM tests passed: ' + passed);
}
)TESTJS";
    if (!out) {
        std::cerr << "Cannot finish writing AE script VM tests\n";
        return 1;
    }
    return 0;
}
