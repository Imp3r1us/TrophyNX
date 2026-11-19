// Tests of the gvas / nrbf format modules: node tools/savelab/test/test_formats.js
// Cross-check with the app's readers (same tree on both sides):
//   /tmp/test_formats --dump /tmp/dump [files...]   (host/test/test_formats.cpp)
//   CPP_DUMP=/tmp/dump node tools/savelab/test/test_formats.js [files...]
const E = require('../engine.js');
const fs = require('fs'), path = require('path');
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const fx = path.join(__dirname, 'fixtures');
const load = (f) => new Uint8Array(fs.readFileSync(path.join(fx, f)));
const one = (tree, p) => { const v = E.queryByml(tree, p); return v && v.length === 1 ? v[0] : undefined; };

// ---- GVAS
const s1 = load('gvas/Slot1.sav');
const id = E.identify(s1);
ok(id.reader === 'gvas' && /Unreal Engine 4\.27\.2/.test(id.format), 'identify: GVAS, engine version');
const t1 = E.parseTree(s1).tree;
ok(E.parseTree(s1).reader === 'gvas', 'parseTree picks the gvas module');
ok(one(t1, 'u8_test') === 129 && one(t1, 'i8_test') === -123 && one(t1, 'int32_test') === -2147483647 && one(t1, 'uint32_test') === 4294967294, 'integer properties');
ok(one(t1, 'f_property') === 3.14159 && one(t1, 'd_property') === 3.14159265358979 && one(t1, 'str_property') === 'Hello world', 'float / double / string');
ok(one(t1, 'struct_property/test_field') === 12345 && E.queryByml(t1, 'array_of_structs[*]/test_field').length === 2, 'structs and arrays of structs');
const af = E.parseTree(load('gvas/assert_failed.sav')).tree;
ok(one(af, 'Achievements/Ironman_95_5DB8DC864790570EF83371B36654A0D2') === true, 'bool in nested struct');
ok(E.queryByml(af, 'Achievements/BooksCollected_91_CA78AEE9434D1D051FE547B257A574FB[=E_Books::NewEnumerator0]').length === 1, 'enum array membership');
ok(E.queryByml(E.parseTree(load('gvas/tagcontainer.sav')).tree, 'TagTest[=Character.Appearance.OverallColor.Gold]').length === 1, 'GameplayTagContainer');
ok(one(E.parseTree(load('gvas/regression_01.bin')).tree, 'Thing') === 'B38299D449D53DDFC757AEB4E438581D', 'Guid as hex');
const vd = E.parseGvas(load('gvas/vector2d.sav'));
ok(vd.meta.skipped === 0 && vd.tree.SettingsChanged === null, 'UE 5.3 file, delegates read as null');
let threw = false; try { E.parseGvas(s1.slice(0, 200)); } catch (e) { threw = true; }
ok(threw, 'truncated GVAS rejected');
ok(E.identify(s1.slice(0, 200)).treeError, 'identify reports the GVAS error');
// a rule on a GVAS save, evaluated like the app does
const defG = { title_id: '0100000000000001', reader: 'gvas', save_files: ['Slot1.sav'], achievements: [
  { id: 'ints', rule: { type: 'count', path: 'array_of_ints[*]', target: 5 } },
  { id: 'field', rule: { type: 'flag', path: 'struct_property/test_field', target: 12345 } },
  { id: 'str', rule: { type: 'flag', path: 'str_property', equals: 'Hello world', target: 1 } },
  { id: 'missing', rule: { type: 'flag', path: 'nope', target: 1 } }] };
ok(!E.validate(defG).length, 'gvas definition validates');
const rg = E.evaluate(defG, [{ path: 'Slot1.sav', data: s1 }]).results;
ok(rg[0].met && rg[1].met && rg[2].met && !rg[3].met && /introuvable/.test(rg[3].note), 'evaluate on GVAS');

// ---- NRBF
const n0 = load('nrbf/save0.dat'), n1 = load('nrbf/save1.dat');
ok(E.identify(n0).reader === 'nrbf' && /SaveData/.test(E.identify(n0).format), 'identify: NRBF, root class');
const a = E.parseTree(n0).tree, b = E.parseTree(n1).tree;
ok(one(a, 'level') === 1 && one(b, 'level') === 12 && one(b, 'beatBoss') === true && one(b, 'difficulty') === 7, 'fields, bool, enum');
ok(one(b, 'stats/kills') === 250 && one(b, 'statsAgain/kills') === 250, 'nested class + shared reference');
ok(E.queryByml(b, 'collected[*]').length === 3 && one(b, 'flags[Key=Ending_A]/Value') === true && one(b, 'perLevel[Key=2]/Value/kills') === 9, 'List, Dictionary');
ok(E.queryByml(b, 'unlocked[=Bow]').length === 1 && one(b, 'Coins') === 77, 'HashSet, auto-property');
ok(E.queryByml(a, 'mixed[*]').length === 300 && one(a, 'loop/next/next') === null && one(a, 'grade') === 'é', 'null runs, cycle, char');
ok(one(E.parseTree(load('nrbf/string.dat')).tree, 'value') === 'base64-payload==' && /chaîne/.test(E.identify(load('nrbf/string.dat')).notes.join(' ')), 'string root');
// differences + automatic achievements work on NRBF trees
const fa = E.flattenByml(a), fb = E.flattenByml(b);
const d = E.diffByml([fa, fb]);
const kinds = Object.fromEntries(d.candidates.map(c => [c.path, c.kind]));
ok(kinds['flags[Key=Ending_A]/Value'] === 'flag' && kinds['stats/kills'] === 'counter' && kinds['unlocked[=Bow]'] === 'flag', 'diff classes on NRBF');
const defN = { title_id: '0100000000000002', reader: 'nrbf', save_files: ['save.dat'], achievements: [
  { id: 'ending', rule: { type: 'flag', path: 'flags[Key=Ending_A]/Value', target: 1 } },
  { id: 'items', rule: { type: 'count', path: 'collected[*]', target: 3 } }] };
