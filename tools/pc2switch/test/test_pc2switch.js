// node tools/pc2switch/test/test_pc2switch.js — matcher quality on Hollow Knight + draft pipeline end to end (offline).
const fs = require('fs'), path = require('path'), os = require('os'), crypto = require('crypto');
const M = require('../match.js'), E = require('../../savelab/engine.js');
const FX = path.join(__dirname, 'fixtures');
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };

// --- words and titles
ok(M.tokens('killedDungDefender').join() === 'kill,dung,defender', 'field tokens');
ok(M.tokens('Defeat the Dung Defender').join() === 'kill,dung,defender', 'description tokens + synonyms');
ok(M.tokens('STAG_STATION_HALF').includes('station'), 'API name tokens');
const sw = [{ id: 'A', name: 'Grand Theft Auto: Vice City – The Definitive Edition' }, { id: 'B', name: 'Ori and the Blind Forest: Definitive Edition' },
  { id: 'C', name: 'Hollow Knight' }, { id: 'D', name: 'Split' }, { id: 'E', name: 'Celeste™' }];
const st = [{ appid: 1, name: 'Grand Theft Auto: San Andreas – The Definitive Edition' }, { appid: 2, name: 'Ori and the Blind Forest' },
  { appid: 3, name: 'Hollow Knight' }, { appid: 4, name: 'Split' }, { appid: 5, name: 'Celeste' }];
const tm = M.matchTitles(sw, st);
ok(!tm.has('A'), 'no match across different subtitles (Vice City ≠ San Andreas)');
ok(tm.get('B') && tm.get('B').appid === 2 && tm.get('B').how === 'normalized', 'edition words ignored');
ok(tm.get('C').how === 'exact' && tm.get('E').appid === 5, 'exact match, ™ ignored');
ok(tm.get('D').how === 'exact-short', 'short generic names flagged');

// --- matcher on Hollow Knight (internal code names: a hard case)
const schema = require(path.join(FX, 'hk_schema.json')).game.availableGameStats.achievements;
const truth = require(path.join(FX, 'hk_truth.json'));
const adv = E.flattenByml(require(path.join(FX, 'hk_112_playerdata.json')));
const d = M.draftRules(schema, adv, null);
const sum = M.summary(d);
const sure = d.filter(x => x.confidence === 'sure');
const right = x => { const t = truth[x.api]; return t.length && x.rule && (t.includes(x.rule.path) || (t[0] === '@group:map' && x.rule.paths && x.rule.paths.every(p => /\/map[A-Z]/.test(p)))); };
ok(sure.length >= 5 && sure.every(right), 'HK: ' + sure.length + ' sure, all correct');
ok(['SPEEDRUN_1', 'SPEEDRUN_2', 'SPEED_COMPLETION', 'STEELSOUL', 'STEELSOUL_COMPLETION'].every(api => d.find(x => x.api === api).confidence === 'skip'), 'HK: time/mode achievements skipped');
const inTop = d.filter(x => x.confidence !== 'sure' && x.confidence !== 'skip' && truth[x.api].length && (x.candidates || []).some(c => truth[x.api].includes(c.path))).length;
ok(inTop >= 25, 'HK: right field among review candidates for ' + inTop + ' more');
// descriptive field names (most Unity/GameMaker games): numbers and flags
const syn = M.draftRules([
  { name: 'GEMS_100', displayName: 'Gem Hoarder', description: 'Collect 100 gems' },
  { name: 'BOSS_GOLEM', displayName: 'Rock Bottom', description: 'Defeat the Stone Golem' },
  { name: 'FAST', displayName: 'Quick', description: 'Finish the game in under 2 hours' },
  { name: 'ALL_KEYS', displayName: 'Locksmith', description: 'Find every key' }],
  new Map([['stats/gemsCollected', 250], ['stats/coins', 10], ['bosses/stoneGolemDefeated', 1], ['bosses/fireDragonDefeated', 0],
    ['keys/keyRed', 1], ['keys/keyBlue', 1], ['keys/keyGreen', 1], ['keys/keyGold', 1], ['options/musicVolume', 8]]), null);
