// Node tests for the Save Lab engine: node tools/savelab/test/test_engine.js
const E = require('../engine.js');
const fs = require('fs'), path = require('path'), zlib = require('zlib');
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };

// hashes match the app (values from the BotW/TotK definitions)
ok(E.crc32('FirstTouchdown') === E.crc32(Buffer.from('FirstTouchdown')), 'crc32 str/bytes');
ok(E.crc32('123456789') === 0xCBF43926, 'crc32 check value');
ok(E.murmur3('') === 0, 'murmur3 empty');
ok(E.murmur3('hello') === 0x248bfa47, 'murmur3("hello")');
ok(E.cFormat('Clear_Dungeon%03d', 7) === 'Clear_Dungeon007', 'pattern %03d');

// --- synthetic binary save: header + counters + flag array + timestamp noise
function mk(st) {
  const b = Buffer.alloc(0x4000);
  b.write('TEST', 0);
  b.writeUInt32LE(st.coins, 0x100);          // u32 counter
  b.writeUInt16LE(st.wins, 0x206);           // u16 counter (unaligned to 4)
  for (let i = 0; i < 40; i++) b[0x1000 + i * 8] = st.flags.includes(i) ? 1 : 0; // flags, stride 8
  b.writeUInt32LE(st.time, 0x300);           // play clock: changes even in control
  b.writeUInt32LE(st.bits, 0x400);           // bitfield
  b.writeFloatLE(st.dist, 0x500);
  return new Uint8Array(b);
}
const states = [
  { coins: 10, wins: 300, flags: [0], time: 1000, bits: 0b1, dist: 1.5 },
  { coins: 10, wins: 300, flags: [0], time: 1100, bits: 0b1, dist: 1.5 },          // control
  { coins: 250, wins: 301, flags: [0, 3], time: 1500, bits: 0b101, dist: 42.25 },
  { coins: 70000, wins: 305, flags: [0, 3, 7], time: 2000, bits: 0b1101, dist: 99.75 },
];
const snaps = states.map(mk);
const id = E.identify(snaps[0], 'data.bin');
ok(id.reader === 'binary_le' && id.magic === 'TEST', 'identify generic binary: ' + id.format);
const d = E.diffBinary(snaps, { controls: [1] });
const at = (o) => d.candidates.find(c => c.off === o);
ok(at(0x100) && at(0x100).kind === 'counter' && at(0x100).size === 4, 'u32 counter found');
ok(!at(0x204) && at(0x206) && at(0x206).size === 2 && at(0x206).kind === 'counter', 'u16 counter at +2 found');
ok(at(0x1018) && at(0x1018).kind === 'flag' && at(0x1018).size === 1, 'flag found');
ok(!at(0x300) && d.noiseCount >= 1, 'clock excluded by control capture');
ok(at(0x400) && at(0x400).kind === 'bits', 'bitfield found');
ok(at(0x500) && at(0x500).float, 'float found');
const ext = E.extendSeries(snaps, [0x1000, 0x1018, 0x1038], 1);
ok(ext.stride === 8 && [0,3,7].every(i => ext.offsets.includes(0x1000 + i * 8)) && ext.offsets.length <= 24 && ext.offsets.length >= 16, 'series extended (' + ext.offsets.length + ' offsets, stride ' + ext.stride + ')');
const sv = E.searchValue(snaps, [10, null, 250, 70000], 'auto');
ok(sv.some(r => r.off === 0x100 && r.size === 4), 'value search finds coins');
const sf = E.searchValue(snaps, [1.5, null, 42.25, null], 'f32');
ok(sf.some(r => r.off === 0x500), 'value search finds float');

// evaluation like the app
const def = { title_id: '0100AAAA00000000', name: 'Test', reader: 'binary_le', magic: 'TEST', save_files: ['data.bin'],
  achievements: [
    { id: 'c', name: 'c', desc: '', points: 10, rule: { type: 'flag', offset: '0x100', size: 4, target: 1000, max: 10000000 } },
    { id: 'f', name: 'f', desc: '', points: 10, rule: { type: 'count', offsets: ext.offsets.map(E.hexOff), size: 1, max: 1, target: 3 } },
    { id: 'p', name: 'p', desc: '', points: 10, rule: { type: 'playtime_hours', target: 3 } },
  ] };
