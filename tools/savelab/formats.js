// Save Lab format modules — binary save formats decoded into the same kind of tree as BYML / JSON,
// so that every path-based tool (differences, ⚡ generate, rules) works on them unchanged.
// Each module mirrors a reader of the Switch app; the trees must be identical on both sides:
//   gvas  Unreal Engine 4 / 5 (< 5.4) SaveGame files (.sav, magic "GVAS")  — source/gvas.cpp
//   nrbf  .NET BinaryFormatter streams (Unity games)                       — source/nrbf.cpp
(function (root) {
'use strict';
// T(english, french): UI language of the page (global T from index.html), English elsewhere (Node tests).
const T = root && typeof root.T === 'function' ? root.T : (en, fr) => en;

const MAX_STR = 1 << 20;
class Reader {
  constructor(buf) { this.b = buf; this.dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength); this.o = 0; }
  need(n) { if (n < 0 || this.o + n > this.b.length) throw new Error(T('unexpected end of file at 0x', 'fin de fichier inattendue à 0x') + this.o.toString(16).toUpperCase()); }
  skip(n) { this.need(n); this.o += n; }
  u8() { this.need(1); return this.b[this.o++]; }
  i8() { this.need(1); return this.dv.getInt8(this.o++); }
  u16() { this.need(2); const v = this.dv.getUint16(this.o, true); this.o += 2; return v; }
  i16() { this.need(2); const v = this.dv.getInt16(this.o, true); this.o += 2; return v; }
  u32() { this.need(4); const v = this.dv.getUint32(this.o, true); this.o += 4; return v; }
  i32() { this.need(4); const v = this.dv.getInt32(this.o, true); this.o += 4; return v; }
  i64() { this.need(8); const v = Number(this.dv.getBigInt64(this.o, true)); this.o += 8; return v; }
  u64() { this.need(8); const v = Number(this.dv.getBigUint64(this.o, true)); this.o += 8; return v; }
  f32() { this.need(4); const v = this.dv.getFloat32(this.o, true); this.o += 4; return fin(+v.toFixed(6)); }
  f64() { this.need(8); const v = this.dv.getFloat64(this.o, true); this.o += 8; return fin(v); }
}
// Non-finite numbers cannot be stored in JSON (cJSON on the console): they become null on both sides.
// Strings end at the first NUL, like the C strings of the app.
const cut = (s) => { const i = s.indexOf('\0'); return i < 0 ? s : s.slice(0, i); };
const fin = (v) => Number.isFinite(v) ? v : null;
const latin1 = (b) => { let s = ''; for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i]); return s; };
const utf8 = new TextDecoder('utf-8');
const utf16 = new TextDecoder('utf-16le');
const hex8 = (v) => (v >>> 0).toString(16).toUpperCase().padStart(8, '0');

// ================================================================== GVAS (Unreal Engine SaveGame)
// File: "GVAS", SaveGameFileVersion (2|3), UE4 package version, [UE5 package version], engine version,
// custom versions, save class name, then a property list ended by "None".
// Tree mapping (same in source/gvas.cpp):
//   Int*/UInt*/Float/Double -> number · Bool -> true/false · Str/Name/Object/Enum -> string
//   Byte -> number (or enum name) · Text -> its source string · Struct -> object of its properties,
//   or for engine structs: Vector/Rotator/Quat… -> {X,Y,Z…}, Guid -> 32 hex digits, DateTime/Timespan -> ticks,
//   GameplayTagContainer -> list of tag names · Array/Set -> list · Map -> list of {Key, Value}
//   a property repeated with ArrayIndex > 0 (static array) -> list · undecodable value -> null
function gvasFstr(r) {
  const n = r.i32();
  if (n === 0) return '';
  if (n > 0) { if (n > MAX_STR) throw new Error(T('string too long', 'chaîne trop longue')); r.need(n); const s = latin1(r.b.subarray(r.o, r.o + n - 1)); r.o += n; return cut(s); }
  const m = -n;
  if (m > MAX_STR) throw new Error(T('string too long', 'chaîne trop longue'));
  r.need(m * 2);
  const s = utf16.decode(r.b.subarray(r.o, r.o + (m - 1) * 2));
  r.o += m * 2;
  return cut(s);
}
function gvasGuid(r) { let s = ''; for (let i = 0; i < 4; i++) s += hex8(r.u32()); return s; }
const PROP_NAME = /^[\x20-\x7e -￿]{1,256}$/;

