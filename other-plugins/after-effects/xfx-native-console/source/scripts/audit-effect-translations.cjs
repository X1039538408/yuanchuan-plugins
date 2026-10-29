'use strict';
// Bundled-dictionary audit only: never reads personal settings or modifies AE.
const fs = require('node:fs');
const path = require('node:path');

function key(value) {
  return value.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g, '').replace(/[ \t\r\n]+/g, ' ')
    .replace(/[A-Z]/g, c => c.toLowerCase()).replace(/^bcc\+ /, 'bcc+');
}
function readDictionary(text) {
  const byMatch = new Map(), byEnglish = new Map();
  let priority = 0;
  for (const raw of text.replace(/^\uFEFF/, '').split(/\r?\n/)) {
    if (!raw || raw.startsWith('#')) continue;
    const fields = raw.split('\t');
    if (fields.length < 3 || fields.length > 6) throw new Error('Invalid dictionary column count');
    const row = { matchName: fields[0], englishName: fields[1], translation: fields[2], priority: ++priority };
    if (!row.matchName && !row.englishName) throw new Error('Dictionary row has no lookup key');
    if (row.matchName) byMatch.set(key(row.matchName), row);
    if (row.englishName) byEnglish.set(key(row.englishName), row);
  }
  return { byMatch, byEnglish };
}
function family(effect) {
  // Only explicit brand signals; do not label every non-ADBE ID as third-party.
  const label = effect.displayName;
  const signals = `${label} ${effect.matchName} ${effect.category || ''}`;
  if (/^S_/i.test(label) || /\bSapphire\b/i.test(signals)) return 'Sapphire';
  if (/^BCC\+/i.test(label)) return 'BCC+';
  if (/^BCC(?:\s|$)/i.test(label) || /\bBoris(?:FX)?\b/i.test(signals)) return 'BCC / BorisFX';
  if (/\bTrapcode\b/i.test(signals)) return 'Trapcode';
  if (/\bUniverse\b/i.test(signals) || /^uni[. ]/i.test(label)) return 'Universe';
  if (/\bMagic Bullet\b/i.test(signals)) return 'Magic Bullet';
  if (/\bTwixtor\b/i.test(signals)) return 'Twixtor';
  if (/\bReelSmart\b|\bRSMB\b/i.test(signals)) return 'ReelSmart / RSMB';
  if (/\bVideo Copilot\b|\bOptical Flares\b/i.test(signals)) return 'Video Copilot';
  if (/^ADBE\b/.test(effect.matchName) || /^CC\s/.test(label)) return 'Adobe / bundled (heuristic)';
  return 'Unclassified (needs review)';
}
function audit(inventory, dictionaryText) {
  if (inventory.schemaVersion !== 1 || !Array.isArray(inventory.effects)) throw new Error('Unsupported inventory schema');
  const dictionary = readDictionary(dictionaryText);
  const groups = {}, effects = [], seen = new Set(), duplicates = [];
  for (const effect of inventory.effects) {
    if (!effect || typeof effect.matchName !== 'string' || !effect.matchName || typeof effect.displayName !== 'string' ||
        (effect.category !== undefined && typeof effect.category !== 'string')) throw new Error('Invalid effect record');
    if (seen.has(effect.matchName)) { duplicates.push(effect.matchName); continue; }
    seen.add(effect.matchName);
    const byMatch = dictionary.byMatch.get(key(effect.matchName));
    const byEnglish = dictionary.byEnglish.get(key(effect.displayName));
    // Same selection priority as load_aliases/append_effects for this single TSV.
    const selected = byEnglish && (!byMatch || byEnglish.priority > byMatch.priority) ? byEnglish : byMatch;
    const translation = selected?.translation || '';
    const covered = /[\u3400-\u9fff\uf900-\ufaff]/.test(translation);
    const group = family(effect);
    groups[group] ||= { total: 0, covered: 0, missing: 0 };
    ++groups[group].total;
    ++groups[group][covered ? 'covered' : 'missing'];
    effects.push({ matchName: effect.matchName, displayName: effect.displayName, category: effect.category || '',
      family: group, translation, covered, matchedBy: selected ? (selected === byEnglish ? 'displayName' : 'matchName') : null,
      conflictingLookup: !!(byMatch && byEnglish && byMatch.translation !== byEnglish.translation) });
  }
  for (const group of Object.values(groups)) group.coveragePercent = Number((100 * group.covered / group.total).toFixed(2));
  return { schemaVersion: 1, aeVersion: inventory.aeVersion || '', scope: 'bundled TSV coverage, not runtime/user override verification',
    familyClassification: 'heuristic; unclassified effects are retained, not silently counted as Adobe',
    total: effects.length, covered: effects.filter(e => e.covered).length, groups, duplicateMatchNames: duplicates,
    missing: effects.filter(e => !e.covered), effects };
}
if (require.main === module) {
  try {
    const [inventoryPath, dictionaryPath] = process.argv.slice(2);
    if (!inventoryPath || !dictionaryPath) throw new Error('Usage: node audit-effect-translations.cjs <ae-effects.json> <aliases.tsv>');
    const inventory = JSON.parse(fs.readFileSync(path.resolve(inventoryPath), 'utf8').replace(/^\uFEFF/, ''));
    process.stdout.write(JSON.stringify(audit(inventory, fs.readFileSync(path.resolve(dictionaryPath), 'utf8')), null, 2) + '\n');
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
module.exports = { audit, key };