const r0 = E.evaluate(defN, [{ path: 'save.dat', data: n0 }]).results, r1 = E.evaluate(defN, [{ path: 'save.dat', data: n1 }]).results;
ok(!r0[0].met && !r0[1].met && r1[0].met && r1[1].met, 'evaluate on NRBF');

// ---- XOR-obfuscated text saves (SIGNALIS lstr.replika: JSON XORed with a fixed phrase)
const F = require('../formats.js');
const sigPlain = new TextEncoder().encode(JSON.stringify({ StringData: [], IntData: [{ Key: 'lastSlot', Value: 1 }], FloatData: [],
  BoolData: [{ Key: 'Calibrated', Value: true }, { Key: 'ach_pen', Value: true }, { Key: 'ach_det', Value: false }, { Key: 'NGP', Value: false }] }));
const sigKey = 'ALL WATCHED OVER BY MACHINES OF LOVING GRACE';
const sig = F.xorBytes(sigPlain, sigKey);
const idx = E.identify(sig);
ok(idx.reader === 'json' && idx.xorKey === sigKey && /SIGNALIS/.test(idx.format), 'XOR: known key (SIGNALIS) found');
const defS = JSON.parse(fs.readFileSync(path.join(__dirname, '../../../romfs/games/0100307018934000.json'), 'utf8'));
ok(!E.validate(defS).length && defS.xor_key === sigKey, 'SIGNALIS definition validates');
const rs = Object.fromEntries(E.evaluate(defS, [{ path: 'Save/01/lstr.replika', data: sig }]).results.filter(x => !x.skip).map(x => [x.id, x.met]));
ok(rs.ach_pen === true && rs.ach_det === false && rs.ach_all === false, 'SIGNALIS rules on the XORed file');
ok(!E.evaluate(Object.assign({}, defS, { xor_key: '' }), [{ path: 'Save/01/lstr.replika', data: sig }]).results.some(x => x.met), 'no key in the definition: nothing read (like the app)');
const other = F.xorBytes(new TextEncoder().encode(JSON.stringify({ player: { name: 'Player01', level: 12 },
  rooms: Array.from({ length: 40 }, (_, i) => ({ id: 'room_' + i, visited: i % 3 === 0, items: i * 7 % 11 })) })), 'Sup3rSecretKey!');
const ido = E.identify(other);
ok(ido.xorKey === 'Sup3rSecretKey!' && ido.reader === 'json', 'XOR: unknown key recovered from the text');
const noise = new Uint8Array(4000).map((_, i) => (i * 2654435761 >>> 24) & 0x7F);
ok(!E.identify(noise).xorKey, 'no XOR false positive on 7-bit noise');

// ---- same tree as the app (C++), on the fixtures and any extra files given
if (process.env.CPP_DUMP) {
  const files = process.argv.slice(2).length ? process.argv.slice(2) : ['gvas/Slot1.sav', 'gvas/assert_failed.sav', 'gvas/tagcontainer.sav', 'gvas/regression_01.bin',
    'gvas/vector2d.sav', 'gvas/Options.sav', 'nrbf/save0.dat', 'nrbf/save1.dat', 'nrbf/string.dat', 'nrbf/list.dat'].map(f => path.join(fx, f));
  const same = (x, y, p) => {
    if (typeof x === 'number' && typeof y === 'number') return x === y || Math.abs(x - y) <= 1e-9 * Math.max(1, Math.abs(x)) ? null : p + ': ' + x + ' ≠ ' + y;
    if (x === null || y === null || typeof x !== 'object' || typeof y !== 'object') return x === y ? null : p + ': ' + JSON.stringify(x) + ' ≠ ' + JSON.stringify(y);
    if (Array.isArray(x) !== Array.isArray(y)) return p + ': liste ≠ objet';
    const kx = Object.keys(x), ky = Object.keys(y);
    if (kx.join('\u0001') !== ky.join('\u0001')) return p + ': clés ' + kx.slice(0, 5) + ' ≠ ' + ky.slice(0, 5);
    for (const k of kx) { const e = same(x[k], y[k], p + '/' + k); if (e) return e; }
    return null;
  };
  for (const f of files) {
    const dump = path.join(process.env.CPP_DUMP, path.basename(f) + '.json');
    if (!fs.existsSync(dump)) { console.log('     (pas de sortie C++ pour ' + path.basename(f) + ')'); continue; }
    let js; try { js = E.parseTree(new Uint8Array(fs.readFileSync(f))); } catch (e) { js = null; }
    if (!js) { console.log('     (illisible côté Save Lab : ' + path.basename(f) + ')'); continue; }
    const err = same(js.tree, JSON.parse(fs.readFileSync(dump, 'utf8')), '');
    ok(!err, 'app = Save Lab : ' + path.basename(f) + (err ? ' → ' + err : ''));
  }
}
console.log(fails ? fails + ' FAILED' : 'ALL PASSED');
process.exit(fails ? 1 : 0);
