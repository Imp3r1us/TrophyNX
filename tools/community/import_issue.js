#!/usr/bin/env node
// Turns a "Share an achievement list" issue (.github/ISSUE_TEMPLATE/achievement-list.yml) into
// drafts/community/<TITLEID>.json, after the same checks as the Save Lab and the app.
// Used by .github/workflows/share-list.yml. Never runs anything from the issue: the list is parsed
// as JSON, checked, and written back re-serialized.
//
//   ISSUE_BODY=... ISSUE_NUMBER=12 node tools/community/import_issue.js
// Writes /tmp/share/result.json: { ok, tid, name, file, errors[], warnings[], count }
// and, when ok, drafts/community/<TID>.json.
//   node tools/community/import_issue.js --index   rebuilds drafts/community/README.md (run on main after a merge)
'use strict';
const fs = require('fs');
const path = require('path');
const E = require('../savelab/engine.js');

const ROOT = path.join(__dirname, '..', '..');
const OUT_DIR = path.join(ROOT, 'drafts', 'community');
const RES_DIR = process.env.SHARE_OUT || '/tmp/share';
const MAX_BYTES = 512 * 1024;
const MAX_ACH = 400;
const TOP_FIELDS = ['title_id', 'name', 'reader', 'magic', 'file_size', 'save_files', 'xor_key', 'close_scan', 'achievements'];
const ACH_FIELDS = ['id', 'name', 'desc', 'points', 'tier', 'hidden', 'rule'];

