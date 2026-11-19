#!/usr/bin/env node
// Collects PC saves of the most played Switch+Steam games from public GitHub repositories, so that bulk.js can
// draft their achievements. No AI; only official APIs (Steam, GitHub REST) and the Ludusavi manifest.
//
//   GITHUB_TOKEN=... node tools/pc2switch/harvest.js [--top 100] [--per-game 4] [--max-reviews 6000] [--max-minutes 50] [--refresh] [--dry-run]
//   (stops cleanly after --max-minutes; everything is cached, the next run goes on where this one stopped)
//
// 1. popularity  number of Steam reviews of every catalogue game with achievements (cached 60 days)
// 2. save paths  Ludusavi manifest (save locations of ~20,000 games, from PCGamingWiki; cached 7 days)
// 3. search      GitHub code search on the last folders of those paths (path:"StardewValley/Saves"), or on file names
// 4. check       the files of each folder found are downloaded (data files only, no executables or archives, ≤ 2 MB),
//                parsed like the console does (JSON/BYML/GVAS/NRBF) and scored against the game's Steam achievements;
//                the best folder wins
// 5. write       samples/pc/<appid>/<files> + samples/pc/sources.json (origin of every file) + samples/pc/SOURCES.md
//
// Each run looks at the --top most played games not tried in the last 30 days, so weekly runs go down the ranking.
// Games with a Switch save in samples/, a hand-made definition, or a PC save already harvested are skipped
// (--refresh retries harvested ones).
'use strict';
const fs = require('fs'), path = require('path');
const M = require('./match.js');
const D = require('./draft.js');
const B = require('./bulk.js');

const ROOT = process.env.PC2SWITCH_ROOT ? path.resolve(process.env.PC2SWITCH_ROOT) : path.resolve(__dirname, '..', '..');
const args = process.argv.slice(2);
const flag = n => args.includes('--' + n);
const opt = (n, d) => { const i = args.indexOf('--' + n); return i >= 0 ? args[i + 1] : d; };
const DATA = path.join(ROOT, 'data', 'pc2switch');
const CACHE = path.resolve(ROOT, opt('cache', '.pc2switch-cache'));
const PCDIR = path.join(ROOT, 'samples', 'pc');
const DAY = 86400e3;
const T0 = Date.now();
const outOfTime = () => Date.now() - T0 > +opt('max-minutes', 50) * 60e3;
const TOKEN = process.env.SEARCH_TOKEN || process.env.GITHUB_TOKEN || process.env.GH_TOKEN || '';
const UA = 'TrophyNX pc2switch harvest (github.com/Imp3r1us/TrophyNX)';
const BAD_EXT = /\.(exe|dll|bat|cmd|ps1|vbs|scr|msi|com|jar|apk|sh|zip|rar|7z|gz|tar|cs|js|ts|py|java|cpp|c|h|lua|md|txt|log|png|jpe?g|gif|webp|bmp|mp3|ogg|wav|mp4|vdf|lnk|url|html?)$/i;
const MAX_FILE = 2 * 1024 * 1024, MAX_FILES = 30;
const GENERIC = new Set(['save', 'saves', 'savedata', 'savegame', 'savegames', 'saved', 'data', 'profiles', 'profile', 'user', 'users', 'config', 'settings', 'game', 'games', 'storage', 'local', 'locallow', 'roaming', 'appdata', 'documents', 'my games', 'remote', 'userdata', 'steam', 'default', 'slot', 'slots', 'backup', 'backups']);

