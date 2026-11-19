#!/usr/bin/env node
// Every Switch game that is also on Steam -> achievement definitions, wherever a compatible save is known.
// No AI. Network only with --engines (PCGamingWiki, cached).
//
//   node tools/pc2switch/bulk.js [--engines] [--install] [--min-sure 1] [--with-maybe] [--allow-pc-only]
//
// Inputs   data/pc2switch/catalog.json              every Switch game matched with Steam (catalog.js)
//          data/pc2switch/steam/<appid>.json         Steam achievement lists (+ .pc2switch-cache/schema/ if present)
//          samples/scan_bundle*.json, samples/<TID>/ Switch saves (console scan, JKSV backups)
//          samples/pc/<appid or TID>/<files>         PC saves (copied from the game's save folder on a PC)
// Outputs  drafts/all/<TID>.json + .md   one definition per game with a readable save (Switch or PC)
//          drafts/all/coverage.md        what is ready, what needs a save, which engines look promising
//          drafts/all/coverage.csv       one line per Switch+Steam game with achievements (all of them)
//          romfs/games/<TID>.json        only with --install: drafts with >= --min-sure sure rules whose save
//                                        is confirmed on Switch (or PC only, with --allow-pc-only); hand-made
//                                        save-based definitions are never overwritten.
//
// Compatibility: a PC save and a Switch save are compared by structure (field paths, list indexes ignored).
// "compatible" = most fields of the smaller save exist in the other one. With a PC save only, the rules are
// written on the PC layout ("pc-only"): harmless on the console if the layout differs (fields absent = nothing
// unlocks), and the app's compatibility scan (ZR) then tells whether they work.
'use strict';
const fs = require('fs'), path = require('path');
const E = require('../savelab/engine.js');
const D = require('./draft.js');