function readTag(r) {
  const name = gvasFstr(r);
  if (name === 'None') return null;
  if (!PROP_NAME.test(name)) throw new Error(T('invalid property name at 0x', 'nom de propriété invalide à 0x') + r.o.toString(16).toUpperCase());
  const type = gvasFstr(r);
  if (!/^[A-Za-z0-9_]{1,64}Property$/.test(type)) throw new Error(T('invalid property type “', 'type de propriété invalide « ') + type.slice(0, 40) + T('”', ' »'));
  const t = { name, type, size: r.i32(), index: r.i32() };
  switch (type) {
    case 'StructProperty': t.struct = gvasFstr(r); r.skip(16); break;
    case 'BoolProperty': t.bool = r.u8(); break;
    case 'ByteProperty': case 'EnumProperty': t.enumName = gvasFstr(r); break;
    case 'ArrayProperty': case 'SetProperty': t.inner = gvasFstr(r); break;
    case 'MapProperty': t.key = gvasFstr(r); t.value = gvasFstr(r); break;
  }
  if (r.u8()) r.skip(16);  // property GUID
  return t;
}
function addProp(obj, multi, name, v, index) {
  if (index > 0 || Object.prototype.hasOwnProperty.call(obj, name)) {
    if (!multi.has(name)) { obj[name] = Object.prototype.hasOwnProperty.call(obj, name) ? [obj[name]] : []; multi.add(name); }
    obj[name].push(v);
  } else obj[name] = v;
}
function readProps(r, ctx, depth) {
  if (depth > 48) throw new Error(T('structure nested too deeply', 'structure trop profonde'));
  const obj = {}, multi = new Set();
  for (;;) {
    const t = readTag(r);
    if (!t) return obj;
    let v = null;
    if (t.type === 'BoolProperty') v = !!t.bool;
    if (t.size < 0 || r.o + t.size > r.b.length) throw new Error(T('invalid size for “', 'taille de « ') + t.name + T('”', ' » invalide'));
    const start = r.o, end = start + t.size;
    if (t.type !== 'BoolProperty') {
      try { v = readValue(r, t, ctx, depth, end); if (r.o > end) { v = null; ctx.skipped++; } }
      catch (e) { v = null; ctx.skipped++; if (ctx.why.length < 8) ctx.why.push(t.name + ' (' + t.type + (t.struct ? ' ' + t.struct : t.inner ? ' de ' + t.inner : '') + ') : ' + e.message); }
    }
    r.o = end;
    if (--ctx.budget < 0) throw new Error(T('document too large', 'document trop grand'));
    addProp(obj, multi, t.name, v, t.index);
  }
}
function readValue(r, t, ctx, depth, end) {
  switch (t.type) {
    case 'ByteProperty': return t.enumName === 'None' || t.size === 1 ? r.u8() : gvasFstr(r);
    case 'StructProperty': return readStruct(r, t.struct, ctx, depth, t.size);
    case 'ArrayProperty': return readArray(r, t.inner, t.size, ctx, depth);
    case 'SetProperty': {
      const rm = r.i32(); if (rm < 0 || rm > t.size) throw new Error(T('invalid set', 'set invalide'));
      for (let i = 0; i < rm; i++) readElem(r, t.inner, ctx, depth);
      const n = r.i32(); if (n < 0 || n > t.size) throw new Error(T('invalid set', 'set invalide'));
      const out = []; for (let i = 0; i < n; i++) out.push(readElem(r, t.inner, ctx, depth));
      return out;
    }
    case 'MapProperty': {
      const rm = r.i32(); if (rm < 0 || rm > t.size) throw new Error(T('invalid map', 'map invalide'));
      for (let i = 0; i < rm; i++) readElem(r, t.key, ctx, depth);
      const n = r.i32(); if (n < 0 || n > t.size) throw new Error(T('invalid map', 'map invalide'));
      const out = [];
      for (let i = 0; i < n; i++) { const Key = readElem(r, t.key, ctx, depth); const Value = readElem(r, t.value, ctx, depth); out.push({ Key, Value }); }
      return out;
    }
    case 'TextProperty': return readText(r, end, ctx);
    case 'DelegateProperty': case 'MulticastInlineDelegateProperty': case 'MulticastSparseDelegateProperty': case 'FieldPathProperty':
      return null;   // engine bindings, never game progress
    default: return readElem(r, t.type, ctx, depth);
  }
}
function readElem(r, type, ctx, depth, structName) {
  switch (type) {
    case 'BoolProperty': return r.u8() !== 0;
    case 'ByteProperty': return r.u8();
    case 'Int8Property': return r.i8();
    case 'Int16Property': return r.i16();
    case 'IntProperty': return r.i32();
    case 'Int64Property': return r.i64();
    case 'UInt16Property': return r.u16();
    case 'UInt32Property': return r.u32();
    case 'UInt64Property': return r.u64();
    case 'FloatProperty': return r.f32();
    case 'DoubleProperty': return r.f64();
    case 'StrProperty': case 'NameProperty': case 'EnumProperty': case 'ObjectProperty': case 'ClassProperty':
    case 'InterfaceProperty': case 'WeakObjectProperty':
      return gvasFstr(r);
    case 'SoftObjectProperty': case 'SoftClassProperty': return softPath(r, ctx);
    case 'TextProperty': return readText(r, -1, ctx);
    case 'StructProperty': return structName ? readStruct(r, structName, ctx, depth, 0) : guessStruct(r, ctx, depth);
    default: throw new Error(T('unsupported type: ', 'type non géré : ') + type);
  }
}
function softPath(r, ctx) {
  const parts = ctx.ue5 >= 1007 ? [gvasFstr(r), gvasFstr(r), gvasFstr(r)] : [gvasFstr(r), gvasFstr(r)];
  return parts.filter(Boolean).join(':');
}
// FText -> its displayable source: Base -> source string, StringTable -> key, formats -> the format pattern,
// AsNumber/AsPercent/AsCurrency -> the number, AsDate/AsTime/AsDateTime -> ticks, Transform -> the source text.
function readText(r, end, ctx, depth = 0) {
  if (depth > 16) throw new Error(T('text nested too deeply', 'texte trop imbriqué'));
  r.u32();               // flags
  const h = r.i8();
  const arg = () => {
    const t = r.i8();
    if (t === 0) return ctx.ue5 ? r.i64() : r.i32();
    if (t === 1) return ctx.ue5 ? r.u64() : r.u32();
    if (t === 2) return r.f32();
    if (t === 3) return r.f64();
    if (t === 4) return readText(r, -1, ctx, depth + 1);
    throw new Error(T('unsupported text argument (', 'argument de texte non géré (') + t + ')');
  };
  const num = () => { const v = arg(); if (r.u32()) r.skip(25); gvasFstr(r); return v; };
  switch (h) {
    case -1:             // no history: optional culture-invariant string
      if (end >= 0 && r.o + 4 > end) return '';
      return r.i32() ? gvasFstr(r) : '';
    case 0: gvasFstr(r); gvasFstr(r); return gvasFstr(r);                 // namespace, key, source string
    case 1: case 3: { const f = readText(r, -1, ctx, depth + 1), n = r.i32(); if (n < 0 || n > 1000) throw new Error(T('invalid text', 'texte invalide')); for (let i = 0; i < n; i++) { gvasFstr(r); arg(); } return f; }
    case 2: { const f = readText(r, -1, ctx, depth + 1), n = r.i32(); if (n < 0 || n > 1000) throw new Error(T('invalid text', 'texte invalide')); for (let i = 0; i < n; i++) arg(); return f; }
    case 4: case 5: return num();
    case 6: gvasFstr(r); return num();
    case 7: { const t = r.i64(); r.i8(); gvasFstr(r); return t; }
    case 8: { const t = r.i64(); r.i8(); gvasFstr(r); gvasFstr(r); return t; }
    case 9: { const t = r.i64(); r.i8(); r.i8(); gvasFstr(r); gvasFstr(r); return t; }
    case 10: { const f = readText(r, -1, ctx, depth + 1); r.i8(); return f; }
    case 11: gvasFstr(r); return gvasFstr(r);                               // string table: table id, key
    default: throw new Error(T('unsupported text (history ', 'texte non géré (historique ') + h + ')');
  }
}
function readArray(r, inner, size, ctx, depth) {
  const n = r.i32();
  if (n < 0 || n > size) throw new Error(T('invalid array', 'tableau invalide'));
  const out = [];
  if (inner === 'StructProperty') {
    gvasFstr(r); gvasFstr(r); const total = r.i64();
    const sname = gvasFstr(r); r.skip(16); if (r.u8()) r.skip(16);
    const each = n ? total / n : 0;
    for (let i = 0; i < n; i++) out.push(readStruct(r, sname, ctx, depth, each));
    return out;
  }
  if (inner === 'ByteProperty' && size - 4 !== n) { for (let i = 0; i < n; i++) out.push(gvasFstr(r)); return out; }
  for (let i = 0; i < n; i++) out.push(readElem(r, inner, ctx, depth));
  return out;
}
// Engine structs serialised natively (not as a property list). `size` = bytes of one value when known.
function readStruct(r, name, ctx, depth, size) {
  const dbl = (n) => size ? size === n * 8 : ctx.lwc;
  const fl = (big) => big ? r.f64() : r.f32();
  switch (name) {
    case 'Vector': { const d = dbl(3); return { X: fl(d), Y: fl(d), Z: fl(d) }; }
    case 'Rotator': { const d = dbl(3); return { Pitch: fl(d), Yaw: fl(d), Roll: fl(d) }; }
    case 'Vector2D': { const d = dbl(2); return { X: fl(d), Y: fl(d) }; }
    case 'Quat': case 'Vector4': case 'Plane': { const d = dbl(4); return { X: fl(d), Y: fl(d), Z: fl(d), W: fl(d) }; }
    case 'LinearColor': return { R: r.f32(), G: r.f32(), B: r.f32(), A: r.f32() };
    case 'Color': return { B: r.u8(), G: r.u8(), R: r.u8(), A: r.u8() };
    case 'IntPoint': return { X: r.i32(), Y: r.i32() };
    case 'IntVector': return { X: r.i32(), Y: r.i32(), Z: r.i32() };
    case 'Guid': return gvasGuid(r);
    case 'DateTime': case 'Timespan': return r.i64();
    case 'GameplayTagContainer': {
      const n = r.i32(); if (n < 0 || n > 100000) throw new Error(T('invalid GameplayTagContainer', 'GameplayTagContainer invalide'));
      const out = []; for (let i = 0; i < n; i++) out.push(gvasFstr(r)); return out;
    }
    case 'SoftObjectPath': case 'SoftClassPath': return softPath(r, ctx);
    case 'Box': { const d = size ? size === 49 : ctx.lwc; return { Min: { X: fl(d), Y: fl(d), Z: fl(d) }, Max: { X: fl(d), Y: fl(d), Z: fl(d) }, IsValid: r.u8() !== 0 }; }
    default: return readProps(r, ctx, depth + 1);
  }
}
// Struct of unknown type (map keys / set elements carry no type name): a property list if it parses, else a GUID.
function guessStruct(r, ctx, depth) {
  const at = r.o, skipped = ctx.skipped, budget = ctx.budget;
  try { return readProps(r, ctx, depth + 1); }
  catch (e) { r.o = at; ctx.skipped = skipped; ctx.budget = budget; return gvasGuid(r); }
}
function isGvas(buf) { return buf.length >= 24 && buf[0] === 0x47 && buf[1] === 0x56 && buf[2] === 0x41 && buf[3] === 0x53; }
function parseGvas(buf) {
  if (!isGvas(buf)) return null;
  const r = new Reader(buf);
  r.skip(4);
  const sgv = r.u32();
  if (sgv < 2 || sgv > 3) throw new Error(T('unsupported GVAS version ', 'version GVAS ') + sgv + T('', ' non gérée'));
  const ue4 = r.u32(), ue5 = sgv >= 3 ? r.u32() : 0;
  if (ue5 >= 1012) throw new Error(T('Unreal 5.4 or newer save (new property format, not supported yet)', 'sauvegarde Unreal 5.4 ou plus récente (nouveau format des propriétés, pas encore géré)'));
  const major = r.u16(), minor = r.u16(), patch = r.u16(); r.u32(); const branch = gvasFstr(r);
  const cvf = r.u32(), ncv = r.u32();
  if (ncv > 10000) throw new Error(T('invalid GVAS header', 'en-tête GVAS invalide'));
  for (let i = 0; i < ncv; i++) { if (cvf === 1) r.skip(8); else { r.skip(20); if (cvf === 2) gvasFstr(r); } }
  const cls = gvasFstr(r);
  const ctx = { ue4, ue5, lwc: ue5 >= 1004, skipped: 0, why: [], budget: 2e6 };
  const tree = readProps(r, ctx, 0);
  return { gvas: true, tree, meta: { saveClass: cls, engine: major + '.' + minor + '.' + patch + (branch ? ' (' + branch + ')' : ''), ue4, ue5, skipped: ctx.skipped, why: ctx.why } };
}

