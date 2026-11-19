#!/usr/bin/env node
// Drafts achievement definitions from Switch saves + Steam achievement lists. No AI, no network.
//
//   node tools/pc2switch/draft.js
//
// Inputs   samples/scan_bundle*.json        written by the app's "Compatibility scan" (one file to upload)
//          samples/<TITLEID>/<save files>   or raw saves copied by hand (e.g. from a JKSV backup)
//          data/pc2switch/catalog.json      title id -> Steam app (catalog.js)
//          data/pc2switch/steam/<appid>.json   Steam achievements (catalog.js)
// Outputs  drafts/<TITLEID>.json   definition in the app's format; every achievement carries "_draft":
//                                  "sure" or "maybe". Review, delete what is wrong, then move it to
//                                  romfs/games/ (or the SD card's games/ folder) and drop the _ keys.
//          drafts/<TITLEID>.md     review sheet: each Steam achievement, the rule found, the candidates
//          drafts/README.md        index of all games with their compatibility verdict
'use strict';
const fs = require('fs'), path = require('path');
const M = require('./match.js');
const E = require('../savelab/engine.js');

const ROOT = process.env.PC2SWITCH_ROOT ? path.resolve(process.env.PC2SWITCH_ROOT) : path.resolve(__dirname, '..', '..');
const SAMPLES = path.join(ROOT, 'samples'), DRAFTS = path.join(ROOT, 'drafts'), DATA = path.join(ROOT, 'data', 'pc2switch');
const readJson = (p, d) => { try { return JSON.parse(fs.readFileSync(p, 'utf8')); } catch (e) { return d; } };
const VERDICT = {
  supported: 'a save-based set already exists',
  draftable: 'readable save (JSON/BYML/GVAS/NRBF): drafted automatically',
  text: 'text save (XML/INI/other): a reader is needed first',
  binary: 'binary save: manual work in Save Lab (captures + diffs)',
  encrypted: 'encrypted or compressed: not readable',
  'no-save': 'no save data found on the console',
};

// ------------------------------------------------------------------ collect saves per title
function walk(dir, base = dir) {
  const out = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...walk(p, base));
    else out.push({ path: path.relative(base, p).split(path.sep).join('/'), data: new Uint8Array(fs.readFileSync(p)) });
  }
  return out;
}
function collect() {
  const games = new Map();   // tid -> {name, verdict, files:[{path,data,format}]}
  if (!fs.existsSync(SAMPLES)) return games;
  for (const f of fs.readdirSync(SAMPLES).sort()) {
    const p = path.join(SAMPLES, f);
    if (/^scan_bundle.*\.json$/i.test(f)) {
      const b = readJson(p, {});
      if (b.format !== 'switch-achievements-scan') { console.log(f + ': not a scan bundle, skipped'); continue; }
      for (const g of b.games || []) {
        const tid = String(g.title_id).toUpperCase();
        const files = (g.files || []).map(x => ({ path: x.path, format: x.format, size: x.size, data: x.data_b64 ? new Uint8Array(Buffer.from(x.data_b64, 'base64')) : null }));
        games.set(tid, { name: g.name, verdict: g.verdict, reader: g.reader || '', files, from: f });
      }
    } else if (/^01[0-9A-Fa-f]{14}$/.test(f) && fs.statSync(p).isDirectory()) {
      const tid = f.toUpperCase();
      const files = walk(p).map(x => ({ ...x, size: x.data.length, format: '' }));
      const prev = games.get(tid);
      games.set(tid, { name: prev ? prev.name : '', verdict: '', reader: prev ? prev.reader : '', files: prev ? prev.files.concat(files) : files, from: 'samples/' + f });
    }
  }
  return games;
}
function treeOf(file) {
  if (!file.data) return null;
  try { const d = E.parseTree(file.data); return d ? { tree: d.tree, reader: d.reader, xorKey: d.xorKey || '' } : null; } catch (e) { return null; }
}
function progressScore(flat) { let n = 0; for (const v of flat.values()) if (typeof v === 'number' && v) n++; return n; }

