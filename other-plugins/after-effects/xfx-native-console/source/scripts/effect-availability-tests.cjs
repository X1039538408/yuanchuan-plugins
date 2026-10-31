'use strict';
const assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, 'export-effect-availability.jsx'), 'utf8');
function run(options = {}) {
  let written = '', queries = [], mutations = 0;
  const readOnly = target => new Proxy(target, {
    set() { ++mutations; throw Error('Project mutation forbidden'); },
    get(obj, key) {
      if (['addProperty', 'addSolid', 'addComp', 'remove', 'save', 'saveWithDialog', 'newProject', 'open', 'beginUndoGroup'].includes(key)) {
        ++mutations; throw Error('Mutation API read forbidden: ' + key);
      }
      return obj[key];
    },
  });
  const group = readOnly({ canAddProperty(id) { queries.push(id); if (id === 'throws') throw Error('unsupported'); return id !== 'hidden'; } });
  const layer = readOnly({ index: 3, hasVideo: true, hasAudio: false, locked: true, property(name) { assert.equal(name, 'ADBE Effect Parade'); return group; } });
  function CompItem() { this.selectedLayers = options.noSelection ? [] : [layer]; this.numLayers = 1; this.layer = () => layer; }
  const comp = readOnly(new CompItem());
  const project = readOnly({ activeItem: options.noActive ? null : comp, numItems: options.emptyProject ? 0 : 1, item: () => comp });
  function File(name) { this.fsName = name; this.parent = { parent: { fsName: '/source' } }; this.open = () => !options.denied; this.write = s => { written = s; return true; }; this.close = () => true; }
  function Folder(name) { this.fsName = name; this.exists = true; }
  const effects = [{ matchName: 'visible', displayName: '有效\"名称', category: 'Test' }, { matchName: 'hidden', displayName: '内部', category: '' }, { matchName: 'throws', displayName: '异常', category: '_Obsolete' }];
  let alerts = [];
  vm.runInNewContext(source, { File, Folder, CompItem, $: { fileName: '/source/scripts/probe.jsx' }, app: readOnly({ project, effects, version: '25.4' }), alert: message => alerts.push(message) });
  assert.equal(mutations, 0);
  return { data: written ? JSON.parse(written) : null, queries, alerts };
}
const selected = run();
assert.equal(selected.data.probeSource, 'selected-layer');
assert.deepEqual(selected.data.probeContext, { layerKind: 'Unknown', layerIndex: 3, hasVideo: true, hasAudio: false, locked: true });
assert.deepEqual(selected.queries, ['visible', 'hidden', 'throws']);
assert.deepEqual(selected.data.effects.map(e => e.canAdd), [true, false, null]);
assert.equal(selected.data.effects[0].displayName, '有效\"名称');
assert.match(selected.data.effects[2].error, /unsupported/);
assert.equal(run({ noSelection: true }).data.probeSource, 'existing-layer');
assert.equal(run({ noActive: true }).data.probeStatus, 'checked-existing-layer');
const empty = run({ noActive: true, emptyProject: true });
assert.equal(empty.data.probeStatus, 'no-compatible-existing-layer');
assert.equal(empty.queries.length, 0);
assert.ok(empty.data.effects.every(e => e.canAdd === null));
assert.equal(run({ denied: true }).alerts.length, 1);
console.log('PASS read-only eligibility probe: full IDs, canAdd false/unknown, no layer/selection mutation, no-active/empty project and write denial');