const by = Object.fromEntries(syn.map(x => [x.api, x]));
ok(by.GEMS_100.confidence === 'sure' && by.GEMS_100.rule.path === 'stats/gemsCollected' && by.GEMS_100.rule.target === 100, 'number in description -> counter target');
ok(by.BOSS_GOLEM.confidence === 'sure' && by.BOSS_GOLEM.rule.path === 'bosses/stoneGolemDefeated', 'boss flag');
ok(by.FAST.confidence === 'skip' && by.FAST.rule === null, 'time limit skipped');
ok(by.ALL_KEYS.rule && by.ALL_KEYS.rule.type === 'count' && by.ALL_KEYS.rule.paths.length === 4 && by.ALL_KEYS.rule.target === 4, 'collection of flags -> count rule');
// games that record their own achievements under the Steam names
{
  const ach = ['GHOST_GUARDIAN', 'SLIME_BOSS', 'PURITY', 'ADRENALINE', 'CATALYST', 'BARRICADED'].map(n => ({ name: n, displayName: n, description: 'x' }));
  const flat = o => E.flattenByml(o);
  const sts = M.selfTracked(ach, [
    { path: 'preferences/STSSeenCards', flat: flat({ Purity: 1, Adrenaline: 1, Catalyst: 1, Barricade: 1, Strike: 1, Defend: 1, Bash: 1, Clash: 1, Anger: 1, Armaments: 1, Havoc: 1, Flex: 1, Rage: 1, Cleave: 1, Feed: 1 }) },
    { path: 'preferences/STSAchievements', flat: flat({ GHOST_GUARDIAN: true, SLIME_BOSS: true }) }]);
  ok(sts && sts.file === 'preferences/STSAchievements', 'self-recorded: achievement file wins over a card list with the same names');
  const byApi = Object.fromEntries(sts.drafts.map(d => [d.api, d]));
  ok(byApi.GHOST_GUARDIAN.confidence === 'sure' && byApi.GHOST_GUARDIAN.rule.path === 'GHOST_GUARDIAN' && byApi.PURITY.rule.path === 'PURITY', 'self-recorded: key names');
  const vsAch = Array.from({ length: 12 }, (_, i) => ({ name: 'Ach' + i }));
  const vs = M.selfTracked(vsAch, [{ path: 'SaveDataUnity', flat: flat({ Coins: 3, Achievements: ['Ach1', 'Ach2', 'Ach3', 'Ach4', 'Ach5', 'Ach6'] }) }]);
  ok(vs && vs.drafts.every(d => d.confidence === 'sure') && vs.drafts[0].rule.path === 'Achievements[=Ach0]', 'self-recorded: string list, unseen ones follow the pattern');
  const vcAch = ['Character_Antonio', 'Relic_Hammer', 'Inn', 'Crown', 'Evo_Tear'].map(n => ({ name: n }));
  const vc = M.selfTracked(vcAch, [{ path: 'SaveProfile0.save', flat: flat({ P: { AchievementsUnlocked: [
    { Key: 'SimpleAchievement_Character_Antonio', Value: true }, { Key: 'SimpleAchievement_Relic_Hammer', Value: true }, { Key: 'DungeonAchievement_Inn', Value: false }] } }) }]);
  const vb = Object.fromEntries(vc.drafts.map(d => [d.api, d]));
  ok(vb.Character_Antonio.rule.path === 'P/AchievementsUnlocked[Key=SimpleAchievement_Character_Antonio]/Value' && vb.Character_Antonio.confidence === 'sure', 'self-recorded: Key/Value list with prefixes');
  ok(vb.Inn.rule.path.includes('DungeonAchievement_Inn') && vb.Inn.confidence === 'maybe' && vb.Crown.confidence === 'maybe', 'self-recorded: listed-but-locked and guessed prefixes are maybe');
  ok(M.selfTracked(ach, [{ path: 'x', flat: flat({ Purity: 1, Strike: 1, Defend: 1, Bash: 1, Clash: 1, Anger: 1, Flex: 1, Rage: 1, Feed: 1, Havoc: 1 }) }]) === null, 'self-recorded: a few name collisions are not a pattern');
  ok(E.parseJson(new Uint8Array(Buffer.from('{"a":"True","b":"12","c":"3.5","d":"0012x","e":"FALSE"}'))).tree.b === 12, 'JSON text values read as numbers/booleans');
}
const def = M.buildDefinition({ title_id: '0100633007D48000', name: 'HK', reader: 'json', save_files: ['user1.dat'], appid: 367520 }, d, { includeMaybe: true });
ok(E.validate(def).length === 0 && def.achievements.every(a => a._draft), 'draft definition validates (' + def.achievements.length + ' rules)');

