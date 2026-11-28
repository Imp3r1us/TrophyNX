// Save Lab engine — pure analysis functions, shared by the web page (index.html) and the Node tests.
// Everything here mirrors what the Switch app does (source/achievements.cpp) so that a rule tested
// here behaves the same on the console.
(function (root) {
'use strict';
// T(english, french): UI language of the page (global T from index.html), English elsewhere (Node tests).
const T = root && typeof root.T === 'function' ? root.T : (en, fr) => en;
const SEP = T(': ', ' : ');
const F = typeof module !== 'undefined' && module.exports ? require('./formats.js') : root.SaveFormats;

// ------------------------------------------------------------------ hashes (same as the app)
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    t[n] = c >>> 0;
  }
  return t;
})();
const enc = new TextEncoder();
function crc32(s) {
  const b = typeof s === 'string' ? enc.encode(s) : s;
  let c = 0xFFFFFFFF;
  for (let i = 0; i < b.length; i++) c = CRC_TABLE[(c ^ b[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}
function murmur3(s) {
  const d = typeof s === 'string' ? enc.encode(s) : s;
  const len = d.length, n = len >>> 2;
  const c1 = 0xcc9e2d51, c2 = 0x1b873593;
  const rotl = (x, r) => ((x << r) | (x >>> (32 - r))) >>> 0;
  let h = 0;
  for (let i = 0; i < n; i++) {
    let k = (d[i * 4] | (d[i * 4 + 1] << 8) | (d[i * 4 + 2] << 16) | (d[i * 4 + 3] << 24)) >>> 0;
    k = Math.imul(k, c1); k = rotl(k, 15); k = Math.imul(k, c2);
    h ^= k; h = rotl(h, 13); h = (Math.imul(h, 5) + 0xe6546b64) >>> 0;
  }
  let k = 0;
  const t = n * 4;
  switch (len & 3) {
    case 3: k ^= d[t + 2] << 16; // fallthrough
    case 2: k ^= d[t + 1] << 8;  // fallthrough
    case 1: k ^= d[t]; k = Math.imul(k, c1); k = rotl(k, 15); k = Math.imul(k, c2); h ^= k;
  }
  h ^= len;
  h ^= h >>> 16; h = Math.imul(h, 0x85ebca6b); h ^= h >>> 13; h = Math.imul(h, 0xc2b2ae35); h ^= h >>> 16;
  return h >>> 0;
}
function fnv1a(s) {
  const b = typeof s === 'string' ? enc.encode(s) : s;
  let h = 0x811c9dc5;
  for (let i = 0; i < b.length; i++) { h ^= b[i]; h = Math.imul(h, 0x01000193); }
  return h >>> 0;
}
const HASHES = { crc32, murmur3, fnv1a };
const hex = (v, w = 8) => '0x' + (v >>> 0).toString(16).toUpperCase().padStart(w, '0');
const hexOff = (v) => '0x' + v.toString(16).toUpperCase();

// ------------------------------------------------------------------ byte helpers
function rd(buf, off, size, le = true) {
  if (off + size > buf.length) return null;
  let v = 0;
  if (le) for (let i = size - 1; i >= 0; i--) v = v * 256 + buf[off + i];
  else for (let i = 0; i < size; i++) v = v * 256 + buf[off + i];
  return v;
}
function rdF32(buf, off) {
  if (off + 4 > buf.length) return null;
  return new DataView(buf.buffer, buf.byteOffset + off, 4).getFloat32(0, true);
}
function entropy(buf, start = 0, end = buf.length) {
  const cnt = new Uint32Array(256);
  for (let i = start; i < end; i++) cnt[buf[i]]++;
  const n = end - start;
  if (!n) return 0;
  let e = 0;
  for (let i = 0; i < 256; i++) if (cnt[i]) { const p = cnt[i] / n; e -= p * Math.log2(p); }
  return e;
}
function asciiAt(buf, off, n) {
  let s = '';
  for (let i = 0; i < n && off + i < buf.length; i++) s += String.fromCharCode(buf[off + i]);
  return s;
}
const printable = (s) => /^[\x20-\x7e]+$/.test(s);

// ------------------------------------------------------------------ ZIP (JKSV exports, phone zips)
async function inflateRaw(data) {
  if (typeof DecompressionStream === 'undefined') throw new Error(T('This browser cannot unzip files', 'Décompression ZIP non prise en charge par ce navigateur'));
  const ds = new DecompressionStream('deflate-raw');
  const stream = new Blob([data]).stream().pipeThrough(ds);
  return new Uint8Array(await new Response(stream).arrayBuffer());
}
async function unzip(buf) {
  // Locate End Of Central Directory.
  let eocd = -1;
  for (let i = buf.length - 22; i >= Math.max(0, buf.length - 65557); i--) {
    if (buf[i] === 0x50 && buf[i + 1] === 0x4b && buf[i + 2] === 5 && buf[i + 3] === 6) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error(T('Invalid ZIP (end of central directory not found)', 'ZIP invalide (fin de répertoire introuvable)'));
  const count = rd(buf, eocd + 10, 2);
  let p = rd(buf, eocd + 16, 4);
  const files = [];
  const dec = new TextDecoder();
  for (let i = 0; i < count; i++) {
    if (rd(buf, p, 4) !== 0x02014b50) throw new Error(T('Invalid ZIP (central directory)', 'ZIP invalide (répertoire central)'));
    const method = rd(buf, p + 10, 2);
    const csize = rd(buf, p + 20, 4), usize = rd(buf, p + 24, 4);
    const nlen = rd(buf, p + 28, 2), xlen = rd(buf, p + 30, 2), clen = rd(buf, p + 32, 2);
    const local = rd(buf, p + 42, 4);
    const name = dec.decode(buf.subarray(p + 46, p + 46 + nlen));
    p += 46 + nlen + xlen + clen;
    if (name.endsWith('/')) continue;
    const lnlen = rd(buf, local + 26, 2), lxlen = rd(buf, local + 28, 2);
    const start = local + 30 + lnlen + lxlen;
    const raw = buf.subarray(start, start + csize);
    let data;
    if (method === 0) data = raw.slice();
    else if (method === 8) data = await inflateRaw(raw);
    else throw new Error(name + T(': unsupported ZIP compression method ', ' : méthode de compression ZIP ') + method + T('', ' non prise en charge'));
    if (data.length !== usize) throw new Error(name + T(': unexpected uncompressed size', ' : taille décompressée inattendue'));
    files.push({ path: name, data });
  }
  return files;
}
// Stored (uncompressed) ZIP writer: captures from the Switch, to share them (GitHub issue, chat...).
// files: [{path, data:Uint8Array}] -> Uint8Array. UTF-8 names (flag bit 11), fixed DOS date.
function zipStore(files) {
  const parts = [], central = [];
  let off = 0;
  const u16 = (v) => [v & 0xFF, (v >>> 8) & 0xFF], u32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];
  const DOS_TIME = 0, DOS_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1;
  for (const f of files) {
    const name = enc.encode(f.path), crc = crc32(f.data), n = f.data.length;
    const head = [0x50, 0x4b, 3, 4, ...u16(20), ...u16(0x800), ...u16(0), ...u16(DOS_TIME), ...u16(DOS_DATE), ...u32(crc), ...u32(n), ...u32(n), ...u16(name.length), ...u16(0)];
    parts.push(new Uint8Array(head), name, f.data);
    central.push(new Uint8Array([0x50, 0x4b, 1, 2, ...u16(20), ...u16(20), ...u16(0x800), ...u16(0), ...u16(DOS_TIME), ...u16(DOS_DATE), ...u32(crc), ...u32(n), ...u32(n),
      ...u16(name.length), ...u16(0), ...u16(0), ...u16(0), ...u16(0), ...u32(0), ...u32(off)]), name);
    off += head.length + name.length + n;
  }
  const cdSize = central.reduce((a, b) => a + b.length, 0);
  const end = new Uint8Array([0x50, 0x4b, 5, 6, ...u16(0), ...u16(0), ...u16(files.length), ...u16(files.length), ...u32(cdSize), ...u32(off), ...u16(0)]);
  const all = [...parts, ...central, end], out = new Uint8Array(all.reduce((a, b) => a + b.length, 0));
  let p = 0;
  for (const a of all) { out.set(a, p); p += a.length; }
  return out;
}
// Strip the common leading folder ("JKSV/Game/2026-11-18/...") so paths match the app's save_files.
function stripCommonPrefix(files) {
  if (files.length < 1) return files;
  const parts = files.map(f => f.path.split('/'));
  let common = 0;
  if (files.length === 1) common = parts[0].length - 1;
  else {
    outer: for (;;) {
      const seg = parts[0][common];
      for (const p of parts) if (p.length - 1 <= common || p[common] !== seg) break outer;
      common++;
    }
  }
  return files.map((f, i) => ({ path: parts[i].slice(common).join('/'), data: f.data }));
}

// ------------------------------------------------------------------ identification
const MAGICS = [
  [[0x78, 0x01], T('zlib (compressed)', 'zlib (compressé)')], [[0x78, 0x9C], T('zlib (compressed)', 'zlib (compressé)')], [[0x78, 0xDA], T('zlib (compressed)', 'zlib (compressé)')],
  [[0x1F, 0x8B], T('gzip (compressed)', 'gzip (compressé)')], [[0x28, 0xB5, 0x2F, 0xFD], T('zstd (compressed)', 'zstd (compressé)')],
  [[0x04, 0x22, 0x4D, 0x18], T('LZ4 frame (compressed)', 'LZ4 frame (compressé)')], [[0x59, 0x61, 0x7A, 0x30], T('Yaz0 (Nintendo compression)', 'Yaz0 (compressé Nintendo)')],
  [[0x53, 0x41, 0x52, 0x43], T('SARC (Nintendo archive)', 'SARC (archive Nintendo)')], [[0x42, 0x59], 'BYML big-endian'], [[0x59, 0x42], 'BYML little-endian'],
  [[0x41, 0x41, 0x4D, 0x50], T('AAMP (Nintendo parameters)', 'AAMP (paramètres Nintendo)')], [[0x4D, 0x73, 0x67, 0x53, 0x74, 0x64, 0x42, 0x6E], T('MSBT (texts)', 'MSBT (textes)')],
  [[0x50, 0x4B, 0x03, 0x04], 'ZIP'], [[0x53, 0x51, 0x4C, 0x69, 0x74, 0x65], 'SQLite'],
  [[0x7B], T('JSON / text', 'JSON / texte')], [[0x3C], T('XML / text', 'XML / texte')],
];

// Known save formats the app already reads.
const KNOWN = [
  { id: 'botw', test: (b) => b.length >= 0x30 && rd(b, 4, 4) === 0xFFFFFFFF, reader: 'gamedata_hash', hash: 'crc32',
    label: T('Zelda BotW-style hash/value table (game_data.sav) — readable with the “gamedata_hash” reader', 'Table hash/valeur type Zelda BotW (game_data.sav) — lisible par le lecteur « gamedata_hash »') },
  { id: 'totk', test: (b) => b.length >= 0x30 && rd(b, 0, 4) === 0x01020304, reader: 'totk_progress', hash: 'murmur3',
    label: T('Zelda TotK-style hash/value table (progress.sav) — readable with the “totk_progress” reader', 'Table hash/valeur type Zelda TotK (progress.sav) — lisible par le lecteur « totk_progress »') },
  { id: 'mk8d', test: (b) => asciiAt(b, 0, 4) === 'SUTC', reader: 'binary_le', magic: 'SUTC',
    label: T('Mario Kart 8 Deluxe (SUTC header) — fixed offsets, “binary_le” reader', 'Mario Kart 8 Deluxe (en-tête SUTC) — offsets fixes, lecteur « binary_le »') },
  { id: 'smash', test: (b) => b.length === 5982968, reader: 'binary_le',
    label: T('Smash Bros. Ultimate system_data.bin (size 5,982,968) — fixed offsets, “binary_le” reader', 'Smash Bros. Ultimate system_data.bin (taille 5 982 968) — offsets fixes, lecteur « binary_le »') },
];

// Heuristic: a run of 8-byte entries "u32 hash, u32 value" where hashes look random and values small.
function detectHashTable(buf) {
  let best = null;
  for (let start = 0; start <= 0x40; start += 4) {
    let good = 0, n = 0;
    const seen = new Set();
    for (let o = start; o + 8 <= buf.length && n < 2000; o += 8, n++) {
      const h = rd(buf, o, 4), v = rd(buf, o + 4, 4);
      let bits = 0; for (let x = h; x; x >>>= 1) bits += x & 1;
      const randomish = bits >= 8 && bits <= 24 && h > 0x00FFFFFF;
      const smallish = v < 0x10000 || v === 0xFFFFFFFF || (v > 0x30000000 && v < 0x50000000);
      if (randomish && smallish) { good++; seen.add(h); }
    }
    if (n >= 16) {
      const score = good / n;
      if (!best || score > best.score) best = { start, score, entries: n, unique: seen.size };
    }
  }
  return best && best.score > 0.6 ? best : null;
}

function extractStrings(buf, min = 5, max = 80) {
  const out = [];
  let cur = '', at = 0;
  for (let i = 0; i <= buf.length; i++) {
    const c = i < buf.length ? buf[i] : 0;
    if (c >= 0x20 && c < 0x7f) { if (!cur) at = i; cur += String.fromCharCode(c); }
    else { if (cur.length >= min) out.push({ off: at, s: cur, enc: 'ASCII' }); cur = ''; }
    if (out.length >= max) break;
  }
  // UTF-16LE (player names, etc.)
  cur = '';
  for (let i = 0; i + 1 < buf.length && out.length < max * 2; i += 2) {
    const c = buf[i] | (buf[i + 1] << 8);
    if (c >= 0x20 && c < 0x7f || (c >= 0xC0 && c < 0x2000)) { if (!cur) at = i; cur += String.fromCharCode(c); }
    else { if (cur.length >= min) out.push({ off: at, s: cur, enc: 'UTF-16' }); cur = ''; }
  }
  return out;
}

function identify(buf, path = '') {
  const notes = [];
  const r = { size: buf.length, path, notes, format: T('unknown', 'inconnu'), reader: '', analyzable: true };
  const bh = findByml(buf);
  if (bh) {
    r.known = 'byml'; r.reader = 'byml'; r.byml = bh;
    r.format = 'Document BYML v' + bh.ver + (bh.le ? ' (little-endian)' : ' (big-endian)') + (bh.base ? T(' after a 0x', ' après un en-tête de 0x') + bh.base.toString(16) + T('-byte header (Super Mario Odyssey layout)', ' octets (format Super Mario Odyssey)') : '');
    notes.push(T('Nintendo data tree: values are identified by their path (e.g. ShineData[*]/IsGet), not by their position. App reader “byml”.', 'Arbre de données Nintendo : les valeurs sont identifiées par leur chemin (ex. ShineData[*]/IsGet), pas par leur position. Lecteur « byml » de l\'app.'));
  }
  if (!bh && F.isDeltarune(buf)) {   // a JSON object of GameMaker text files: before the plain JSON reader
    try {
      const d = F.parseDeltarune(buf);
      r.known = r.reader = 'deltarune'; r.meta = d.meta;
      r.format = T('DELTARUNE save (', 'Sauvegarde DELTARUNE (') + d.meta.files + T(' chapter files, trophies ', ' fichiers de chapitre, trophées ') + d.meta.trophies + '/30)';
      notes.push(T('DELTARUNE deltarune.sav: each chapter file decoded (files[*]/flags/<n>, plot, weapon…), plus completed[n], patch[i] (TrophyNX patch flags) and trophy[i]. App reader “deltarune”.', 'deltarune.sav de DELTARUNE : chaque fichier de chapitre décodé (files[*]/flags/<n>, plot, weapon…), plus completed[n], patch[i] (drapeaux du patch TrophyNX) et trophy[i]. Lecteur « deltarune » de l\'app.'));
    } catch (e) { r.treeError = e.message; }
  }
  if (!bh && !r.known && F.isRpgMaker(buf)) {   // LZString / zlib / JsonEx JSON: before the plain JSON reader
    try {
      const d = F.parseRpgMaker(buf);
      r.known = r.reader = 'rpgmaker'; r.meta = d.meta;
      r.format = T('RPG Maker MV/MZ save (', 'Sauvegarde RPG Maker MV/MZ (') + d.meta.encoding + ', ' + Math.round(d.meta.jsonBytes / 1024) + T(' KB of JSON)', ' Ko de JSON)');
      notes.push(T('RPG Maker MV/MZ save: switches/_data[n], variables/_data[n], party/_gold, party/_items/<id>, actors/_data[n]/_level, system/_battleCount… App reader “rpgmaker”.', 'Sauvegarde RPG Maker MV/MZ : switches/_data[n], variables/_data[n], party/_gold, party/_items/<id>, actors/_data[n]/_level, system/_battleCount… Lecteur « rpgmaker » de l\'app.'));
    } catch (e) { r.treeError = e.message; }
  }
  if (!bh && !r.known && looksJson(buf)) {
    let ok = false; try { ok = !!parseJson(buf); } catch (e) { r.jsonError = e.message; }
    if (ok) {
      r.known = 'json'; r.reader = 'json'; r.format = T('JSON document (text)', 'Document JSON (texte)');
      notes.push(T('Plain-text JSON save (Unity games, e.g. Hollow Knight): values are identified by their path (e.g. playerData/grubsCollected). App reader “json”.', 'Sauvegarde en texte JSON (jeux Unity, ex. Hollow Knight) : les valeurs sont identifiées par leur chemin (ex. playerData/grubsCollected). Lecteur « json » de l\'app.'));
    }
  }
  if (!bh && !r.known && F.isGameMaker(buf)) {   // GameMaker INI / base64 INI / ds_list-ds_map hex lines
    try {
      const d = F.parseGameMaker(buf);
      r.known = r.reader = 'gamemaker'; r.meta = d.meta;
      r.format = T('GameMaker ', 'Sauvegarde GameMaker ') + (d.meta.format === 'ini' ? 'INI' : T('text', 'texte')) + (d.meta.encoding === 'base64' ? ' (base64)' : '') + ', ' + d.meta.keys + (d.meta.format === 'ini' ? T(' keys', ' clés') : T(' lines', ' lignes'));
      notes.push(T('GameMaker text save: INI values by section/key (e.g. Progress/Level), or lines[n]; ds_list / ds_map hex strings are decoded into lists / objects. App reader “gamemaker”.', 'Sauvegarde texte GameMaker : valeurs INI par section/clé (ex. Progress/Level), ou lines[n] ; les chaînes hex ds_list / ds_map sont décodées en listes / objets. Lecteur « gamemaker » de l\'app.'));
    } catch (e) { r.treeError = e.message; }
  }
  if (!bh && !r.known && F.isSgb(buf)) {
    r.known = r.reader = 'sgb';
    try {
      const d = F.parseSgb(buf);
      r.meta = d.meta;
      r.format = T('Supergiant save (SGB1 v', 'Sauvegarde Supergiant (SGB1 v') + d.meta.version + ', ' + d.meta.runs + T(' runs)', ' parties)');
      notes.push(T('Hades save: the game\'s Lua state (LZ4 + luabins), values identified by path (e.g. GameState/QuestStatus/WeaponClears). App reader “sgb”.', 'Sauvegarde Hades : l\'état Lua du jeu (LZ4 + luabins), valeurs identifiées par chemin (ex. GameState/QuestStatus/WeaponClears). Lecteur « sgb » de l\'app.'));
    } catch (e) { r.treeError = e.message; r.format = T('Supergiant save (SGB1) unreadable: ', 'Sauvegarde Supergiant (SGB1) illisible : ') + e.message; }
  }
  if (!bh && !r.known && (F.isGvas(buf) || F.isNrbf(buf))) {
    const gv = F.isGvas(buf);
    r.known = r.reader = gv ? 'gvas' : 'nrbf';
    try {
      const d = gv ? F.parseGvas(buf) : F.parseNrbf(buf);
      r.meta = d.meta;
      if (gv) {
        r.format = T('Unreal Engine ', 'Sauvegarde Unreal Engine ') + d.meta.engine + T(' save (GVAS, class ', ' (GVAS, classe ') + d.meta.saveClass + ')';
        notes.push(T('Standard Unreal save format: properties are identified by name (e.g. Achievements/BooksCollected[=E_Books::Book1]). App reader “gvas”.', 'Format standard des jeux Unreal : les propriétés sont identifiées par leur nom (ex. Achievements/BooksCollected[=E_Books::Book1]). Lecteur « gvas » de l\'app.'));
        if (d.meta.skipped) notes.push(d.meta.skipped + T(' value(s) could not be decoded and were replaced by null: ', ' valeur(s) non décodée(s), remplacées par null : ') + d.meta.why.slice(0, 3).join(' ; '));
      } else {
        r.format = T('.NET BinaryFormatter data (NRBF, root object ', 'Données .NET BinaryFormatter (NRBF, objet racine ') + (d.meta.rootClass.split('[')[0] || '?') + ')';
        notes.push(T('.NET serialization format used by some Unity games: fields are identified by name (e.g. flags[Key=Ending_A]/Value). App reader “nrbf”.', 'Format de sérialisation .NET utilisé par des jeux Unity : les champs sont identifiés par leur nom (ex. flags[Key=Ending_A]/Value). Lecteur « nrbf » de l\'app.'));
        if (d.meta.rootClass === 'System.String') notes.push(T('The file only holds a string: the real save is probably encrypted or encoded inside it (e.g. Hollow Knight on PC).', 'Le fichier ne contient qu\'une chaîne : la vraie sauvegarde est probablement chiffrée ou encodée dedans (ex. Hollow Knight sur PC).'));
      }
    } catch (e) {
      r.treeError = e.message;
      r.format = (gv ? T('Unreal save (GVAS)', 'Sauvegarde Unreal (GVAS)') : T('.NET data (NRBF)', 'Données .NET (NRBF)')) + T(' unreadable: ', ' illisible : ') + e.message;
    }
  }
  if (!bh && !r.known && F.isTheatrhythm(buf, path)) {   // encrypted: needs the password typed in the page
    let d = null;
    try { d = F.parseTheatrhythm(buf); } catch (e) { r.treeError = e.message; }
    if (d) {
      r.known = r.reader = 'theatrhythm'; r.meta = d.meta;
      r.format = T('THEATRHYTHM FINAL BAR LINE save (', 'Sauvegarde THEATRHYTHM FINAL BAR LINE (') + (d.meta.ps4 ? 'PS4' : 'Switch') + ', MessagePack ' + Math.round(d.meta.size / 1024) + ' KB, v' + d.meta.version + ')';
      notes.push(T('AES-128-CBC + raw DEFLATE + MessagePack. Values identified by path (e.g. MusicStageInfo/TotalAbilityCount). App reader “theatrhythm”; the console needs the password in sdmc:/switch/TrophyNX/keys/theatrhythm.txt.', 'AES-128-CBC + DEFLATE brut + MessagePack. Valeurs identifiées par chemin (ex. MusicStageInfo/TotalAbilityCount). Lecteur « theatrhythm » de l\'app ; la console a besoin du mot de passe dans sdmc:/switch/TrophyNX/keys/theatrhythm.txt.'));
    } else if (path) {
      r.known = 'theatrhythm'; r.format = T('THEATRHYTHM FINAL BAR LINE save (encrypted)', 'Sauvegarde THEATRHYTHM FINAL BAR LINE (chiffrée)');
      notes.push(T('Enter the save password (Theatrhythm field, step 1) to decode it: it is not bundled with the Save Lab.', 'Saisissez le mot de passe des sauvegardes (champ Theatrhythm, étape 1) pour la décoder : il n\'est pas fourni avec le Save Lab.'));
    }
  }
  if (!bh && !r.known && F.looksXorText(buf)) {
    let d = null;
    try { d = parseTree(buf); } catch (e) { d = null; }
    if (d && d.xorKey) {
      r.known = r.reader = d.reader; r.xorKey = d.xorKey;
      r.format = treeFormat(d.reader).label + T(' scrambled with XOR (repeating ', ' chiffré par XOR (clé répétée de ') + d.xorKey.length + T('-character key', ' caractères') + (d.xorKnown ? T(', known: ', ', connue : ') + d.xorKnown : T(', recovered automatically', ', retrouvée automatiquement')) + ')';
      notes.push(T('Key: “', 'Clé : « ') + d.xorKey.replace(/[^\x20-\x7e]/g, '·') + T('”. It is added to the definition (xor_key field): the app unscrambles the file before reading it.', ' ». Elle est ajoutée à la définition (champ xor_key) : l\'app déchiffre le fichier avant de le lire.'));
    }
  }
  if (!bh && !r.known) for (const k of KNOWN) if (k.test(buf)) { r.known = k.id; r.format = k.label; r.reader = k.reader; r.hash = k.hash || ''; if (k.magic) r.magic = k.magic; break; }
  for (const [m, label] of MAGICS) if (m.every((b, i) => buf[i] === b)) { r.container = label; break; }
  const head = asciiAt(buf, 0, 4);
  r.magicText = printable(head) ? head : '';
  r.entropy = entropy(buf);
  const block = Math.max(256, Math.ceil(buf.length / 256));
  r.blocks = [];
  let high = 0, zero = 0, counted = 0;
  // random bytes only reach ~8 - 255/(2n ln 2) bits/byte over n bytes (7.3 for 256): the "looks random"
  // threshold follows the block size, else small encrypted files were never flagged
  const highAt = (n) => Math.min(7.6, 8 - 255 / (2 * Math.min(n, 65536) * Math.LN2) - 0.35);
  for (let o = 0; o < buf.length; o += block) {
    const end = Math.min(buf.length, o + block);
    const e = entropy(buf, o, end);
    r.blocks.push(e);
    if (end - o >= 64) { counted++; if (e > highAt(end - o)) high++; }
  }
  for (let i = 0; i < buf.length; i++) if (!buf[i]) zero++;
  r.zeroRatio = buf.length ? zero / buf.length : 0;
  r.highBlocks = counted ? high / counted : 0;
  if (!r.known) {
    const ht = detectHashTable(buf);
    if (ht) {
      r.hashTable = ht;
      r.format = T('Probable hash/value table (8-byte entries from ', 'Table hash/valeur probable (entrées de 8 octets à partir de ') + hexOff(ht.start) + ')';
      notes.push(T('The keys look like hashes (CRC32/Murmur3) of flag names: paste candidate names into the dictionary to identify them.', 'Les clés ressemblent à des empreintes (CRC32/Murmur3) de noms de drapeaux : collez des noms candidats dans le dictionnaire pour les identifier.'));
    }
  }
  if (TREE_READERS.includes(r.known)) { /* structured */ }
  else if (r.container && /compress/.test(r.container)) {
    r.analyzable = false;
    notes.push(T('The file is compressed (', 'Le fichier est compressé (') + r.container + T('): offsets move with every save. The app cannot read this case yet.', ') : les offsets changent à chaque sauvegarde. L\'app ne sait pas encore lire ce cas.'));
  } else if (r.highBlocks > 0.85 && r.entropy > Math.min(7.8, 8 - 255 / (2 * buf.length * Math.LN2) - 0.2)) {
    r.analyzable = false;
    r.format = r.known ? r.format : T('Encrypted or compressed (entropy ', 'Chiffré ou compressé (entropie ') + r.entropy.toFixed(2) + T(' bits/byte)', ' bits/octet)');
    notes.push(T('Almost the whole file looks like random noise: it is probably encrypted (e.g. Pokémon, Animal Crossing) or compressed. Comparing offsets will give nothing until it is decrypted.', 'Presque tout le fichier ressemble à du bruit aléatoire : il est sans doute chiffré (ex. Pokémon, Animal Crossing) ou compressé. La comparaison d\'offsets ne donnera rien tant qu\'il n\'est pas déchiffré.'));
  } else if (r.highBlocks > 0.3) {
    notes.push(Math.round(r.highBlocks * 100) + T('% of the file has very high entropy (compressed or encrypted areas); the rest may be usable.', ' % du fichier a une entropie très élevée (zones compressées ou chiffrées) ; le reste peut être exploitable.'));
  }
  if (!r.known && !r.hashTable && r.analyzable) {
    r.format = r.magicText ? T('Binary with header “', 'Binaire à en-tête « ') + r.magicText + T('”', ' »') : T('Probably a fixed-layout binary', 'Binaire à structure fixe probable');
    r.reader = 'binary_le';
    if (r.magicText) r.magic = r.magicText;
    notes.push(T('Fits the “binary_le” reader: compare before/after captures to find counters and flags.', 'Format adapté au lecteur « binary_le » : comparez des captures avant/après pour trouver les compteurs et drapeaux.'));
  }
  r.strings = extractStrings(buf);
  return r;
}

// ------------------------------------------------------------------ hash tables (mirror of GamedataSave::parse)
function parseHashTable(buf, reader, start) {
  const map = new Map();
  let le = true;
  if (buf.length < 0x30) return null;
  if (reader === 'totk_progress') {
    if (rd(buf, 0, 4) !== 0x01020304) return null;
    for (let o = 0x28; o + 8 <= buf.length; o += 8) {
      const h = rd(buf, o, 4);
      if (h === 0xa3db7114) break;
      if (!map.has(h)) map.set(h, o + 4);
    }
  } else if (reader === 'gamedata_hash') {
    if (rd(buf, 4, 4) !== 0xFFFFFFFF) return null;
    if (rd(buf, 0, 4) >= 0x01000000) le = false;
    for (let o = 0x0C; o + 8 <= buf.length; o += 8) { const h = rd(buf, o, 4, le); if (!map.has(h)) map.set(h, o + 4); }
  } else {
    for (let o = start || 0; o + 8 <= buf.length; o += 8) { const h = rd(buf, o, 4); if (!map.has(h)) map.set(h, o + 4); }
  }
  return { le, get: (h) => map.has(h) ? rd(buf, map.get(h), 4, le) : undefined, keys: () => map.keys(), size: map.size, offsetOf: (h) => map.get(h) };
}


// ------------------------------------------------------------------ BYML (Super Mario Odyssey File*.bin, ...)
// Mirrors source/byml.cpp: document at offset 0 or 0x10, "YB" (LE) or "BY" (BE).
function findByml(buf) {
  for (const b of [0, 0x10]) {
    if (buf.length < b + 0x10) continue;
    const le = buf[b] === 0x59 && buf[b + 1] === 0x42, be = buf[b] === 0x42 && buf[b + 1] === 0x59;
    if (!le && !be) continue;
    const ver = le ? buf[b + 2] | (buf[b + 3] << 8) : (buf[b + 2] << 8) | buf[b + 3];
    if (ver >= 1 && ver <= 10) return { base: b, le, ver };
  }
  return null;
}
function parseByml(buf) {
  const h = findByml(buf);
  if (!h) return null;
  const { base, le } = h;
  const u32 = (o) => rd(buf, o, 4, le) ?? 0;
  const u24 = (o) => le ? (buf[o] | (buf[o + 1] << 8) | (buf[o + 2] << 16)) : ((buf[o] << 16) | (buf[o + 1] << 8) | buf[o + 2]);
  const dec = new TextDecoder();
  const strtab = (off) => {
    if (!off) return [];
    const o = base + off;
    if (buf[o] !== 0xC2) throw new Error(T('invalid string table', 'table de chaînes invalide'));
    const n = u24(o + 1), out = [];
    for (let i = 0; i < n; i++) {
      const s = o + u32(o + 4 + 4 * i);
      let e = s; while (e < buf.length && buf[e]) e++;
      out.push(dec.decode(buf.subarray(s, e)));
    }
    return out;
  };
  const keys = strtab(u32(base + 4)), strs = strtab(u32(base + 8));
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  let budget = 2e6;
  const val = (t, v, depth) => {
    switch (t) {
      case 0xA0: return strs[v] ?? '';
      case 0xD0: return !!v;
      case 0xD1: return v | 0;
      case 0xD2: { const b = new DataView(new ArrayBuffer(4)); b.setUint32(0, v, true); return +b.getFloat32(0, true).toFixed(6); }
      case 0xD3: return v >>> 0;
      case 0xD4: return Number(dv.getBigInt64(base + v, le));
      case 0xD5: return Number(dv.getBigUint64(base + v, le));
      case 0xD6: return dv.getFloat64(base + v, le);
      case 0xC0: case 0xC1: return node(v, depth + 1);
      default: return null;
    }
  };
  const node = (off, depth) => {
    if (depth > 64 || --budget < 0) throw new Error(T('BYML document nested too deeply', 'document BYML trop profond'));
    const o = base + off, t = buf[o], n = u24(o + 1);
    if (t === 0xC0) {
      const vo = o + 4 + ((n + 3) & ~3), arr = new Array(n);
      for (let i = 0; i < n; i++) arr[i] = val(buf[o + 4 + i], u32(vo + 4 * i), depth);
      return arr;
    }
    if (t === 0xC1) {
      const r = {};
      for (let i = 0; i < n; i++) { const e = o + 4 + 8 * i; r[keys[u24(e)]] = val(buf[e + 3], u32(e + 4), depth); }
      return r;
    }
    throw new Error(T('unknown BYML node 0x', 'nœud BYML inconnu 0x') + t.toString(16));
  };
  return Object.assign(h, { tree: node(u32(base + 12), 0) });
}
// Plain-text JSON saves (Unity games on Switch, e.g. Hollow Knight user1.dat). Mirrors source/json_doc.cpp:
// optional UTF-8 BOM, trailing NULs/whitespace ignored, must start with '{' or '['.
function parseJson(buf) {
  let s = 0, e = buf.length;
  if (buf[0] === 0xEF && buf[1] === 0xBB && buf[2] === 0xBF) s = 3;
  while (s < e && (buf[s] === 0x20 || buf[s] === 0x09 || buf[s] === 0x0A || buf[s] === 0x0D)) s++;
  while (e > s && (buf[e - 1] === 0 || buf[e - 1] === 0x20 || buf[e - 1] === 0x0A || buf[e - 1] === 0x0D || buf[e - 1] === 0x09)) e--;
  if (s >= e || (buf[s] !== 0x7B && buf[s] !== 0x5B)) return null;
  const text = new TextDecoder('utf-8', { fatal: true }).decode(buf.subarray(s, e));
  let tree;
  try { tree = JSON.parse(text); }
  catch (err) {
    // Like cJSON in the app: accept data after the document (e.g. Paper Mario's trailing checksum "2de37ed9").
    const close = text[0] === '{' ? '}' : ']';
    const end = text.lastIndexOf(close);
    if (end <= 0) throw err;
    tree = JSON.parse(text.slice(0, end + 1));
  }
  return tree && typeof tree === 'object' ? { json: true, tree: coerceJson(tree) } : null;
}
// Values saved as text ("True", "12", "3.5") read as booleans / numbers, like json_doc.cpp in the app.
const NUM_TEXT = /^-?\d{1,15}(\.\d{1,9})?$/;
function coerceJson(x) {
  if (typeof x === 'string') { const l = x.toLowerCase(); return l === 'true' ? true : l === 'false' ? false : NUM_TEXT.test(x) ? +x : x; }
  if (Array.isArray(x)) { for (let i = 0; i < x.length; i++) x[i] = coerceJson(x[i]); return x; }
  if (x && typeof x === 'object') { for (const k of Object.keys(x)) x[k] = coerceJson(x[k]); return x; }
  return x;
}
function looksJson(buf) {
  let s = buf[0] === 0xEF && buf[1] === 0xBB && buf[2] === 0xBF ? 3 : 0;
  while (s < buf.length && (buf[s] === 0x20 || buf[s] === 0x09 || buf[s] === 0x0A || buf[s] === 0x0D)) s++;
  return buf[s] === 0x7B || buf[s] === 0x5B;
}
// Path-addressed documents. Each module turns a save into a tree (objects, lists, numbers, strings);
// `reader` is the name of the matching reader in the app. Order matters: the first match wins.
const TREE_FORMATS = [
  { reader: 'byml', label: 'BYML', parse: parseByml },
  { reader: 'gvas', label: 'GVAS (Unreal)', parse: (b) => F.parseGvas(b) },
  { reader: 'nrbf', label: 'NRBF (.NET / Unity)', parse: (b) => F.parseNrbf(b) },
  { reader: 'sgb', label: 'Supergiant SGB1 (Hades)', parse: (b) => F.parseSgb(b) },
  { reader: 'deltarune', label: 'DELTARUNE (deltarune.sav)', parse: (b) => F.parseDeltarune(b) },
  { reader: 'theatrhythm', label: 'THEATRHYTHM FINAL BAR LINE (AES + DEFLATE + MessagePack)', parse: (b) => F.parseTheatrhythm(b) },
  { reader: 'es3', label: 'Easy Save 3 (Unity, AES)', parse: (b) => F.parseEs3(b) },
  { reader: 'rpgmaker', label: 'RPG Maker MV/MZ', parse: (b, explicit) => { if (!explicit && !F.isRpgMaker(b)) return null; try { return F.parseRpgMaker(b); } catch (e) { if (explicit) throw e; return null; } } },
  { reader: 'json', label: 'JSON', parse: parseJson },
  { reader: 'gamemaker', label: 'GameMaker (INI / text)', parse: (b, explicit) => { if (!explicit && !F.isGameMaker(b)) return null; try { return F.parseGameMaker(b); } catch (e) { if (explicit) throw e; return null; } } },
];
const TREE_READERS = TREE_FORMATS.map(f => f.reader);
function treeFormat(reader) { return TREE_FORMATS.find(f => f.reader === reader) || null; }
// opts.xorKey: undo a repeating-key XOR first (definition field "xor_key", as the app does);
// opts.guess (default true): if nothing matches, try known / recovered XOR keys (Save Lab only).
function parseTree(buf, reader, opts = {}) {
  if (opts.xorKey) buf = F.xorBytes(buf, opts.xorKey);
  for (const f of TREE_FORMATS) {
    if (reader && f.reader !== reader) continue;
    const d = f.parse(buf, !!reader);
    if (d) { d.reader = f.reader; if (opts.xorKey) d.xorKey = opts.xorKey; return d; }
  }
  if (opts.xorKey || opts.guess === false) return null;
  let found = null;
  const u = F.unwrapXor(buf, (data) => (found = parseTree(data, reader, { guess: false })));
  if (!u || !found) return null;
  return Object.assign(found, { xorKey: u.key, xorKnown: u.known });
}
const ID_FIELDS = ['UniqueId', 'Name', 'Id', 'ObjId', 'Key'];
const isObj = (x) => x && typeof x === 'object' && !Array.isArray(x);
// Flattens a tree into Map(path -> number|string). Lists of strings become membership leaves
// "List[=Value]" -> 1; lists of dictionaries with a unique id field use "List[UniqueId=205]".
function flattenByml(tree) {
  const out = new Map();
  const walk = (x, p) => {
    if (Array.isArray(x)) {
      if (x.length && x.every(v => typeof v === 'string')) { for (const v of x) if (v) out.set(p + '[=' + v + ']', 1); return; }
      let idf = null;
      if (x.length && x.every(isObj)) {
        for (const f of ID_FIELDS) {
          const ids = x.map(v => v[f]);
          if (ids.every(v => (typeof v === 'number' || (typeof v === 'string' && v)) && !String(v).includes(']')) && new Set(ids).size === ids.length) { idf = f; break; }
        }
      }
      x.forEach((v, i) => walk(v, p + (idf ? '[' + idf + '=' + v[idf] + ']' : '[' + i + ']')));
      return;
    }
    if (isObj(x)) { for (const k of Object.keys(x)) walk(x[k], p ? p + '/' + k : k); return; }
    out.set(p, typeof x === 'boolean' ? (x ? 1 : 0) : x);
  };
  walk(tree, '');
  return out;
}
// Path query with the same syntax and semantics as BymlDoc::query (source/byml.cpp).
function queryByml(tree, path) {
  const atoms = [];
  for (const seg of path.split('/')) {
    if (!seg) continue;
    const m = /^([^[]*)((?:\[[^\]]*\])*)$/.exec(seg);
    if (!m) return null;
    if (m[1]) atoms.push(m[1]);
    for (const sel of m[2].match(/\[[^\]]*\]/g) || []) atoms.push(sel);
  }
  if (!atoms.length) return null;
  const eqv = (v, want) => typeof v === 'string' ? v === want : (want === 'true' ? v === true || v === 1 : want === 'false' ? v === false || v === 0 : want !== '' && !isNaN(+want) && +v === +want);
  const out = [];
  const walk = (n, i) => {
    if (i === atoms.length) { out.push(n); return; }
    const a = atoms[i];
    if (a[0] !== '[') { if (isObj(n) && a in n) walk(n[a], i + 1); return; }
    const sel = a.slice(1, -1);
    if (sel === '*' && isObj(n)) { Object.values(n).forEach(c => walk(c, i + 1)); return; }   // every value of an object (Lua tables, dictionaries)
    if (!Array.isArray(n)) return;
    if (sel === '*') { n.forEach(c => walk(c, i + 1)); return; }
    const eq = sel.indexOf('=');
    if (eq < 0) { if (/^\d+$/.test(sel) && +sel < n.length) walk(n[+sel], i + 1); return; }
    const f = sel.slice(0, eq), want = sel.slice(eq + 1);
    for (const c of n) {
      if (!f) { if (eqv(c, want)) walk(c, i + 1); }
      else if (isObj(c) && f in c && eqv(c[f], want)) walk(c, i + 1);
    }
  };
  walk(tree, 0);
  return out;
}
const leafNum = (v) => typeof v === 'boolean' ? (v ? 1 : 0) : typeof v === 'string' ? (v ? 1 : 0) : Array.isArray(v) ? v.length : isObj(v) ? Object.keys(v).length : typeof v === 'number' ? v : 0;
const truthy = (v) => leafNum(v) !== 0;
function bymlRuleValue(rule, tree, eqStr) {
  let total = 0, best = 0, any = false, found = 0;
  for (const p of rule.paths) {
    const vals = queryByml(tree, p);
    if (!vals || !vals.length) continue;
    found++;
    for (const v of vals) {
      let x;
      if (eqStr !== undefined) {
        x = typeof v === 'string' ? (v === eqStr ? 1 : 0) : eqStr === 'true' ? (leafNum(v) === 1 ? 1 : 0) : eqStr === 'false' ? (leafNum(v) === 0 ? 1 : 0) : (leafNum(v) === +eqStr ? 1 : 0);
      } else x = leafNum(v);
      if (rule.type === 'count') total += eqStr !== undefined ? x : (truthy(v) ? 1 : 0);
      else if (rule.type === 'sum') total += x;
      else if (!any || x > best) best = x;
      any = true;
    }
  }
  return { value: rule.type === 'flag' || rule.type === 'value' ? best : total, found };
}
// "requires": first condition { path, min?, below? } that fails on this tree (null: all hold). Value = largest at the
// path, missing = 0 (same as the app).
function failedRequire(reqs, tree) {
  for (const c of reqs || []) {
    const vals = queryByml(tree, c.path) || [];
    const v = vals.length ? Math.max(...vals.map(leafNum)) : 0;
    if ((c.min !== undefined && v < c.min) || (c.below !== undefined && !(v < c.below))) return c;
  }
  return null;
}
const describeRequire = (c) => c.path + (c.min !== undefined ? ' >= ' + c.min : '') + (c.below !== undefined ? ' < ' + c.below : '');
function patternOf(path) { return path.replace(/\[[^\]]*\]/g, '[*]'); }
function isTimestampPath(p, vals) { return /Time(Total)?$|Date$|IdFor/i.test(p) || vals.some(v => typeof v === 'number' && v > 1e9); }
function diffByml(flats, opts = {}) {
  const controls = opts.controls || [];
  const all = new Set();
  for (const f of flats) for (const k of f.keys()) all.add(k);
  const out = [];
  let noiseCount = 0;
  for (const p of all) {
    const member = /\[=[^\]]*\]$/.test(p);
    const vals = flats.map(f => f.has(p) ? f.get(p) : (member ? 0 : undefined));
    if (vals.every(v => v === vals[0])) continue;
    if (vals.some(v => v === undefined)) {
      if (vals[vals.length - 1] !== undefined) out.push({ path: p, vals, kind: 'new', score: 1 });
      continue;
    }
    if (vals.some(v => typeof v === 'string')) { out.push({ path: p, vals, kind: 'variable', score: 0.2, text: true }); continue; }
    let cls = classify(vals, controls);
    if (!cls) continue;
    if (cls.kind === 'noise') { noiseCount++; if (!opts.showNoise) continue; }
    else if (isTimestampPath(p, vals)) cls = { kind: 'timestamp', score: 0.1 };
    out.push({ path: p, vals, kind: cls.kind, score: cls.score, float: vals.some(v => !Number.isInteger(v)) });
  }
  out.sort((a, b) => b.score - a.score || (a.path < b.path ? -1 : 1));
  return { candidates: out, total: out.length, noiseCount, mode: 'byml' };
}
function searchValueByml(flats, known) {
  const idx = known.map((v, i) => v === null || v === '' || v === undefined ? -1 : i).filter(i => i >= 0);
  const res = [];
  if (!idx.length) return res;
  for (const [p] of flats[idx[0]]) {
    if (idx.every(i => { const v = flats[i].get(p); return typeof v === 'number' && Math.abs(v - +known[i]) <= (Number.isInteger(+known[i]) ? 0 : 0.01); })) {
      res.push({ path: p, vals: flats.map(f => f.get(p)) });
      if (res.length >= 500) break;
    }
  }
  return res;
}
// Generalises selected byml candidates to their pattern ("ShineData[UniqueId=205]/IsGet" -> "ShineData[*]/IsGet").
function bymlGroupRule(tree, paths) {
  const pats = [...new Set(paths.map(p => /\[=[^\]]*\]$/.test(p) ? p.replace(/\[=[^\]]*\]$/, '[*]') : patternOf(p)))];
  const total = pats.reduce((n, p) => n + (queryByml(tree, p) || []).length, 0);
  return { paths: pats, total };
}