// "### Heading\n\nvalue" sections of an issue form
function sections(body) {
  const out = {};
  const parts = String(body || '').replace(/\r\n/g, '\n').split(/^### +/m).slice(1);
  for (const p of parts) {
    const nl = p.indexOf('\n');
    const head = (nl < 0 ? p : p.slice(0, nl)).trim();
    let val = (nl < 0 ? '' : p.slice(nl + 1)).trim();
    if (val === '_No response_') val = '';
    out[head] = val;
  }
  return out;
}
function field(sec, prefix) {
  const k = Object.keys(sec).find(h => h.toLowerCase().startsWith(prefix.toLowerCase()));
  return k ? sec[k] : '';
}
function stripFences(s) {
  let t = s.trim();
  for (let i = 0; i < 2; i++) {   // the form adds ```json, people sometimes paste their own fence too
    const m = t.match(/^```[a-zA-Z]*\n([\s\S]*?)\n?```$/);
    if (m) t = m[1].trim();
  }
  return t;
}
const clean = (s, max) => String(s || '').replace(/[\u0000-\u001f\u007f]/g, ' ').trim().slice(0, max);

function run(body, issue) {
  const res = { ok: false, errors: [], warnings: [] };
  const sec = sections(body);
  const raw = stripFences(field(sec, 'Achievement list') || '');
  if (!raw) { res.errors.push('The “Achievement list (JSON)” field is empty.'); return res; }
  if (Buffer.byteLength(raw) > MAX_BYTES) { res.errors.push('The list is larger than 512 KB.'); return res; }
  let def;
  try { def = JSON.parse(raw); } catch (e) { res.errors.push('This is not valid JSON: ' + e.message + '. A validator such as jsonlint.com shows where.'); return res; }
  if (!def || typeof def !== 'object' || Array.isArray(def)) { res.errors.push('The JSON must be an object with "title_id", "name" and "achievements".'); return res; }
  if (!Array.isArray(def.achievements) || !def.achievements.length) { res.errors.push('There is no "achievements" list (or it is empty).'); return res; }
  if (def.achievements.length > MAX_ACH) { res.errors.push('More than ' + MAX_ACH + ' achievements.'); return res; }
  def.title_id = String(def.title_id || '').trim().toUpperCase();

  // keep the fields the app reads (notes starting with "_" are dropped, the bot adds its own)
  const out = {};
  for (const k of TOP_FIELDS) if (def[k] !== undefined) out[k] = def[k];
  const dropped = Object.keys(def).filter(k => !TOP_FIELDS.includes(k) && !k.startsWith('_'));
  if (dropped.length) res.warnings.push('Ignored unknown fields: ' + dropped.join(', '));
  out.achievements = def.achievements.map(a => {
    const o = {};
    for (const k of ACH_FIELDS) if (a && a[k] !== undefined) o[k] = a[k];
    return o;
  });
  for (const a of out.achievements) {
    if (typeof a.id !== 'string' || !/^[A-Za-z0-9_.-]{1,64}$/.test(a.id)) res.errors.push('Invalid id ' + JSON.stringify(a.id) + ': use letters, digits and _ (64 characters max).');
    if (typeof a.name !== 'string' || !a.name.trim()) res.errors.push((a.id || '?') + ': the name is missing.');
    if (a.points !== undefined && !(Number.isFinite(+a.points) && +a.points >= 0 && +a.points <= 1000)) res.errors.push((a.id || '?') + ': points must be a number from 0 to 1000.');
  }
  if (out.save_files !== undefined && !(Array.isArray(out.save_files) && out.save_files.every(f => typeof f === 'string' && f && !f.includes('..') && !f.startsWith('/')))) res.errors.push('"save_files" must be a list of paths inside the save (no "..", no leading "/").');
  res.errors.push(...E.validate(out));
  if (out.name === undefined || !String(out.name).trim()) out.name = clean(field(sec, 'Game'), 120);
  res.tid = out.title_id; res.name = clean(out.name, 120); res.count = out.achievements.length;
  if (res.errors.length) return res;

  const version = clean(field(sec, 'Game version'), 40);
  const tested = clean(field(sec, 'Tested'), 80);
  const credit = clean(field(sec, 'Credit'), 60);
  const final = Object.assign({}, out);
  delete final.achievements;
  final._community = Object.assign({ issue: issue ? +issue : null }, version ? { game_version: version } : {}, tested ? { tested } : {}, credit ? { credit } : {});
  final.achievements = out.achievements;

  fs.mkdirSync(OUT_DIR, { recursive: true });
  res.file = 'drafts/community/' + out.title_id + '.json';
  if (fs.existsSync(path.join(ROOT, res.file))) res.warnings.push('A shared list for this game already exists: this one replaces it in the pull request (the maintainer compares both before merging).');
  if (fs.existsSync(path.join(ROOT, 'romfs', 'games', out.title_id + '.json'))) {
    const b = JSON.parse(fs.readFileSync(path.join(ROOT, 'romfs', 'games', out.title_id + '.json'), 'utf8'));
    if (b.reader) res.warnings.push('TrophyNX already has save-based achievements for this game: copying this file to the SD card replaces them.');
  }
  fs.writeFileSync(path.join(ROOT, res.file), JSON.stringify(final, null, 1) + '\n');
  res.ok = true;
  return res;
}

function writeIndex() {
  const rows = fs.readdirSync(OUT_DIR).filter(f => /^[0-9A-F]{16}\.json$/.test(f)).sort().map(f => {
    let d; try { d = JSON.parse(fs.readFileSync(path.join(OUT_DIR, f), 'utf8')); } catch (e) { return null; }
    const c = d._community || {};
    const name = String(d.name || '').replace(/[|\n]/g, ' ');
    return { name, line: `| ${name} | \`${d.title_id}\` | [${f}](${f}) | ${d.achievements.length} | ${d.reader || 'play time'} | ${c.game_version || '?'} | ${c.tested ? c.tested.split(',')[0] : '?'} | ${c.issue ? '#' + c.issue : ''} |` };
  }).filter(Boolean).sort((a, b) => a.name.localeCompare(b.name));
  const md = `# Community achievement lists

Lists shared by players with the **Share** button of the [Save Lab](https://imp3r1us.github.io/TrophyNX/) or the
[Share an achievement list](https://github.com/Imp3r1us/TrophyNX/issues/new?template=achievement-list.yml) issue form.
A bot checks each one (same checks as the app) and opens a pull request; the maintainer reviews and merges it.

**To use one:** open the file, press **Raw**, save it, copy it to \`sdmc:/switch/TrophyNX/games/\` on the SD card,
then open TrophyNX and press **Y**. Full guide: [Making your own achievement list](../../docs/custom-achievements.md).

| Game | Title ID | File | Achievements | Reader | Game version | Tested on console | Shared in |
|---|---|---|---|---|---|---|---|
${rows.map(r => r.line).join('\n')}
`;
  fs.writeFileSync(path.join(OUT_DIR, 'README.md'), md);
}

if (require.main === module && process.argv[2] === '--index') { fs.mkdirSync(OUT_DIR, { recursive: true }); writeIndex(); }
else if (require.main === module) {
  const res = run(process.env.ISSUE_BODY || '', process.env.ISSUE_NUMBER || '');
  fs.mkdirSync(RES_DIR, { recursive: true });
  fs.writeFileSync(path.join(RES_DIR, 'result.json'), JSON.stringify(res, null, 1));
  console.log(JSON.stringify(res, null, 1));
}
module.exports = { run, sections, stripFences, writeIndex };
