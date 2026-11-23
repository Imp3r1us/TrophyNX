#!/usr/bin/env node
// Builds the PC -> Switch catalogue: every Switch game (titledb) matched to its Steam version, with the
// number of Steam achievements. No AI involved; Steam calls are cached so weekly runs only fetch what is new.
//
//   [STEAM_API_KEY=...] node tools/pc2switch/catalog.js [--max-schemas 3000] [--cache .pc2switch-cache]
//
// The Steam key is optional. Without it (same idea as the Achievements app by PSerban93 / Achievement Watcher):
//   achievement lists  IPlayerService/GetGameAchievements, a public endpoint (names, descriptions, hidden flag,
//                      share of players who unlocked each one: used for the points of drafts)
//   Steam app list     the legacy public ISteamApps/GetAppList, else the matches already in catalog.json
//
// Writes  data/pc2switch/catalog.json   compact list used by Save Lab and draft.js
//         data/pc2switch/catalog.md     readable summary (opens nicely on GitHub mobile)
//         data/pc2switch/steam/<appid>.json   achievement lists for the games we care about
//                                             (games in romfs/games, in samples/ (Switch or PC saves), or in a console scan)
'use strict';
const fs = require('fs'), path = require('path');
const M = require('./match.js');

const ROOT = process.env.PC2SWITCH_ROOT ? path.resolve(process.env.PC2SWITCH_ROOT) : path.resolve(__dirname, '..', '..');
const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf('--' + name); return i >= 0 ? args[i + 1] : def; };
const CACHE = path.resolve(ROOT, opt('cache', '.pc2switch-cache'));
const OUT = path.join(ROOT, 'data', 'pc2switch');
const MAX_SCHEMAS = +opt('max-schemas', 3000);
const KEY = process.env.STEAM_API_KEY || '';
const REGIONS = (opt('regions', 'US.en,GB.en')).split(',');
const DAY = 86400e3;

function readJson(p, def) { try { return JSON.parse(fs.readFileSync(p, 'utf8')); } catch (e) { return def; } }
function writeJson(p, v, pretty) { fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, JSON.stringify(v, null, pretty ? 1 : 0) + '\n'); }
function fresh(p, maxAge) { try { return Date.now() - fs.statSync(p).mtimeMs < maxAge; } catch (e) { return false; } }
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function get(url, { json = true, tries = 4 } = {}) {
  for (let i = 1; ; i++) {
    try {
      const r = await fetch(url, { signal: AbortSignal.timeout(60000) });
      if (r.status === 429 || r.status >= 500) throw new Error('HTTP ' + r.status);
      if (!r.ok) return { status: r.status, body: null };
      return { status: r.status, body: json ? await r.json() : await r.text() };
    } catch (e) {
      if (i >= tries) throw new Error(url.replace(/key=[^&]+/, 'key=***') + ': ' + e.message);
      await sleep(1000 * i * i);
    }
  }
}

// ------------------------------------------------------------------ Switch titles (blawar/titledb)
async function switchTitles() {
  const byId = new Map();
  for (const reg of REGIONS) {
    const file = path.join(CACHE, 'titledb_' + reg + '.json');
    if (!fresh(file, 7 * DAY)) {
      console.log('download titledb ' + reg);
      const r = await get('https://raw.githubusercontent.com/blawar/titledb/master/' + reg + '.json', { json: false });
      if (!r.body) throw new Error('titledb ' + reg + ': HTTP ' + r.status);
      fs.mkdirSync(CACHE, { recursive: true });
      fs.writeFileSync(file, r.body);
    }
    const db = readJson(file, {});
    for (const v of Object.values(db)) {
      const id = String(v.id || '').toUpperCase();
      if (!/^01[0-9A-F]{11}000$/.test(id) || !v.name || byId.has(id)) continue;   // base games only (updates end in 800, DLC differ)
      byId.set(id, { id, name: v.name, publisher: v.publisher || '' });
    }
  }
  return [...byId.values()];
}

// ------------------------------------------------------------------ Steam
async function steamApps() {
  const file = path.join(CACHE, 'steam_apps.json');
  if (fresh(file, 7 * DAY)) return readJson(file, []);
  if (!KEY) {
    try {
      const r = await get('https://api.steampowered.com/ISteamApps/GetAppList/v2/', { tries: 2 });
      const list = (r.body && r.body.applist && r.body.applist.apps) || [];
      if (list.length) { const apps = list.filter(a => a.name).map(a => ({ appid: a.appid, name: a.name })); writeJson(file, apps); return apps; }
    } catch (e) { console.log('public Steam app list unavailable: ' + e.message); }
    console.log('no STEAM_API_KEY: using the cached Steam app list / the matches of catalog.json');
    return readJson(file, []);
  }
  const apps = [];
  let last = 0;
  for (;;) {
    const r = await get(`https://api.steampowered.com/IStoreService/GetAppList/v1/?key=${KEY}&max_results=50000&last_appid=${last}`);
    const resp = (r.body && r.body.response) || {};
    for (const a of resp.apps || []) apps.push({ appid: a.appid, name: a.name });
    console.log('steam apps: ' + apps.length);
    if (!resp.have_more_results || !resp.last_appid) break;
    last = resp.last_appid;
  }
  if (apps.length) writeJson(path.join(CACHE, 'steam_apps.json'), apps);
  return apps;
}
// Steam sends unlock percentages as strings ("66.3") or numbers; undefined when missing
function pct(v) { const n = parseFloat(v); return Number.isFinite(n) ? Math.round(n * 10) / 10 : undefined; }