// ================================================================== NRBF (.NET BinaryFormatter, MS-NRBF)
// Records are read into an object table, then the tree is built from the root object, following references.
// Tree mapping (same in source/nrbf.cpp):
//   class -> object of its members ("<Name>k__BackingField" -> "Name") · enum (single "value__") -> number
//   List<T> -> list (first _size items) · HashSet<T> -> list · Dictionary<K,V> -> list of {Key, Value}
//   KeyValuePair<K,V> -> {Key, Value} · arrays -> list · DateTime / TimeSpan -> ticks · Decimal -> number
//   char -> 1-character string · a root that is not an object/list -> { value: root } · cycles -> null
function lps(r) {
  let n = 0, shift = 0, b;
  do { if (shift > 28) throw new Error(T('invalid string length', 'longueur de chaîne invalide')); b = r.u8(); n |= (b & 0x7f) << shift; shift += 7; } while (b & 0x80);
  if (n > MAX_STR) throw new Error(T('string too long', 'chaîne trop longue'));
  r.need(n);
  const s = utf8.decode(r.b.subarray(r.o, r.o + n));
  r.o += n;
  return cut(s);
}
function nrbfPrim(r, t) {
  switch (t) {
    case 1: return r.u8() !== 0;
    case 2: return r.u8();
    case 3: { const c = r.b[r.o], n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; r.need(n); const s = utf8.decode(r.b.subarray(r.o, r.o + n)); r.o += n; return cut(s); }
    case 5: { const s = lps(r); const v = parseFloat(s); return Number.isFinite(v) ? v : s; }
    case 6: return r.f64();
    case 7: return r.i16();
    case 8: return r.i32();
    case 9: case 12: return r.i64();
    case 10: return r.i8();
    case 11: return r.f32();
    case 13: { const lo = r.u32(), hi = r.u32() & 0x3FFFFFFF; return hi * 4294967296 + lo; }   // DateTime ticks without the Kind bits
    case 14: return r.u16();
    case 15: return r.u32();
    case 16: return r.u64();
    case 17: return null;
    case 18: return lps(r);
    default: throw new Error(T('unknown primitive type ', 'type primitif inconnu ') + t);
  }
}
const REF = (id) => ({ $ref: id });
function nrbfParseRecords(buf) {
  const r = new Reader(buf);
  if (r.u8() !== 0) throw new Error(T('missing NRBF header', 'en-tête NRBF manquant'));
  const rootId = r.i32(); r.i32();
  if (r.i32() !== 1 || r.i32() !== 0) throw new Error(T('unsupported NRBF version', 'version NRBF non gérée'));
  const objs = new Map(), metas = new Map();
  let budget = 2e6;
  const classInfo = () => { const id = r.i32(), name = lps(r), n = r.i32(); if (n < 0 || n > 100000) throw new Error(T('invalid class', 'classe invalide')); const names = []; for (let i = 0; i < n; i++) names.push(lps(r)); return { id, name, names }; };
  const typeInfo = (n) => {
    const bt = []; for (let i = 0; i < n; i++) bt.push(r.u8());
    const extra = bt.map(t => t === 0 || t === 7 ? r.u8() : t === 3 ? lps(r) : t === 4 ? (lps(r), r.i32(), 0) : 0);
    return { bt, extra };
  };
  const members = (meta) => {
    const vals = [];
    for (let i = 0; i < meta.names.length; i++) {
      if (meta.types && meta.types.bt[i] === 0) vals.push(nrbfPrim(r, meta.types.extra[i]));
      else { const v = record(); if (v && v.$nulls) throw new Error(T('several nulls in a class', 'nulls multiples dans une classe')); vals.push(v); }
    }
    return vals;
  };
  const elements = (n, prim) => {
    const out = [];
    while (out.length < n) {
      if (--budget < 0) throw new Error(T('document too large', 'document trop grand'));
      if (prim !== undefined) { out.push(nrbfPrim(r, prim)); continue; }
      const v = record();
      if (v && v.$nulls) { if (out.length + v.$nulls > n) throw new Error(T('invalid array', 'tableau invalide')); for (let k = 0; k < v.$nulls; k++) out.push(null); }
      else out.push(v);
    }
    return out;
  };
  const record = () => {
    for (;;) {
      if (--budget < 0) throw new Error(T('document too large', 'document trop grand'));
      const t = r.u8();
      switch (t) {
        case 1: { const id = r.i32(), meta = metas.get(r.i32()); if (!meta) throw new Error(T('unknown referenced class', 'classe de référence inconnue')); objs.set(id, { cls: meta.name, names: meta.names, vals: members(meta) }); return REF(id); }
        case 2: case 3: case 4: case 5: {
          const ci = classInfo();
          const meta = { name: ci.name, names: ci.names, types: t >= 4 ? typeInfo(ci.names.length) : null };
          if (t === 3 || t === 5) r.i32();   // library id
          metas.set(ci.id, meta);
          objs.set(ci.id, { cls: meta.name, names: meta.names, vals: members(meta) });
          return REF(ci.id);
        }
        case 6: { const id = r.i32(); objs.set(id, lps(r)); return REF(id); }
        case 7: {
          const id = r.i32(), at = r.u8(), rank = r.i32();
          if (rank < 1 || rank > 32) throw new Error(T('invalid array', 'tableau invalide'));
          let n = 1; for (let i = 0; i < rank; i++) { const l = r.i32(); if (l < 0) throw new Error(T('invalid array', 'tableau invalide')); n *= l; }
          if (at >= 3 && at <= 5) r.skip(4 * rank);
          const bt = r.u8(), ti = { bt: [bt], extra: [] };
          ti.extra = [bt === 0 || bt === 7 ? r.u8() : bt === 3 ? lps(r) : bt === 4 ? (lps(r), r.i32(), 0) : 0];
          if (n > buf.length) throw new Error(T('array too large', 'tableau trop grand'));
          objs.set(id, elements(n, bt === 0 ? ti.extra[0] : undefined));
          return REF(id);
        }
        case 8: return nrbfPrim(r, r.u8());
        case 9: return REF(r.i32());
        case 10: return null;
        case 11: return { $end: true };
        case 12: r.i32(); lps(r); continue;   // library: followed by the record it introduces
        case 13: return { $nulls: r.u8() };
        case 14: return { $nulls: r.i32() };
        case 15: { const id = r.i32(), n = r.i32(), p = r.u8(); if (n < 0 || n > buf.length) throw new Error(T('invalid array', 'tableau invalide')); objs.set(id, elements(n, p)); return REF(id); }
        case 16: case 17: { const id = r.i32(), n = r.i32(); if (n < 0 || n > buf.length) throw new Error(T('invalid array', 'tableau invalide')); objs.set(id, elements(n)); return REF(id); }
        default: throw new Error(T('unknown NRBF record ', 'enregistrement NRBF inconnu ') + t + T(' at 0x', ' à 0x') + (r.o - 1).toString(16).toUpperCase());
      }
    }
  };
  for (;;) { const v = record(); if (v && v.$end) break; }
  return { rootId, objs };
}
const cleanMember = (n) => { const m = /^<(.+)>k__BackingField$/.exec(n); return m ? m[1] : n; };
function nrbfBuild(objs, rootId) {
  let budget = 2e6;
  const stack = new Set();
  const conv = (v, depth) => {
    if (--budget < 0) throw new Error(T('document too large', 'document trop grand'));
    if (v === null || typeof v !== 'object') return v;
    if (Array.isArray(v)) return v.map(x => conv(x, depth + 1));
    if (v.$ref === undefined) return null;
    const id = v.$ref;
    if (stack.has(id) || depth > 64 || !objs.has(id)) return null;
    stack.add(id);
    try { return node(objs.get(id), depth); } finally { stack.delete(id); }
  };
  const node = (o, depth) => {
    if (o === null || typeof o !== 'object') return o;
    if (Array.isArray(o)) return o.map(x => conv(x, depth + 1));
    const m = {};
    o.names.forEach((n, i) => { m[n] = o.vals[i]; });
    const cls = o.cls;
    if (o.names.length === 1 && o.names[0] === 'value__') return conv(m.value__, depth + 1);
    if (/^System\.Collections\.Generic\.List`1/.test(cls) && '_items' in m) {
      const items = conv(m._items, depth + 1), size = typeof m._size === 'number' ? m._size : 0;
      return Array.isArray(items) ? items.slice(0, Math.max(0, size)) : [];
    }
    if (/^System\.Collections\.Generic\.HashSet`1/.test(cls)) { const e = conv(m.Elements, depth + 1); return Array.isArray(e) ? e : []; }
    if (/^System\.Collections\.Generic\.(Dictionary|SortedDictionary)`2/.test(cls)) {
      const kv = conv(m.KeyValuePairs, depth + 1);
      return Array.isArray(kv) ? kv : [];
    }
    if (/^System\.Collections\.Generic\.KeyValuePair`2/.test(cls)) return { Key: conv(m.key, depth + 1), Value: conv(m.value, depth + 1) };
    const out = {};
    o.names.forEach((n, i) => { out[cleanMember(n)] = conv(o.vals[i], depth + 1); });
    return out;
  };
  const tree = conv(REF(rootId), 0);
  return tree !== null && typeof tree === 'object' ? tree : { value: tree };
}
function isNrbf(buf) {
  if (buf.length < 17 || buf[0] !== 0) return false;
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  return dv.getInt32(9, true) === 1 && dv.getInt32(13, true) === 0;
}
// Byte arrays holding JSON text (Blasphemous keeps its whole save as JSON in byte[] values) become the
// parsed JSON. Same rule as the app (source/save_formats.cpp, decodeJsonByteArrays).
function decodeJsonBytes(x, depth) {
  if (depth > 64 || !x || typeof x !== 'object') return x;
  if (Array.isArray(x)) {
    if (x.length >= 2 && (x[0] === 0x7B || x[0] === 0x5B) && x.every(v => Number.isInteger(v) && v >= 0 && v <= 255)) {
      try { return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(Uint8Array.from(x))); } catch (e) { }
    }
    return x.map(v => decodeJsonBytes(v, depth + 1));
  }
  for (const k of Object.keys(x)) x[k] = decodeJsonBytes(x[k], depth + 1);
  return x;
}
function parseNrbf(buf) {
  if (!isNrbf(buf)) return null;
  const { rootId, objs } = nrbfParseRecords(buf);
  const root = objs.get(rootId);
  return { nrbf: true, tree: decodeJsonBytes(nrbfBuild(objs, rootId), 0), meta: { rootClass: root && root.cls ? root.cls : typeof root === 'string' ? 'System.String' : '', objects: objs.size } };
}

