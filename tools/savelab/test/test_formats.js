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
ok(rg[0].met && rg[1].met && rg[2].met && !rg[3].met && /not found/.test(rg[3].note), 'evaluate on GVAS');

// ---- NRBF
const n0 = load('nrbf/save0.dat'), n1 = load('nrbf/save1.dat');
ok(E.identify(n0).reader === 'nrbf' && /SaveData/.test(E.identify(n0).format), 'identify: NRBF, root class');
const a = E.parseTree(n0).tree, b = E.parseTree(n1).tree;
ok(one(a, 'level') === 1 && one(b, 'level') === 12 && one(b, 'beatBoss') === true && one(b, 'difficulty') === 7, 'fields, bool, enum');
ok(one(b, 'stats/kills') === 250 && one(b, 'statsAgain/kills') === 250, 'nested class + shared reference');
ok(E.queryByml(b, 'collected[*]').length === 3 && one(b, 'flags[Key=Ending_A]/Value') === true && one(b, 'perLevel[Key=2]/Value/kills') === 9, 'List, Dictionary');
ok(E.queryByml(b, 'unlocked[=Bow]').length === 1 && one(b, 'Coins') === 77, 'HashSet, auto-property');
ok(E.queryByml(a, 'mixed[*]').length === 300 && one(a, 'loop/next/next') === null && one(a, 'grade') === 'é', 'null runs, cycle, char');
ok(one(E.parseTree(load('nrbf/string.dat')).tree, 'value') === 'base64-payload==' && /only holds a string/.test(E.identify(load('nrbf/string.dat')).notes.join(' ')), 'string root');
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