// --- draft pipeline end to end, offline, in a temporary root
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'pc2s-'));
fs.mkdirSync(path.join(root, 'romfs', 'games'), { recursive: true });
fs.mkdirSync(path.join(root, 'data', 'pc2switch', 'steam'), { recursive: true });
fs.mkdirSync(path.join(root, 'samples'), { recursive: true });
const TID = '0100AAAA00000000', TB = '0100BBBB00000000', TC = '0100CCCC00000000';
fs.writeFileSync(path.join(root, 'data', 'pc2switch', 'catalog.json'), JSON.stringify({ games: [{ id: TID, name: 'Test Knight', appid: 367520, steam: 'Hollow Knight', how: 'exact', ach: 63 }] }));
fs.writeFileSync(path.join(root, 'data', 'pc2switch', 'steam', '367520.json'), JSON.stringify({ appid: 367520, achievements: schema }));
const b64 = buf => Buffer.from(buf).toString('base64');
const hkSave = fs.readFileSync(path.join(FX, 'hk_112_playerdata.json'));
const bundle = { format: 'switch-achievements-scan', version: 1, games: [
  { title_id: TID, name: 'Test Knight', verdict: 'draftable', files: [{ path: 'user1.dat', size: hkSave.length, format: 'json', data_b64: b64(hkSave) }] },
  { title_id: TB, name: 'Binary Game', verdict: 'binary', files: [{ path: 'save.bin', size: 4096, format: 'binary' }] },
  { title_id: TC, name: 'Locked Game', verdict: 'encrypted', files: [{ path: 'data.sav', size: 4096, format: 'encrypted' }] }] };
fs.writeFileSync(path.join(root, 'samples', 'scan_bundle.json'), JSON.stringify(bundle));
process.env.PC2SWITCH_ROOT = root;
delete require.cache[require.resolve('../draft.js')];
const index = require('../draft.js').main();
const dj = JSON.parse(fs.readFileSync(path.join(root, 'drafts', TID + '.json'), 'utf8'));
ok(dj.reader === 'json' && dj.save_files.includes('user1.dat') && dj.save_files.includes('user4.dat'), 'draft: reader and save slots');
ok(E.validate(dj).length === 0, 'draft file validates');
ok(fs.readFileSync(path.join(root, 'drafts', TID + '.md'), 'utf8').includes('| sure |'), 'review sheet written');
const readme = fs.readFileSync(path.join(root, 'drafts', 'README.md'), 'utf8');
ok(/Binary Game.*binary/.test(readme) && /Locked Game.*encrypted/.test(readme), 'verdicts for unreadable games');
ok(index.length === 3, 'index lists every scanned game');
// evaluated by Save Lab's engine like the app would: the 112% save meets every sure rule
const res = E.evaluate(dj, [{ path: 'user1.dat', data: new Uint8Array(hkSave) }]).results.filter(r => !r.skip);
const sureIds = new Set(dj.achievements.filter(a => a._draft === 'sure').map(a => a.id));
ok(res.filter(r => sureIds.has(r.id)).every(r => r.met), 'sure rules are met by the reference save');
fs.rmSync(root, { recursive: true, force: true });