const readJson = (p, d) => { try { return JSON.parse(fs.readFileSync(p, 'utf8')); } catch (e) { return d; } };
const writeJson = (p, v, pretty) => { fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, JSON.stringify(v, null, pretty ? 1 : 0) + '\n'); };
const fresh = (p, age) => { try { return Date.now() - fs.statSync(p).mtimeMs < age; } catch (e) { return false; } };
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ------------------------------------------------------------------ PCGamingWiki save paths -> GitHub queries
// "{{Game data/saves|Windows|{{p|appdata}}\..\LocalLow\Team Cherry\Hollow Knight\}}" -> [["Team Cherry","Hollow Knight"]]
function savePaths(wikitext) {
  const out = [];
  const re = /\{\{\s*Game data\/saves\s*\|\s*(Windows|Steam|Microsoft Store|Linux|OS X|macOS)\s*\|/gi;
  let m;
  while ((m = re.exec(wikitext || ''))) {
    // read up to the matching "}}" (paths contain {{p|...}} templates)
    let i = re.lastIndex, depth = 1, s = '';
    while (i < wikitext.length && depth) {
      if (wikitext.startsWith('{{', i)) { depth++; s += '{{'; i += 2; continue; }
      if (wikitext.startsWith('}}', i)) { if (--depth) s += '}}'; i += 2; continue; }
      s += wikitext[i++];
    }
    s = s.replace(/<ref[^>]*\/>|<ref[^>]*>[\s\S]*?<\/ref>/gi, '').replace(/<\/?code>/gi, '').replace(/<br\s*\/?>/gi, '|');
    // split alternatives on "|" outside templates
    const alts = []; let cur = '', d = 0;
    for (let k = 0; k < s.length; k++) {
      if (s.startsWith('{{', k)) d++; if (s.startsWith('}}', k)) d--;
      if (s[k] === '|' && d === 0) { alts.push(cur); cur = ''; } else cur += s[k];
    }
    alts.push(cur);
    for (const a of alts) {
      const segs = a.replace(/\{\{[^{}]*\}\}/g, '\\').replace(/~/g, '\\').split(/[\\/]+/).map(x => x.trim())
        .filter(x => x && x !== '..' && x !== '.' && !/[<>{}*%$?"|#]|^\d+$|^[A-Z]:$/.test(x) && !/^(home|library|application support|\.local|share|\.config)$/i.test(x));
      if (segs.length) out.push(segs);
    }
  }
  const seen = new Set();
  return out.filter(p => { const k = p.join('/').toLowerCase(); return !seen.has(k) && seen.add(k); });
}
const isGeneric = s => GENERIC.has(s.toLowerCase()) || /^(save|slot|profile|user|data)[ _-]?\d*$/i.test(s);
function queriesFor(paths, name) {
  const qs = [];
  const clean = String(name || '').replace(/[™®©:]/g, '').replace(/\s+/g, ' ').trim();
  for (const segs of paths) {
    const app = segs.findIndex(x => /^\d{3,}$/.test(x));
    if (app >= 0) { qs.push(`path:"${segs.slice(app, app + 2).join('/')}"`); continue; }
    const last = segs[segs.length - 1], prev = segs[segs.length - 2];
    if (/\.[A-Za-z0-9]{1,8}$/.test(last) && !isGeneric(last.replace(/\.[^.]*$/, ''))) qs.push('filename:' + last);
    else if (/\.[A-Za-z0-9]{1,8}$/.test(last) && prev) qs.push(`filename:${last} path:"${prev}"`);
    else if (prev && !(isGeneric(prev) && isGeneric(last))) qs.push(`path:"${prev}/${last}"`);
    else if (!isGeneric(last) && last.length >= 6) qs.push(`path:"${last}"`);
    else if (segs.length === 1 && clean.length >= 4) qs.push(`path:"${clean}/${last}"`);   // relative to the game folder: Celeste/Saves
  }
  if (!qs.length && clean.split(' ').length >= 2 && clean.length >= 8) qs.push(`path:"${clean}"`);
  return [...new Set(qs)].slice(0, 3);
}

// ------------------------------------------------------------------ scoring a candidate folder
function steamList(appid) {
  const a = readJson(path.join(DATA, 'steam', appid + '.json'), null);
  if (a && a.achievements && a.achievements.length) return a.achievements;
  const b = readJson(path.join(CACHE, 'schema', appid + '.json'), null);
  return b && b.achievements && b.achievements.length ? b.achievements : null;
}
function scoreFiles(files, achievements) {
  const { trees, kinds } = B.parseFiles(files);
  if (!trees.length) return { score: 0, kinds, why: 'no JSON/BYML/GVAS/NRBF file' };
  const leaves = trees.reduce((n, t) => n + t.flat.size, 0);
  if (!achievements) return { score: 0, kinds, why: 'no Steam list to compare with (run catalog.js)' };
  const st = M.selfTracked(achievements, trees.map(t => ({ path: t.path, flat: t.flat })));
  if (st) return { score: 10 + st.found * 3, kinds, why: 'records its own achievements (' + st.found + ' found)' };
  trees.sort((a, b) => b.flat.size - a.flat.size);
  const s = M.summary(M.draftRules(achievements, trees[0].flat, null));
  return { score: s.sure * 2 + s.maybe, kinds, why: s.sure + ' sure, ' + s.maybe + ' maybe' };
}

// ------------------------------------------------------------------ network
function makeNet(fetchFn) {
  let lastSearch = 0, failures = 0, successes = 0;
  async function get(url, { json = true, headers = {}, tries = 3, pass429 = false } = {}) {
    for (let i = 1; ; i++) {
      try {
        const r = await fetchFn(url, { headers: { 'User-Agent': UA, ...headers }, signal: AbortSignal.timeout(20000) });
        if ((r.status === 429 && !pass429) || r.status >= 500) throw new Error('HTTP ' + r.status);
        return { status: r.status, headers: r.headers, body: r.ok ? (json ? await r.json() : new Uint8Array(await r.arrayBuffer())) : null };
      } catch (e) {
        if (i >= tries) return { status: 0, body: null, error: e.message };
        await sleep(1500 * i * i);
      }
    }
  }
  const gh = (url, extra) => get(url, { ...extra, headers: { Accept: 'application/vnd.github+json', ...(TOKEN ? { Authorization: 'Bearer ' + TOKEN } : {}) } });
  async function search(q, retry = 0) {
    // code search: 10 requests per minute
    const wait = lastSearch + 6500 - Date.now();
    if (wait > 0 && !process.env.HARVEST_NO_WAIT) await sleep(wait);
    lastSearch = Date.now();
    const r = await gh('https://api.github.com/search/code?per_page=50&q=' + encodeURIComponent(q), { pass429: true });
    if (r.status === 429 && retry < 2) {
      const wait = (+(r.headers && r.headers.get('retry-after')) || 60) * 1000;
      console.log(`search: rate limited, waiting ${Math.round(wait / 1000)} s`);
      if (!process.env.HARVEST_NO_WAIT) await sleep(wait);
      return search(q, retry + 1);
    }
    if (r.status === 403 || r.status === 401) {
      const reset = r.headers && +r.headers.get('x-ratelimit-reset');
      if (reset && r.headers.get('x-ratelimit-remaining') === '0') { await sleep(Math.max(0, reset * 1000 - Date.now()) + 1000); return search(q); }
      throw new Error('GitHub code search refused (HTTP ' + r.status + '): add a SEARCH_TOKEN secret (fine-grained token, public repositories read-only)');
    }
    if (r.status !== 200) {
      failures++;
      console.log(`search ${q}: ${r.error || 'HTTP ' + r.status}`);
      if (failures >= 3 && !successes) throw new Error('GitHub code search does not work with this token (' + (r.error || 'HTTP ' + r.status) + '): add a SEARCH_TOKEN secret (personal access token, public repositories read-only)');
      return [];
    }
    successes++;
    return (r.body && r.body.items) || [];
  }
  return { get, gh, search };
}

// Steam reviews per game: IStoreBrowseService/GetItems (100 games per call, needs STEAM_API_KEY),
// else the store's appreviews endpoint (one call per game).
async function popularity(net, games, budget) {
  const out = new Map();
  const todo = [];
  for (const g of games) {
    const f = path.join(CACHE, 'reviews', g.appid + '.json');
    if (fresh(f, 60 * DAY)) out.set(g.appid, readJson(f, { n: 0 }).n);
    else todo.push(g);
  }
  console.log(`reviews: ${out.size} cached, ${todo.length} to fetch`);
  const save = (appid, n) => { writeJson(path.join(CACHE, 'reviews', appid + '.json'), { n }); out.set(appid, n); };
  const KEY = process.env.STEAM_API_KEY || '';
  let queue = todo.slice(0, budget);
  if (KEY) {
    for (let i = 0; i < queue.length && !outOfTime(); i += 100) {
      const batch = queue.slice(i, i + 100);
      const input = { ids: batch.map(g => ({ appid: g.appid })), context: { language: 'english', country_code: 'US' }, data_request: { include_reviews: true } };
      const r = await net.get(`https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?key=${KEY}&input_json=${encodeURIComponent(JSON.stringify(input))}`);
      const items = (r.body && r.body.response && r.body.response.store_items) || [];
      if (!items.length) { console.log('reviews: GetItems gave nothing (' + (r.error || 'HTTP ' + r.status) + '), using appreviews'); break; }
      for (const it of items) {
        const rv = it.reviews || {}, sm = rv.summary_filtered || rv.summary_unfiltered || {};
        save(it.appid, sm.review_count || 0);
      }
      if (i === 0 && !items.some(it => it.reviews)) console.log('reviews: no review data in GetItems answer, sample: ' + JSON.stringify(items[0]).slice(0, 300));
      if ((i / 100) % 10 === 0) console.log(`reviews: ${Math.min(i + 100, queue.length)} of ${queue.length} (${Math.round((Date.now() - T0) / 60e3)} min)`);
    }
    queue = queue.filter(g => !out.has(g.appid));
  }
  let fetched = 0;
  async function worker() {
    for (let g; (g = queue.shift());) {
      if (outOfTime()) return;
      const r = await net.get(`https://store.steampowered.com/appreviews/${g.appid}?json=1&language=all&purchase_type=all&num_per_page=0&filter=all`);
      const n = r.body && r.body.query_summary ? r.body.query_summary.total_reviews || 0 : null;
      if (n === null) continue;
      save(g.appid, n);
      if (++fetched % 250 === 0) console.log(`reviews: ${fetched} (${Math.round((Date.now() - T0) / 60e3)} min)`);
      await sleep(250);
    }
  }
  if (queue.length) await Promise.all([worker(), worker(), worker()]);
  if (todo.length > budget) console.log(`reviews: ${todo.length - budget} games left for the next runs (--max-reviews)`);
  return out;
}

// Save locations from the Ludusavi manifest (github.com/mtkennerly/ludusavi-manifest, built from PCGamingWiki;
// PCGamingWiki itself answers GitHub's servers with an anti-bot page). appid -> [[segments]], Windows save paths first.
function parseLudusavi(yaml) {
  const out = new Map();
  let game = null, sec = '', file = null, inTags = false, inWhen = false;
  const flush = () => {
    if (game && game.steam && game.files.length) {
      const saves = game.files.filter(f => f.save && (!f.os.length || f.os.includes('windows')));
      if (saves.length) out.set(game.steam, saves.map(f => f.path.replace(/<base>/g, '<base:' + (game.dir || game.name) + '>')));
    }
  };
  const unq = k => k.replace(/:$/, '').replace(/^"(.*)"$/, '$1').replace(/\\"/g, '"');
  for (const line of yaml.split('\n')) {
    if (!line.trim() || line.trim().startsWith('#')) continue;
    const ind = line.length - line.trimStart().length, t = line.trim();
    if (ind === 0) { flush(); game = { name: unq(t), files: [], steam: 0, dir: '' }; sec = ''; file = null; continue; }
    if (!game) continue;
    if (ind === 2) { sec = t.replace(/:.*$/, ''); file = null; continue; }
    if (sec === 'steam' && ind === 4 && t.startsWith('id:')) { game.steam = +t.slice(3).trim(); continue; }
    if (sec === 'installDir' && ind === 4 && !game.dir) { game.dir = unq(t.replace(/:\s*\{\}\s*$/, ':')); continue; }
    if (sec !== 'files') continue;
    if (ind === 4) { file = { path: unq(t), save: false, os: [] }; game.files.push(file); inTags = inWhen = false; continue; }
    if (!file) continue;
    if (ind === 6) { inTags = t.startsWith('tags'); inWhen = t.startsWith('when'); continue; }
    if (ind === 8 && inTags && t === '- save') file.save = true;
    if (ind >= 8 && inWhen && /os:\s*(\w+)/.test(t)) file.os.push(t.match(/os:\s*(\w+)/)[1]);
  }
  flush();
  return out;
}
function ludusaviSegments(p) {
  // "<home>/AppData/LocalLow/Team Cherry/Hollow Knight/*.dat" -> ["AppData","LocalLow","Team Cherry","Hollow Knight"]
  // "<base:Dead Cells>/save/user_*.dat" -> ["Dead Cells","save"]; Steam Cloud "<root>/userdata/<storeUserId>/588650/remote" -> ["588650","remote"]
  if (/\/Packages\/|SystemAppData|\/wgs\b/i.test(p)) return [];   // Microsoft Store containers: never in repositories as such
  const steamApp = (p.match(/<root>\/userdata\/<storeUserId>\/(\d+)/) || [])[1];
  p = p.replace(/<root>\/userdata\/<storeUserId>\/\d+/, '/@' + (steamApp || '') + '/').replace(/<base:([^>]+)>/, '/$1/');
  return p.replace(/<[^>]+>/g, '/').split(/[\\/]+/).map(x => x.trim())
    .filter(x => x && x !== '..' && x !== '.' && !/[<>{}*%$?"|#\[\]]|^\d+$|^[A-Z]:$/.test(x)).map(x => x.replace(/^@/, ''));
}
async function savePathsFor(net, appids) {
  const raw = path.join(CACHE, 'ludusavi_manifest.yaml'), parsed = path.join(CACHE, 'ludusavi_saves.json');
  let map = fresh(parsed, 7 * DAY) ? readJson(parsed, null) : null;
  if (!map) {
    let text = fresh(raw, 7 * DAY) ? fs.readFileSync(raw, 'utf8') : null;
    if (text == null) {
      console.log('Ludusavi manifest: download');
      const r = await net.get('https://raw.githubusercontent.com/mtkennerly/ludusavi-manifest/master/data/manifest.yaml', { json: false });
      if (r.body) { text = Buffer.from(r.body).toString('utf8'); fs.mkdirSync(CACHE, { recursive: true }); fs.writeFileSync(raw, text); }
      else console.log('Ludusavi manifest unavailable: ' + (r.error || 'HTTP ' + r.status));
    }
    map = text ? Object.fromEntries(parseLudusavi(text)) : {};
    if (text) writeJson(parsed, map);
    console.log('Ludusavi manifest: save paths for ' + Object.keys(map).length + ' Steam games');
  }
  const res = new Map();
  for (const a of appids) {
    const seen = new Set();
    const paths = (map[a] || []).map(ludusaviSegments).filter(sg => sg.length && !seen.has(sg.join('/')) && seen.add(sg.join('/')));
    res.set(a, { paths });
  }
  return res;
}

// one folder of one repository: the files the search found (raw download), then their neighbours (contents API)
const rawUrl = htmlUrl => String(htmlUrl).replace('https://github.com/', 'https://raw.githubusercontent.com/').replace(/\/blob\//, '/');
async function fetchFolder(net, repo, dir, hits = []) {
  const files = [], names = new Set();
  const take = async (name, size, url, sha, html) => {
    if (files.length >= MAX_FILES || names.has(name) || !size || size > MAX_FILE || BAD_EXT.test(name) || /^(readme|license|\.git)/i.test(name)) return;
    names.add(name);
    const d = await net.get(url, { json: false });
    if (d.body && d.body.length <= MAX_FILE) files.push({ path: name, data: d.body, sha, url: html });
  };
  for (const it of hits) await take(it.name, it.size || 1, rawUrl(it.html_url), it.sha, it.html_url);
  const r = await net.gh(`https://api.github.com/repos/${repo}/contents/${dir.split('/').map(encodeURIComponent).join('/')}`);
  if (Array.isArray(r.body)) for (const e of r.body) if (e.type === 'file') await take(e.name, e.size, e.download_url, e.sha, e.html_url);
  return files;
}

// ------------------------------------------------------------------ main
async function main({ fetchFn = globalThis.fetch } = {}) {
  const net = makeNet(fetchFn);
  const TOP = +opt('top', 100), PER_GAME = +opt('per-game', 4);
  const catalog = readJson(path.join(DATA, 'catalog.json'), { games: [] });
  const sourcesFile = path.join(PCDIR, 'sources.json');
  const sources = readJson(sourcesFile, { games: {} });
  const defs = new Set();
  for (const f of fs.readdirSync(path.join(ROOT, 'romfs', 'games'))) { const d = readJson(path.join(ROOT, 'romfs', 'games', f), null); if (d && d.reader && !d._generated) defs.add(String(d.title_id).toUpperCase()); }
  const sw = D.collect();
  const seen = new Set();
  const games = catalog.games.filter(g => g.ach > 0 && !seen.has(g.appid) && seen.add(g.appid));
  const pop = await popularity(net, games, +opt('max-reviews', 6000));
  const all = games.filter(g => pop.has(g.appid)).sort((a, b) => pop.get(b.appid) - pop.get(a.appid));
  const rankOf = new Map(all.map((g, i) => [g.appid, i + 1]));
  const skip = g => {
    if (defs.has(g.id)) return 'hand-made set';
    const s = sw.get(g.id);
    if (s && B.parseFiles(s.files).trees.length) return 'Switch save in samples/';
    const prev = sources.games[g.appid];
    if (prev && prev.found && !flag('refresh') && fs.existsSync(path.join(PCDIR, String(g.appid)))) return 'already harvested';
    if (prev && !prev.found && Date.now() - Date.parse(prev.date) < 30 * DAY && !flag('refresh')) return 'nothing found ' + prev.date;
    return '';
  };
  // the TOP most played games not tried recently: each weekly run goes further down the ranking
  const ranked = [], skipped = [];
  for (const g of all) { if (ranked.length >= TOP) break; (skip(g) ? skipped : ranked).push(g); }
  console.log(`${ranked.length} games to look at, ranks ${ranked.length ? rankOf.get(ranked[0].appid) + '–' + rankOf.get(ranked[ranked.length - 1].appid) : '-'} by Steam reviews (${pop.size} of ${games.length} ranked; ${skipped.length} higher-ranked games skipped)`);
  const todo = ranked;
  const paths = await savePathsFor(net, todo.map(g => g.appid));
  console.log(`save paths known for ${todo.filter(g => paths.get(g.appid).paths.length).length} of ${todo.length} games`);
  const report = [];
  for (const g of skipped) report.push({ g, status: 'skipped: ' + skip(g) });
  for (const g of ranked) {
    if (outOfTime()) { console.log('time budget reached (--max-minutes): the next run goes on'); break; }
    const p = paths.get(g.appid) || { paths: [] };
    const qs = queriesFor(p.paths, g.steam || g.name);
    const ach = steamList(g.appid);
    if (!qs.length) { report.push({ g, status: 'no known save path' }); sources.games[g.appid] = { name: g.name, found: false, date: new Date().toISOString().slice(0, 10), why: 'no query' }; continue; }
    // hits grouped by folder; folders with more hits first
    const folders = new Map();
    for (const q of qs) {
      let items;
      try { items = await net.search(q); } catch (e) { console.log('STOP: ' + e.message); return finish(); }
      for (const it of items) {
        if (BAD_EXT.test(it.name) || !it.repository) continue;
        const dir = it.path.includes('/') ? it.path.slice(0, it.path.lastIndexOf('/')) : '';
        const k = it.repository.full_name + '\n' + dir;
        if (!folders.has(k)) folders.set(k, []);
        if (!folders.get(k).some(x => x.path === it.path)) folders.get(k).push(it);
      }
    }
    const order = [...folders].sort((a, b) => b[1].length - a[1].length).slice(0, PER_GAME);
    let best = null;
    for (const [k, hits] of order) {
      const [repo, dir] = k.split('\n');
      const files = await fetchFolder(net, repo, dir, hits);
      if (!files.length) continue;
      const sc = scoreFiles(files, ach);
      if (sc.score > 0 && (!best || sc.score > best.sc.score)) best = { repo, dir, files, sc };
    }
    const date = new Date().toISOString().slice(0, 10);
    if (!best) {
      report.push({ g, status: folders.size ? `${folders.size} folder(s) found, none readable or matching` : 'nothing on GitHub', qs });
      console.log(`#${rankOf.get(g.appid)} ${g.name}: ${report[report.length - 1].status}`);
      sources.games[g.appid] = { name: g.name, found: false, date, queries: qs };
      continue;
    }
    if (!flag('dry-run')) {
      const out = path.join(PCDIR, String(g.appid));
      fs.rmSync(out, { recursive: true, force: true });
      for (const f of best.files) { fs.mkdirSync(out, { recursive: true }); fs.writeFileSync(path.join(out, f.path), f.data); }
    }
    sources.games[g.appid] = { name: g.name, tid: g.id, found: true, date, repo: best.repo, dir: best.dir, score: +best.sc.score.toFixed(1), why: best.sc.why,
      files: best.files.map(f => ({ name: f.path, sha: f.sha, url: f.url })) };
    report.push({ g, status: `found: ${best.repo}/${best.dir} (${best.sc.why})`, qs });
    console.log(`#${rankOf.get(g.appid)} ${g.name}: found in ${best.repo} (${best.sc.why})`);
  }
  return finish();

  function finish() {
    if (flag('dry-run')) { for (const r of report) console.log(r.g.name + ': ' + r.status); return report; }
    writeJson(sourcesFile, sources, true);
    const found = Object.entries(sources.games).filter(([, v]) => v.found);
    fs.writeFileSync(path.join(PCDIR, 'SOURCES.md'), [
      '# PC saves collected from public GitHub repositories', '',
      'Written by `tools/pc2switch/harvest.js`. Each folder `<Steam app id>/` is one player\'s save, used only to find where the ' +
      'game stores its progress (bulk.js). Delete a folder to drop it; its origin is below and in `sources.json`.', '',
      '| Game | Steam app | Source | Why kept | Date |', '|---|---|---|---|---|',
      ...found.sort((a, b) => a[1].name.localeCompare(b[1].name)).map(([a, v]) =>
        `| ${v.name} | ${a} | [${v.repo}/${v.dir}](https://github.com/${v.repo}/tree/HEAD/${encodeURI(v.dir)}) | ${v.why} | ${v.date} |`), '',
      `Last run: ${report.filter(r => /^found/.test(r.status)).length} found, ${report.filter(r => /^skipped/.test(r.status)).length} skipped, ` +
      `${report.filter(r => !/^found|^skipped/.test(r.status)).length} without a usable save (retried after 30 days).`, '',
    ].join('\n'));
    console.log(`harvest: ${report.filter(r => /^found/.test(r.status)).length} found, ${report.filter(r => /^skipped/.test(r.status)).length} skipped, ${report.filter(r => !/^found|^skipped/.test(r.status)).length} without a usable save`);
    return report;
  }
}

if (require.main === module) main().catch(e => { console.error(e.stack || e); process.exit(1); });
module.exports = { main, savePaths, queriesFor, scoreFiles, parseLudusavi, ludusaviSegments };
