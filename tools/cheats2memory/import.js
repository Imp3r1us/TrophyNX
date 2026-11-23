#!/usr/bin/env node
// Drafts live "memory" achievements (include/memrules.h) from cheat codes: a code that sets a game's money or
// level already says where that value lives in memory, per game version (build ID). This turns such codes into
// tiers like "Have 50,000 gold" / "Reach level 50", one draft per game, for review before use.
//
//   node tools/cheats2memory/import.js <switch-cheats-db clone> [--out drafts/memory] [--only <TITLEID,...>]
//
// Source: https://github.com/HamletDuFromage/switch-cheats-db (cheats/<TITLEID>.json = { <build id>: { "[name]": code } }).
// The codes are written by their authors (named in each draft's "_source"). Idea from HomeNX's achievement packs
// (github.com/ad3333m/homenx); independent implementation.
//
// Only simple codes are used: one value written at a fixed address or at the end of a pointer chain
// (5 = load pointer, 7 = add, 6 = store; or a single 0 = store to heap/alias). Code patches (writes to main),
// conditional or button codes are skipped. Floats are detected from the value the code writes (a u32 read of a
// float would unlock at once). Nothing here has been checked on a console: drafts are "_draft": "unverified".
'use strict';
const fs = require('fs'), path = require('path');

// ------------------------------------------------------------------ Atmosphère cheat VM (the subset we read)
const REGION = ['main', 'heap', 'alias'];   // M = 3 (aslr) is not supported