// ------------------------------------------------------------------ series classification
// vals: numbers (or undefined when missing). controls: indices i where snapshot i is a "control"
// (nothing was done in game since snapshot i-1) — any change there means noise.
function classify(vals, controls = []) {
  const v = vals;
  let changed = false, nonDec = true, nonInc = true, bitsOnly = true, binary = true;
  for (let i = 0; i < v.length; i++) {
    if (v[i] === undefined || v[i] === null) return null;
    if (v[i] !== 0 && v[i] !== 1) binary = false;
    if (i) {
      if (v[i] !== v[i - 1]) changed = true;
      if (v[i] < v[i - 1]) nonDec = false;
      if (v[i] > v[i - 1]) nonInc = false;
      if (((v[i] & v[i - 1]) >>> 0) !== (v[i - 1] >>> 0)) bitsOnly = false;
    }
  }
  if (!changed) return null;
  for (const c of controls) if (c > 0 && c < v.length && v[c] !== v[c - 1]) return { kind: 'noise', score: 0 };
  if (binary && nonDec) return { kind: 'flag', score: 5 };
  // bits need evidence: a step that adds bits to an already non-zero value (from 0 anything looks like bits)
  const bitSteps = v.some((x, i) => i && v[i - 1] !== 0 && x !== v[i - 1]);
  if (nonDec && bitsOnly && bitSteps && v[v.length - 1] > 1 && popcount(v[v.length - 1]) > 1 && !isSmallStep(v)) return { kind: 'bits', score: 4 };
  if (nonDec) return { kind: 'counter', score: 3 };
  if (nonInc) return { kind: 'decreasing', score: 1 };
  return { kind: 'variable', score: 0.5 };
}
function popcount(x) { let n = 0; x >>>= 0; while (x) { n += x & 1; x >>>= 1; } return n; }
function isSmallStep(v) { return v.every((x, i) => !i || x - v[i - 1] <= 3); }

