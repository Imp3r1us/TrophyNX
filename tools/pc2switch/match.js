// pc2switch matcher — deterministic, no AI. Shared by the Node scripts (catalog.js, draft.js) and Save Lab.
//  1. matchTitles: Switch titles <-> Steam apps by normalised name.
//  2. draftRules:  Steam achievements (apiName, displayName, description) <-> save fields
//                  (flattened tree paths, e.g. "playerData/killedDungDefender"), with a confidence:
//                  "sure"      strong, unambiguous name match and the field is set in the advanced save
//                  "maybe"     plausible match: review before shipping
//                  "unmatched" nothing usable (candidates listed for manual work)
//                  "skip"      not a save condition (time limits, "without", difficulty modes...)
(function (root) {
'use strict';

// ------------------------------------------------------------------ words
const IRREGULAR = { slain: 'kill', slew: 'kill', beaten: 'kill', beat: 'kill', dead: 'kill', died: 'kill', die: 'kill', killed: 'kill',
  got: 'get', gotten: 'get', found: 'get', has: 'get', have: 'get', had: 'get', bought: 'get', won: 'win', wins: 'win',
  freed: 'rescue', saved: 'rescue', spoken: 'talk', spoke: 'talk', met: 'talk', seen: 'see', saw: 'see', children: 'child', enemies: 'enemy', bosses: 'boss', keys: 'key', items: 'item' };
const SYN = {
  kill: ['kill', 'defeat', 'slay', 'destroy', 'vanquish', 'beat', 'conquer', 'banish', 'eliminate', 'murder'],
  get: ['get', 'obtain', 'acquire', 'collect', 'find', 'own', 'unlock', 'buy', 'purchase', 'receive', 'earn', 'gain', 'pick', 'gather', 'learn'],
  rescue: ['rescue', 'free', 'save', 'release', 'liberate'],
  complete: ['complete', 'finish', 'clear', 'done', 'beat', 'pass'],
  open: ['open', 'unlock', 'activate'],
  visit: ['visit', 'discover', 'reach', 'enter', 'arrive', 'explore'],
  talk: ['talk', 'speak', 'meet', 'convo', 'conversation'],
};
const CANON = {};
for (const [k, list] of Object.entries(SYN)) for (const w of list) if (!CANON[w]) CANON[w] = k;
const VERBS = new Set(Object.keys(SYN));
const STOP = new Set(('the a an of in on at to and or your you yours its it is are be been by for from with into onto as this that these those ' +
  'his her their them they he she we our my me i up out over than then there here so any some one game achievement ' +
  'player data save total times time number num count amount value state').split(' '));
// dialogue / UI bookkeeping flags: rarely what an achievement means
const NOISE = new Set(['see', 'talk', 'convo', 'notify', 'notification', 'prompt', 'hint', 'tutorial', 'intro', 'cutscene', 'popup', 'tip', 'explain', 'pin', 'marker', 'shop', 'price', 'cost']);
const QUANT = new Set(['all', 'every', 'each', 'half', 'first', 'entire', 'whole', 'full']);
const NOT_SAVE = /\b(under|within|less than|in (?:a|one) (?:single )?(?:run|life|sitting)|without|no damage|hitless|deathless|speedrun|seconds?|minutes?|hours?|in a row|consecutive|simultaneous|at once|on (?:hard|nightmare|expert|insane)|difficulty|co-?op|online|multiplayer|friend|leaderboard|daily)\b/i;

function splitWords(s) {
  return String(s || '')
    .replace(/([a-z])([A-Z])/g, '$1 $2').replace(/([A-Z]+)([A-Z][a-z])/g, '$1 $2')
    .replace(/([a-zA-Z])(\d)/g, '$1 $2').replace(/(\d)([a-zA-Z])/g, '$1 $2')
    .toLowerCase().replace(/['’]s\b/g, '').split(/[^a-z0-9]+/).filter(Boolean);
}
function stem(w) {
  if (IRREGULAR[w]) return IRREGULAR[w];
  if (/^\d+$/.test(w) || w.length <= 3) return w;
  let s = w;
  if (s.endsWith('ies') && s.length > 4) s = s.slice(0, -3) + 'y';
  else if (/(ches|shes|sses|xes)$/.test(s)) s = s.slice(0, -2);
  else if (s.endsWith('s') && !s.endsWith('ss') && !s.endsWith('us')) s = s.slice(0, -1);
  if (s.endsWith('ing') && s.length > 5) s = s.slice(0, -3);
  else if (s.endsWith('ed') && s.length > 4) { s = s.slice(0, -2); if (/(.)\1$/.test(s) && !/(ll|ss)$/.test(s)) s = s.slice(0, -1); }
  return IRREGULAR[s] || s;
}
function canon(w) { const s = stem(w); return CANON[s] || CANON[w] || s; }
function tokens(s) { return splitWords(s).map(canon).filter(w => w && !STOP.has(w)); }

// ------------------------------------------------------------------ titles
const EDITION = /\b(nintendo switch|switch|for nintendo switch|edition|deluxe|definitive|complete|goty|game of the year|remastered|remaster|hd|anniversary|ultimate|enhanced|director'?s cut|collection|bundle|digital|special|standard|plus|reloaded|redux|legendary|royal|voidheart)\b/g;
function titleKey(name) {
  return String(name || '').toLowerCase().normalize('NFKD').replace(/[̀-ͯ]/g, '')
    .replace(/[™®©]/g, '').replace(/&/g, ' and ').replace(/[^a-z0-9]+/g, ' ').trim().replace(/^the /, '');
}
function titleKeys(name) {
  const full = titleKey(name);
  const keys = new Set([full]);
  const bare = full.replace(EDITION, ' ').replace(/\s+/g, ' ').trim();
  if (bare) keys.add(bare);
  const main = titleKey(String(name || '').split(/\s[-–—:]\s|:\s/)[0]);
  if (main && main.length >= 4) keys.add(main);
  return [...keys];
}
// switchTitles: [{id, name}], steamApps: [{appid, name}] -> Map(id -> {appid, name, how})
function matchTitles(switchTitles, steamApps) {
  // Steam side: full name and name without edition words. Never Steam's own "main title" (avoids
  // "Grand Theft Auto: Vice City" -> "Grand Theft Auto: San Andreas").
  const byFull = new Map(), byBare = new Map();
  for (const a of steamApps) {
    const full = titleKey(a.name), bare = full.replace(EDITION, ' ').replace(/\s+/g, ' ').trim();
    if (full && !byFull.has(full)) byFull.set(full, a);
    if (bare && !byBare.has(bare)) byBare.set(bare, a);
  }
  const out = new Map();
  for (const t of switchTitles) {
    const full = titleKey(t.name), bare = full.replace(EDITION, ' ').replace(/\s+/g, ' ').trim();
    const main = titleKey(String(t.name || '').split(/\s[-–—:]\s|:\s/)[0]);
    let hit = byFull.get(full), how = 'exact';
    if (!hit && bare) { hit = byFull.get(bare) || byBare.get(bare) || byBare.get(full); how = 'normalized'; }
    if (!hit && main && main !== full && main.split(' ').length >= 2) { hit = byFull.get(main); how = 'main-title'; }
    if (!hit) continue;
    if (how === 'exact' && !/\s/.test(full) && full.length < 6) how = 'exact-short';   // "Split", "Hollow"...: check the publisher
    out.set(t.id, { appid: hit.appid, name: hit.name, how });
  }
  return out;
}

// ------------------------------------------------------------------ achievements -> save fields
function isTimestamp(path, v) { return /time(stamp)?$|date$|^.*played$|lastsave/i.test(path.split('/').pop()) || (typeof v === 'number' && v > 1e9); }
function fieldsOf(flat) {
  // flat: Map(path -> value) from flattenByml; keep numbers and booleans (stored as 0/1)
  const fields = [];
  for (const [path, v] of flat) {
    if (typeof v !== 'number' || !isFinite(v) || isTimestamp(path, v)) continue;
    const segs = path.replace(/\[[^\]]*\]/g, '').split('/');
    const tk = new Set(tokens(segs.slice(-2).join(' ')));
    if (!tk.size) continue;
    fields.push({ path, value: v, tokens: tk, leaf: segs[segs.length - 1] });
  }
  return fields;
}
function idfTable(fields) {
  const df = new Map();
  for (const f of fields) for (const t of f.tokens) df.set(t, (df.get(t) || 0) + 1);
  const n = fields.length || 1;
  return t => Math.log(1 + n / (1 + (df.get(t) || 0)));
}
function achTokens(a) {
  const m = new Map();
  const add = (s, w) => { for (const t of tokens(s)) if (!QUANT.has(t)) m.set(t, Math.max(m.get(t) || 0, w)); };
  add(a.name, 1.0);            // API name, e.g. DUNG_DEFENDER
  add(a.description, 1.0);
  add(a.displayName, 0.5);     // often a pun: weaker
  return m;
}
function numberIn(desc) {
  const m = String(desc || '').replace(/,(\d{3})/g, '$1').match(/\b(\d{1,7})\b/);
  return m ? +m[1] : null;
}
function quantity(desc) {
  const d = String(desc || '').toLowerCase();
  if (/\bhalf\b/.test(d)) return 'half';
  if (/\b(all|every|each|entire|whole)\b/.test(d)) return 'all';
  if (/\b(first|a|an|one)\b/.test(d)) return 'one';
  return null;
}

// achievements: Steam schema entries [{name, displayName, description, hidden}]
// adv: Map(path -> value) of an advanced/100% save; fresh (optional): Map of a new game.
function draftRules(achievements, adv, fresh, opts = {}) {
  const fields = fieldsOf(adv);
  const idf = idfTable(fields);
  const freshVal = p => (fresh && fresh.has(p) ? fresh.get(p) : null);
  const out = [];
  for (const a of achievements) {
    const text = (a.displayName || '') + ' — ' + (a.description || '');
    const base = { api: a.name, title: a.displayName || a.name, desc: a.description || '', hidden: !!a.hidden };
    if (NOT_SAVE.test(a.description || '') || /speedrun|steel_?soul/i.test(a.name || '')) { out.push({ ...base, confidence: 'skip', rule: null, why: 'needs a time limit, a mode or a condition not kept in saves' }); continue; }
    const at = achTokens(a);
    let totalW = 0;
    for (const [t, w] of at) totalW += w * (VERBS.has(t) ? 0.4 : idf(t));
    if (!totalW) { out.push({ ...base, confidence: 'unmatched', rule: null, candidates: [] }); continue; }
    const apiT = new Set(tokens(a.name).filter(t => !VERBS.has(t) && !/^\d+$/.test(t)));
    const scored = [];
    const nIn = numberIn(a.description), qty = quantity(a.description);
    for (const f of fields) {
      let hitW = 0, hits = 0, content = 0;
      for (const [t, w] of at) if (f.tokens.has(t)) { const iw = VERBS.has(t) ? 0.4 : idf(t); hitW += w * iw; hits++; if (!VERBS.has(t)) content++; }
      if (!content) continue;
      const fieldCover = hits / f.tokens.size;
      const missW = totalW - hitW;
      let s = (hitW / (hitW + 0.35 * missW)) * (0.6 + 0.4 * fieldCover);
      // every content word of the API name found in the field name (DUNG_DEFENDER -> killedDungDefender)
      if (apiT.size && [...apiT].every(t => f.tokens.has(t))) s = Math.min(1, s + 0.15);
      for (const t of f.tokens) if (NOISE.has(t) && !at.has(t)) { s *= 0.5; break; }
      const achVerbs = [...at.keys()].filter(t => VERBS.has(t)), fVerbs = [...f.tokens].filter(t => VERBS.has(t));
      if (achVerbs.length && fVerbs.length) s *= fVerbs.some(v => achVerbs.includes(v)) ? 1.1 : 0.75;
      if ((qty === 'half' || qty === 'all' || nIn !== null) && (f.value === 0 || f.value === 1)) s *= 0.8;   // quantities point to counters
      const fv = freshVal(f.path);
      if (f.value === 0) s *= 0.6;                     // never set even in the advanced save
      else if (fv !== null && fv === f.value) s *= 0.7;  // did not change since a new game
      scored.push({ f, s, content });
    }
    scored.sort((x, y) => y.s - x.s || x.f.path.length - y.f.path.length);
    const n = numberIn(a.description), q = quantity(a.description);
    // collections: "a map of each area" -> many sibling flags sharing the matched token (mapCrossroads, mapGreenpath...)
    let group = null;
    if (q === 'all' && scored.length) {
      const after = (String(a.description || '').toLowerCase().match(/\b(?:all|each|every)\s+(?:of\s+)?(?:the\s+)?([a-z]+)/) || [])[1];
      const nouns = new Set([...apiT, ...(after ? [canon(after)] : [])]);
      const key = [...nouns].find(t => fields.some(f => f.leaf.toLowerCase().startsWith(t) && /^[A-Z]/.test(f.leaf.slice(t.length))));
      if (key) {
        const sib = fields.filter(f => f.leaf.toLowerCase().startsWith(key) && f.leaf.length > key.length + 2 && (f.value === 0 || f.value === 1) && /^[A-Z]/.test(f.leaf.slice(key.length)) && !/^(All|Any|Count|Num|Total)/.test(f.leaf.slice(key.length)));
        const depth = sib.length ? sib[0].path.split('/').length : 0;
        if (sib.length >= 4 && sib.length <= 200 && sib.every(f => f.path.split('/').length === depth) && sib.filter(f => f.value === 1).length >= sib.length * 0.75) group = sib;
      }
    }
    if (!scored.length) { out.push({ ...base, confidence: 'unmatched', rule: null, candidates: [] }); continue; }
    const best = scored[0], second = scored[1];
    const margin = best.s - (second ? second.s : 0);
    const isBool = best.f.value === 0 || best.f.value === 1;
    let rule, note = '';
    if (group) {
      const set = group.filter(f => f.value === 1).length;
      rule = { type: 'count', paths: group.map(f => f.path), target: set };
      note = group.length + ' sibling flags (' + group[0].leaf + '…), ' + set + ' set in the reference save' + (set < group.length ? ' — target = ' + set + ', check the unset ones' : '');
    } else if (n !== null && !isBool) {
      rule = { type: 'flag', path: best.f.path, target: n };
    } else if (!isBool && q === 'half') {
      rule = { type: 'flag', path: best.f.path, target: Math.max(1, Math.round(best.f.value / 2)) };
      note = 'half of ' + best.f.value + ' (value in the advanced save — check it is the maximum)';
    } else if (!isBool && q === 'all') {
      rule = { type: 'flag', path: best.f.path, target: best.f.value };
      note = 'target = value in the advanced save (' + best.f.value + ') — check it is the maximum';
    } else if (!isBool && q === 'one') {
      rule = { type: 'flag', path: best.f.path, target: 1 };
    } else {
      rule = { type: 'flag', path: best.f.path, target: isBool ? 1 : best.f.value };
      if (!isBool) note = 'numeric field without a number in the description: target = advanced value ' + best.f.value;
    }
    let confidence;
    const strong = best.s >= 0.6 && best.content >= 1 && margin >= 0.15;
    if (group) confidence = apiT.size === 1 && apiT.has(groupKeyOf(group)) && group.every(f => f.value === 1) ? 'sure' : 'maybe';
    else if (strong && best.f.value !== 0 && ((isBool && !q) || (isBool && q === 'one') || (!isBool && n !== null))) confidence = 'sure';
    else if (best.s >= 0.3) confidence = 'maybe';
    else confidence = 'unmatched';
    if (n !== null && n > best.f.value && !group) { confidence = confidence === 'unmatched' ? confidence : 'maybe'; note = (note ? note + '; ' : '') + 'advanced save value ' + best.f.value + ' is below ' + n; }
    out.push({ ...base, confidence, score: +best.s.toFixed(3), rule: confidence === 'unmatched' ? null : rule, note,
      candidates: scored.slice(0, 3).map(c => ({ path: c.f.path, score: +c.s.toFixed(3), value: c.f.value })) });
  }
  return out;
}

function groupKeyOf(group) { const l = group[0].leaf; const m = l.match(/^[a-z]+/); return m ? canon(m[0]) : ''; }

// ------------------------------------------------------------------ games that record their own achievements
// Many ports keep the platform achievement list in the save itself, under the Steam API names:
//   Slay the Spire   preferences/STSAchievements  {"GHOST_GUARDIAN": "True", ...}      -> key
//   Vampire Survivors SaveDataUnity               "Achievements": ["ReachLV10", ...]     -> [=NAME]
//   Vampire Crawlers SaveProfile0.save            [{"Key": "SimpleAchievement_Character_Antonio", "Value": true}] -> [Key=prefix_NAME]/Value
// files: [{path, flat}] (flat = flattenByml of one file). Returns null or {file, drafts}.
function normApi(s) { return String(s).toLowerCase().replace(/[^a-z0-9]/g, ''); }
function selfTracked(achievements, files) {
  const api = new Map();
  for (const a of achievements) if (a.name) api.set(normApi(a.name), a.name);
  if (api.size < 3) return null;
  const slotOf = t => t.replace(/=[A-Za-z]+[_\-. ]\{API\}\]/g, '={API}]').replace(/(^|\/)[A-Za-z]+[_\-. ]\{API\}$/, '$1{API}');
  let best = null;
  for (const f of files) {
    const groups = new Map();   // template -> Map(api -> value)
    const totals = new Map();   // slot (value replaced by {API}) -> number of entries in that list / object
    const hit = (tpl, apiName, value) => {
      if (!groups.has(tpl)) groups.set(tpl, new Map());
      groups.get(tpl).set(apiName, value);
    };
    const tryName = (text, make, value) => {
      totals.set(make(''), (totals.get(make('')) || 0) + 1);
      const n = normApi(text);
      if (api.has(n)) { hit(make(''), api.get(n), value); return; }
      // prefixed form: SimpleAchievement_Character_Antonio -> prefix "SimpleAchievement_" + Character_Antonio
      const m = String(text).match(/^([A-Za-z]+[_\-. ])(.+)$/);
      if (m && api.has(normApi(m[2]))) hit(make(m[1]), api.get(normApi(m[2])), value);
    };
    for (const [path, value] of f.flat) {
      // 1. selector values: Achievements[=NAME], List[Key=NAME]/Value
      const re = /\[(\w*)=([^\]]+)\]/g;
      let m;
      while ((m = re.exec(path))) {
        const [whole, field, val] = m, at = m.index;
        if (field && path.endsWith(whole + '/' + field)) continue;   // the element's own id field (…[Key=X]/Key): use its siblings (Value…)
        tryName(val, (pre) => path.slice(0, at) + '[' + field + '=' + pre + '{API}]' + path.slice(at + whole.length), value);
      }
      // 2. key names: {"GHOST_GUARDIAN": true}
      const segs = path.split('/');
      const leaf = segs[segs.length - 1];
      if (!/[\[\]]/.test(leaf)) tryName(leaf, (pre) => segs.slice(0, -1).concat(pre + '{API}').join('/'), value);
    }
    // score each slot: how many Steam names, and how much of the list they make up (cards named like
    // achievements must not win over the real achievement list), bonus when it is called "achievement"
    const slots = new Map();
    for (const [tpl, seen] of groups) {
      const sl = slotOf(tpl);
      if (!slots.has(sl)) slots.set(sl, []);
      slots.get(sl).push([tpl, seen]);
    }
    for (const [sl, list] of slots) {
      const hits = list.reduce((n, [, g]) => n + g.size, 0);
      if (hits < 2) continue;
      const ratio = hits / Math.max(hits, totals.get(sl) || hits);
      const score = hits * ratio + (/achiev|trophy|trophee|troph/i.test(f.path + ' ' + sl) ? 5 : 0);
      if (ratio < 0.2 && !/achiev|troph/i.test(f.path + ' ' + sl)) continue;
      if (!best || score > best.score) {
        list.sort((a, b) => b[1].size - a[1].size);
        best = { file: f.path, tpl: list[0][0], seen: list[0][1], groups: new Map(list), score };
      }
    }
  }
  if (!best) return null;
  // sibling templates in the same file (DungeonAchievement_{API} next to SimpleAchievement_{API})
  const tpls = [...best.groups];
  const seenAll = new Map();
  for (const [t, g] of tpls) for (const [n, v] of g) if (!seenAll.has(n)) seenAll.set(n, { tpl: t, v });
  const strong = seenAll.size >= 5 && tpls.length === 1;
  const drafts = achievements.map(a => {
    const base = { api: a.name, title: a.displayName || a.name, desc: a.description || '', hidden: !!a.hidden };
    const s = seenAll.get(a.name);
    const tpl = s ? s.tpl : best.tpl;
    const path = tpl.replace('{API}', a.name);
    const rule = { type: 'count', path, target: 1 };
    const how = 'the game records its achievements in ' + best.file + ' (' + seenAll.size + ' found under their Steam names)';
    if (s) return { ...base, confidence: s.v ? 'sure' : 'maybe', rule, note: s.v ? how : how + '; listed but not unlocked yet', candidates: [{ path, score: 1, value: s.v }] };
    return { ...base, confidence: strong ? 'sure' : 'maybe', rule, note: how + (strong ? '' : tpls.length > 1 ? '; not seen yet — prefix guessed (' + tpls.map(([t]) => t.split('/').pop()).join(' or ') + ')' : '; not seen yet'),
      candidates: [{ path, score: strong ? 0.9 : 0.6, value: 0 }] };
  });
  return { file: best.file, drafts, found: seenAll.size };
}

// Draft -> app definition (romfs/games format). Unknown keys (_draft, _api) are ignored by the app.
function slugId(s) { return String(s).toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '').slice(0, 40) || 'ach'; }
function buildDefinition(meta, drafts, opts = {}) {
  const keep = new Set(opts.includeMaybe ? ['sure', 'maybe'] : ['sure']);
  const ids = new Set();
  const achievements = [];
  for (const d of drafts) {
    if (!keep.has(d.confidence) || !d.rule) continue;
    let id = slugId(d.api), k = 2;
    while (ids.has(id)) id = slugId(d.api) + '_' + k++;
    ids.add(id);
    const a = { id, name: d.title, desc: d.desc || d.title, points: d.rule.target >= 50 || d.hidden ? 25 : 10, rule: d.rule, _draft: d.confidence, _api: d.api };
    if (d.hidden) a.hidden = true;
    achievements.push(a);
  }
  return { title_id: meta.title_id, name: meta.name, reader: meta.reader, save_files: meta.save_files || [], _steam_appid: meta.appid, _generated: 'pc2switch (review before use)', achievements };
}
function summary(drafts) {
  const c = { sure: 0, maybe: 0, unmatched: 0, skip: 0 };
  for (const d of drafts) c[d.confidence]++;
  return c;
}

const api = { splitWords, stem, canon, tokens, titleKey, titleKeys, matchTitles, fieldsOf, draftRules, selfTracked, buildDefinition, summary, NOT_SAVE };
if (typeof module !== 'undefined' && module.exports) module.exports = api;
else root.PC2Switch = api;
})(typeof self !== 'undefined' ? self : this);
