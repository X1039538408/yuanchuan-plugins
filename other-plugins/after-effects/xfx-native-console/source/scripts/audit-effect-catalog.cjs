'use strict';
// Inventory diagnostics only. Discovery visibility != canAddProperty != rendering.
const fs = require('node:fs');
function reason(effect) {
  if (!effect || typeof effect.matchName !== 'string' || !effect.matchName.trim()) return 'missing-stable-id';
  if (typeof effect.displayName !== 'string' || !effect.displayName.trim()) return 'missing-display-name';
  if (typeof effect.category !== 'string') return 'unknown-category';
  const category = effect.category.trim();
  if (!category) return 'uncategorized-registration';
  if (category === '_Obsolete') return 'hidden-legacy-registration';
  return null;
}
function audit(inventory) {
  if (inventory.schemaVersion !== 1 || !Array.isArray(inventory.effects)) throw Error('Unsupported effect inventory schema');
  const seen = new Set(), visible = [], excluded = [], duplicateIds = [], labels = new Map(), counts = {};
  for (const e of inventory.effects) {
    const hidden = reason(e);
    if (hidden) { excluded.push({ ...e, reason: hidden }); counts[hidden] = (counts[hidden] || 0) + 1; continue; }
    if (seen.has(e.matchName)) { duplicateIds.push(e.matchName); continue; }
    seen.add(e.matchName);
    visible.push(e);
    const label = e.displayName.trim().toLowerCase();
    if (!labels.has(label)) labels.set(label, []);
    labels.get(label).push(e.matchName);
  }
  return { schemaVersion: 1, aeVersion: inventory.aeVersion || '',
    scope: 'default discovery policy only; no canAddProperty, licensing or rendering claim',
    totalRegistrations: inventory.effects.length, visibleCount: visible.length,
    excludedCount: excluded.length, excludedReasons: counts, duplicateIds,
    sameLabelVisibleEffects: Array.from(labels, ([displayName, matchNames]) => ({ displayName, matchNames })).filter(row => row.matchNames.length > 1),
    excluded, visible };
}
if (require.main === module) {
  try {
    if (process.argv.length !== 3) throw Error('Usage: node audit-effect-catalog.cjs <ae-effects.json>');
    process.stdout.write(JSON.stringify(audit(JSON.parse(fs.readFileSync(process.argv[2], 'utf8').replace(/^\uFEFF/, ''))), null, 2) + '\n');
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
module.exports = { audit, reason };