const KIND_LABEL = {
  flag: T('Flag 0→1', 'Drapeau 0→1'), bits: T('Bit field (bits added)', 'Champ de bits (bits ajoutés)'), counter: T('Rising counter', 'Compteur croissant'),
  decreasing: T('Decreasing', 'Décroissant'), variable: T('Variable (noise?)', 'Variable (bruit ?)'), noise: T('Noise (changes on its own)', 'Bruit (change sans action)'), float: T('Float', 'Flottant'), timestamp: T('Timestamp / clock', 'Horodatage / horloge'), new: T('New entry', 'Nouvelle entrée'),
};

// ------------------------------------------------------------------ binary diff
// snaps: Uint8Array[] (same file across captures, oldest first).
function diffBinary(snaps, opts = {}) {
  const controls = opts.controls || [];
  const limit = opts.limit || 4000;
  const len = Math.min(...snaps.map(s => s.length));
  const out = [];
  const n = snaps.length;
  // 1) bytes that changed anywhere
  const changedWords = [];
  for (let w = 0; w < len; w += 4) {
    let ch = 0;
    for (let b = 0; b < 4 && w + b < len; b++) {
      const x = snaps[0][w + b];
      for (let s = 1; s < n; s++) if (snaps[s][w + b] !== x) { ch |= 1 << b; break; }
    }
    if (ch) changedWords.push([w, ch]);
  }
  let noiseCount = 0;
  const push = (off, size, vals, cls, extra) => {
    if (!cls) return;
    if (cls.kind === 'noise') { noiseCount++; if (!opts.showNoise) return; }
    out.push(Object.assign({ off, size, vals, kind: cls.kind, score: cls.score }, extra || {}));
  };
  for (const [w, ch] of changedWords) {
    // u32 interpretation when the high bytes take part in a plausible integer
    const v32 = snaps.map(s => rd(s, w, 4));
    const c32 = classify(v32, controls);
    const max32 = Math.max(...v32);
    if (c32 && (ch & 0b1100) && (ch & 0b0011) && (c32.kind === 'counter' || c32.kind === 'bits' || c32.kind === 'flag' || c32.kind === 'noise') && max32 < 0x40000000) {
      push(w, 4, v32, c32); continue;
    }
    // float
    if (ch & 0b1100) {
      const f = snaps.map(s => rdF32(s, w));
      const plausible = f.every(x => isFinite(x) && (x === 0 || (Math.abs(x) > 1e-3 && Math.abs(x) < 1e7)));
      if (plausible && f.some(x => x !== Math.round(x))) {
        const cf = classify(f, controls);
        if (cf) { push(w, 4, f.map(x => +x.toFixed(4)), cf.kind === 'noise' ? cf : { kind: 'float', score: cf.kind === 'counter' ? 1.5 : 0.3 }, { float: true, trend: cf.kind }); continue; }
      }
    }
    // u16 halves
    let done = 0;
    for (const half of [0, 2]) {
      const mask = half ? 0b1100 : 0b0011;
      if (!(ch & mask)) { done |= mask; continue; }
      const lowFlag = classify(snaps.map(s => s[w + half]), controls);
      if (!(lowFlag && lowFlag.kind === 'flag' && !(ch & (1 << (half + 1))))) {
        const v16 = snaps.map(s => rd(s, w + half, 2));
        const c16 = classify(v16, controls);
        if (c16 && c16.kind !== 'variable' && Math.max(...v16) > 255) {
          // a counter whose upper half is always zero is most likely a u32: read it as one (survives > 65535)
          if (!half && c16.kind === 'counter' && zeroIn(snaps, w + 2, 2)) { push(w, 4, v16, c16); done = 0b1111; break; }
          push(w + half, 2, v16, c16); done |= mask;
        }
      }
    }
    // remaining single bytes
    for (let b = 0; b < 4; b++) {
      if (!(ch & (1 << b)) || (done & (1 << b))) continue;
      const v8 = snaps.map(s => s[w + b]);
      const c8 = classify(v8, controls);
      if (!b && c8 && c8.kind === 'counter' && Math.max(...v8) > 1 && zeroIn(snaps, w + 1, 3)) { push(w, 4, v8, c8); break; }
      push(w + b, 1, v8, c8);
    }
  }
  out.sort((a, b) => b.score - a.score || a.off - b.off);
  return { candidates: out.slice(0, limit), total: out.length, changedWords: changedWords.length, noiseCount, length: len };
}

