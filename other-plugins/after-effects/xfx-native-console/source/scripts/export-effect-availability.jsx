// Read-only eligibility probe. Does not add effects/layers, change selection,
// open/save projects, or create undo groups. Writes only a diagnostic JSON file.
(function () {
    function quote(value) {
        return '"' + String(value == null ? '' : value).replace(/["\\\u0000-\u001f]/g, function (ch) {
            if (ch === '"' || ch === '\\') return '\\' + ch;
            return '\\u' + ('0000' + ch.charCodeAt(0).toString(16)).slice(-4);
        }) + '"';
    }
    function effectsOf(layer) {
        try { return layer && layer.property('ADBE Effect Parade'); } catch (ignored) { return null; }
    }
    function findProbe(project) {
        function candidate(group, layer, source) {
            var kind = 'Unknown';
            if (typeof AVLayer !== 'undefined' && layer instanceof AVLayer) kind = 'AVLayer';
            else if (typeof CameraLayer !== 'undefined' && layer instanceof CameraLayer) kind = 'CameraLayer';
            else if (typeof LightLayer !== 'undefined' && layer instanceof LightLayer) kind = 'LightLayer';
            return { group: group, source: source,
                context: '{"layerKind":' + quote(kind) + ',"layerIndex":' +
                    (typeof layer.index === 'number' && isFinite(layer.index) ? layer.index : 'null') +
                    ',"hasVideo":' + String(!!layer.hasVideo) + ',"hasAudio":' + String(!!layer.hasAudio) +
                    ',"locked":' + String(!!layer.locked) + '}' };
        }
        function fromComp(comp) {
            if (!comp || !(comp instanceof CompItem)) return null;
            var selected = comp.selectedLayers || [];
            var i, group;
            for (i = 0; i < selected.length; ++i) {
                group = effectsOf(selected[i]);
                if (group) return candidate(group, selected[i], 'selected-layer');
            }
            for (i = 1; i <= comp.numLayers; ++i) {
                var layer = comp.layer(i);
                group = effectsOf(layer);
                if (group) return candidate(group, layer, 'existing-layer');
            }
            return null;
        }
        if (!project) return null;
        var probe = fromComp(project.activeItem);
        if (probe) return probe;
        for (var i = 1; i <= project.numItems; ++i) {
            probe = fromComp(project.item(i));
            if (probe) return probe;
        }
        return null;
    }
    var source = new File($.fileName);
    var directory = new Folder(source.parent.parent.fsName + '/artifacts/effect-inventory');
    var output = null;
    try {
        var probe = findProbe(app.project);
        var rows = [], effects = app.effects;
        for (var i = 0; i < effects.length; ++i) {
            var e = effects[i], allowed = 'null', error = '';
            if (probe) {
                try { allowed = probe.group.canAddProperty(e.matchName) ? 'true' : 'false'; }
                catch (failure) { error = String(failure); }
            }
            rows.push('{"matchName":' + quote(e.matchName) + ',"displayName":' + quote(e.displayName) +
                ',"category":' + quote(e.category) + ',"canAdd":' + allowed + ',"error":' + quote(error) + '}');
        }
        if (!directory.exists && !directory.create()) throw new Error('Cannot create probe directory');
        output = new File(directory.fsName + '/ae-effect-availability.json');
        output.encoding = 'UTF-8';
        if (!output.open('w')) throw new Error('Cannot write probe: ' + output.error);
        var json = '{"schemaVersion":1,"aeVersion":' + quote(app.version) +
            ',"probeStatus":' + quote(probe ? 'checked-existing-layer' : 'no-compatible-existing-layer') +
            ',"probeSource":' + quote(probe ? probe.source : '') +
            ',"probeContext":' + (probe ? probe.context : 'null') +
            ',"scope":"canAddProperty only; not render/licensing validation","effects":[' + rows.join(',\n') + ']}\n';
        if (!output.write(json)) throw new Error('Probe write failed');
        if (!output.close()) throw new Error('Probe close failed');
        output = null;
    } catch (error) {
        if (output) { try { output.close(); } catch (ignored) {} }
        alert('Effect eligibility probe failed: ' + error.toString());
    }
})();