// ================================================================== XOR obfuscation (repeating key)
// Many games "encrypt" a text save by XOR with a fixed phrase. The app undoes it when the definition has
// "xor_key" (source/achievements.cpp); here we also try known keys and recover unknown ones from the text.
const KNOWN_XOR_KEYS = [
  { key: 'ALL WATCHED OVER BY MACHINES OF LOVING GRACE', game: 'SIGNALIS' },
];
const asBytes = (k) => typeof k === 'string' ? Uint8Array.from(k, c => c.charCodeAt(0) & 0xFF) : k;
function xorBytes(buf, key) {
  const k = asBytes(key), out = new Uint8Array(buf.length);
  if (!k.length) { out.set(buf); return out; }
  for (let i = 0; i < buf.length; i++) out[i] = buf[i] ^ k[i % k.length];
  return out;
}
// Log-weights of characters in JSON / text saves: lowercase, quotes and digits are frequent, control bytes are not.
const TEXT_W = (() => {
  const w = new Float64Array(256).fill(-8);
  const set = (s, v) => { for (const c of s) w[c.charCodeAt(0)] = v; };
  set('abcdefghijklmnopqrstuvwxyz', -2.5); set('etaoinsr', -1.8); set('ABCDEFGHIJKLMNOPQRSTUVWXYZ', -4); set('0123456789', -2.6);
  set('"', -1.5); set(':,{}', -2.5); set('[]._- /=<>', -3.5); set('\n\r\t', -5);
  return w;
})();
// A text XORed with a printable key stays below 0x80: cheap test before any work.
function looksXorText(buf) {
  if (buf.length < 64) return false;
  const n = Math.min(buf.length, 65536);
  let hi = 0, printable = 0;
  for (let i = 0; i < n; i++) { const c = buf[i]; if (c >= 0x80) hi++; else if (c >= 0x20 && c < 0x7F) printable++; }
  return hi <= n * 0.01 && printable < n * 0.97;
}
// Recovers a repeating key (1..128 bytes) from the first 4 KB, assuming the plaintext is text.
function guessXorKey(buf, maxLen = 128) {
  const b = buf.subarray(0, Math.min(buf.length, 4096));
  let best = null;
  for (let L = 1; L <= Math.min(maxLen, b.length >> 3); L++) {
    let tot = 0;
    const key = new Uint8Array(L);
    for (let c = 0; c < L; c++) {
      let bk = 0, bs = -Infinity;
      for (let k = 0; k < 256; k++) { let sc = 0; for (let i = c; i < b.length; i += L) sc += TEXT_W[b[i] ^ k]; if (sc > bs) { bs = sc; bk = k; } }
      key[c] = bk; tot += bs;
    }
    const score = tot / b.length;
    if (!best || score > best.score + 0.05) best = { score, key };   // a multiple of the true length scores barely better
  }
  // printable keys only: the definition stores the key as text
  return best && best.score > -3.2 && best.key.every(c => c >= 0x20 && c < 0x7F) ? latin1(best.key) : null;
}
// Returns {key, data, known} when `parse(data)` accepts the de-XORed bytes, else null.
function unwrapXor(buf, parse) {
  if (!looksXorText(buf)) return null;
  const tryKey = (key, known) => { const data = xorBytes(buf, key); try { if (parse(data)) return { key, data, known }; } catch (e) { } return null; };
  for (const k of KNOWN_XOR_KEYS) { const r = tryKey(k.key, k.game); if (r) return r; }
  const g = guessXorKey(buf);
  return g ? tryKey(g, '') : null;
}

