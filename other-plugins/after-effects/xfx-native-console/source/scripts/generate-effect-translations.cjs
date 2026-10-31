'use strict';
// The installed TSV is the source of truth; compiled defaults must not drift.
const fs = require('node:fs');
const path = require('node:path');
function generate(text) {
  const rows = [];
  const ids = new Set();
  for (const [index, line] of text.replace(/^\uFEFF/, '').split(/\r?\n/).entries()) {
    if (!line || line.startsWith('#')) continue;
    const fields = line.split('\t');
    if (fields.length < 3 || fields.length > 6) throw new Error(`Invalid TSV columns on line ${index + 1}`);
    while (fields.length < 6) fields.push('');
    if ((!fields[0] && !fields[1]) || !fields[2]) throw new Error(`Missing translation/key on line ${index + 1}`);
    if (fields[0] && ids.has(fields[0])) throw new Error(`Duplicate stable ID: ${fields[0]}`);
    if (fields[0]) ids.add(fields[0]);
    const wide = s => Array.from(JSON.stringify(s), ch => {
      const cp = ch.codePointAt(0);
      return cp <= 0x7e ? ch : (cp <= 0xffff ? '\\u' + cp.toString(16).padStart(4, '0') : '\\U' + cp.toString(16).padStart(8, '0'));
    }).join('');
    rows.push(`    {${JSON.stringify(fields[0])}, ${JSON.stringify(fields[1])}, L${wide(fields[2])}, L${wide(fields[5])}},`);
  }
  if (!rows.length) throw new Error('Empty translation table');
  return '// Generated from installer/XFXNativeConsole.aliases.tsv; edit that file, then regenerate.\n' +
    '#pragma once\nnamespace xfx::search_data {\n' +
    'struct EffectTranslation { const char* match_name; const char* english_name;\n' +
    '    const wchar_t* chinese_name; const wchar_t* aliases; };\n' +
    'inline constexpr EffectTranslation translations[] = {\n' + rows.join('\n') + '\n};\n}\n';
}
if (require.main === module) {
  try {
    const root = path.resolve(__dirname, '..');
    const output = path.join(root, 'native/src/data/effect_translations.h');
    const content = generate(fs.readFileSync(path.join(root, 'installer/XFXNativeConsole.aliases.tsv'), 'utf8'));
    if (process.argv.includes('--check')) {
      if (fs.readFileSync(output, 'utf8').replace(/\r\n/g, '\n') !== content) throw new Error('Compiled defaults differ from TSV; run generate-effect-translations.cjs');
      console.log('PASS compiled translation defaults match installed TSV');
    } else { fs.writeFileSync(output, content); console.log('Generated compiled translation defaults from TSV'); }
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
module.exports = { generate };