// ---- THEATRHYTHM FINAL BAR LINE (AES-128-CBC + raw DEFLATE + MessagePack), synthetic saves with a DUMMY password
// (tools/savelab/test/make_theatrhythm_fixtures.js). The game's password is never in the repo.
{
  const zlib = require('zlib'), crypto = require('crypto');
  const TH = (f) => load('theatrhythm/' + f);
  const PW = fs.readFileSync(path.join(fx, 'theatrhythm/password.txt'), 'utf8').trim();
  // primitives against Node's own implementations
  const rnd = Buffer.from(Array.from({ length: 3000 }, (_, i) => (i * 2654435761 >>> 13) & 0xff));
  ok(Buffer.from(F.sha1(rnd)).equals(crypto.createHash('sha1').update(rnd).digest()) && Buffer.from(F.sha1(new Uint8Array(55))).equals(crypto.createHash('sha1').update(Buffer.alloc(55)).digest()), 'SHA-1');
  const salt = Buffer.from('0123456789abcdef'), longPw = Buffer.alloc(100, 7);
  ok(Buffer.from(F.pbkdf2Sha1(Buffer.from(PW), salt, 1000, 16)).equals(crypto.pbkdf2Sync(PW, salt, 1000, 16, 'sha1')) &&
     Buffer.from(F.pbkdf2Sha1(longPw, salt, 3, 16)).equals(crypto.pbkdf2Sync(longPw, salt, 3, 16, 'sha1')), 'PBKDF2-HMAC-SHA1 (short and > 64-byte password)');
  const k = Buffer.alloc(16, 3), iv = Buffer.alloc(16, 9), c = crypto.createCipheriv('aes-128-cbc', k, iv).setAutoPadding(false);
  const ct = Buffer.concat([c.update(rnd.subarray(0, 1024)), c.final()]);
  ok(Buffer.from(F.aes128CbcDecrypt(k, iv, ct)).equals(rnd.subarray(0, 1024)), 'AES-128-CBC decryption');
  const words = Buffer.from(Array.from({ length: 4000 }, (_, i) => 'lorem ipsum dolor sit amet '[i % 27]).join('') + rnd.toString('latin1'), 'latin1');
  const inf = [0, 1, 6, 9].every(l => Buffer.from(F.inflateRawSync(zlib.deflateRawSync(words, { level: l }))).equals(words));
  ok(inf && Buffer.from(F.inflateRawSync(zlib.deflateRawSync(Buffer.from('short text short text')))).toString() === 'short text short text', 'raw inflate: stored, fixed and dynamic blocks');
  let bad = 0;
  for (const junk of [Buffer.from([0xff, 0xff]), zlib.deflateRawSync(words).subarray(0, 100), Buffer.from([0x07])]) { try { F.inflateRawSync(junk); } catch (e) { bad++; } }
  ok(bad === 3, 'raw inflate rejects malformed / truncated data');
  threw = false; try { F.inflateRawSync(zlib.deflateRawSync(Buffer.alloc(100000)), 5000); } catch (e) { threw = /too large/.test(e.message); }
  ok(threw, 'raw inflate output limit');

  // without the password nothing is recognised; with it, every file decodes
  F.setPassword('theatrhythm', '');
  ok(E.parseTree(TH('backup.inf')) === null && /encrypted|chiffr/.test(E.identify(TH('backup.inf'), 'backup.inf').format), 'no password: not decoded, identified as encrypted');
  F.setPassword('theatrhythm', 'wrong password');
  ok(E.parseTree(TH('backup.inf')) === null, 'wrong password: not decoded');
  F.setPassword('theatrhythm', PW + '\n');
  const bk = E.parseTree(TH('backup.inf')), mu = E.parseTree(TH('musicBackup.inf')), pr = E.parseTree(TH('proficaBackup.inf'));
  ok(bk.reader === 'theatrhythm' && mu.reader === 'theatrhythm' && pr.reader === 'theatrhythm', 'parseTree picks the theatrhythm module (Switch + PS4 layouts)');
  ok(mu.meta.ps4 && !bk.meta.ps4 && /Switch/.test(E.identify(TH('backup.inf'), 'backup.inf').format), 'PS4 length prefix detected');
  const b = bk.tree, one1 = (t, p) => one(t, p);
  ok(one1(b, 'MusicStageInfo/TotalScore') === 5000000123 && one1(b, 'RhythpoInfo/TotalRhythpoValue') === 123456 && one1(b, 'UserInfo/PlayTimeDouble') === 3600.25, '64-bit int, int, double');
  ok(one1(b, 'MusicSelectInfo/PickupLastLottedDateUniversal') === 1795262771 && one1(b, 'RhythpoInfo/EverydayPlayBonus/LastDateTime') === -62135596800, 'timestamps -> seconds');
  ok(one1(b, 'SeriesQuestInfo/WorldDictionary/50000003/Cleared') === true && E.queryByml(b, 'SeriesQuestInfo/WorldDictionary[*]/Completed').filter(Boolean).length === 11, 'int-keyed Dictionary -> object');
  ok(Object.keys(b.CharacterInfo.Characters['230000003'].AbilityData).join() === '240000229,4294967295', 'key order: integer-like keys, then 4294967295');
  const ps = mu.tree.MusicInfo.MusicRecordDictionary['160000005'].DifficultyRecordDictionary['1'].PlayStyleRecordDictionary['0'];
  ok(ps.Highscore === 9900000 && ps.TotalPlayCount === 11 && ps.TotalFullChainClearCount === 6 && ps.BestClearMark === 0 && ps.CriticalBarZones[1] === 0.05, 'PlayStyleRecord fields labelled');
  ok(Object.keys(mu.tree.MusicInfo.MusicRecordDictionary).join() === '160000000,160000001,160000005', 'music ids in ascending order');
  ok(one1(pr.tree, 'ProficaInfo/MyProfica/ProficaIllustID') === 150000031 && one1(pr.tree, 'ProficaInfo/MyProfica/LatestPlayedMusics[0]') === 160000005, 'MyProfica labelled');
  const ed = F.parseTheatrhythm(TH('edge.inf')).tree;
  ok(Object.keys(ed).slice(0, 3).join() === '2,5,10' && ed.dup === 2 && Object.keys(ed).indexOf('dup') < Object.keys(ed).indexOf('ts4'), 'MessagePack keys: order, duplicates');
  ok(ed['4294967295'] === 'max u32 key' && ed['12345678901234567890'] === 'u64 key' && ed['-1'] === 'neg key' && ed.null === 'nil key' && ed['<float>'] === 'float key' && ed['<map>'] === 'map key', 'MessagePack keys of every type');
  ok(ed.nul === 'a' && ed.bin === '0001ABFF' && ed.f32 === 0.1 && ed.nan === null && ed.inf === null && ed.ts4 === 1700000000 && ed.ts8 === 1700000001 && ed.ext === 'ext:7:DEAD', 'MessagePack values: NUL, bin, float, non-finite, ext');
  ok(Object.getPrototypeOf(ed) === Object.prototype && ed.__proto__ && !({}).polluted, '__proto__ key stored as data');
  threw = false; try { F.parseMsgpack(new Uint8Array([0xdd, 0xff, 0xff, 0xff, 0xff])); } catch (e) { threw = true; }
  ok(threw, 'MessagePack: absurd length rejected');
  // the definition, evaluated on the synthetic saves
  const defT = JSON.parse(fs.readFileSync(path.join(__dirname, '../../../romfs/games/010081B01777C000.json'), 'utf8'));
  ok(!E.validate(defT).length && defT.reader === 'theatrhythm', 'THEATRHYTHM definition validates');
  const rt = E.evaluate(defT, [{ path: 'backup.inf', data: TH('backup.inf') }, { path: 'musicBackup.inf', data: TH('musicBackup.inf') }, { path: 'proficaBackup.inf', data: TH('proficaBackup.inf') }]).results.filter(x => !x.skip);
  const met = rt.filter(x => x.met).map(x => x.id);
  const notFound = rt.filter(x => /not found/.test(x.note || '')).map(x => x.id);
  ok(!notFound.length, 'every rule path exists in the synthetic saves' + (notFound.length ? ' (missing: ' + notFound + ')' : ''));
  ok(['first_perfect_chain', 'all_critical', 'ending', 'played_300', 'encore', 'chains_100', 'points_5b', 'items_100', 'abilities_1000', 'enemies_5000', 'lv99_x4', 'star9',
      'rhythmia_100k', 'cards_300', 'drops_1000', 'summonstones_30', 'quests_10', 'quest_complete_10', 'endless_20', 'top_hits_5'].every(i => met.includes(i)) &&
     !met.includes('cards_1000') && !met.includes('quest_complete_20'), 'THEATRHYTHM rules on the synthetic saves (met: ' + met.length + ')');
  ok(['feat_first_summon', 'feat_played_200', 'feat_quests_5', 'feat_endless_chain_1500', 'feat_moogles', 'feat_score_expert'].every(i => met.includes(i)) &&
     !['feat_airships', 'feat_score_basic', 'feat_score_supreme', 'feat_played_500', 'feat_proficard_edit', 'feat_star9_x4'].some(i => met.includes(i)), 'THEATRHYTHM feats on the synthetic saves');
  F.setPassword('theatrhythm', '');
}

