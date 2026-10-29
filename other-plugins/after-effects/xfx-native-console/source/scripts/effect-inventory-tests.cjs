'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, 'export-effect-inventory.jsx'), 'utf8');
function run(effects, writable = true) {
  let text = '', closed = 0;
  const alerts = [];
  function File(name) {
    this.fsName = name;
    this.parent = { parent: { fsName: '/fixture/source' } };
    this.open = mode => { assert.equal(mode, 'w'); return writable; };
    this.write = value => { text += value; return true; };
    this.close = () => { ++closed; return true; };
    this.error = 'fixture write denied';
  }
  function Folder(name) { this.fsName = name; this.exists = true; }
  // Any accidental project access throws instead of silently mutating a fixture.
  const app = { version: '25.4', effects };
  Object.defineProperty(app, 'project', { get() { throw new Error('Project must not be accessed'); } });
  vm.runInNewContext(source, { File, Folder, app, $: { fileName: '/fixture/source/scripts/export.jsx' }, alert: value => alerts.push(value) });
  return { text, closed, alerts };
}
const fixture = [
  { matchName: 'vendor:id\\1', displayName: 'BCC+Rectangular Wipe', category: '第三方\t转场', version: 1 },
  { matchName: 'emoji-id', displayName: '中文 "引号"\n🎬', category: '', version: null },
];
const success = run(fixture);
assert.deepEqual(JSON.parse(success.text), { schemaVersion: 1, aeVersion: '25.4', effects: fixture.map(x => ({ ...x, version: x.version == null ? '' : String(x.version) })) });
assert.equal(success.closed, 1);
assert.equal(success.alerts.length, 0);
assert.deepEqual(JSON.parse(run([]).text).effects, []);
const failed = run(fixture, false);
assert.equal(failed.text, '');
assert.equal(failed.alerts.length, 1);
console.log('PASS inventory JSON, Unicode/control characters, empty catalog, denied writes and no project access');