ok(E.validate(def).length === 0, 'definition valid');
const e0 = E.evaluate(def, [{ path: 'data.bin', data: snaps[0] }]).results, e3 = E.evaluate(def, [{ path: 'x/data.bin', data: snaps[3] }]).results;
ok(!e0[0].met && e3[0].met && !e0[1].met && e3[1].met && e3[1].value === 3 && e3[2].skip, 'evaluate binary_le');
ok(E.validate({ title_id: 'xx', reader: 'binary_le', save_files: [], achievements: [{ id: 'a', rule: { type: 'flag' } }] }).length >= 3, 'validate catches errors');

// --- BotW-style hash table
function botw(flags) {
  const names = Object.keys(flags);
  const b = Buffer.alloc(0x0C + names.length * 8);
  b.writeUInt32LE(0x471B, 0); b.writeUInt32LE(0xFFFFFFFF, 4); b.writeUInt32LE(1, 8);
  names.forEach((n, i) => { b.writeUInt32LE(E.crc32(n), 0x0C + i * 8); b.writeInt32LE(flags[n], 0x10 + i * 8); });
  return new Uint8Array(b);
}
const b0 = botw({ FirstTouchdown: 0, Clear_Dungeon000: 0, Clear_Dungeon001: 0, Rupee: 100, PlayTime: 50 });
const b1 = botw({ FirstTouchdown: 0, Clear_Dungeon000: 0, Clear_Dungeon001: 0, Rupee: 100, PlayTime: 60 });
const b2 = botw({ FirstTouchdown: 1, Clear_Dungeon000: 1, Clear_Dungeon001: 0, Rupee: 340, PlayTime: 900 });
const ib = E.identify(b0, '0/game_data.sav');
ok(ib.known === 'botw' && ib.reader === 'gamedata_hash', 'identify BotW');
const tabs = [b0, b1, b2].map(x => E.parseHashTable(x, 'gamedata_hash'));
const hd = E.diffHash(tabs, { controls: [1] });
ok(hd.candidates.some(c => c.hash === E.crc32('FirstTouchdown') && c.kind === 'flag'), 'hash diff flag');
ok(!hd.candidates.some(c => c.hash === E.crc32('PlayTime')), 'hash diff: clock is noise');
const dict = E.matchDictionary(['FirstTouchdown', 'Rupee', 'Nope'], hd.candidates.map(c => c.hash));
ok(dict.found.get(E.crc32('Rupee')).name === 'Rupee' && dict.stats.crc32 === 2, 'dictionary crc32');
ok(E.searchValueHash(tabs, [100, null, 340]).some(r => r.hash === E.crc32('Rupee')), 'hash value search');
const bdef = { title_id: '01007EF00011E000', reader: 'gamedata_hash', save_files: ['0/game_data.sav'], achievements: [
  { id: 's', rule: { type: 'count', pattern: 'Clear_Dungeon%03d', from: 0, to: 1, target: 1 } },
  { id: 't', rule: { type: 'flag', name: 'FirstTouchdown' } }] };
const be = E.evaluate(bdef, [{ path: '0/game_data.sav', data: b2 }]).results;
ok(be[0].met && be[1].met, 'evaluate gamedata_hash');

// --- real BotW test save from the repo + built-in definition
const real = new Uint8Array(fs.readFileSync(path.join(__dirname, '../../../host2/saves/01007EF00011E000/0/game_data.sav')));
const rdef = JSON.parse(fs.readFileSync(path.join(__dirname, '../../../romfs/games/01007EF00011E000.json')));
ok(E.identify(real).known === 'botw', 'real BotW test save identified');
const rr = E.evaluate(rdef, [{ path: '0/game_data.sav', data: real }]).results;
ok(rr.some(r => r.met), 'real BotW def evaluates (' + rr.filter(r => r.met).length + ' met)');
// all built-in definitions pass validation
const dir = path.join(__dirname, '../../../romfs/games');
let bad = 0;
for (const f of fs.readdirSync(dir)) { const v = E.validate(JSON.parse(fs.readFileSync(path.join(dir, f)))); if (v.length) { bad++; console.log('  ', f, v.slice(0, 2)); } }
ok(bad === 0, 'built-in definitions validate');