// Returns {n, achievements} from cache or Steam; null when it could not be fetched this run.
async function schema(appid, budget) {
  const file = path.join(CACHE, 'schema', appid + '.json');
  const cached = readJson(file, null);
  // names-only lists of old keyless runs, and lists from before unlock percentages were kept (no `pct`), are fetched again
  if (cached && !cached.keyless && cached.pct && fresh(file, 90 * DAY)) return cached;
  if (budget.left <= 0) return cached;
  let achievements = null, keyless = false;
  if (KEY) {
    const r = await get(`https://api.steampowered.com/ISteamUserStats/GetSchemaForGame/v2/?key=${KEY}&appid=${appid}&l=english`);
    const g = r.body && r.body.game;
    if (r.status === 200 || r.status === 400 || r.status === 403) {
      achievements = ((g && g.availableGameStats && g.availableGameStats.achievements) || [])
        .map(a => ({ name: a.name, displayName: a.displayName || '', description: a.description || '', hidden: a.hidden ? 1 : 0 }));
      // GetSchemaForGame has no unlock percentages: take them from the public global stats
      if (achievements.length) try {
        const p = await get(`https://api.steampowered.com/ISteamUserStats/GetGlobalAchievementPercentagesForApp/v2/?gameid=${appid}`, { tries: 2 });
        const list = (p.body && p.body.achievementpercentages && p.body.achievementpercentages.achievements) || [];
        const by = new Map(list.map(x => [x.name, pct(x.percent)]));
        for (const a of achievements) if (by.get(a.name) !== undefined) a.percent = by.get(a.name);
      } catch (e) { }
    }
  }
  if (!achievements) {
    // no key (or the keyed call failed): public endpoint used by Steam's own pages, full names and descriptions
    try {
      const r = await get(`https://api.steampowered.com/IPlayerService/GetGameAchievements/v1/?appid=${appid}&language=english`, { tries: 2 });
      const resp = r.body && r.body.response;
      if (r.status === 200 && resp) achievements = (resp.achievements || [])
        .map(a => ({ name: a.internal_name, displayName: a.localized_name || '', description: a.localized_desc || '', hidden: a.hidden ? 1 : 0, percent: pct(a.player_percent_unlocked) }))
        .filter(a => a.name);
    } catch (e) { }
  }
  if (!achievements && !KEY) {
    // last resort: API names only (no titles or descriptions), still enough to know a game has achievements
    try {
      const r = await get(`https://api.steampowered.com/ISteamUserStats/GetGlobalAchievementPercentagesForApp/v2/?gameid=${appid}`, { tries: 2 });
      const list = r.body && r.body.achievementpercentages && r.body.achievementpercentages.achievements;
      if (list) { achievements = list.map(a => ({ name: a.name, displayName: '', description: '', hidden: 0, percent: pct(a.percent) })); keyless = true; }
    } catch (e) { }
  }
  budget.left--;
  if (!achievements) return cached;
  for (const a of achievements) if (a.percent === undefined) delete a.percent;
  const v = { appid, n: achievements.length, keyless, pct: true, achievements };
  writeJson(file, v);
  await sleep(60);
  return v;
}

// ------------------------------------------------------------------ local state: definitions, samples, console scans
function localState() {
  const defs = new Map();
  for (const f of fs.readdirSync(path.join(ROOT, 'romfs', 'games'))) {
    const d = readJson(path.join(ROOT, 'romfs', 'games', f), null);
    if (d && d.title_id) defs.set(d.title_id.toUpperCase(), { reader: d.reader || '', n: (d.achievements || []).length });
  }
  const scanned = new Map();   // title id -> {formats, verdict}
  const sdir = path.join(ROOT, 'samples');
  if (fs.existsSync(sdir)) {
    for (const f of fs.readdirSync(sdir)) {
      if (/^scan_bundle.*\.json$/i.test(f)) {
        const b = readJson(path.join(sdir, f), {});
        for (const g of b.games || []) scanned.set(String(g.title_id).toUpperCase(), { verdict: g.verdict, formats: [...new Set((g.files || []).map(x => x.format))] });
      } else if (/^01[0-9A-Fa-f]{14}$/.test(f)) {
        if (!scanned.has(f.toUpperCase())) scanned.set(f.toUpperCase(), { verdict: 'sample', formats: [] });
      }
    }
  }
  // PC saves: samples/pc/<Steam app id or TITLEID>/ (bulk.js)
  const pcKeys = new Set();
  const pdir = path.join(sdir, 'pc');
  if (fs.existsSync(pdir)) for (const f of fs.readdirSync(pdir)) if (/^\d+$|^01[0-9A-Fa-f]{14}$/.test(f)) pcKeys.add(f.toUpperCase());
  return { defs, scanned, pcKeys };
}