// ---- same tree as the app (C++), on the fixtures and any extra files given
if (process.env.CPP_DUMP) {
  // THEATRHYTHM files need a password: THEATRHYTHM_KEY=<file> (default: the dummy one of the fixtures)
  F.setPassword('theatrhythm', fs.readFileSync(process.env.THEATRHYTHM_KEY || path.join(fx, 'theatrhythm/password.txt'), 'utf8'));
  const files = process.argv.slice(2).length ? process.argv.slice(2) : ['gvas/Slot1.sav', 'gvas/assert_failed.sav', 'gvas/tagcontainer.sav', 'gvas/regression_01.bin',
    'gvas/vector2d.sav', 'gvas/Options.sav', 'nrbf/save0.dat', 'nrbf/save1.dat', 'nrbf/string.dat', 'nrbf/list.dat',
    'theatrhythm/backup.inf', 'theatrhythm/musicBackup.inf', 'theatrhythm/proficaBackup.inf', 'theatrhythm/edge.inf'].map(f => path.join(fx, f));
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
// ---------------------------------------------------------------- Hades SGB1 (same bytes as host/test/test_sgb.cpp)
{
  const E = require('../engine.js');
  const sgb = Uint8Array.from(Buffer.from('5347423100000000100000000000000000000000110000004c6f636174696f6e5f54617274617275732a000000070000000300000000010000000004000000526f6f6d000000003f010000f10901540000000003000000530900000047616d6553746174651700f10504000000530b00000051756573745374617475731900f10602000000530c000000576561706f6e436c656172734100914361736865644f75741f00f50c5061637455706772616465735308000000556e6c6f636b6564530d3d00021300025800140171005253776f72645d00325402000100124e070023f03f0900231440090014000900f701f03f530a000000456e656d794b696c6c5500f3020d000000547261696e696e674d656c65653c00b230405305000000466c61672e00010b01f12808000000486172644d6f64653153030000004f66663053070000004e6f7468696e672d530f000000546578744c696e65735265636f72641c0101c400110887007164696e673031313800df50616464696e6753c8000000780100af507878787878', 'hex'));
  ok(F.isSgb(sgb), 'SGB1 detected');
  const d = F.parseSgb(sgb), t = d.tree;
  ok(t._header.runs === 42 && t._header.hell_mode === 1 && t._header.location === 'Location_Tartarus', 'SGB1 header');
  ok(t.GameState.EnemyKills.TrainingMelee === 16 && t.GameState.QuestStatus.WeaponClears === 'CashedOut', 'luabins values');
  ok(E.queryByml(t, 'GameState/WeaponUnlocks/SwordWeapon/1')[0] === 5, 'number keys as "1"');
  ok(E.queryByml(t, 'GameState/WeaponUnlocks[*][*]').join() === '5,1', '[*] over objects');
  ok(E.identify(sgb, 'Profile1.sav').reader === 'sgb', 'identify -> sgb');
  let threw = 0;
  for (const cut of [20, 60, sgb.length - 30]) { try { F.parseSgb(sgb.subarray(0, cut)); } catch (e) { threw++; } }
  ok(threw === 3, 'truncated SGB1 rejected');
}
// ---------------------------------------------------------------- DELTARUNE deltarune.sav (same checks as host/test/test_deltarune.cpp)
// Synthetic saves from tools/deltarune/make_test_save.py (run here, or given with DELTARUNE_SAVES=dir).
// With CPP_DUMP=dir (test_deltarune <saves> --dump dir), the trees must be identical to the app's.
{
  const os = require('os'), { spawnSync } = require('child_process');
  let dir = process.env.DELTARUNE_SAVES;
  if (!dir) {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), 'drsave-'));
    const gen = path.join(__dirname, '..', '..', 'deltarune', 'make_test_save.py');
    for (const py of [process.env.PYTHON, 'python3', 'python'].filter(Boolean)) {
      const r = spawnSync(py, ['-I', gen, dir], { stdio: 'ignore', timeout: 60000 });
      if (r.status === 0 && fs.existsSync(path.join(dir, 'deltarune.sav'))) break;
    }
  }
  const file = path.join(dir, 'deltarune.sav');
  if (!fs.existsSync(file)) ok(false, 'DELTARUNE: no python to run tools/deltarune/make_test_save.py (set PYTHON or DELTARUNE_SAVES)');
  else {
    const buf = new Uint8Array(fs.readFileSync(file));
    const id = E.identify(buf, 'deltarune.sav');
    ok(id.reader === 'deltarune' && F.isDeltarune(buf), 'identify -> deltarune (not plain json)');
    const t = E.parseTree(buf).tree, q = (p) => E.queryByml(t, p), o = (p) => { const v = q(p); return v && v.length === 1 ? v[0] : undefined; };
    const nz = (p) => q(p).map((v, i) => v ? i : -1).filter(i => i >= 0).join();
    ok(q('files[*]').length === 5 && o('files[0]/name') === 'filech1_0' && o('files[name=filech1_3]/completion') === 1, 'chapter files, completion slots');
    ok(o('files[name=filech5_0_b]/route') === '_b' && o('files[name=filech5_0_b]/chapter') === 5, 'chapter 5 route suffix');
    ok(o('files[0]/gold') === 30 && o('files[0]/plot') === 40 && o('files[0]/time') === 9000 && o('files[0]/chararmor1[1]') === 1 && q('files[0]/weapon[*]').length === 13, 'chapter 1 layout');
    ok(o('files[0]/flags/106') === 1 && o('files[0]/flags/2405') === 1 && q('files[0]/flags/107').length === 0, 'sparse flags');
    ok(o('files[name=filech2_0]/time') === 50000.5 && q('files[name=filech2_0]/pocketitem[*]').length === 72 && o('files[name=filech2_0]/weapon[0]') === 13, 'chapter 2+ layout');
    ok(o('ini/dr.ini/G0/Name') === 'Kris' && o('ini/dr.ini/G0/Time') === 9000 && o('ini/config_0.ini/LANG/LANG') === 'en', 'ini files');
    ok(nz('completed[*]') === '1' && nz('patch[*]') === '5,23' && nz('trophy[*]') === '1,5,9,14,18,22,23,27', 'completed / patch / trophy');
    const a = E.parseTree(new Uint8Array(fs.readFileSync(path.join(dir, 'deltarune_all.sav')))).tree;
    ok(E.queryByml(a, 'files[1]/error')[0] === 'unknown file layout' && E.queryByml(a, 'files[1]/time')[0] === 99.5, 'unknown layout: flags + plot/room/time');
    ok(E.queryByml(a, 'trophy[0]')[0] === 1 && E.queryByml(a, 'patch[*]').filter(Boolean).length === 29, 'every trophy -> CONNECTION');
    let threw = 0;
    for (const c of [10, buf.length / 2 | 0]) { try { F.parseDeltarune(buf.subarray(0, c)); } catch (e) { threw++; } }
    ok(threw === 2, 'truncated deltarune.sav rejected');
    if (process.env.CPP_DUMP) {
      for (const f of ['deltarune.sav', 'deltarune_all.sav']) {
        const dump = path.join(process.env.CPP_DUMP, f + '.json');
        if (!fs.existsSync(dump)) { console.log('     (no C++ output for ' + f + ')'); continue; }
        const js = E.parseTree(new Uint8Array(fs.readFileSync(path.join(dir, f)))).tree;
        ok(JSON.stringify(js) === fs.readFileSync(dump, 'utf8'), 'app = Save Lab: ' + f);
      }
    }
  }
}
// ---- THEATRHYTHM password search in a (fake) global-metadata.dat: literal table + data, dummy password at #4
(async () => {
  const PW = fs.readFileSync(path.join(fx, 'theatrhythm/password.txt'), 'utf8').trim();
  const lits = ['UnityEngine', 'password', 'abc', 'Not the key either', PW, 'zzzzzzzz'].map(s => Buffer.from(s));
  const table = Buffer.alloc(8 * lits.length), data = Buffer.concat(lits), head = Buffer.alloc(32);
  let off = 0;
  lits.forEach((l, i) => { table.writeUInt32LE(l.length, 8 * i); table.writeUInt32LE(off, 8 * i + 4); off += l.length; });
  head.writeUInt32LE(0xFAB11BAF, 0); head.writeUInt32LE(29, 4); head.writeUInt32LE(32, 8); head.writeUInt32LE(table.length, 12);
  head.writeUInt32LE(32 + table.length, 16); head.writeUInt32LE(data.length, 20);
  const meta = new Uint8Array(Buffer.concat([head, table, data]));
  const r = await F.theatrhythmFindPassword(meta, load('theatrhythm/proficaBackup.inf'));
  ok(r && r.password === PW && r.index === 4, 'THEATRHYTHM password found in global-metadata.dat');
  let threw = false;
  try { await F.theatrhythmFindPassword(meta.slice(4), load('theatrhythm/proficaBackup.inf')); } catch (e) { threw = true; }
  ok(threw, 'not a metadata file: error');
  if (process.env.TH_META && process.env.TH_SAVE) {   // real files: prints only the index
    const t0 = Date.now(), rr = await F.theatrhythmFindPassword(new Uint8Array(fs.readFileSync(process.env.TH_META)), new Uint8Array(fs.readFileSync(process.env.TH_SAVE)));
    console.log('     real global-metadata.dat: ' + (rr ? 'found #' + rr.index + ', ' + rr.password.length + ' characters' : 'not found') + ' in ' + (Date.now() - t0) + ' ms');
  }
  console.log(fails ? fails + ' FAILED' : 'ALL PASSED');
  process.exit(fails ? 1 : 0);
})();