// --- auto generation: "new game" vs "100 %"
{
  const fresh = mk({ coins: 0, wins: 0, flags: [], time: 0, bits: 0, dist: 0 });
  const full = mk({ coins: 99999, wins: 812, flags: [...Array(40).keys()], time: 9e6, bits: 0xFF, dist: 5000.5 });
  const dd = Object.assign(E.diffBinary([fresh, full]), { mode: 'binary' });
  const auto = E.autoAchievements([fresh, full], dd);
  const coll = auto.filter(a => a.group === 'Collection A');
  ok(coll.length === 3 && coll[2].rule.target === 40 && coll[2].rule.offsets.length === 40 && coll[1].rule.target === 20, 'auto: 40-flag collection, milestones 1/20/40');
  ok(auto.some(a => a.rule.offset === '0x100' && a.rule.target === 50000), 'auto: coin counter milestones (' + auto.filter(a => a.rule.offset === '0x100').map(a => a.rule.target) + ')');
  const ad = { title_id: '0100AAAA00000000', reader: 'binary_le', save_files: ['d'], achievements: auto.map((a, i) => Object.assign({ id: 'a' + i }, a)) };
  ok(E.validate(ad).length === 0, 'auto: definition valid');
  ok(E.evaluate(ad, [{ path: 'd', data: full }]).results.every(r => r.met), 'auto: all met by the 100 % save');
  ok(E.evaluate(ad, [{ path: 'd', data: fresh }]).results.every(r => !r.met), 'auto: none met by the fresh save');
}
{
  const f0 = botw(Object.fromEntries([...Array(5).keys()].map(i => ['Clear_Dungeon00' + i, 0]).concat([['Rupee', 0]])));
  const f1 = botw(Object.fromEntries([...Array(5).keys()].map(i => ['Clear_Dungeon00' + i, 1]).concat([['Rupee', 999]])));
  const t2 = [f0, f1].map(x => E.parseHashTable(x, 'gamedata_hash'));
  const hd2 = Object.assign(E.diffHash(t2), { mode: 'hash' });
  const dict2 = E.matchDictionary([...Array(5).keys()].map(i => 'Clear_Dungeon00' + i).concat(['Rupee']), [...t2[1].keys()]).found;
  const a2 = E.autoAchievements([f0, f1], hd2, { dict: dict2 });
  ok(a2.some(a => a.group === 'Clear_Dungeon' && a.rule.target === 5 && a.rule.names.length === 5), 'auto: named hash group');
  const d2 = { title_id: '01007EF00011E000', reader: 'gamedata_hash', save_files: ['0/game_data.sav'], achievements: a2.map((a, i) => Object.assign({ id: 'a' + i }, a)) };
  ok(E.evaluate(d2, [{ path: '0/game_data.sav', data: f1 }]).results.every(r => r.met), 'auto: hash achievements met by full save');
}

