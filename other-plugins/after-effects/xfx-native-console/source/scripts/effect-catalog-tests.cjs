'use strict';
const assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path');
const { audit, reason } = require('./audit-effect-catalog.cjs');
const inv = { schemaVersion: 1, effects: [
  { matchName: 'a', displayName: 'Same Name', category: 'Vendor' },
  { matchName: 'b', displayName: 'Same Name', category: 'BCC Obsolete' },
  { matchName: 'a', displayName: 'Same Name', category: 'Vendor' },
  { matchName: 'hidden', displayName: 'Same Name', category: ' _Obsolete ' },
  { matchName: 'internal', displayName: 'Internal', category: '' },
  { matchName: 'user', displayName: 'Custom', category: '_Vendor' },
  { matchName: 'old', displayName: 'Old', category: '过时' },
] };
const result = audit(inv);
assert.equal(result.visibleCount, 4);
assert.equal(result.excludedCount, 2);
assert.deepEqual(result.duplicateIds, ['a']);
assert.deepEqual(result.sameLabelVisibleEffects, [{ displayName: 'same name', matchNames: ['a', 'b'] }]);
assert.equal(reason({matchName: ' ', displayName: 'x', category: 'V'}), 'missing-stable-id');
assert.equal(reason({matchName: 'ID', displayName: ' ', category: 'V'}), 'missing-display-name');
assert.equal(reason({matchName: 'ID', displayName: 'x'}), 'unknown-category');
assert.throws(() => audit({ effects: [] }), /schema/);
const fixture = fs.readFileSync(path.join(__dirname, '../native/tests/data/ae-25.4-effects.txt'), 'utf8');
const effects = fixture.split(/\r?\n/).filter(line => line && !line.startsWith('#')).map(line => {
  const [matchName, displayName, category] = line.split('\t'); return { matchName, displayName, category };
});
const full = audit({ schemaVersion: 1, effects });
assert.equal(full.totalRegistrations, 1401);
assert.equal(full.visibleCount, 1203);
assert.equal(full.excludedCount, 198);
assert.equal(full.sameLabelVisibleEffects.length, 0);
assert.ok(full.visible.some(e => e.matchName === 'ADBE Geometry2'));
assert.ok(!full.visible.some(e => e.matchName === 'ADBE Geometry' || /^ADBE PSL /.test(e.matchName)));
assert.equal(full.visible.filter(e => e.category === 'BCC Obsolete').length, 117);
assert.equal(full.visible.filter(e => e.category === '过时').length, 10);
console.log('PASS catalog policy: actual 1401 -> 1203, hidden/internal 198, zero remaining snapshot label collisions; distinct valid IDs/deprecated groups preserved');
