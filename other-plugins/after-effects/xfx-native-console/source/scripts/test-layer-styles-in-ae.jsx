// Optional host integration test. Only test-owned comp is changed and removed.
// User-authorized temporary-comp mode: never opens/saves/replaces the project
// or changes any original comp/layer/media. Creates/deletes leave undo/dirty state.
(function () {
    var source = new File($.fileName);
    var root = source.parent.parent.fsName;
    var directory = new Folder(root + '/artifacts/layer-styles-20261029');
    var output = new File(directory.fsName + '/ae-layer-style-validation.json');
    var comp = null, removed = false, results = [], error = '', previous = null;
    var originalIds = {}, originalCount = -1, testSources = [], testFolders = [];
    function quote(value) {
        return '"' + String(value == null ? '' : value).replace(/["\\\u0000-\u001f]/g, function (ch) {
            if (ch === '"' || ch === '\\') return '\\' + ch;
            return '\\u' + ('0000' + ch.charCodeAt(0).toString(16)).slice(-4);
        }) + '"';
    }
    function require(value, message) { if (!value) throw new Error(message); }
    function write(text) {
        if (!directory.exists && !directory.create()) throw new Error('Cannot create diagnostic directory');
        output.encoding = 'UTF-8';
        if (!output.open('w')) throw new Error('Cannot write diagnostic report; no test should proceed');
        output.write(text); output.close();
    }
    try {
        // Check write permission before creating any test project item.
        write('{"status":"started"}\n');
        require(app.project, 'Open an AE project first');
        originalCount = app.project.numItems;
        for (var itemIndex = 1; itemIndex <= originalCount; ++itemIndex)
            originalIds['id' + app.project.item(itemIndex).id] = true;
        var dataFile = new File(root + '/native/build/ae-script-tests/layer_style_scripts.json');
        dataFile.encoding = 'UTF-8';
        require(dataFile.open('r'), 'Run run-ae-script-tests.ps1 first to export actual scripts');
        var text = dataFile.read(); dataFile.close();
        var tests = eval('(' + text + ')');
        require(tests.length === 9, 'Expected nine exported native style actions');
        previous = app.project.activeItem;
        comp = app.project.items.addComp('__XFX_LAYER_STYLE_TEST_' + (new Date()).getTime(), 128, 128, 1, 1, 24);
        function addTestSolid(name) {
            var layer = comp.layers.addSolid([0, 0, 0], name, 128, 128, 1, 1);
            var item = layer.source;
            if (item && !originalIds['id' + item.id]) {
                testSources.push(item);
                var folder = item.parentFolder;
                if (folder && folder.id !== app.project.rootFolder.id && !originalIds['id' + folder.id])
                    testFolders.push(folder);
            }
            return layer;
        }
        var one = addTestSolid('__XFX_STYLE_ONE__');
        var two = addTestSolid('__XFX_STYLE_TWO__');
        var untouched = addTestSolid('__XFX_STYLE_UNSELECTED__');
        comp.openInViewer();
        for (var i = 0; i < tests.length; ++i) {
            var test = tests[i];
            for (var n = 1; n <= comp.numLayers; ++n) comp.layer(n).selected = false;
            one.selected = true; two.selected = true;
            var reply = String(eval(test.script));
            var a = one.property('ADBE Layer Styles').property(test.id);
            var b = two.property('ADBE Layer Styles').property(test.id);
            var c = untouched.property('ADBE Layer Styles').property(test.id);
            require(reply === 'OK\t2', 'Command failed ' + test.id + ': ' + reply);
            require(a && b && a.canSetEnabled && b.canSetEnabled && a.enabled && b.enabled, 'Style postcondition failed: ' + test.id);
            require(!c || !c.canSetEnabled || !c.enabled, 'Unselected layer was changed: ' + test.id);
            require(one.selected && two.selected && !untouched.selected, 'Layer selection was not restored: ' + test.id);
            var properties = a.numProperties, probeId = '', probeValue = null;
            for (var p = 1; p <= a.numProperties; ++p) {
                var parameter = a.property(p);
                if (parameter && /\/opacity$/.test(parameter.matchName) && typeof parameter.value === 'number') {
                    parameter.setValue(37); probeId = parameter.matchName; probeValue = 37; break;
                }
            }
            // Existing enabled style must remain enabled and not create siblings.
            var repeat = String(eval(test.script));
            require(repeat === 'OK\t2', 'Repeated style failed: ' + test.id);
            a = one.property('ADBE Layer Styles').property(test.id);
            require(a.enabled && a.numProperties === properties, 'Repeated style toggled/reset: ' + test.id);
            if (probeId) require(a.property(probeId).value === probeValue, 'Repeated style reset opacity: ' + test.id);
            a.enabled = false;
            var reenable = String(eval(test.script));
            a = one.property('ADBE Layer Styles').property(test.id);
            require(reenable === 'OK\t2' && a.enabled, 'Disabled style did not re-enable: ' + test.id);
            results.push('{"id":' + quote(test.id) + ',"command":' + test.command + ',"status":"passed","initial":' + quote(reply) + ',"repeated":' + quote(repeat) + ',"reenabled":' + quote(reenable) + '}');
        }
    } catch (failure) { error = String(failure); }
    finally {
        if (comp) { try { comp.remove(); removed = true; } catch (cleanup) { error += '; cleanup: ' + cleanup.toString(); } }
        for (var s = 0; s < testSources.length; ++s) {
            try { if (testSources[s].usedIn.length === 0) testSources[s].remove(); }
            catch (sourceCleanup) { error += '; test source cleanup: ' + sourceCleanup.toString(); }
        }
        var cleanedFolders = {};
        for (var f = testFolders.length - 1; f >= 0; --f) {
            try {
                var folderId = 'id' + testFolders[f].id;
                if (!cleanedFolders[folderId] && testFolders[f].numItems === 0) {
                    cleanedFolders[folderId] = true; testFolders[f].remove();
                }
            } catch (folderCleanup) {
                // Repeated references to a test-owned folder already removed
                // are expected; no original folder is ever a cleanup target.
            }
        }
        if (previous && previous instanceof CompItem) { try { previous.openInViewer(); } catch (ignored) {} }
        var json = '{"schemaVersion":1,"aeVersion":' + quote(app.version) + ',"status":' + quote(error ? 'failed' : 'passed') +
            ',"tested":' + results.length + ',"testCompRemoved":' + String(removed) +
            ',"originalItemCount":' + originalCount + ',"finalItemCount":' + (app.project ? app.project.numItems : -1) +
            ',"scope":"test-owned temp comp and test solid sources only; undo/dirty state may change; no render or undo-host assertion","error":' + quote(error) + ',"results":[' + results.join(',') + ']}\n';
        try { write(json); } catch (reportError) { alert('Layer style test report failed: ' + reportError.toString()); }
        if (error) alert('Layer style validation stopped: ' + error);
    }
})();