// ------------------------------------------------------------------ hash-table diff
function diffHash(tables, opts = {}) {
  const controls = opts.controls || [];
  const all = new Set();
  for (const t of tables) for (const k of t.keys()) all.add(k);
  const out = [];
  let noiseCount = 0;
  for (const h of all) {
    const vals = tables.map(t => t.get(h));
    if (vals.some(v => v === undefined)) {
      if (vals[0] === undefined && vals[vals.length - 1] !== undefined) out.push({ hash: h, vals, kind: 'new', score: 2 });
      continue;
    }
    const signed = vals.map(v => v | 0);
    let cls = classify(signed, controls);
    if (!cls) continue;
    if (cls.kind === 'noise') { noiseCount++; if (!opts.showNoise) continue; }
    // floats stored as raw u32
    const asF = vals.map(v => { const dv = new DataView(new ArrayBuffer(4)); dv.setUint32(0, v, true); return dv.getFloat32(0, true); });
    const isF = cls.kind === 'variable' && asF.every(x => isFinite(x) && (x === 0 || Math.abs(x) > 1e-3 && Math.abs(x) < 1e7)) && asF.some(x => x !== Math.round(x));
    if (isF) { const cf = classify(asF, controls); cls = { kind: 'float', score: cf && cf.kind === 'counter' ? 1.5 : 0.3 }; }
    out.push({ hash: h, vals: isF ? asF.map(x => +x.toFixed(3)) : signed, kind: cls.kind, score: cls.score, float: isF });
  }
  out.sort((a, b) => b.score - a.score || a.hash - b.hash);
  return { candidates: out, total: out.length, noiseCount };
}