async function main() {
  fs.mkdirSync(CACHE, { recursive: true });
  const titles = await switchTitles();
  console.log('switch base titles: ' + titles.length);
  const apps = await steamApps();
  console.log('steam apps: ' + apps.length);
  let matches = M.matchTitles(titles, apps);
  if (!apps.length) {
    // no Steam app list this run (no key, public list unavailable): keep the matches of the last catalogue
    const prev = readJson(path.join(OUT, 'catalog.json'), { games: [] }).games || [];
    const known = new Set(titles.map(t => t.id));
    matches = new Map(prev.filter(g => g.appid && known.has(g.id)).map(g => [g.id, { appid: g.appid, name: g.steam, how: g.how }]));
    console.log('reusing ' + matches.size + ' matches from catalog.json');
  }
  const titleById = new Map(titles.map(t => [t.id, t]));
  console.log('name matches: ' + matches.size);
  const { defs, scanned, pcKeys } = localState();
  const wanted = new Set([...defs.keys(), ...scanned.keys()]);
  for (const [id, m] of matches) if (pcKeys.has(id) || pcKeys.has(String(m.appid))) wanted.add(id);
  // schemas: games we care about first, then the rest within the budget
  const budget = { left: MAX_SCHEMAS };
  const order = [...matches.keys()].sort((a, b) => (wanted.has(b) - wanted.has(a)));
  const rows = [];
  let fetched = 0;
  for (const id of order) {
    const m = matches.get(id), t = titleById.get(id);
    const before = budget.left;
    const s = await schema(m.appid, budget);
    if (budget.left < before) fetched++;
    if (fetched && fetched % 200 === 0 && budget.left < before) console.log('schemas fetched: ' + fetched);
    const row = { id, name: t.name, appid: m.appid, steam: m.name, how: m.how, ach: s ? s.n : null };
    const d = defs.get(id), sc = scanned.get(id);
    row.status = d && d.reader ? 'supported:' + d.reader : sc ? 'scanned:' + (sc.verdict || '?') : d ? 'playtime-only' : 'not-installed';
    if (sc && sc.formats.length) row.formats = sc.formats;
    rows.push(row);
    if (s && s.n && wanted.has(id)) writeJson(path.join(OUT, 'steam', m.appid + '.json'), { appid: m.appid, name: m.name, achievements: s.achievements }, true);
  }
  rows.sort((a, b) => a.name.localeCompare(b.name));
  const withAch = rows.filter(r => r.ach > 0);
  writeJson(path.join(OUT, 'catalog.json'), { generated: new Date().toISOString().slice(0, 10), switch_titles: titles.length, steam_apps: apps.length,
    matched: rows.length, with_achievements: withAch.length, pending_schemas: rows.filter(r => r.ach === null).length, games: rows });
  // readable report
  const mine = rows.filter(r => wanted.has(r.id));
  const md = [
    '# PC → Switch catalogue', '',
    `Generated ${new Date().toISOString().slice(0, 10)} by \`tools/pc2switch/catalog.js\` (no AI). ` +
    `${titles.length} Switch games, ${rows.length} also on Steam, **${withAch.length} with Steam achievements**` +
    (rows.some(r => r.ach === null) ? `, ${rows.filter(r => r.ach === null).length} still to check (next runs)` : '') + '.', '',
    '## Your games', '',
    '| Game | Steam | Achievements | Status | Save formats |', '|---|---|---|---|---|',
    ...mine.map(r => `| ${r.name} | [${r.steam}](https://store.steampowered.com/app/${r.appid}) ${r.how !== 'exact' ? '(' + r.how + ')' : ''} | ${r.ach ?? '?'} | ${r.status} | ${(r.formats || []).join(', ')} |`),
    '', 'Status: `supported:<reader>` = save-based set exists · `scanned:draftable` = readable save, run draft.js · `scanned:text` = text save, reader needed · `scanned:binary`/`encrypted` = manual work · `playtime-only` = only play-log achievements so far.',
    '', '## Top matches with achievements (first 300)', '',
    '| Game | Steam app | Achievements | Match |', '|---|---|---|---|',
    ...withAch.slice().sort((a, b) => b.ach - a.ach).slice(0, 300).map(r => `| ${r.name} | ${r.appid} | ${r.ach} | ${r.how} |`), '',
  ].join('\n');
  fs.writeFileSync(path.join(OUT, 'catalog.md'), md);
  console.log(`catalog: ${rows.length} matched, ${withAch.length} with achievements, ${mine.length} of your games; schemas fetched this run: ${MAX_SCHEMAS - budget.left}`);
}

if (require.main === module) main().catch(e => { console.error(e.message || e); process.exit(1); });
module.exports = { switchTitles, localState };
