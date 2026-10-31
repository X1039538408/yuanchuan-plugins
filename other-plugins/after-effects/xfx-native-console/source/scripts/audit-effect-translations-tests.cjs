'use strict';
const assert = require('node:assert/strict');
const { audit, key } = require('./audit-effect-translations.cjs');
const inventory = { schemaVersion: 1, aeVersion: '25.4', effects: [
  { matchName: 'Sapphire-id', displayName: 'S_WipeBubble' },
  { matchName: 'bcc-id', displayName: 'BCC+Rectangular Wipe' },
  { matchName: 'other-id', displayName: 'Unlisted Vendor Effect' },
  { matchName: 'ADBE Gaussian Blur 2', displayName: 'Gaussian Blur' },
  { matchName: 'no-han', displayName: 'S_Unknown' },
  { matchName: 'bcc-id', displayName: 'Duplicate label' },
] };
const table = '\uFEFF# columns\n' + [
  '\tS_WipeBubble\tSapphire 气泡擦除',
  'bcc-id\t\t旧译名',
  '\tBCC+ Rectangular Wipe\tBCC+ 矩形擦除',
  'ADBE Gaussian Blur 2\tGaussian Blur\t高斯模糊',
  '\tS_Unknown\tUnknown',
].join('\n');
const report = audit(inventory, table);
assert.equal(report.total, 5);
assert.equal(report.covered, 3);
assert.deepEqual(report.duplicateMatchNames, ['bcc-id']);
assert.deepEqual(report.groups['BCC+'], { total: 1, covered: 1, missing: 0, coveragePercent: 100 });
assert.equal(report.groups.Sapphire.coveragePercent, 50);
assert.equal(report.effects[1].translation, 'BCC+ 矩形擦除');
assert.equal(report.effects[1].conflictingLookup, true);
assert.equal(report.effects[1].matchName, 'bcc-id');
assert.equal(report.missing.length, 2);
assert.equal(report.missing[0].family, 'Unclassified (needs review)');
assert.equal(key(' BCC+\tRectangular Wipe '), key('bcc+Rectangular Wipe'));
assert.notEqual(key('BCC Linear Wipe'), key('BCC+Linear Wipe'));
assert.throws(() => audit({ effects: [] }, table), /schema/);
assert.throws(() => audit({ schemaVersion: 1, effects: [{ matchName: '', displayName: 'x' }] }, table), /record/);
assert.equal(audit({ schemaVersion: 1, effects: [] }, table).total, 0);
const families = audit({ schemaVersion: 1, effects: [
  { matchName: 'BCC_AMBIENTLIGHT', displayName: ' BCC+Ambient Light', category: 'BCC Lights' },
  { matchName: 'ScriptET', displayName: 'Extruded Text', category: 'BCC 3D Objects' },
  { matchName: 'tc Form', displayName: 'Form', category: 'RG Particles and 3D' },
  { matchName: 'rg Mir', displayName: 'Mir 3', category: 'RG Particles and 3D' },
] }, table);
assert.equal(families.groups['BCC+'].total, 1);
assert.equal(families.groups['BCC / BorisFX'].total, 1);
assert.equal(families.groups.Trapcode.total, 1);
assert.equal(families.groups['Red Giant / Maxon'].total, 1);
console.log('PASS coverage audit: missing/unclassified retained, BCC+ spacing, priority conflicts, IDs, duplicates and invalid input');
