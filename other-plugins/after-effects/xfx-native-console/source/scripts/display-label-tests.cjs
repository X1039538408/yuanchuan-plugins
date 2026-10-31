'use strict';
const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');
const text=rel=>fs.readFileSync(path.join(root,rel),'utf8').replace(/^\uFEFF/,'');
const lines=rel=>text(rel).split(/\r?\n/).filter(line=>line && !line.startsWith('#'));
const dictionary=lines('installer/XFXNativeConsole.aliases.tsv').map(line=>{
 const fields=line.split('\t');while(fields.length<6)fields.push('');assert.equal(fields.length,6);return fields;
});
assert.equal(dictionary.length,1564);
const rules=lines('native/tests/data/display-label-cleanups.txt').map(line=>{
 const fields=line.split('\t');assert.equal(fields.length,5);return fields;
});
assert.equal(rules.length,82);
const identity=new Set();
for(const [id,english,original,previous,replacement] of rules){
 assert.ok(id||english);assert.ok(previous&&replacement&&previous!==replacement);
 assert.ok(/[\u3400-\u9fff]/u.test(replacement));
 const key=id+'\t'+english;assert.ok(!identity.has(key));identity.add(key);
 const row=dictionary.find(row=>row[0]===id && row[1]===english);
 assert.ok(row,'missing rule row '+key);assert.equal(row[2],replacement,key);
 assert.ok(row[5].split('|').includes(previous),'old display lost from aliases '+key);
 assert.ok(!replacement.toLowerCase().includes(original.toLowerCase()),'original English still duplicated '+key);
}
const deep=dictionary.find(row=>row[0]==='PEDG2');assert.equal(deep[2],'深度辉光');
assert.ok(deep[5].includes('Plugin Everything Deep Glow 2 深度辉光'));
const cleanupHeader=text('native/src/data/display_label_cleanups.h').replace(/\\u([0-9a-f]{4})/gi,(_,hex)=>String.fromCharCode(parseInt(hex,16)));
assert.equal(cleanupHeader.split(/\r?\n/).filter(line=>/^\s*\{"/u.test(line)).length,82);
const headerRules=[];
for(const line of cleanupHeader.split(/\r?\n/)){
 const match=line.match(/^\s*\{("(?:\\.|[^"\\])*"), ("(?:\\.|[^"\\])*"), L("(?:\\.|[^"\\])*"), L("(?:\\.|[^"\\])*")\},$/u);
 if(match)headerRules.push(match.slice(1).map(value=>JSON.parse(value)));
}
assert.deepEqual(headerRules,rules.map(([id,english,,previous,replacement])=>[id,english,previous,replacement]),'C++ cleanup identities/previous/replacement differ from independent fixture');
const inventory=lines('native/tests/data/ae-25.4-effects.txt').map(line=>line.split('\t'));
assert.equal(inventory.length,1401);assert.equal(new Set(inventory.map(row=>row[0])).size,1401);
const native=new Map();
for(const line of text('native/src/data/saved_native_labels.h').split(/\r?\n/)){
 const match=line.match(/^\s*\{("(?:\\.|[^"\\])*"),L("(?:\\.|[^"\\])*")\},$/u);
 if(match){const id=JSON.parse(match[1]),name=JSON.parse(match[2]);assert.ok(!native.has(id));native.set(id,name);}
}
assert.equal(native.size,193);assert.equal(native.get('ADBE Drop Shadow'),'投影');
for(const [id,name] of native){const row=inventory.find(row=>row[0]===id);assert.ok(row);assert.equal(row[1],name);assert.notEqual(row[2],'_Obsolete');}
for(const id of ['PEDG2','ADBE TimeMachine','ADBE PSL Drop Shadow'])assert.ok(!native.has(id));
console.log('PASS 82 exact display cleanups; old full names remain searchable aliases; 1564 rows and 1401 identities retained');
console.log('PASS 193 exact saved native Chinese labels including Drop Shadow; unknown and hidden IDs not inferred');
console.log('Data-only tests: C++ formatter/menu compilation and live AE display are not covered.');
