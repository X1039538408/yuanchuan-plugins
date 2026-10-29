// Read-only AE catalog export: never opens, saves, or changes a project.
// Run via File > Scripts > Run Script File, or AfterFX.exe -r <this script>.
(function () {
    function quote(value) {
        return '"' + String(value == null ? "" : value).replace(/["\\\u0000-\u001f]/g, function (ch) {
            if (ch === '"' || ch === '\\') return '\\' + ch;
            return '\\u' + ('0000' + ch.charCodeAt(0).toString(16)).slice(-4);
        }) + '"';
    }
    var source = new File($.fileName);
    var directory = new Folder(source.parent.parent.fsName + '/artifacts/effect-inventory');
    var output = null;
    try {
        if (!directory.exists && !directory.create()) throw new Error('Cannot create inventory directory');
        var rows = [];
        var effects = app.effects;
        for (var i = 0; i < effects.length; ++i) {
            var effect = effects[i];
            rows.push('{"matchName":' + quote(effect.matchName) +
                ',"displayName":' + quote(effect.displayName) +
                ',"category":' + quote(effect.category) +
                ',"version":' + quote(effect.version) + '}');
        }
        output = new File(directory.fsName + '/ae-effects.json');
        output.encoding = 'UTF-8';
        if (!output.open('w')) throw new Error('Cannot write inventory: ' + output.error);
        if (!output.write('{"schemaVersion":1,"aeVersion":' + quote(app.version) +
            ',"effects":[' + rows.join(',\n') + ']}\n')) throw new Error('Inventory write failed');
        if (!output.close()) throw new Error('Inventory close failed');
        output = null;
    } catch (error) {
        if (output) { try { output.close(); } catch (ignored) {} }
        alert('Effect inventory export failed: ' + error.toString());
    }
})();