// ------------------------------------------------------------------ search by known value (Cheat Engine style)
// known: array (one entry per snapshot) of numbers or null (unknown). type: u8|u16|u32|f32|auto
function searchValue(snaps, known, type = 'auto', opts = {}) {
  const idx = known.map((v, i) => v === null || v === '' || v === undefined ? -1 : i).filter(i => i >= 0);
  if (!idx.length) return [];
  const types = type === 'auto' ? ['u8', 'u16', 'u32', 'f32'] : [type];
  const len = Math.min(...snaps.map(s => s.length));
  const res = [];
  const limit = opts.limit || 500;
  for (const t of types) {
    const size = t === 'u8' ? 1 : t === 'u16' ? 2 : 4;
    const step = opts.unaligned ? 1 : size;
    const first = idx[0], want = +known[first];
    if (t !== 'f32' && (want < 0 || want !== Math.floor(want) || want >= 2 ** (8 * size))) continue;
    for (let o = 0; o + size <= len; o += step) {
      let ok = true;
      for (const i of idx) {
        const w = +known[i];
        const v = t === 'f32' ? rdF32(snaps[i], o) : rd(snaps[i], o, size);
        if (t === 'f32' ? !(Math.abs(v - w) <= Math.max(0.01, Math.abs(w) * 1e-4)) : v !== w) { ok = false; break; }
      }
      if (ok) {
        // a value that never changes across captures is a weak match when several values were given
        res.push({ off: o, size, type: t, float: t === 'f32', vals: snaps.map(s => t === 'f32' ? +rdF32(s, o).toFixed(4) : rd(s, o, size)) });
        if (res.length >= limit) return res;
      }
    }
  }
  return res;
}
function searchValueHash(tables, known) {
  const idx = known.map((v, i) => v === null || v === '' || v === undefined ? -1 : i).filter(i => i >= 0);
  const res = [];
  if (!idx.length) return res;
  for (const h of tables[idx[0]].keys()) {
    let ok = true;
    for (const i of idx) {
      const raw = tables[i].get(h);
      if (raw === undefined) { ok = false; break; }
      const w = +known[i];
      const dv = new DataView(new ArrayBuffer(4)); dv.setUint32(0, raw, true);
      const f = dv.getFloat32(0, true);
      if ((raw | 0) !== w && !(Math.abs(f - w) <= Math.max(0.01, Math.abs(w) * 1e-4))) { ok = false; break; }
    }
    if (ok) {
      const isInt = idx.every(i => (tables[i].get(h) | 0) === +known[i]);
      res.push({ hash: h, float: !isInt, vals: tables.map(t => { const r = t.get(h); if (r === undefined) return undefined; if (isInt) return r | 0; const dv = new DataView(new ArrayBuffer(4)); dv.setUint32(0, r, true); return +dv.getFloat32(0, true).toFixed(3); }) });
    }
    if (res.length >= 500) break;
  }
  return res;
}