// One game: Steam list + parsed save trees -> {def, md, sum, st, errs}. Shared with bulk.js.
// trees: [{path, reader, tree, flat}]; def: existing romfs definition (its play-log rules are kept).
function draftGame({ tid, name, c, steam, trees, from, def }) {
  // most advanced readable file = reference; least advanced (if different) = "fresh" hint
  trees.sort((a, b) => progressScore(b.flat) - progressScore(a.flat));
  const adv = trees[0], fresh = trees.length > 1 && progressScore(trees[trees.length - 1].flat) < progressScore(adv.flat) * 0.5 ? trees[trees.length - 1] : null;
  // 1. the game records its own achievements under the Steam names? 2. otherwise match names with fields
  const st = M.selfTracked(steam.achievements, trees.map(t => ({ path: t.path, flat: t.flat })));
  const ref = st ? trees.find(t => t.path === st.file) : adv;
  const drafts = st ? st.drafts : M.draftRules(steam.achievements, adv.flat, fresh && fresh.flat);
  const sum = M.summary(drafts);
  const sameReader = st ? [st.file] : trees.filter(t => t.reader === adv.reader).map(t => t.path);
  const saveFiles = [...new Set(sameReader.concat(!st && adv.path.match(/\d/) ? [1, 2, 3, 4].map(i => adv.path.replace(/\d(?=[^\d]*$)/, i)) : []))];
  const defOut = M.buildDefinition({ title_id: tid, name, reader: ref.reader, save_files: saveFiles, appid: c.appid }, drafts, { includeMaybe: true });
  if (ref.xorKey) defOut.xor_key = ref.xorKey;   // e.g. SIGNALIS: the app undoes the XOR before parsing
  const keep = (def && def.achievements || []).filter(a => a.rule && ['playtime_hours', 'launches'].includes(a.rule.type));
  defOut.achievements.push(...keep);
  const errs = E.validate(defOut);
  const esc = s => String(s == null ? '' : s).replace(/\|/g, '\\|').replace(/\n/g, ' ');
  const ruleTxt = r => !r ? '' : r.paths ? 'count of ' + r.paths.length + ' flags (' + r.paths[0] + ' …) ≥ ' + r.target : '`' + r.path + '` ≥ ' + r.target;
  const order = { sure: 0, maybe: 1, unmatched: 2, skip: 3 };
  const md = [
    `# ${name} — draft from Steam (${c.steam}, app ${c.appid})`, '',
    st ? `**The game records its own achievements** in \`${st.file}\` (${st.found} found under their Steam names): every achievement gets an exact rule on that file. · from ${from}`
       : `Reference save: \`${adv.path}\` (${adv.reader})${fresh ? `, new-game hint: \`${fresh.path}\`` : ''} · from ${from}`, '',
    `**${sum.sure} sure · ${sum.maybe} maybe · ${sum.unmatched} unmatched · ${sum.skip} not save-based** (of ${drafts.length} Steam achievements).`, '',
    'Sure = names match and the field is set in the reference save. Maybe = check the field (the candidates column lists alternatives). ' +
    'Unmatched = find the field in Save Lab (Différences / Valeur connue) or leave it out. Draft file: `drafts/' + tid + '.json`' + (errs.length ? ' — validation: ' + errs.join('; ') : ''), '',
    '| | Steam achievement | Rule | Candidates (score, value in save) |', '|---|---|---|---|',
    ...drafts.slice().sort((a, b) => order[a.confidence] - order[b.confidence]).map(d =>
      `| ${d.confidence} | **${esc(d.title)}** — ${esc(d.desc)} | ${esc(ruleTxt(d.rule))}${d.note ? '<br>_' + esc(d.note) + '_' : ''}${d.why ? '_' + esc(d.why) + '_' : ''} | ${(d.candidates || []).map(x => '`' + esc(x.path) + '` ' + x.score + ', ' + x.value).join('<br>')} |`),
    '',
  ].join('\n');
  return { def: defOut, md, sum, st, errs };
}

// ------------------------------------------------------------------ main
function main() {
  const catalog = readJson(path.join(DATA, 'catalog.json'), { games: [] });
  const cat = new Map(catalog.games.map(g => [g.id, g]));
  const defs = new Map();
  for (const f of fs.readdirSync(path.join(ROOT, 'romfs', 'games'))) { const d = readJson(path.join(ROOT, 'romfs', 'games', f), null); if (d) defs.set(d.title_id.toUpperCase(), d); }
  const games = collect();
  fs.mkdirSync(DRAFTS, { recursive: true });
  const index = [];
  for (const [tid, g] of [...games].sort((a, b) => (a[1].name || a[0]).localeCompare(b[1].name || b[0]))) {
    const c = cat.get(tid), def = defs.get(tid);
    const name = g.name || (c && c.name) || (def && def.name) || tid;
    const row = { tid, name, steam: c ? c.steam + ' (' + c.appid + ')' : '—', ach: c ? c.ach : null, verdict: g.verdict || '', result: '' };
    // parse readable files
    const trees = [];
    for (const f of g.files) { const t = treeOf(f); if (t) trees.push({ ...t, path: f.path, flat: E.flattenByml(t.tree) }); }
    if (!row.verdict) {
      const ids = g.files.filter(f => f.data).map(f => E.identify(f.data, f.path));
      row.verdict = trees.length ? 'draftable' : ids.some(i => i.entropy > 7.5) ? 'encrypted' : ids.some(i => i.strings && i.strings.length > 20) ? 'text' : g.files.length ? 'binary' : 'no-save';
    }
    if (def && def.reader) { row.verdict = 'supported'; row.result = def.reader + ', ' + def.achievements.length + ' achievements'; index.push(row); continue; }
    const steam = c && readJson(path.join(DATA, 'steam', c.appid + '.json'), null);
    if (!trees.length || !steam || !steam.achievements || !steam.achievements.length) {
      row.result = !trees.length ? (row.verdict === 'draftable' ? 'console says JSON/BYML/GVAS/NRBF but Save Lab could not parse it' : VERDICT[row.verdict] || '') : !c ? 'no Steam version found' : 'Steam achievement list not fetched yet (catalog.js)';
      index.push(row); continue;
    }
    const { def: defOut, md, sum } = draftGame({ tid, name, c, steam, trees, from: g.from, def });
    fs.writeFileSync(path.join(DRAFTS, tid + '.json'), JSON.stringify(defOut, null, 1) + '\n');
    fs.writeFileSync(path.join(DRAFTS, tid + '.md'), md);
    row.result = `[draft](${tid}.md): ${sum.sure} sure, ${sum.maybe} maybe, ${sum.unmatched} unmatched, ${sum.skip} skipped`;
    index.push(row);
  }
  const counts = {};
  for (const r of index) counts[r.verdict] = (counts[r.verdict] || 0) + 1;
  fs.writeFileSync(path.join(DRAFTS, 'README.md'), [
    '# Compatibility & drafts', '',
    `Generated by \`tools/pc2switch/draft.js\` from ${games.size} game(s) in \`samples/\`. ` + Object.entries(counts).map(([k, v]) => v + ' ' + k).join(', ') + '.', '',
    '| Game | Steam | Steam achievements | Verdict | Result |', '|---|---|---|---|---|',
    ...index.map(r => `| ${r.name} \`${r.tid}\` | ${r.steam} | ${r.ach ?? '?'} | ${r.verdict} | ${r.result} |`), '',
    'Verdicts: ' + Object.entries(VERDICT).map(([k, v]) => '`' + k + '` ' + v).join(' · '), '',
  ].join('\n'));
  console.log(`drafts: ${games.size} game(s); ` + Object.entries(counts).map(([k, v]) => v + ' ' + k).join(', '));
  return index;
}

if (require.main === module) main();
module.exports = { main, collect, draftGame, treeOf, walk, progressScore };