// Returns { region, offsets: [BigInt], width, value: BigInt } or null when the code is not a single plain store.
function decode(code) {
  const lines = String(code).split(/\r?\n/).map(l => l.trim()).filter(l => l && !/^[[{]/.test(l));
  if (!lines.length) return null;
  const words = lines.map(l => l.split(/\s+/));
  if (words.some(w => w.some(x => !/^[0-9A-Fa-f]{8}$/.test(x)))) return null;
  const off40 = (w0, w1) => BigInt('0x' + w0.slice(6, 8) + w1);
  // single static store: 0TMR00AA AAAAAAAA VVVVVVVV [VVVVVVVV]
  if (words.length === 1 && words[0][0][0] === '0') {
    const w = words[0], T = +w[0][1], M = +w[0][2];
    if (![1, 2, 4, 8].includes(T) || (M !== 1 && M !== 2) || w[0].slice(4, 6) !== '00') return null;   // main = code patch
    if (w.length !== (T === 8 ? 4 : 3)) return null;
    const value = BigInt('0x' + (T === 8 ? w[2] + w[3] : w[2]));
    return { region: REGION[M], offsets: [off40(w[0], w[1])], width: T, value };
  }
  // pointer chain: 5T M R 0 0 (R = [base + A]), 5T 0 R 1 0 (R = [R + A]), 7T 0 R 0 (R += V), then 6T 0 R 0 0 0 0
  let reg = null, region = null;
  const offsets = [];
  let pending = 0n;
  for (let i = 0; i < words.length; i++) {
    const w = words[i], op = w[0][0], last = i === words.length - 1;
    if (op === '5' && !last) {
      if (w.length !== 2 || w[0][1] !== '8' || w[0][5] !== '0') return null;   // pointers are 8 bytes
      const M = +w[0][2], R = w[0][3], S = w[0][4], A = off40(w[0], w[1]);
      if (S === '0') {
        if (reg !== null || M > 2) return null;   // the chain starts once, from a region
        reg = R; region = REGION[M]; offsets.push(A);
      } else if (S === '1') {
        if (reg === null || R !== reg) return null;
        offsets.push(pending + A); pending = 0n;
      } else return null;
    } else if (op === '7' && !last) {
      if (w.length !== 2 || reg === null || w[0][3] !== reg || w[0][2] !== '0' || !/^[01]000$/.test(w[0].slice(4, 8))) return null;
      const v = BigInt('0x' + w[1]);
      pending += w[0][4] === '0' ? v : -v;
    } else if (op === '6' && last) {
      if (w.length !== 3 || reg === null || w[0][3] !== reg || w[0].slice(4, 8) !== '0000' || w[0][2] !== '0') return null;
      const T = +w[0][1];
      if (![1, 2, 4, 8].includes(T)) return null;
      offsets.push(pending);
      return { region, offsets, width: T, value: BigInt('0x' + w[1] + w[2]) };
    } else return null;
  }
  return null;
}

// Value type of what the code writes: floats are recognised by their bit pattern.
function valueOf(d) {
  const buf = Buffer.alloc(8);
  buf.writeBigUInt64LE(d.value);
  if (d.width === 4) {
    const u = buf.readUInt32LE(0), f = buf.readFloatLE(0);
    if (u >= 0x3F000000 && u < 0x7F800000 && f >= 0.5 && f <= 1e10) return { type: 'f32', v: f };
    return { type: 'u32', v: u };
  }
  if (d.width === 8) {
    const u = buf.readBigUInt64LE(0), f = buf.readDoubleLE(0);
    if (u >= 0x3FE0000000000000n && u < 0x7FF0000000000000n && f >= 0.5 && f <= 1e15) return { type: 'f64', v: f };
    return { type: 'u64', v: Number(u) };
  }
  return { type: d.width === 1 ? 'u8' : 'u16', v: Number(d.value & (d.width === 1 ? 0xFFn : 0xFFFFn)) };
}

function formatAddress(region, offsets) {
  const o = v => (v < 0n ? '-' : '+') + (v < 0n ? -v : v).toString(16).toUpperCase();
  let s = '['.repeat(offsets.length - 1) + region + o(offsets[0]);
  for (const v of offsets.slice(1)) s += ']' + (v ? o(v) : '');
  return s;
}

// ------------------------------------------------------------------ what a code is about
const CURRENCY = /\b(money|gold|gil|munny|cash|rupees?|zenny|credits?|souls|dollars|funds|bells|coins?|gems|jewels|crystals|tears|g|geo|bolts|scrap|silver|pesos|medals|tokens|points|pts|yen|bucks|lucre|essence|shards|spirits?)\b/i;
const LEVEL = /\b(level|lv|lvl)\b/i;
// "Max Gold" yes; "Gold multiplier", "Enemy level", "Shop prices", "Gold never decreases" no.
const EXCLUDE = /multipl|\bx\s?\d|\d+\s?x\b|rate|drop|enemy|enemies|boss|shop|price|cost|sell|buy|gain|per |reward|bonus|decreas|lose|loss|never|freeze|lock|item|weapon|skill|exp\b|experience|\bxp\b|stage|world|area|chapter|difficulty|slot\s*machine|%|reset|zero|min\b|minimum|rank|class|job|limit|cap\b|speed|damage|time|timer|hp\b|mp\b|health|stamina|ammo|ability|stat|party|team|pet|friend|affection|bond|relation|skill/i;
const NICE = [100, 250, 500, 1e3, 2500, 5e3, 1e4, 25e3, 5e4, 1e5, 25e4, 5e5, 1e6, 25e5, 5e6, 1e7, 25e6, 5e7, 1e8, 1e9];
const niceAtMost = x => { let b = null; for (const n of NICE) if (n <= x * 1.0001) b = n; return b; };
const roundTo5 = x => Math.max(5, Math.round(x / 5) * 5);
const fmtNum = n => n.toLocaleString('en-US');

function cleanName(raw) { return String(raw).replace(/^\[|\]$/g, '').replace(/^\s*\d+[#.)]\s*/, '').trim(); }

function classify(name) {
  if (EXCLUDE.test(name)) return null;
  const c = CURRENCY.exec(name);
  if (c && /\b(max|inf|infinite|unlimited|999|have|set|full|lots|many|\+)\b|^\s*(\w+\s)?(money|gold|gil|cash|coins?|gems)\s*$/i.test(name)) {
    let word = c[1].toLowerCase();
    if (word === 'g') word = 'gold';
    if (/^(pts|points)$/.test(word)) return null;   // score-like: rarely kept between sessions
    return { kind: 'currency', word };
  }
  if (LEVEL.test(name) && /\b(max|99|100|set|level\s*\d)\b/i.test(name) && !/\bup\b|\bnext\b|\bunlock/i.test(name)) return { kind: 'level', word: 'level' };
  return null;
}

const WORD_LABEL = { gil: 'Gil', munny: 'munny', zenny: 'Zenny', rupee: 'Rupees', rupees: 'Rupees', geo: 'Geo', bells: 'Bells', money: 'money', cash: 'cash', funds: 'money', dollars: 'dollars', coin: 'coins', coins: 'coins', credit: 'credits', credits: 'credits', soul: 'souls', souls: 'souls', gem: 'gems' };
const label = w => WORD_LABEL[w] || w;

// Tiers: a code sets a currency to its cap (99,999), or just to something huge (4,294,967,295). A tenth of the cap
// (at most 1,000,000) is the top tier, then a tenth and a hundredth of that: a guess, to be tuned on a console.
function tiers(kind, v) {
  if (kind === 'currency') {
    const top = niceAtMost(Math.min(v, 1e7) / 10);
    if (!top || top < 1000) return [];
    return [...new Set([niceAtMost(top / 100), niceAtMost(top / 10), top].filter(x => x && x >= 100))];
  }
  if (!(v >= 10 && v <= 9999 && Number.isInteger(v))) return [];
  const top = v <= 100 ? v : niceAtMost(v) || v;
  return [...new Set([roundTo5(top / 4), roundTo5(top / 2), top])].filter(x => x <= top);
}

const ACH_NAMES = {
  currency: ['Saving Up', 'Well Off', 'Rolling In It'],
  level: ['Getting Stronger', 'Seasoned', 'Peak Form'],
};

// ------------------------------------------------------------------ one game
// cheats = { <build>: { "[name]": code }, attribution: {...} } -> draft definition or null
function draftGame(tid, cheats, opts = {}) {
  const groups = new Map();   // kind:word -> { kind, word, builds: Map(build -> Set(address)), types:Set, maxV, sources:Set }
  for (const [build, list] of Object.entries(cheats)) {
    if (build === 'attribution' || !/^[0-9A-Fa-f]{16}$/.test(build) || typeof list !== 'object') continue;
    for (const [rawName, code] of Object.entries(list)) {
      const name = cleanName(rawName);
      const cls = classify(name);
      if (!cls) continue;
      const d = decode(code);
      if (!d) continue;
      const val = valueOf(d);
      if (cls.kind === 'level' && /^f/.test(val.type)) continue;
      const key = cls.kind + ':' + cls.word;
      let g = groups.get(key);
      if (!g) groups.set(key, g = { ...cls, builds: new Map(), types: new Set(), values: [], names: new Set() });
      const addr = formatAddress(d.region, d.offsets);
      if (!g.builds.has(build.toUpperCase())) g.builds.set(build.toUpperCase(), new Set());
      g.builds.get(build.toUpperCase()).add(addr);
      g.types.add(val.type);
      g.values.push(val.v);
      g.names.add(name);
    }
  }
  const achievements = [];
  const notes = [];
  for (const g of groups.values()) {
    if (g.types.size !== 1) { notes.push(`${g.kind} ${g.word}: codes disagree on the value type (${[...g.types].join(', ')}), skipped`); continue; }
    const type = [...g.types][0];
    const v = Math.min(...g.values);   // the most conservative of the codes' values
    const ts = tiers(g.kind, v);
    if (!ts.length) { notes.push(`${g.kind} ${g.word}: value ${v} too small or odd for tiers, skipped`); continue; }
    const builds = {};
    for (const [b, set] of [...g.builds.entries()].sort()) { const a = [...set].sort(); builds[b] = a.length === 1 ? a[0] : a; }
    const max = g.kind === 'level' ? Math.max(v * 10, 100) : Math.max(v * 10, ts[ts.length - 1] * 10);
    ts.forEach((t, i) => {
      const what = g.kind === 'currency' ? `Have ${fmtNum(t)} ${label(g.word)}` : `Reach level ${t}`;
      const nm = ACH_NAMES[g.kind][ts.length === 1 ? 2 : ts.length === 2 ? i + 1 : i];
      achievements.push({
        id: 'mem_' + g.kind + (g.kind === 'currency' ? '_' + g.word.replace(/[^a-z0-9]/g, '') : '') + '_' + t,
        name: nm, desc: what + (g.kind === 'level' && g.builds.size && [...g.builds.values()].some(s => s.size > 1) ? ' (any character or save slot)' : ''),
        points: [10, 25, 50][ts.length === 1 ? 2 : ts.length === 2 ? i + 1 : i],
        rule: { type: 'memory', value_type: type, cmp: '>=', target: t, max, builds },
        _draft: 'unverified',
        _cheats: [...g.names].slice(0, 6),
      });
    });
  }
  if (!achievements.length) return { def: null, notes };
  const sources = Object.keys(cheats.attribution || {}).map(s => s.replace(/\.txt$/i, ''));
  return { def: { achievements, _source: sources, _notes: notes }, notes };
}

// Game name: the repo's definitions, titledb (cached by tools/pc2switch/catalog.js), else the cheat file names.
function nameFor(tid, catalog, cheats) {
  if (catalog.has(tid)) return catalog.get(tid);
  const files = Object.keys(cheats.attribution || {});
  for (const f of files) {
    const n = f.replace(/\.txt$/i, '').replace(/_/g, ' ').replace(/\s+(v(er)?\.?\s?\d.*|version.*|by\s.*|\[.*|\(.*)$/i, '').trim();
    if (n && !/^[0-9A-F]{16}$/i.test(n)) return n;
  }
  return tid;
}

function main() {
  const args = process.argv.slice(2);
  const opt = (k, d) => { const i = args.indexOf('--' + k); return i >= 0 ? args[i + 1] : d; };
  const db = args.find(a => !a.startsWith('--') && args[args.indexOf(a) - 1] !== '--out' && args[args.indexOf(a) - 1] !== '--only');
  if (!db) { console.error('usage: import.js <switch-cheats-db clone> [--out drafts/memory] [--only TITLEID,...]'); process.exit(2); }
  const ROOT = path.resolve(__dirname, '..', '..');
  const OUT = path.resolve(opt('out', path.join(ROOT, 'drafts', 'memory')));
  const only = opt('only', '') ? new Set(opt('only').toUpperCase().split(',')) : null;
  const catalog = new Map();
  for (const reg of ['GB.en', 'US.en']) {   // later wins: US names
    try { for (const v of Object.values(JSON.parse(fs.readFileSync(path.join(ROOT, '.pc2switch-cache', 'titledb_' + reg + '.json'), 'utf8')))) if (v && v.id && v.name) catalog.set(String(v.id).toUpperCase(), v.name); } catch (e) { }
  }
  try { for (const g of JSON.parse(fs.readFileSync(path.join(ROOT, 'data', 'pc2switch', 'catalog.json'), 'utf8')).games || []) catalog.set(g.id.toUpperCase(), g.name); } catch (e) { }
  const builtins = new Map();
  for (const f of fs.readdirSync(path.join(ROOT, 'romfs', 'games'))) {
    try { const d = JSON.parse(fs.readFileSync(path.join(ROOT, 'romfs', 'games', f), 'utf8')); if (d.title_id) { builtins.set(d.title_id.toUpperCase(), d); catalog.set(d.title_id.toUpperCase(), d.name); } } catch (e) { }
  }
  fs.mkdirSync(OUT, { recursive: true });
  const dir = path.join(db, 'cheats');
  const rows = [];
  for (const f of fs.readdirSync(dir).sort()) {
    const tid = f.replace(/\.json$/i, '').toUpperCase();
    if (!/^[0-9A-F]{16}$/.test(tid) || (only && !only.has(tid))) continue;
    let cheats;
    try { cheats = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8')); } catch (e) { continue; }
    const { def } = draftGame(tid, cheats);
    if (!def) continue;
    const name = nameFor(tid, catalog, cheats);
    // A game that already has a definition: keep it whole and add the memory achievements (a file on the SD card
    // replaces the built-in one, so a memory-only file would hide the save achievements).
    const base = builtins.get(tid);
    const out = base ? { ...base, achievements: [...(base.achievements || []).filter(a => !(a.rule && a.rule.type === 'memory')), ...def.achievements] }
                     : { title_id: tid, name, achievements: def.achievements };
    out._generated = 'cheats2memory from switch-cheats-db (unverified: test on a console before use)';
    out._source = def._source;
    if (def._notes.length) out._notes = def._notes;
    fs.writeFileSync(path.join(OUT, tid + '.json'), JSON.stringify(out, null, 1) + '\n');
    const builds = new Set(def.achievements.flatMap(a => Object.keys(a.rule.builds)));
    rows.push({ tid, name, n: def.achievements.length, builds: builds.size, types: [...new Set(def.achievements.map(a => a.rule.value_type))].join(', '), merged: !!base,
      what: [...new Set(def.achievements.map(a => a.desc.replace(/\d[\d,]*/, 'N')))].join('; ') });
  }
  const md = ['# Memory drafts (from cheat codes)', '',
    `Generated by \`tools/cheats2memory/import.js\` from [switch-cheats-db](https://github.com/HamletDuFromage/switch-cheats-db) (codes by their authors, see each file's \`_source\`).`,
    `**${rows.length} games.** None has been checked on a console: copy one to \`sdmc:/switch/TrophyNX/games/\`, play, and watch`,
    '`live_log.txt` (it logs the build ID and the values read). Keep what unlocks at the right moment.', '',
    '| Game | Title ID | Achievements | Builds | Type | What |', '|---|---|---|---|---|---|',
    ...rows.map(r => `| ${r.name.replace(/\|/g, '/')}${r.merged ? ' (merged with its definition)' : ''} | \`${r.tid}\` | ${r.n} | ${r.builds} | ${r.types} | ${r.what} |`)];
  fs.writeFileSync(path.join(OUT, 'README.md'), md.join('\n') + '\n');
  console.log(`cheats2memory: ${rows.length} games drafted in ${OUT}`);
}

module.exports = { decode, valueOf, formatAddress, classify, tiers, draftGame };
if (require.main === module) main();