// --- catalog from a pre-filled cache (no network: fresh cache files, schema budget 0)
{
  const { execFileSync } = require('child_process');
  const root2 = fs.mkdtempSync(path.join(os.tmpdir(), 'pc2c-')), cache = path.join(root2, 'cache');
  fs.mkdirSync(path.join(root2, 'romfs', 'games'), { recursive: true });
  fs.mkdirSync(path.join(cache, 'schema'), { recursive: true });
  fs.copyFileSync(path.join(__dirname, '..', '..', '..', 'romfs', 'games', '0100633007D48000.json'), path.join(root2, 'romfs', 'games', '0100633007D48000.json'));
  fs.writeFileSync(path.join(cache, 'titledb_US.en.json'), JSON.stringify({
    1: { id: '0100633007D48000', name: 'Hollow Knight', publisher: 'Team Cherry' },
    2: { id: '0100633007D48800', name: 'Hollow Knight' },                      // update: ignored
    3: { id: '0100182014022000', name: 'Grand Theft Auto: Vice City – The Definitive Edition' },
    4: { id: '0100E65002BB8000', name: 'Stardew Valley' } }));
  fs.writeFileSync(path.join(cache, 'steam_apps.json'), JSON.stringify([{ appid: 367520, name: 'Hollow Knight' }, { appid: 413150, name: 'Stardew Valley' },
    { appid: 1547000, name: 'Grand Theft Auto: San Andreas – The Definitive Edition' }]));
  fs.writeFileSync(path.join(cache, 'schema', '367520.json'), JSON.stringify({ appid: 367520, n: schema.length, achievements: schema }));
  const log = execFileSync(process.execPath, [path.join(__dirname, '..', 'catalog.js'), '--cache', cache, '--regions', 'US.en', '--max-schemas', '0'],
    { env: { ...process.env, PC2SWITCH_ROOT: root2, STEAM_API_KEY: '' } }).toString();
  const cat = JSON.parse(fs.readFileSync(path.join(root2, 'data', 'pc2switch', 'catalog.json'), 'utf8'));
  ok(cat.switch_titles === 3 && cat.matched === 2, 'catalog: base titles only, 2 matched (' + log.trim().split('\n').pop() + ')');
  const hk = cat.games.find(g => g.id === '0100633007D48000'), sv = cat.games.find(g => g.id === '0100E65002BB8000');
  ok(hk.ach === 63 && hk.status === 'supported:json' && sv.ach === null && sv.status === 'not-installed', 'catalog: counts and status');
  ok(fs.existsSync(path.join(root2, 'data', 'pc2switch', 'steam', '367520.json')), 'catalog: achievement list saved for your games');
  ok(fs.readFileSync(path.join(root2, 'data', 'pc2switch', 'catalog.md'), 'utf8').includes('| Hollow Knight |'), 'catalog: readable report');
  fs.rmSync(root2, { recursive: true, force: true });
}