// ------------------------------------------------------------------ flag series extension
// Given a few flag offsets (same size, regular stride), walk outwards while every capture holds 0/1.
function extendSeries(snaps, offsets, size, opts = {}) {
  const sorted = [...new Set(offsets)].sort((a, b) => a - b);
  let stride = opts.stride || 0;
  if (!stride) {
    if (sorted.length < 2) return { offsets: sorted, stride: 0 };
    stride = sorted[1] - sorted[0];
    for (let i = 2; i < sorted.length; i++) stride = gcd(stride, sorted[i] - sorted[i - 1]);
  }
  if (stride < size) stride = size;
  const maxZeroRun = opts.maxZeroRun ?? 8, maxCount = opts.maxCount || 1024;
  const len = Math.min(...snaps.map(s => s.length));
  const val = (o) => (o < 0 || o + size > len) ? -1 : snaps.reduce((m, s) => { const v = rd(s, o, size); return v > 1 || m < 0 ? -1 : Math.max(m, v); }, 0);
  const set = new Set(sorted);
  for (let o = sorted[0]; o <= sorted[sorted.length - 1]; o += stride) if (val(o) >= 0) set.add(o);
  for (const dir of [-1, 1]) {
    let o = dir < 0 ? sorted[0] : sorted[sorted.length - 1], zeros = 0;
    for (;;) {
      o += dir * stride;
      const v = val(o);
      if (v < 0 || set.size >= maxCount) break;
      if (v === 0 && ++zeros > maxZeroRun) break;
      if (v === 1) zeros = 0;
      set.add(o);
    }
  }
  return { offsets: [...set].sort((a, b) => a - b), stride };
}
function zeroIn(snaps, off, n) { return snaps.every(s => { for (let i = 0; i < n; i++) if (s[off + i]) return false; return true; }); }
function gcd(a, b) { while (b) [a, b] = [b, a % b]; return Math.abs(a); }

// ------------------------------------------------------------------ dictionary
function matchDictionary(names, keys) {
  const keySet = new Set(keys);
  const found = new Map();
  const algos = Object.keys(HASHES);
  const stats = Object.fromEntries(algos.map(a => [a, 0]));
  for (let name of names) {
    name = name.trim();
    if (!name) continue;
    for (const a of algos) {
      const h = HASHES[a](name);
      if (keySet.has(h) && !found.has(h)) { found.set(h, { name, algo: a }); stats[a]++; }
    }
  }
  return { found, stats };
}

// ------------------------------------------------------------------ rule evaluation (mirror of the app)
function cFormat(pattern, i) {
  return pattern.replace(/%(0?)(\d*)d/, (m, z, w) => { const s = String(i); return w ? s.padStart(+w, z ? '0' : ' ') : s; });
}
function parseHashOrName(s, fn) {
  if (typeof s === 'string' && /^0x/i.test(s)) return parseInt(s, 16) >>> 0;
  return fn(String(s));
}
function parseOff(v) {
  if (typeof v === 'number') return v;
  if (typeof v === 'string') { const s = v.trim(); return /^0x/i.test(s) ? parseInt(s, 16) : /^0[0-7]+$/.test(s) ? parseInt(s, 8) : parseInt(s, 10) || 0; }
  return 0;
}
function compileRule(r, fn) {
  const rule = { type: r.type, hashes: [], offsets: [], size: 4, target: r.target ?? 1, max: r.max || 0, isFloat: !!r.float };
  const single = r.name || r.hash;
  if (single) rule.hashes.push(parseHashOrName(single, fn));
  if (Array.isArray(r.names)) for (const n of r.names) rule.hashes.push(parseHashOrName(n, fn));
  if (r.pattern) for (let i = r.from || 0; i <= (r.to ?? -1); i++) rule.hashes.push(fn(cFormat(r.pattern, i)));
  if (r.equals !== undefined) { rule.hasEquals = true; rule.equals = typeof r.equals === 'number' ? r.equals >>> 0 : parseHashOrName(r.equals, fn); }
  if (r.offset !== undefined) rule.offsets.push(parseOff(r.offset));
  if (Array.isArray(r.offsets)) for (const o of r.offsets) rule.offsets.push(parseOff(o));
  rule.size = [1, 2, 4].includes(r.size) ? r.size : 4;
  rule.mask = r.mask !== undefined ? parseOff(r.mask) >>> 0 : 0;   // bit flags: value -> 1 when any bit of mask is set
  rule.masks = Array.isArray(r.masks) ? r.masks.map(m => parseOff(m) >>> 0) : [];   // one mask per offset
  rule.bits = !!r.bits;   // value -> its number of set bits (within mask, if any), same as the app
  // needs: the rule only counts when the value at needs.offset is >= needs.min (same as the app)
  if (r.needs && typeof r.needs === 'object') rule.needs = { offset: parseOff(r.needs.offset), size: [1, 2, 4].includes(r.needs.size) ? r.needs.size : 4, min: r.needs.min ?? 1 };
  rule.file = typeof r.file === 'string' ? r.file : '';   // only this save file (and its slots) counts
  return rule;
}
const SAVE_TYPES = ['flag', 'value', 'count', 'sum'];
// def: game JSON; files: [{path, data}] of ONE capture. Returns per achievement {value, met, note}.
function evaluate(def, files, opts = {}) {
  const reader = def.reader || '';
  const fn = reader === 'totk_progress' ? murmur3 : crc32;
  const res = [];
  // opts.fallback: the analysed file, used when no save_files name matches (renamed captures)
  const anyNamed = (def.save_files || []).some(p => files.some(x => x.path === p || x.path.endsWith('/' + p)));
  const pick = (paths) => {
    for (const p of paths || []) { const f = files.find(x => x.path === p || x.path.endsWith('/' + p)); if (f) return f; }
    if (!anyNamed && opts.fallback && paths && paths.length && paths[0] === (def.save_files || [])[0]) return opts.fallback;
    return null;
  };
  if (reader === 'binary_le') {
    // every recognised file (save slots) counts: each achievement takes its best value over them (same as the app)
    const files = [];
    let why = '';
    for (const p of def.save_files || []) {
      const f = pick([p]);
      if (!f || files.some(x => x.data === f.data)) continue;
      if (def.file_size && f.data.length !== def.file_size) { why = p + T(': size ', ' : taille ') + f.data.length + ' ≠ ' + def.file_size; continue; }
      if (def.magic && asciiAt(f.data, 0, def.magic.length) !== def.magic) { why = p + T(': header differs from “', ' : en-tête différent de « ') + def.magic + T('”', ' »'); continue; }
      const L = def.slots && def.slots[p];
      if (!L) { files.push(Object.assign({}, f, { file: p, base: 0, size: f.data.length })); continue; }
      // "slots": one file holding save slots at base + i * stride; each valid slot is read like a file (same as the app)
      const base = parseOff(L.base), stride = parseOff(L.stride), count = parseOff(L.count);
      for (let i = 0; i < count && stride > 0; i++) {
        const start = base + i * stride;
        if (start >= f.data.length) break;
        const len = Math.min(stride, f.data.length - start);
        if (L.magic && (L.magic.length > len || asciiAt(f.data, start, L.magic.length) !== L.magic)) continue;
        if (L.check && typeof L.check === 'object') {
          const co = parseOff(L.check.offset), cs = [1, 2, 4].includes(L.check.size) ? L.check.size : 4;
          if (co + cs > len) continue;
          const v = rd(f.data, start + co, cs);
          if (v < (L.check.min ?? 1) || (L.check.max > 0 && v > L.check.max)) continue;
        }
        files.push({ path: p + ' slot ' + i, data: f.data, file: p, base: start, size: len });
      }
    }
    for (const a of def.achievements || []) {
      const t = a.rule && a.rule.type;
      if (!SAVE_TYPES.includes(t)) { res.push({ id: a.id, skip: true }); continue; }
      if (!files.length) { res.push({ id: a.id, value: 0, met: false, note: why || T('none of the save_files was found', 'aucun fichier de save_files trouvé') }); continue; }
      const r = compileRule(a.rule, fn);
      let best = 0, bestFile = null, anyBad = false, skipNote = '', allSkipped = true;
      for (const file of files) {
        if (r.file && r.file !== file.file) continue;
        if (r.needs) {
          const nv = r.needs.offset + r.needs.size <= file.size ? rd(file.data, file.base + r.needs.offset, r.needs.size) : 0;
          if (nv < r.needs.min) { skipNote = T('not in this save: needs ', 'absent de cette sauvegarde : il faut ') + r.needs.min + T(', has ', ', a ') + nv; continue; }
        }
        allSkipped = false;
        let total = 0, bad = false;
        for (let oi = 0; oi < r.offsets.length; oi++) {
          const o = r.offsets[oi], mask = oi < r.masks.length ? r.masks[oi] : r.mask;
          if (o + r.size > file.size) { bad = true; continue; }
          let v = rd(file.data, file.base + o, r.size);
          if (r.max > 0 && v > r.max) { bad = true; continue; }
          if (r.bits) { let x = (mask ? v & mask : v) >>> 0, c = 0; while (x) { c += x & 1; x >>>= 1; } v = c; }
          else if (mask) v = (v & mask) ? 1 : 0;
          if (t === 'count') total += v ? 1 : 0; else total += v;
        }
        if (bad) { anyBad = true; continue; }
        if (!bestFile || total > best) { best = total; bestFile = file; }
      }
      const note = anyBad ? T('value out of range (offset moved?)', 'valeur hors limites (offset déplacé ?)') : allSkipped ? skipNote : '';
      res.push({ id: a.id, value: best, met: best + 1e-9 >= r.target, note, file: bestFile && files.length > 1 ? bestFile.path : undefined });
    }
    return { file: files.length ? files.map(f => f.path).join(', ') : null, results: res };
  }
  if (TREE_READERS.includes(reader)) {
    const docs = [];
    for (const p of def.save_files || []) { const f = pick([p]); if (!f) continue; try { const d = parseTree(f.data, reader, { xorKey: def.xor_key || '', guess: false }); if (d) docs.push(d.tree); } catch (e) { } }
    for (const a of def.achievements || []) {
      const t = a.rule && a.rule.type;
      if (!SAVE_TYPES.includes(t)) { res.push({ id: a.id, skip: true }); continue; }
      if (!docs.length) { res.push({ id: a.id, value: 0, met: false, note: T('no readable ', 'aucun fichier ') + treeFormat(reader).label + T(' file', ' lisible') }); continue; }
      const r = a.rule;
      const rule = { type: t, paths: [].concat(r.path ? [r.path] : [], Array.isArray(r.paths) ? r.paths : []) };
      const eq = r.equals === undefined ? undefined : typeof r.equals === 'boolean' ? String(r.equals) : String(r.equals);
      let best = -Infinity, foundMax = 0, blocked = null;
      const target = r.target ?? 1;
      for (const tree of docs) {
        const v = bymlRuleValue(rule, tree, eq);
        const c = failedRequire(r.requires, tree);
        if (c) { if (v.value + 1e-9 >= target) blocked = c; v.value = 0; }
        if (v.value > best) best = v.value;
        foundMax = Math.max(foundMax, v.found);
      }
      const met = best + 1e-9 >= target;
      const note = foundMax < rule.paths.length ? (rule.paths.length - foundMax) + '/' + rule.paths.length + T(' path(s) not found', ' chemin(s) introuvable(s)')
        : !met && blocked ? T('requires ', 'il faut ') + describeRequire(blocked) : '';
      res.push({ id: a.id, value: best, met, note });
    }
    return { results: res };
  }
  if (reader === 'gamedata_hash' || reader === 'totk_progress') {
    const slots = [];
    for (const p of def.save_files || []) { const f = pick([p]); if (f) { const t = parseHashTable(f.data, reader); if (t) slots.push({ p, t }); } }
    for (const a of def.achievements || []) {
      const t = a.rule && a.rule.type;
      if (!SAVE_TYPES.includes(t)) { res.push({ id: a.id, skip: true }); continue; }
      if (!slots.length) { res.push({ id: a.id, value: 0, met: false, note: T('no readable file', 'aucun fichier lisible') }); continue; }
      const r = compileRule(a.rule, fn);
      let best = -Infinity, missing = r.hashes.length;
      for (const { t: s } of slots) {
        let total = 0, found = 0, single = null;
        for (const h of r.hashes) {
          const raw = s.get(h);
          if (raw === undefined) continue;
          found++;
          let v;
          if (r.isFloat) { const dv = new DataView(new ArrayBuffer(4)); dv.setUint32(0, raw, true); v = dv.getFloat32(0, true); } else v = raw | 0;
          const hit = r.hasEquals ? (raw >>> 0) === r.equals : raw !== 0;
          if (t === 'count') total += hit ? 1 : 0;
          else if (t === 'sum') total += v;
          else { single = r.hasEquals ? (hit ? 1 : 0) : v; break; }
        }
        const val = (t === 'flag' || t === 'value') ? (single ?? 0) : total;
        if (val > best) best = val;
        missing = Math.min(missing, r.hashes.length - found);
      }
      res.push({ id: a.id, value: best, met: best + 1e-9 >= r.target, note: missing ? missing + '/' + r.hashes.length + T(' flag(s) missing from the save', ' drapeau(x) absent(s) de la save') : '' });
    }
    return { results: res };
  }
  for (const a of def.achievements || []) res.push({ id: a.id, skip: true });
  return { results: res };
}