// ================================================================== Supergiant "SGB1" (Hades)
// Header (version 15/16) + LZ4 block + luabins Lua state. Same tree as the app (source/save_formats.cpp):
// the saved Lua globals, every table as an object (number keys "1", "2"...), plus "_header".
function lz4Block(src, maxOut) {
  let out = new Uint8Array(Math.min(maxOut, Math.max(1024, src.length * 8))), o = 0, p = 0;
  const grow = (need) => { if (o + need <= out.length) return; if (o + need > maxOut) throw new Error('LZ4: output too large'); let n = out.length; while (n < o + need) n *= 2; const b = new Uint8Array(Math.min(n, maxOut)); b.set(out.subarray(0, o)); out = b; };
  while (p < src.length) {
    const tok = src[p++];
    let lit = tok >> 4;
    if (lit === 15) { let b; do { if (p >= src.length) throw new Error('LZ4: truncated'); b = src[p++]; lit += b; } while (b === 255); }
    if (lit > src.length - p) throw new Error('LZ4: truncated literals');
    grow(lit); out.set(src.subarray(p, p + lit), o); o += lit; p += lit;
    if (p >= src.length) break;
    if (p + 2 > src.length) throw new Error('LZ4: truncated offset');
    const off = src[p] | (src[p + 1] << 8); p += 2;
    if (!off || off > o) throw new Error('LZ4: bad offset');
    let len = tok & 15;
    if (len === 15) { let b; do { if (p >= src.length) throw new Error('LZ4: truncated'); b = src[p++]; len += b; } while (b === 255); }
    len += 4;
    grow(len);
    for (let i = 0; i < len; i++) { out[o] = out[o - off]; o++; }
  }
  return out.subarray(0, o);
}
function isSgb(buf) {
  if (buf.length < 16 || buf[0] !== 0x53 || buf[1] !== 0x47 || buf[2] !== 0x42 || buf[3] !== 0x31) return false;
  const v = buf[8] | (buf[9] << 8) | (buf[10] << 16) | (buf[11] << 24);
  return v === 15 || v === 16;
}
function parseSgb(buf) {
  if (!isSgb(buf)) return null;
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength), dec = new TextDecoder();
  let p = 8;
  const u32 = () => { if (p + 4 > buf.length) throw new Error('SGB1: truncated header'); const v = dv.getUint32(p, true); p += 4; return v; };
  const str = () => { const n = u32(); if (p + n > buf.length) throw new Error('SGB1: truncated header'); const s = dec.decode(buf.subarray(p, p + n)); p += n; return s; };
  const version = u32();
  if (version >= 16) p += 8;   // timestamp
  const location = str(), runs = u32(), meta = u32(), shrine = u32(), god = buf[p], hell = buf[p + 1];
  p += 2;
  const nk = u32(); for (let i = 0; i < nk; i++) str();
  const map = str(); str();
  const packed = u32();
  if (p + packed > buf.length) throw new Error('SGB1: truncated');
  const raw = lz4Block(buf.subarray(p, p + packed), 9388032), rv = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
  let q = 0;
  const key = (k) => typeof k === 'number' ? (Number.isInteger(k) ? String(k) : String(k)) : typeof k === 'boolean' ? String(k) : String(k ?? '');
  const val = (depth) => {
    if (depth > 64) throw new Error('Lua tables nested too deep');
    if (q >= raw.length) throw new Error('truncated Lua state');
    const t = raw[q++];
    if (t === 0x2D) return null;           // '-'
    if (t === 0x30) return false;          // '0'
    if (t === 0x31) return true;           // '1'
    if (t === 0x4E) { const v = rv.getFloat64(q, true); q += 8; return v; }   // 'N'
    if (t === 0x53) { const n = rv.getUint32(q, true); q += 4; const s = dec.decode(raw.subarray(q, q + n)); q += n; return s; }   // 'S'
    if (t === 0x54) {   // 'T'
      const na = rv.getUint32(q, true), nh = rv.getUint32(q + 4, true); q += 8;
      if (na + nh > (raw.length - q) / 2) throw new Error('bad Lua table size');
      const o = {};
      for (let i = 0; i < na + nh; i++) { const k = val(depth + 1); o[key(k)] = val(depth + 1); }
      return o;
    }
    throw new Error('unknown Lua value tag 0x' + t.toString(16) + ' at ' + (q - 1));
  };
  const count = raw[q++];
  let tree = null;
  for (let i = 0; i < count; i++) { const v = val(0); if (!tree && v && typeof v === 'object') tree = v; }
  tree = tree || {};
  tree._header = { version, location, runs, meta_points: meta, shrine_points: shrine, god_mode: god, hell_mode: hell, map };
  return { sgb: true, tree, meta: { version, runs, location, luaBytes: raw.length } };
}

const api = { Reader, parseGvas, isGvas, parseNrbf, isNrbf, parseSgb, isSgb, lz4Block, KNOWN_XOR_KEYS, xorBytes, looksXorText, guessXorKey, unwrapXor };
if (typeof module !== 'undefined' && module.exports) module.exports = api;
else root.SaveFormats = api;
})(typeof self !== 'undefined' ? self : this);