// --- bulk.js: every catalogue game, Switch and PC saves, compatibility, install
{
  const { execFileSync } = require('child_process');
  const B = require('../bulk.js');
  ok(B.engineHint(['Unity']).level === 'likely' && B.engineHint(['Unreal Engine 4']).level === 'reader-needed' && B.engineHint(['Clausewitz']).level === 'unknown' && B.engineHint([]) === null, 'bulk: engine hints');
  const r3 = fs.mkdtempSync(path.join(os.tmpdir(), 'pc2b-'));
  for (const d of ['romfs/games', 'data/pc2switch/steam', 'samples/pc/111', 'samples/pc/222', 'samples/0100CCCC00000000']) fs.mkdirSync(path.join(r3, d), { recursive: true });
  const ach = ['WIN_1', 'WIN_2', 'WIN_3', 'WIN_4', 'WIN_5', 'WIN_6'].map(n => ({ name: n, displayName: n, description: '' }));
  const save = (got) => JSON.stringify({ Achievements: got, Coins: 12, Player: { Level: 3, Name: 'x' } });
  const games = [
    { id: '0100AAAA00000000', name: 'Same Game', appid: 111, steam: 'Same Game', how: 'exact', ach: 6 },        // Switch + PC, same layout
    { id: '0100BBBB00000000', name: 'PC Only Game', appid: 222, steam: 'PC Only Game', how: 'exact', ach: 6 },  // PC save only
    { id: '0100CCCC00000000', name: 'Other Layout', appid: 333, steam: 'Other Layout', how: 'exact', ach: 6 },  // Switch save + different PC save
    { id: '0100DDDD00000000', name: 'No Save Yet', appid: 444, steam: 'No Save Yet', how: 'exact', ach: 30 },
    { id: '0100EEEE00000000', name: 'No Achievements', appid: 555, steam: 'No Achievements', how: 'exact', ach: 0 }];
  fs.writeFileSync(path.join(r3, 'data/pc2switch/catalog.json'), JSON.stringify({ games }));
  for (const a of [111, 222, 333, 444]) fs.writeFileSync(path.join(r3, 'data/pc2switch/steam', a + '.json'), JSON.stringify({ appid: a, achievements: ach }));
  const b64s = s => Buffer.from(s).toString('base64');
  fs.writeFileSync(path.join(r3, 'samples/scan_bundle.json'), JSON.stringify({ format: 'switch-achievements-scan', games: [
    { title_id: '0100AAAA00000000', name: 'Same Game', verdict: 'draftable', files: [{ path: 'SaveData', format: 'json', data_b64: b64s(save(['WIN_1', 'WIN_2', 'WIN_3', 'WIN_4', 'WIN_5'])) }] }] }));
  fs.writeFileSync(path.join(r3, 'samples/pc/111/SaveData'), save(['WIN_1']));
  fs.mkdirSync(path.join(r3, 'samples/pc/222/76561198000000000'));
  fs.writeFileSync(path.join(r3, 'samples/pc/222/76561198000000000/SaveData'), save(['WIN_1', 'WIN_2', 'WIN_3', 'WIN_4', 'WIN_5', 'WIN_6']));
  fs.writeFileSync(path.join(r3, 'samples/0100CCCC00000000/SaveData'), save(['WIN_1', 'WIN_2', 'WIN_3', 'WIN_4', 'WIN_5']));
  fs.mkdirSync(path.join(r3, 'samples/pc/333'));
  fs.writeFileSync(path.join(r3, 'samples/pc/333/prefs.json'), JSON.stringify({ volume: 1, lang: 'en', window: { w: 1, h: 2 } }));
  const log = execFileSync(process.execPath, [path.join(__dirname, '..', 'bulk.js'), '--install', '--cache', path.join(r3, 'cache')], { env: { ...process.env, PC2SWITCH_ROOT: r3 } }).toString();
  const rows = fs.readFileSync(path.join(r3, 'drafts/all/coverage.csv'), 'utf8').trim().split('\n');
  const row = tid => rows.find(l => l.startsWith(tid)) || '';
  ok(rows.length === 1 + 4, 'bulk: one line per game with achievements (' + log.trim() + ')');
  ok(/,compatible,1\.00,6,0,"?drafted \(self-recorded\), installed"?$/.test(row('0100AAAA00000000')), 'bulk: PC and Switch saves compared, compatible, installed');
  ok(/,pc-only,,6,0,drafted/.test(row('0100BBBB00000000')) && !/installed/.test(row('0100BBBB00000000')), 'bulk: PC-only save drafted, not installed without --allow-pc-only');
  ok(/,different,0\.00,/.test(row('0100CCCC00000000')), 'bulk: different PC layout detected, Switch save used');
  ok(/needs a save$/.test(row('0100DDDD00000000')), 'bulk: games without a save listed');
  const pcd = JSON.parse(fs.readFileSync(path.join(r3, 'drafts/all/0100BBBB00000000.json'), 'utf8'));
  ok(pcd._compat === 'pc-only' && pcd.save_files.includes('76561198000000000/SaveData') && E.validate(pcd).length === 0, 'bulk: PC-only draft keeps the PC path and validates');
  const inst = JSON.parse(fs.readFileSync(path.join(r3, 'romfs/games/0100AAAA00000000.json'), 'utf8'));
  ok(inst.achievements.length === 6 && inst.achievements.every(a => !('_draft' in a)) && E.validate(inst).length === 0, 'bulk: installed definition is clean and validates');
  ok(fs.readFileSync(path.join(r3, 'drafts/all/coverage.md'), 'utf8').includes('**3 drafted**'), 'bulk: coverage report');
  fs.rmSync(r3, { recursive: true, force: true });
}