// --- BYML: Super Mario Odyssey saves (optional real files via SMO_SAVES=a.bin:b.bin)
if (process.env.SMO_SAVES) {
  const [a, b] = process.env.SMO_SAVES.split(':').map(f => new Uint8Array(fs.readFileSync(f)));
  const ia = E.identify(a);
  ok(ia.reader === 'byml' && ia.byml.base === 0x10, 'SMO save identified as BYML at 0x10');
  const ta = E.parseByml(a).tree, tb = E.parseByml(b).tree;
  ok(E.queryByml(tb, 'ShineData[*]/IsGet').filter(x => x).length === 3, 'query ShineData[*]/IsGet');
  ok(E.queryByml(tb, 'ShineData[UniqueId=205]/IsGet')[0] === true && E.queryByml(tb, 'HackDictionary[=Wanwan]').length === 1, 'selectors');
  const fa = E.flattenByml(ta), fb = E.flattenByml(tb);
  ok(fb.get('ShineData[UniqueId=205]/IsGet') === 1 && fb.get('HackDictionary[=Wanwan]') === 1 && fb.get('CoinNum') === 41, 'flatten with id selectors + membership');
  const bd = E.diffByml([fa, fb]);
  const kinds = Object.fromEntries(bd.candidates.map(c => [c.path, c.kind]));
  ok(kinds['ShineData[UniqueId=205]/IsGet'] === 'flag' && kinds['CoinNum'] === 'counter' && kinds['HackDictionary[=Wanwan]'] === 'flag' && kinds['PlayTimeTotal'] === 'timestamp', 'byml diff classes');
  console.log('     byml diff:', bd.candidates.length, 'changes', bd.candidates.slice(0, 6).map(c => c.path + '=' + c.kind).join(', '));
  ok(E.searchValueByml([fa, fb], [18, 41]).some(r => r.path === 'CoinNum'), 'byml value search');
  const smo = JSON.parse(fs.readFileSync(path.join(__dirname, '../../../romfs/games/0100000000010000.json')));
  ok(E.validate(smo).length === 0, 'built-in SMO definition valid');
  const ev = E.evaluate(smo, [{ path: 'File0.bin', data: b }]).results, ev0 = E.evaluate(smo, [{ path: 'File0.bin', data: a }]).results;
  const byId = (r, id) => r[smo.achievements.findIndex(x => x.id === id)];
  ok(byId(ev, 'moon_1').met && !byId(ev0, 'moon_1').met && byId(ev, 'capture_10').value === 4 && byId(ev, 'kingdom_5').value === 2 && byId(ev, 'music_10').value === 5, 'SMO built-in evaluated like the console');
  ok(ev.every(r => r.skip || !r.note), 'every SMO path exists in the save');
  const auto = E.autoAchievements([ta, tb], bd);
  ok(auto.some(x => x.rule.path === 'ShineData[*]/IsGet') && auto.some(x => x.rule.path === 'HackDictionary[*]') && auto.some(x => x.rule.path === 'CoinNum'), 'byml auto generation (' + [...new Set(auto.map(x => x.group))].join(' | ') + ')');
  const ad = { title_id: '0100000000010000', reader: 'byml', save_files: ['File0.bin'], achievements: auto.map((x, i) => Object.assign({ id: 'a' + i }, x)) };
  ok(E.validate(ad).length === 0, 'byml auto definition valid');
}
// malformed BYML must not throw out of identify
ok(E.identify(new Uint8Array([0x59, 0x42, 3, 0, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0])).reader === 'byml', 'tiny BYML header identified');
let threw = false; try { E.parseByml(new Uint8Array([0x59, 0x42, 3, 0, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0])); } catch (e) { threw = true; }
ok(true, 'malformed BYML ' + (threw ? 'rejected with an error' : 'tolerated'));

// --- encrypted-looking file
const rnd = new Uint8Array(require('crypto').randomBytes(200000));
ok(!E.identify(rnd).analyzable, 'random data flagged as encrypted/compressed');

