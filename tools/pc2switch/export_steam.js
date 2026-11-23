#!/usr/bin/env node
// Publishes the Steam achievement lists fetched by catalog.js (cache .pc2switch-cache/schema/<appid>.json)
// as one small file per game, for the Save Lab website: <out>/<appid>.json = { appid, name, achievements }.
// The Save Lab loads them on demand ("Use this list" in the PC -> Switch catalogue). Public data only.
//   node tools/pc2switch/export_steam.js [--cache .pc2switch-cache] [--out _site/steam]
'use strict';
const fs = require('fs');
const path = require('path');
const opt = (k, d) => { const i = process.argv.indexOf('--' + k); return i > 0 ? process.argv[i + 1] : d; };
const ROOT = path.join(__dirname, '..', '..');
const CACHE = opt('cache', path.join(ROOT, '.pc2switch-cache'));
const OUT = opt('out', path.join(ROOT, '_site', 'steam'));

const names = new Map();
try {
  for (const g of JSON.parse(fs.readFileSync(path.join(ROOT, 'data', 'pc2switch', 'catalog.json'), 'utf8')).games || [])
    if (g.appid && g.steam) names.set(String(g.appid), g.steam);
} catch (e) { }
fs.mkdirSync(OUT, { recursive: true });
const done = new Set();
const write = (appid, name, achievements) => {
  const list = achievements.map(a => Object.assign({ name: a.name, displayName: a.displayName || '', description: a.description || '', hidden: a.hidden ? 1 : 0 },
    typeof a.percent === 'number' ? { percent: a.percent } : {}));
  fs.writeFileSync(path.join(OUT, appid + '.json'), JSON.stringify({ appid: +appid, name: name || '', achievements: list }));
  done.add(appid);
};
// 1. everything fetched by the catalogue (Actions cache)
const dir = path.join(CACHE, 'schema');
if (fs.existsSync(dir)) {
  for (const f of fs.readdirSync(dir)) {
    const m = f.match(/^(\d+)\.json$/); if (!m) continue;
    let s; try { s = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8')); } catch (e) { continue; }
    if (s && Array.isArray(s.achievements) && s.achievements.length) write(m[1], names.get(m[1]), s.achievements);
  }
} else console.log('no cache at ' + dir + ' (run the "PC to Switch" workflow once)');
// 2. the lists kept in the repository (data/pc2switch/steam) always win: they are the reviewed ones
const repo = path.join(ROOT, 'data', 'pc2switch', 'steam');
if (fs.existsSync(repo)) for (const f of fs.readdirSync(repo)) {
  let d; try { d = JSON.parse(fs.readFileSync(path.join(repo, f), 'utf8')); } catch (e) { continue; }
  if (d && d.appid && Array.isArray(d.achievements) && d.achievements.length) write(String(d.appid), d.name || names.get(String(d.appid)), d.achievements);
}
console.log(done.size + ' Steam achievement lists written to ' + OUT);