// ------------------------------------------------------------------ automatic achievement generation
// Turns a diff (typically "new game" vs "100 %" save) into ready-made achievements:
//  - flag collections: nearby 0->1 flags with a regular stride, extended to their unset siblings,
//    become milestone "count" achievements (first, half, all);
//  - counters become milestone "flag" achievements (value >= target) scaled on the best value seen.
function niceNum(v) {
  if (v <= 1) return 1;
  const p = Math.pow(10, Math.floor(Math.log10(v)));
  for (const m of [1, 2, 2.5, 5, 10]) if (m * p >= v - 1e-9) return m * p;
  return 10 * p;
}
function niceBelow(v) {
  if (v <= 1) return 1;
  const p = Math.pow(10, Math.floor(Math.log10(v)));
  for (const m of [10, 5, 2.5, 2, 1]) if (m * p <= v + 1e-9) return m * p;
  return p;
}
function findFlagGroups(snaps, cands, opts = {}) {
  const maxGap = opts.maxGap || 64, minFlags = opts.minFlags || 3;
  const groups = [];
  for (const size of [1, 2, 4]) {
    const offs = cands.filter(c => c.kind === 'flag' && c.size === size && c.off !== undefined).map(c => c.off).sort((a, b) => a - b);
    let cluster = [];
    const flush = () => {
      if (cluster.length >= minFlags) {
        const ext = extendSeries(snaps, cluster, size, { maxZeroRun: opts.maxZeroRun ?? 0, maxCount: 2048 });
        if (ext.offsets.length >= minFlags) groups.push({ size, stride: ext.stride, offsets: ext.offsets, seed: cluster.length });
      }
      cluster = [];
    };
    for (const o of offs) {
      if (cluster.length && o - cluster[cluster.length - 1] > maxGap) flush();
      cluster.push(o);
    }
    flush();
  }
  groups.sort((a, b) => b.offsets.length - a.offsets.length);
  const kept = [];
  for (const g of groups) {
    if (!kept.some(k => k.size === g.size && g.offsets.every(o => k.offsets.includes(o)))) kept.push(g);
  }
  return kept.sort((a, b) => a.offsets[0] - b.offsets[0]);
}
function findHashGroups(cands, dict) {
  // Named flags sharing a prefix + number ("Clear_Dungeon003") form one group.
  const by = new Map();
  for (const c of cands) {
    if (c.kind !== 'flag') continue;
    const n = dict && dict.get(c.hash);
    const m = n && /^(.*?)(\d+)$/.exec(n.name);
    const key = m ? m[1] + '#' + m[2].length : '?';
    if (!by.has(key)) by.set(key, []);
    by.get(key).push(c);
  }
  const out = [];
  for (const [key, list] of by) {
    if (key === '?') { if (list.length >= 3) out.push({ hashes: list.map(c => c.hash), label: '' }); continue; }
    if (list.length < 2) continue;
    out.push({ hashes: list.map(c => c.hash), label: key.split('#')[0], names: list.map(c => dict.get(c.hash).name) });
  }
  return out;
}
const LETTERS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ';
function autoAchievements(snaps, diff, opts = {}) {
  const out = [];
  const last = snaps.length - 1;
  const isHash = diff.mode === 'hash';
  const cands = diff.candidates;
  const milestones = (total) => {
    const t = [1];
    if (total >= 8) t.push(Math.round(total / 2));
    if (total > 1) t.push(total);
    return [...new Set(t)];
  };
  const pts = (i, n) => n === 1 ? 25 : i === 0 ? 10 : i === n - 1 ? 50 : 25;
  let gi = 0;
  if (!isHash) {
    for (const g of findFlagGroups(snaps, cands, opts)) {
      const L = 'Collection ' + LETTERS[gi++ % 26];
      const setNow = g.offsets.filter(o => rd(snaps[last], o, g.size) === 1).length;
      const total = g.offsets.length;
      const ms = milestones(total);
      ms.forEach((t, i) => out.push({
        group: L, auto: true,
        name: t === 1 ? L + T(': first', ' : premier') : t === total ? L + T(': complete', ' : complète') : L + SEP + t + '/' + total,
        desc: T('Get ', 'Obtenir ') + t + T(' item(s) out of ', ' élément(s) sur ') + total + ' (' + total + T(' flags from ', ' drapeaux dès ') + hexOff(g.offsets[0]) + T(', stride ', ', pas ') + g.stride + ' ; ' + setNow + T(' set in the last capture)', ' obtenus dans la dernière capture)'),
        points: pts(i, ms.length),
        rule: { type: 'count', offsets: g.offsets.map(hexOff), size: g.size, max: 1, target: t },
      }));
    }
  } else {
    for (const g of findHashGroups(cands, opts.dict)) {
      const label = g.label ? g.label.replace(/[_.]+$/, '') : 'Collection ' + LETTERS[gi++ % 26];
      const total = g.hashes.length;
      const ms = milestones(total);
      ms.forEach((t, i) => out.push({
        group: label, auto: true,
        name: t === 1 ? label + T(': first', ' : premier') : t === total ? label + T(': complete', ' : complet') : label + SEP + t + '/' + total,
        desc: T('Get ', 'Obtenir ') + t + T(' out of ', ' sur ') + total,
        points: pts(i, ms.length),
        rule: { type: 'count', names: g.names || g.hashes.map(h => hex(h)), target: t },
      }));
    }
  }
  if (diff.mode === 'byml') return autoByml(snaps, diff, opts);
  const counters = cands.filter(c => c.kind === 'counter' && !c.float && typeof c.vals[last] === 'number' && c.vals[last] >= 5)
    .sort((a, b) => b.score - a.score || b.vals[last] - a.vals[last]).slice(0, opts.maxCounters || 8);
  let ci = 0;
  for (const c of counters) {
    const best = c.vals[last];
    const N = T('Counter ', 'Compteur ') + (++ci);
    const ts = [...new Set([niceBelow(best / 10), niceBelow(best / 2), niceBelow(best)])].filter(t => t >= 1);
    const named = isHash && opts.dict && opts.dict.get(c.hash);
    const ref = isHash ? (named ? named.name : hex(c.hash)) : hexOff(c.off);
    ts.forEach((t, i) => {
      const rule = { type: 'flag', target: t };
      if (isHash) rule[named ? 'name' : 'hash'] = ref;
      else { rule.offset = ref; rule.size = c.size; rule.max = Math.min(4294967295, Math.max(1000, niceNum(best * 1000))); }
      out.push({ group: N, auto: true, name: N + SEP + t, desc: T('Reach ', 'Atteindre ') + t + ' (' + ref + ' ; ' + best + T(' in the last capture)', ' dans la dernière capture)'), points: ts.length === 1 ? 25 : [10, 25, 50][i] || 25, rule });
    });
  }
  return out;
}