// --- harvest.js: PC saves from GitHub, offline with a fake network
(async () => {
  const r4 = fs.mkdtempSync(path.join(os.tmpdir(), 'pc2h-'));
  for (const d of ['romfs/games', 'data/pc2switch/steam', 'samples']) fs.mkdirSync(path.join(r4, d), { recursive: true });
  const ach = ['WIN_1', 'WIN_2', 'WIN_3', 'WIN_4', 'WIN_5'].map(n => ({ name: n, displayName: n, description: '' }));
  const games = [{ id: '0100AAAA00000000', name: 'Popular Game', appid: 10, steam: 'Popular Game', how: 'exact', ach: 5 },
    { id: '0100BBBB00000000', name: 'Less Popular', appid: 20, steam: 'Less Popular', how: 'exact', ach: 5 },
    { id: '0100CCCC00000000', name: 'Obscure Game', appid: 30, steam: 'Obscure Game', how: 'exact', ach: 5 }];
  fs.writeFileSync(path.join(r4, 'data/pc2switch/catalog.json'), JSON.stringify({ games }));
  for (const a of [10, 20, 30]) fs.writeFileSync(path.join(r4, 'data/pc2switch/steam', a + '.json'), JSON.stringify({ appid: a, achievements: ach }));
  const manifest = ['Popular Game:', '  files:', '    "<home>/AppData/LocalLow/Studio/Popular Game/save*.json":', '      tags:', '        - save', '      when:', '        - os: windows',
    '    "<home>/Library/Studio/Popular Game":', '      tags:', '        - save', '      when:', '        - os: mac', '  steam:', '    id: 10',
    '"Less Popular":', '  files:', '    "<base>/Saves":', '      tags:', '        - save', '  installDir:', '    Less Popular: {}', '  steam:', '    id: 20',
    'Store Game:', '  files:', '    "<winLocalAppData>/Packages/X_1/SystemAppData/wgs":', '      tags:', '        - save', '  steam:', '    id: 30', ''].join('\n');
  const lm = H0 => H0.parseLudusavi(manifest);
  const good = JSON.stringify({ Achievements: ['WIN_1', 'WIN_2', 'WIN_3'], Gold: 5 });
  const files = { 'good/Popular Game/save1.json': good, 'good/Popular Game/settings.json': '{"volume":1}', 'good/Popular Game/cheat.exe': 'MZ',
    'other/Popular Game/save1.json': '{"x":1}', 'xml/Less Popular/Saves/0.sav': '<?xml version="1.0"?><Save/>' };
  const searched = [];
  const res = (body, status = 200) => ({ ok: status === 200, status, headers: new Map(), json: async () => body, arrayBuffer: async () => Buffer.from(typeof body === 'string' ? body : JSON.stringify(body)) });
  const fakeFetch = async url => {
    const u = decodeURIComponent(url);
    let m;
    if ((m = u.match(/appreviews\/(\d+)/))) return res({ query_summary: { total_reviews: { 10: 900, 20: 500, 30: 2 }[m[1]] } });
    if (u.includes('ludusavi-manifest')) return res(manifest);
    if (u.includes('/search/code')) {
      const q = u.split('q=')[1]; searched.push(q);
      const hit = p => { const [repo, ...rest] = p.split('/'); return { name: rest[rest.length - 1], path: rest.join('/'), sha: 'x', size: 10, repository: { full_name: 'u/' + repo }, html_url: `https://github.com/u/${repo}/blob/abc/${rest.join('/')}` }; };
      if (q.includes('Studio/Popular Game')) return res({ items: [hit('other/Popular Game/save1.json'), hit('good/Popular Game/save1.json')] });
      if (q.includes('Less Popular/Saves')) return res({ items: [hit('xml/Less Popular/Saves/0.sav')] });
      return res({ items: [] });
    }
    if ((m = u.match(/api\.github\.com\/repos\/u\/(\w+)\/contents\/(.*)$/))) {
      const pre = m[1] + '/' + m[2] + '/';
      return res(Object.keys(files).filter(k => k.startsWith(pre)).map(k => ({ type: 'file', name: k.slice(pre.length), size: files[k].length, sha: 'y', download_url: 'https://raw.githubusercontent.com/u/' + k, html_url: 'https://github.com/u/' + k })));
    }
    if ((m = u.match(/raw\.githubusercontent\.com\/u\/(\w+)\/(?:abc\/)?(.*)$/))) return files[m[1] + '/' + m[2]] != null ? res(files[m[1] + '/' + m[2]]) : res(null, 404);
    return res(null, 404);
  };
  process.env.PC2SWITCH_ROOT = r4; process.env.HARVEST_NO_WAIT = '1';
  for (const m of ['../harvest.js', '../bulk.js', '../draft.js']) delete require.cache[require.resolve(m)];
  const argv = process.argv; process.argv = argv.slice(0, 2).concat(['--top', '2', '--cache', path.join(r4, 'cache')]);
  const H = require('../harvest.js');
  const report = await H.main({ fetchFn: fakeFetch });
  process.argv = argv;
  ok(report.length === 2 && report[0].g.appid === 10, 'harvest: top games by Steam reviews');
  // second run: the games tried are skipped, the next one in the ranking is looked at
  for (const m of ['../harvest.js', '../bulk.js', '../draft.js']) delete require.cache[require.resolve(m)];
  const report2 = await require('../harvest.js').main({ fetchFn: fakeFetch });
  ok(report2.filter(r => !/^skipped/.test(r.status)).map(r => r.g.appid).join() === '30', 'harvest: next run goes further down the ranking');
  ok(searched.some(q => q === 'path:"Studio/Popular Game"') && searched.some(q => q === 'path:"Less Popular/Saves"'), 'harvest: queries from the Ludusavi manifest');
  const lmap = lm(H);
  ok(lmap.get(10).length === 1 && lmap.get(20)[0] === '<base:Less Popular>/Saves' && !H.ludusaviSegments(lmap.get(30)[0]).length, 'harvest: manifest parsing (Windows saves, install folder, store containers dropped)');
  ok(H.queriesFor([H.ludusaviSegments('<root>/userdata/<storeUserId>/588650/remote/x_*.json')], 'Dead Cells')[0] === 'path:"588650/remote"', 'harvest: Steam Cloud folders searched by app id');
  const got = fs.readdirSync(path.join(r4, 'samples/pc/10')).sort();
  ok(got.join() === 'save1.json,settings.json', 'harvest: best folder kept, executables never downloaded');
  const srcs = JSON.parse(fs.readFileSync(path.join(r4, 'samples/pc/sources.json'), 'utf8')).games;
  ok(srcs[10].repo === 'u/good' && srcs[10].found && srcs[20].found === false, 'harvest: sources recorded, unreadable XML save rejected');
  ok(H.queriesFor(H.savePaths('{{Game data/saves|Windows|{{p|appdata}}\\StardewValley\\Saves\\<ref>x</ref>}}'), 'Stardew Valley')[0] === 'path:"StardewValley/Saves"', 'harvest: save path parsing');
  fs.rmSync(r4, { recursive: true, force: true });
  console.log(fails ? fails + ' FAILED' : 'ALL PASSED');
  process.exit(fails ? 1 : 0);
})();