const ROOT = process.env.PC2SWITCH_ROOT ? path.resolve(process.env.PC2SWITCH_ROOT) : path.resolve(__dirname, '..', '..');
const args = process.argv.slice(2);
const flag = n => args.includes('--' + n);
const opt = (n, d) => { const i = args.indexOf('--' + n); return i >= 0 ? args[i + 1] : d; };
const DATA = path.join(ROOT, 'data', 'pc2switch'), OUT = path.join(ROOT, 'drafts', 'all');
const CACHE = path.resolve(ROOT, opt('cache', '.pc2switch-cache'));
const PCDIR = path.join(ROOT, 'samples', 'pc');
const MIN_SURE = +opt('min-sure', 1);
const readJson = (p, d) => { try { return JSON.parse(fs.readFileSync(p, 'utf8')); } catch (e) { return d; } };
const esc = s => String(s == null ? '' : s).replace(/\|/g, '\\|').replace(/\n/g, ' ');
const csv = s => { s = String(s == null ? '' : s); return /[",\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s; };

// ------------------------------------------------------------------ saves
function parseFiles(files) {
  const trees = [], kinds = [];
  for (const f of files) {
    if (!f.data) { kinds.push(f.format || 'not-copied'); continue; }
    const t = D.treeOf(f);
    if (t) { trees.push({ ...t, path: f.path, flat: E.flattenByml(t.tree) }); kinds.push(t.reader); continue; }
    const id = E.identify(f.data, f.path);
    const head = Buffer.from(f.data.slice(0, 64)).toString('latin1');
    kinds.push(/^\s*<\?xml|^\s*<[A-Za-z]/.test(head) ? 'xml' : /^GVAS/.test(head) ? 'gvas' : id.entropy > 7.5 ? 'encrypted' : id.strings && id.strings.length > 20 ? 'text' : 'binary');
  }
  return { trees, kinds: [...new Set(kinds)] };
}
// PC saves: samples/pc/<appid>/ or samples/pc/<TITLEID>/ (any sub-folders, copied as they are on the PC)
function pcSaves(byAppid) {
  const out = new Map();   // tid -> {files, from}
  if (!fs.existsSync(PCDIR)) return out;
  for (const d of fs.readdirSync(PCDIR)) {
    const p = path.join(PCDIR, d);
    if (!fs.statSync(p).isDirectory()) continue;
    const tids = /^01[0-9A-Fa-f]{14}$/.test(d) ? [d.toUpperCase()] : /^\d+$/.test(d) ? (byAppid.get(+d) || []) : [];
    if (!tids.length) { console.log('samples/pc/' + d + ': not a Steam app id or title id of the catalogue, skipped'); continue; }
    const files = D.walk(p).map(x => ({ ...x, size: x.data.length, format: '' }));
    for (const tid of tids) out.set(tid, { files, from: 'samples/pc/' + d });
  }
  return out;
}

// structure of a save: field paths with list positions/selectors removed
const shapeOf = trees => { const s = new Set(); for (const t of trees) for (const p of t.flat.keys()) s.add(p.replace(/\[[^\]]*\]/g, '[]')); return s; };
function compare(pcTrees, swTrees) {
  const a = shapeOf(pcTrees), b = shapeOf(swTrees);
  if (!a.size || !b.size) return { overlap: 0 };
  let inter = 0;
  for (const p of a) if (b.has(p)) inter++;
  return { overlap: inter / Math.min(a.size, b.size) };
}

// ------------------------------------------------------------------ engines (PCGamingWiki), optional
const ENGINE_HINT = [
  [/unity/i, 'likely', 'Unity: often the same files (JSON, PlayerPrefs or binary)'],
  [/gamemaker|game maker/i, 'likely', 'GameMaker: INI/JSON, usually identical'],
  [/monogame|fna|xna/i, 'likely', 'MonoGame/FNA/XNA: XML or binary, usually identical'],
  [/godot/i, 'likely', 'Godot: JSON/ConfigFile/binary, usually identical'],
  [/construct|defold|l(ö|o)ve|haxe|heaps|openfl|pico/i, 'likely', 'indie engine with portable saves'],
  [/ren'?py/i, 'reader-needed', "Ren'Py: same files, Python pickle (no reader in the app)"],
  [/rpg maker/i, 'reader-needed', 'RPG Maker: same files, compressed JSON (no reader in the app)'],
  [/unreal/i, 'reader-needed', 'Unreal: same GVAS .sav layout (no reader in the app)'],
];
function engineHint(engines) {
  for (const e of engines || []) for (const [re, level, note] of ENGINE_HINT) if (re.test(e)) return { level, note, engine: e };
  return engines && engines.length ? { level: 'unknown', note: '', engine: engines[0] } : null;
}
async function fetchEngines(appids) {
  const file = path.join(CACHE, 'pcgw_engines.json');
  const cache = readJson(file, {});
  const todo = appids.filter(a => !(a in cache));
  if (!todo.length || !flag('engines')) return cache;
  console.log('PCGamingWiki: engines for ' + todo.length + ' games');
  for (let i = 0; i < todo.length; i += 40) {
    const batch = todo.slice(i, i + 40);
    const where = batch.map(a => `Infobox_game.Steam_AppID HOLDS "${a}"`).join(' OR ');
    const url = 'https://www.pcgamingwiki.com/w/api.php?action=cargoquery&format=json&limit=500&tables=Infobox_game' +
      '&fields=' + encodeURIComponent('Infobox_game.Steam_AppID=Appid,Infobox_game.Engines=Engines') + '&where=' + encodeURIComponent(where);
    try {
      const r = await fetch(url, { headers: { 'User-Agent': 'TrophyNX pc2switch (github.com/Imp3r1us/TrophyNX)' }, signal: AbortSignal.timeout(20000) });
      const j = await r.json();
      for (const a of batch) cache[a] = [];
      for (const row of j.cargoquery || []) {
        const t = row.title || {};
        const engines = String(t.Engines || '').split(',').map(s => s.replace(/^Engine:/, '').trim()).filter(Boolean);
        for (const a of String(t.Appid || '').split(',').map(s => +s.trim())) if (batch.includes(a)) cache[a] = engines;
      }
    } catch (e) { console.log('PCGamingWiki: ' + e.message + ' (engine hints skipped for the rest of this run)'); break; }
    await new Promise(r => setTimeout(r, 300));
  }
  fs.mkdirSync(CACHE, { recursive: true });
  fs.writeFileSync(file, JSON.stringify(cache));
  return cache;
}

// ------------------------------------------------------------------ main
function steamList(appid) {
  const a = readJson(path.join(DATA, 'steam', appid + '.json'), null);
  if (a && a.achievements && a.achievements.length) return a;
  const b = readJson(path.join(CACHE, 'schema', appid + '.json'), null);
  return b && b.achievements && b.achievements.length ? b : null;
}
function installable(def, withMaybe) {
  const out = { ...def, _generated: 'pc2switch bulk.js (' + new Date().toISOString().slice(0, 10) + ')' };
  out.achievements = def.achievements.filter(a => !a._draft || a._draft === 'sure' || (withMaybe && a._draft === 'maybe'))
    .map(a => Object.fromEntries(Object.entries(a).filter(([k]) => !k.startsWith('_'))));
  return out;
}

async function main() {
  const catalog = readJson(path.join(DATA, 'catalog.json'), { games: [] });
  const games = catalog.games.filter(g => g.ach !== 0);   // with achievements, or not checked yet (null)
  const byAppid = new Map();
  for (const g of catalog.games) { if (!byAppid.has(g.appid)) byAppid.set(g.appid, []); byAppid.get(g.appid).push(g.id); }
  const defs = new Map();
  const gdir = path.join(ROOT, 'romfs', 'games');
  for (const f of fs.readdirSync(gdir)) { const d = readJson(path.join(gdir, f), null); if (d && d.title_id) defs.set(d.title_id.toUpperCase(), d); }
  const sw = D.collect(), pc = pcSaves(byAppid);
  const engines = await fetchEngines([...new Set(games.map(g => g.appid))]);
  fs.mkdirSync(OUT, { recursive: true });
  for (const f of fs.readdirSync(OUT)) if (/^01[0-9A-F]{14}\.(json|md)$/.test(f)) fs.unlinkSync(path.join(OUT, f));   // regenerate cleanly

  const rows = [], installed = [];
  for (const c of games) {
    const tid = c.id, def = defs.get(tid), steam = c.ach ? steamList(c.appid) : null;
    const s = sw.get(tid) && sw.get(tid).files.length ? sw.get(tid) : null, p = pc.get(tid) && pc.get(tid).files.length ? pc.get(tid) : null;
    const S = s ? parseFiles(s.files) : null, P = p ? parseFiles(p.files) : null;
    const hint = engineHint(engines[c.appid]);
    const row = { tid, name: c.name, appid: c.appid, steam: c.steam, how: c.how, ach: c.ach, engine: hint ? hint.engine : '', hint: hint ? hint.level : '',
      switch_save: S ? S.kinds.join('+') || 'empty' : '', pc_save: P ? P.kinds.join('+') || 'empty' : '', compat: '', overlap: '', sure: '', maybe: '', status: '' };
    // which save gives the rules, and is it the same on both platforms?
    let trees = null, from = '';
    if (S && S.trees.length && P && P.trees.length) {
      const cmp = compare(P.trees, S.trees);
      row.overlap = cmp.overlap.toFixed(2);
      row.compat = cmp.overlap >= 0.6 ? 'compatible' : cmp.overlap >= 0.3 ? 'partial' : 'different';
      trees = S.trees; from = s.from + ' (PC save ' + row.compat + ', ' + Math.round(cmp.overlap * 100) + '% of fields shared)';
    } else if (S && S.trees.length) { row.compat = 'switch'; trees = S.trees; from = s.from; }
    else if (P && P.trees.length) {
      row.compat = S && S.files.length ? 'pc-only (Switch save unreadable)' : 'pc-only';
      trees = P.trees; from = p.from + ' (PC save — layout not yet confirmed on Switch)';
    }
    if (def && def.reader && !def._generated) row.status = 'supported:' + def.reader;
    else if (!trees) row.status = S || P ? 'save not readable (' + [row.switch_save, row.pc_save].filter(Boolean).join(' / ') + ')' : 'needs a save';
    else if (c.ach === null) row.status = 'Steam list not fetched yet';
    else if (!steam) row.status = 'Steam list missing (run catalog.js)';
    else {
      const r = D.draftGame({ tid, name: c.name, c, steam, trees, from, def });
      r.def._compat = row.compat;
      fs.writeFileSync(path.join(OUT, tid + '.json'), JSON.stringify(r.def, null, 1) + '\n');
      fs.writeFileSync(path.join(OUT, tid + '.md'), r.md);
      row.sure = r.sum.sure; row.maybe = r.sum.maybe;
      row.status = r.sum.sure + r.sum.maybe ? 'drafted' + (r.st ? ' (self-recorded)' : '') : 'drafted, no rule found';
      const confirmed = row.compat === 'switch' || row.compat === 'compatible' || (flag('allow-pc-only') && row.compat.startsWith('pc-only'));
      if (flag('install') && confirmed && r.sum.sure >= MIN_SURE && !r.errs.length) {
        fs.writeFileSync(path.join(gdir, tid + '.json'), JSON.stringify(installable(r.def, flag('with-maybe')), null, 1) + '\n');
        installed.push(tid);
        row.status += ', installed';
      }
    }
    rows.push(row);
  }
  // games with saves but no catalogue row (no Steam version) still get a line
  for (const [tid, s] of sw) if (!rows.some(r => r.tid === tid) && s.files.length)
    rows.push({ tid, name: s.name || tid, appid: '', steam: '', how: '', ach: '', engine: '', hint: '', switch_save: parseFiles(s.files).kinds.join('+'), pc_save: '', compat: '', overlap: '', sure: '', maybe: '', status: 'no Steam version with achievements' });

  // ---------------------------------------------------------------- reports
  const cols = ['tid', 'name', 'appid', 'steam', 'how', 'ach', 'engine', 'hint', 'switch_save', 'pc_save', 'compat', 'overlap', 'sure', 'maybe', 'status'];
  fs.writeFileSync(path.join(OUT, 'coverage.csv'), [cols.join(','), ...rows.map(r => cols.map(k => csv(r[k])).join(','))].join('\n') + '\n');
  const count = f => rows.filter(f).length;
  const drafted = rows.filter(r => /^drafted/.test(r.status)).sort((a, b) => (b.sure || 0) - (a.sure || 0));
  const want = rows.filter(r => r.status === 'needs a save' && r.ach > 0 && r.hint === 'likely').sort((a, b) => b.ach - a.ach);
  const hintsKnown = rows.some(r => r.engine);
  const md = [
    '# PC → Switch — every game', '',
    `Generated ${new Date().toISOString().slice(0, 10)} by \`tools/pc2switch/bulk.js\` (no AI). ` +
    `${count(r => r.ach > 0)} Switch games are on Steam with achievements` + (count(r => r.ach === null) ? ` (+ ${count(r => r.ach === null)} whose Steam list is not fetched yet)` : '') + '. ' +
    `**${drafted.length} drafted** (${count(r => /^drafted/.test(r.status) && !r.compat.startsWith('pc-only'))} from a Switch save, ` +
    `${count(r => /^drafted/.test(r.status) && r.compat.startsWith('pc-only'))} from a PC save only), ` +
    `${count(r => /^supported/.test(r.status))} already supported by a dedicated reader, ` +
    `${count(r => /not readable/.test(r.status))} with a save the app cannot read, ${count(r => r.status === 'needs a save')} waiting for a save.` +
    (installed.length ? ` Installed in romfs/games: ${installed.length}.` : ''), '',
    'Full list (one line per game, opens in any spreadsheet): [`coverage.csv`](coverage.csv).', '',
    '## Drafted', '',
    '| Game | Steam | Save used | PC ↔ Switch | Sure | Maybe | Status |', '|---|---|---|---|---|---|---|',
    ...drafted.map(r => `| [${esc(r.name)}](${r.tid}.md) \`${r.tid}\` | ${r.appid} | ${r.switch_save ? 'Switch' : 'PC'} | ${r.compat}${r.overlap ? ' (' + r.overlap + ')' : ''} | ${r.sure} | ${r.maybe} | ${r.status} |`), '',
    '## Saves the app cannot read yet', '',
    '| Game | Switch save | PC save | Status |', '|---|---|---|---|',
    ...rows.filter(r => /not readable|no Steam/.test(r.status)).map(r => `| ${esc(r.name)} \`${r.tid}\` | ${r.switch_save} | ${r.pc_save} | ${r.status} |`), '',
    '## Worth a save next', '',
    hintsKnown ? 'Games with the most Steam achievements whose engine usually keeps the same save files on PC and Switch (PCGamingWiki). ' +
      'Copy a save into `samples/pc/<Steam app id>/` (from a PC) or `samples/<TITLEID>/` (from the Switch) and the next run drafts it.'
      : 'Run with `--engines` to rank the games waiting for a save by engine (PCGamingWiki).', '',
    ...(hintsKnown ? ['| Game | Steam app | Achievements | Engine |', '|---|---|---|---|',
      ...want.slice(0, 300).map(r => `| ${esc(r.name)} \`${r.tid}\` | [${r.appid}](https://store.steampowered.com/app/${r.appid}) | ${r.ach} | ${esc(r.engine)} |`)] : []), '',
    'Compatibility: `switch` = rules written on the Switch save · `compatible`/`partial`/`different` = PC and Switch saves compared (share of fields in common) · ' +
    '`pc-only` = rules written on a PC save, to confirm with a Switch scan (ZR).', '',
  ].join('\n');
  fs.writeFileSync(path.join(OUT, 'coverage.md'), md);
  console.log(`bulk: ${games.length} games, ${drafted.length} drafted, ${installed.length} installed, ${want.length} promising games waiting for a save`);
  return rows;
}

if (require.main === module) main().catch(e => { console.error(e.stack || e); process.exit(1); });
module.exports = { main, compare, engineHint, parseFiles };