function autoByml(trees, diff, opts = {}) {
  // trees: parsed BYML trees (oldest first)
  const out = [];
  const last = trees[trees.length - 1];
  const groups = new Map();
  const singles = [];
  for (const c of diff.candidates) {
    if (c.kind !== 'flag' && c.kind !== 'new') continue;
    const member = /\[=[^\]]*\]$/.test(c.path);
    const generic = member ? c.path.replace(/\[=[^\]]*\]$/, '[*]') : patternOf(c.path);
    if (generic === c.path) { singles.push(c); continue; }
    if (!groups.has(generic)) groups.set(generic, { member, paths: [] });
    groups.get(generic).paths.push(c.path);
  }
  const label = (p) => p.replace(/\[\*\]/g, '').split('/').filter(Boolean).slice(-2).join(' ');
  for (const [pat, g] of groups) {
    const slots = (queryByml(last, pat) || []).length;
    const have = (queryByml(last, pat) || []).filter(truthy).length;
    let ms;
    if (g.member) ms = [1, 10, 25, 50, 100].filter(t => t <= slots);
    else if (slots >= 40) ms = [1, niceBelow(slots / 10), niceBelow(slots / 4), niceBelow(slots / 2), slots];
    else ms = slots >= 8 ? [1, Math.round(slots / 2), slots] : [1, slots];
    ms = [...new Set(ms)].filter(t => t >= 1);
    const L = label(pat);
    ms.forEach((t, i) => out.push({
      group: L, auto: true,
      name: L + SEP + t, desc: T('Reach ', 'Atteindre ') + t + (g.member ? '' : T(' out of ', ' sur ') + slots) + ' (' + pat + ' ; ' + have + T(' in the last capture)', ' dans la dernière capture)'),
      points: i === 0 ? 5 : i === ms.length - 1 ? 50 : i === 1 ? 10 : 25,
      rule: { type: 'count', path: pat, target: t },
    }));
  }
  for (const c of singles.slice(0, opts.maxSingles || 20)) {
    const L = label(c.path) || c.path;
    out.push({ group: T('Milestones', 'Étapes'), auto: true, name: L, desc: c.path + T(' becomes 1', ' passe à 1'), points: 10, rule: { type: 'flag', path: c.path, target: 1 } });
  }
  const counters = diff.candidates.filter(c => c.kind === 'counter' && !c.float && c.vals[c.vals.length - 1] >= 2).slice(0, opts.maxCounters || 12);
  for (const c of counters) {
    const best = c.vals[c.vals.length - 1];
    const ts = best >= 20 ? [...new Set([niceBelow(best / 10), niceBelow(best / 2), niceBelow(best)])] : [best];
    const L = label(c.path) || c.path;
    ts.forEach((t, i) => out.push({ group: L, auto: true, name: L + SEP + t, desc: T('Reach ', 'Atteindre ') + t + ' (' + c.path + ' ; ' + best + T(' in the last capture)', ' dans la dernière capture)'), points: ts.length === 1 ? 10 : [10, 25, 50][i], rule: { type: 'flag', path: c.path, target: t } }));
  }
  return out;
}

// "memory" rules (include/memrules.h): "[[main+2D21A90]+20]+1C0" -> { region, offsets } or null.
const MEM_TYPES = ['u8', 'u16', 'u32', 'u64', 's8', 's16', 's32', 's64', 'i8', 'i16', 'i32', 'i64', 'f32', 'f64', 'float', 'double'];
const MEM_CMPS = ['>=', '>', '<=', '<', '==', '=', '!=', '&'];
function parseMemAddress(text) {
  const m = /^\s*(\[\s*)*(main|heap|alias)\s*(?:([+-])\s*(?:0x)?([0-9a-f]{1,16}))?((?:\s*\]\s*(?:[+-]\s*(?:0x)?[0-9a-f]{1,16})?)*)\s*$/i.exec(String(text));
  if (!m) return null;
  const depth = (String(text).match(/\[/g) || []).length;
  const tail = [...(m[5] || '').matchAll(/\]\s*(?:([+-])\s*(?:0x)?([0-9a-f]{1,16}))?/gi)];
  if (tail.length !== depth || depth > 16) return null;
  const off = (sg, h) => h ? (sg === '-' ? -1n : 1n) * BigInt('0x' + h) : 0n;
  return { region: m[2].toLowerCase(), offsets: [off(m[3], m[4]), ...tail.map(t => off(t[1], t[2]))] };
}
function formatMemAddress(a) {
  const o = v => (v < 0n ? '-' : '+') + (v < 0n ? -v : v).toString(16).toUpperCase();
  let s = '['.repeat(a.offsets.length - 1) + a.region + o(a.offsets[0]);
  for (const v of a.offsets.slice(1)) s += ']' + (v ? o(v) : '');
  return s;
}
function validateMemRule(id, r, errs) {
  if (r.value_type !== undefined && !MEM_TYPES.includes(String(r.value_type).toLowerCase())) errs.push(id + ': value_type must be u8…u64, s8…s64, f32 or f64');
  if (r.cmp !== undefined && !MEM_CMPS.includes(r.cmp)) errs.push(id + ': cmp must be >=, >, <=, <, ==, != or &');
  const b = r.builds;
  if (!b || typeof b !== 'object' || Array.isArray(b) || !Object.keys(b).length) { errs.push(id + T(': memory rule needs "builds": { "<build id>": "<address>" }', ' : une règle memory a besoin de "builds": { "<build id>": "<adresse>" }')); return; }
  for (const [k, v] of Object.entries(b)) {
    if (!/^[0-9A-Fa-f]{1,64}$/.test(k)) errs.push(id + ': build id “' + k + '” is not hex');
    const list = Array.isArray(v) ? v : [v];
    if (!list.length) errs.push(id + ': build ' + k + ' has no address');
    for (const a of list) if (typeof a !== 'string' || !parseMemAddress(a)) errs.push(id + T(': malformed address “', ' : adresse mal formée « ') + a + T('” (main/heap/alias+offset, [ ] for pointers)', ' » (main/heap/alias+offset, [ ] pour les pointeurs)'));
  }
}

// Validation like the app's parser: catches what the console would silently drop.
function validate(def) {
  const errs = [];
  if (!/^[0-9A-Fa-f]{16}$/.test(def.title_id || '')) errs.push(T('title_id must be 16 hexadecimal characters', 'title_id doit faire 16 caractères hexadécimaux'));
  const ids = new Set();
  const types = ['flag', 'value', 'count', 'sum', 'playtime_hours', 'launches', 'days_played', 'streak_days', 'comeback_days', 'screenshots', 'videos', 'memory'];
  for (const a of def.achievements || []) {
    if (!a.id) errs.push(T('an achievement has no id', 'un succès n\'a pas d\'id'));
    else if (ids.has(a.id)) errs.push(T('duplicate id: ', 'id en double : ') + a.id);
    ids.add(a.id);
    const r = a.rule || {};
    if (!types.includes(r.type)) { errs.push(a.id + T(': unknown rule type “', ' : type de règle inconnu « ') + r.type + T('”', ' »')); continue; }
    if (r.type === 'memory') validateMemRule(a.id, r, errs);
    if (SAVE_TYPES.includes(r.type)) {
      const hasSrc = r.name || r.hash || (r.names && r.names.length) || r.pattern || r.offset !== undefined || (r.offsets && r.offsets.length) || r.path || (r.paths && r.paths.length);
      if (!hasSrc) errs.push(a.id + T(': needs an offset, a hash or a name', ' : il faut un offset, un hash ou un nom'));
      const hasPath = r.path || (r.paths && r.paths.length);
      const tree = TREE_READERS.includes(def.reader);
      if (tree && !hasPath) errs.push(a.id + T(': the ', ' : le lecteur ') + def.reader + T(' reader uses paths (path / paths)', ' utilise des chemins (path / paths)'));
      if (!tree && hasPath) errs.push(a.id + T(': path/paths only work with the readers ', ' : path/paths ne servent qu\'aux lecteurs ') + TREE_READERS.join(', '));
      if (tree && hasPath) for (const pp of [].concat(r.path || [], r.paths || [])) if (!/^(?:[^[\]/]+(?:\[[^\]]*\])*|(?:\[[^\]]*\])+)(?:\/[^[\]/]*(?:\[[^\]]*\])*)*$/.test(pp)) errs.push(a.id + T(': malformed path “', ' : chemin mal formé « ') + pp + T('”', ' »'));
      if (r.requires !== undefined) {
        if (!Array.isArray(r.requires) || !hasPath) errs.push(a.id + T(': "requires" is a list of conditions for path rules', ' : "requires" est une liste de conditions pour les règles à chemins'));
        else for (const c of r.requires) if (!c || typeof c.path !== 'string' || !c.path || (typeof c.min !== 'number' && typeof c.below !== 'number'))
          errs.push(a.id + T(': each "requires" condition needs a path and min and/or below', ' : chaque condition "requires" a besoin d\'un path et de min et/ou below'));
      }
      if (def.reader === 'binary_le' && (r.name || r.hash || r.names || r.pattern)) errs.push(a.id + T(': the binary_le reader only uses offsets', ' : le lecteur binary_le n\'utilise que des offsets'));
      if (def.reader !== 'binary_le' && !TREE_READERS.includes(def.reader) && (r.offset !== undefined || r.offsets)) errs.push(a.id + T(': this reader only uses hashes/names', ' : ce lecteur n\'utilise que des hashes/noms'));
      if (!def.reader) errs.push(a.id + T(': save rule but no reader chosen', ' : règle de sauvegarde mais aucun lecteur (reader) choisi'));
    }
  }
  if (def.reader && !(def.save_files || []).length) errs.push(T('save_files is empty: the app won\'t know which file to open', 'save_files est vide : l\'app ne saura pas quel fichier ouvrir'));
  return errs;
}

const api = { crc32, murmur3, fnv1a, HASHES, hex, hexOff, rd, rdF32, entropy, unzip, zipStore, stripCommonPrefix, identify,
  parseHashTable, classify, KIND_LABEL, diffBinary, diffHash, searchValue, searchValueHash, extendSeries,
  matchDictionary, evaluate, validate, parseMemAddress, formatMemAddress, MEM_TYPES, autoAchievements, findByml, parseByml, parseJson, parseTree, TREE_READERS, TREE_FORMATS, treeFormat, parseGvas: F.parseGvas, parseNrbf: F.parseNrbf, flattenByml, queryByml, diffByml, searchValueByml, bymlGroupRule, bymlRuleValue, patternOf, findFlagGroups, findHashGroups, niceNum, niceBelow, compileRule, cFormat, extractStrings, detectHashTable };
if (typeof module !== 'undefined' && module.exports) module.exports = api;
else root.SaveLab = api;
})(typeof self !== 'undefined' ? self : this);