// --- ZIP with deflate + stored, JKSV-like folder
(async () => {
  const files = [['JKSV/Test/2026-11-18/data.bin', Buffer.from(snaps[3])], ['JKSV/Test/2026-11-18/sub/other.dat', Buffer.from('hello')]];
  const parts = [], central = [];
  let off = 0;
  for (const [name, data] of files) {
    const comp = zlib.deflateRawSync(data), nb = Buffer.from(name);
    const crc = E.crc32(data);
    const lh = Buffer.alloc(30); lh.writeUInt32LE(0x04034b50, 0); lh.writeUInt16LE(8, 8); lh.writeUInt32LE(crc, 14); lh.writeUInt32LE(comp.length, 18); lh.writeUInt32LE(data.length, 22); lh.writeUInt16LE(nb.length, 26);
    const ch = Buffer.alloc(46); ch.writeUInt32LE(0x02014b50, 0); ch.writeUInt16LE(8, 10); ch.writeUInt32LE(crc, 16); ch.writeUInt32LE(comp.length, 20); ch.writeUInt32LE(data.length, 24); ch.writeUInt16LE(nb.length, 28); ch.writeUInt32LE(off, 42);
    parts.push(lh, nb, comp); central.push(ch, nb); off += 30 + nb.length + comp.length;
  }
  const cd = Buffer.concat(central), eo = Buffer.alloc(22);
  eo.writeUInt32LE(0x06054b50, 0); eo.writeUInt16LE(files.length, 8); eo.writeUInt16LE(files.length, 10); eo.writeUInt32LE(cd.length, 12); eo.writeUInt32LE(off, 16);
  const zip = new Uint8Array(Buffer.concat([...parts, cd, eo]));
  const out = E.stripCommonPrefix(await E.unzip(zip));
  ok(out.length === 2 && out[0].path === 'data.bin' && out[1].path === 'sub/other.dat' && out[0].data.length === 0x4000, 'unzip + strip JKSV prefix');
  // --- JSON reader (Hollow Knight-style plain-text save)
  {
    const enc = (o, pad = '') => new Uint8Array(Buffer.from('﻿' + JSON.stringify(o) + pad, 'utf8'));
    const pd = (g, m, door, flags) => ({ playerData: { grubsCollected: g, maxHealthBase: m, hasCharm: g > 0, bossDoorStateTier1: { completed: door }, mapCrossroads: flags > 0, mapGreenpath: flags > 1 },
      sceneData: { persistentBoolItems: [{ id: 'Battle Scene', sceneName: 'Crossroads_04', activated: g > 3 }, { id: 'Chest', sceneName: 'X', activated: false }] } });
    const j0 = enc(pd(0, 5, false, 0), '\0\0'), j1 = enc(pd(5, 6, false, 1)), j2 = enc(pd(46, 9, true, 2), '\n');
    ok(E.identify(j0, 'user1.dat').reader === 'json', 'identify JSON save (BOM + trailing NULs)');
    ok(E.identify(new Uint8Array(Buffer.from('{not json')), 'x').reader !== 'json', 'broken JSON is not claimed');
    ok(E.parseTree(j2).tree.playerData.grubsCollected === 46, 'parseTree falls back to JSON');
    ok(E.parseJson(new Uint8Array(Buffer.from('{"a":{"b":2}}2de37ed9'))).tree.a.b === 2, 'JSON with trailing checksum (Paper Mario)');
    const t2 = E.parseJson(j2).tree;
    ok(E.queryByml(t2, 'sceneData/persistentBoolItems[id=Battle Scene]/activated')[0] === true, 'JSON [Field=value] selector');
    ok(E.queryByml(t2, 'playerData/bossDoorStateTier1/completed')[0] === true, 'JSON nested object path');
    const jd = E.diffByml([j0, j1, j2].map(b => E.flattenByml(E.parseJson(b).tree)));
    ok(jd.candidates.some(c => c.path === 'playerData/grubsCollected' && c.kind === 'counter'), 'JSON diff finds the grub counter');
    const jdef = { title_id: '0100633007D48000', reader: 'json', save_files: ['user1.dat', 'user2.dat'], achievements: [
      { id: 'g', rule: { type: 'flag', path: 'playerData/grubsCollected', target: 46 } },
      { id: 'm', rule: { type: 'flag', path: 'playerData/maxHealthBase', target: 6 } },
      { id: 'd', rule: { type: 'flag', path: 'playerData/bossDoorStateTier1/completed', target: 1 } },
      { id: 'maps', rule: { type: 'count', paths: ['playerData/mapCrossroads', 'playerData/mapGreenpath'], target: 2 } },
      { id: 'miss', rule: { type: 'flag', path: 'playerData/noSuchField', target: 1 } }] };
    ok(E.validate(jdef).length === 0, 'json definition validates');
    ok(E.validate({ ...jdef, achievements: [{ id: 'x', rule: { type: 'flag', name: 'Foo', target: 1 } }] }).length > 0, 'json reader rejects hash rules');
    const r0 = E.evaluate(jdef, [{ path: 'user1.dat', data: j0 }]).results, r2 = E.evaluate(jdef, [{ path: 'user1.dat', data: j0 }, { path: 'user2.dat', data: j2 }]).results;
    ok(r0.every(x => !x.met), 'fresh JSON save: nothing met');
    ok(['g', 'm', 'd', 'maps'].every(id => r2.find(x => x.id === id).met) && !r2.find(x => x.id === 'miss').met, 'best slot wins, missing path not met');
    ok(r2.find(x => x.id === 'miss').note.includes('introuvable'), 'missing JSON path reported');
  }
  // perf: Smash-sized diff
  const big = [0, 1, 2].map(i => { const b = new Uint8Array(5982968); b[0x53FF40 + i * 8] = 1; b[1000] = i; return b; });
  const t = Date.now(); const bd = E.diffBinary(big); ok(bd.total >= 2 && Date.now() - t < 3000, 'diff 6 MB x3 in ' + (Date.now() - t) + ' ms');
  console.log(fails ? fails + ' FAILED' : 'ALL PASSED');
  process.exit(fails ? 1 : 0);
})();
