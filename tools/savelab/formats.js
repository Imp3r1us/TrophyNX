// Save Lab format modules — binary save formats decoded into the same kind of tree as BYML / JSON,
// so that every path-based tool (differences, ⚡ generate, rules) works on them unchanged.
// Each module mirrors a reader of the Switch app; the trees must be identical on both sides:
//   gvas  Unreal Engine 4 / 5 SaveGame files (.sav, magic "GVAS")  — source/gvas.cpp
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

// UE 5.4+ (package version 1012, PROPERTY_TAG_COMPLETE_TYPE_NAME): the type is a tree of names
// (name, number of parameters, parameters...), e.g. ArrayProperty(StructProperty(TrophyData(/Game/...), <guid>)).
function readTypeName(r, depth = 0) {
  if (depth > 16) throw new Error(T('property type nested too deeply', 'type de propriété trop imbriqué'));
  const name = gvasFstr(r), n = r.i32();
  if (n < 0 || n > 64) throw new Error(T('invalid property type', 'type de propriété invalide'));
  const params = [];
  for (let i = 0; i < n; i++) params.push(readTypeName(r, depth + 1));
  return { name, params };
}
const param = (tn, i) => (tn && tn.params[i]) || null;
const pname = (tn, i) => { const p = param(tn, i); return p ? p.name : ''; };
// Flags byte: 1 array index, 2 property GUID, 4 extensions, 8 binary/native serialize, 0x10 bool true, 0x20 skipped.
function readTag54(r, name) {
  const tn = readTypeName(r);
  const type = tn.name;
  if (!/^[A-Za-z0-9_]{1,64}Property$/.test(type)) throw new Error(T('invalid property type “', 'type de propriété invalide « ') + type.slice(0, 40) + T('”', ' »'));
  const t = { name, type, size: r.i32(), index: 0 };
  const flags = r.u8();
  if (flags & 1) t.index = r.i32();
  if (flags & 2) r.skip(16);
  if (flags & 4) { const ext = r.u8(); if (ext & 2) { r.u8(); r.u32(); } }   // overridable information
  switch (type) {
    case 'StructProperty': t.struct = pname(tn, 0); break;
    case 'BoolProperty': t.bool = flags & 0x10 ? 1 : 0; break;
    case 'ByteProperty': t.enumName = pname(tn, 0) || 'None'; break;
    case 'EnumProperty': t.enumName = pname(tn, 0); break;
    case 'ArrayProperty': case 'SetProperty': t.inner = pname(tn, 0); t.innerStruct = pname(param(tn, 0), 0); break;
    case 'MapProperty': t.key = pname(tn, 0); t.value = pname(tn, 1); t.keyStruct = pname(param(tn, 0), 0); t.valueStruct = pname(param(tn, 1), 0); break;
  }
  return t;
}
function readTag(r, ctx) {
  const name = gvasFstr(r);
  if (name === 'None') return null;
  if (!PROP_NAME.test(name)) throw new Error(T('invalid property name at 0x', 'nom de propriété invalide à 0x') + r.o.toString(16).toUpperCase());
  if (ctx.tags54) return readTag54(r, name);
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
    const t = readTag(r, ctx);
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
    case 'ArrayProperty': return readArray(r, t.inner, t.size, ctx, depth, t.innerStruct);
    case 'SetProperty': {
      const rm = r.i32(); if (rm < 0 || rm > t.size) throw new Error(T('invalid set', 'set invalide'));
      for (let i = 0; i < rm; i++) readElem(r, t.inner, ctx, depth, t.innerStruct);
      const n = r.i32(); if (n < 0 || n > t.size) throw new Error(T('invalid set', 'set invalide'));
      const out = []; for (let i = 0; i < n; i++) out.push(readElem(r, t.inner, ctx, depth, t.innerStruct));
      return out;
    }
    case 'MapProperty': {
      const rm = r.i32(); if (rm < 0 || rm > t.size) throw new Error(T('invalid map', 'map invalide'));
      for (let i = 0; i < rm; i++) readElem(r, t.key, ctx, depth, t.keyStruct);
      const n = r.i32(); if (n < 0 || n > t.size) throw new Error(T('invalid map', 'map invalide'));
      const out = [];
      for (let i = 0; i < n; i++) { const Key = readElem(r, t.key, ctx, depth, t.keyStruct); const Value = readElem(r, t.value, ctx, depth, t.valueStruct); out.push({ Key, Value }); }
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
function readArray(r, inner, size, ctx, depth, innerStruct) {
  const n = r.i32();
  if (n < 0 || n > size) throw new Error(T('invalid array', 'tableau invalide'));
  const out = [];
  if (inner === 'StructProperty' && ctx.tags54) {   // 5.4+: no inner tag, the struct name is in the type
    const each = n ? (size - 4) / n : 0;
    for (let i = 0; i < n; i++) out.push(readStruct(r, innerStruct, ctx, depth, each));
    return out;
  }
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
  const major = r.u16(), minor = r.u16(), patch = r.u16(); r.u32(); const branch = gvasFstr(r);
  const cvf = r.u32(), ncv = r.u32();
  if (ncv > 10000) throw new Error(T('invalid GVAS header', 'en-tête GVAS invalide'));
  for (let i = 0; i < ncv; i++) { if (cvf === 1) r.skip(8); else { r.skip(20); if (cvf === 2) gvasFstr(r); } }
  const cls = gvasFstr(r);
  const tags54 = ue5 >= 1012;
  if (tags54) r.u8();    // 5.4+: one byte after the class name (0 in every save seen)
  const ctx = { ue4, ue5, lwc: ue5 >= 1004, tags54, skipped: 0, why: [], budget: 2e6 };
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

// ================================================================== DELTARUNE (deltarune.sav)
// Same tree as deltaruneToJson in source/save_formats.cpp (see include/save_formats.h): a JSON object
// {file name: file text}; "filechN_S" texts are the lines of scr_saveprocess (console branch), arrays as
// GameMaker ds_list_write hex (u32 0x12F, u32 count, per entry u32 type + value).
const DR_FLAG_BASE = 2400, DR_TROPHIES = 30;
function isDeltarune(buf) {
  let s = buf[0] === 0xEF && buf[1] === 0xBB && buf[2] === 0xBF ? 3 : 0;
  while (s < buf.length && (buf[s] === 0x20 || buf[s] === 0x09 || buf[s] === 0x0D || buf[s] === 0x0A)) s++;
  if (buf[s] !== 0x7B) return false;
  const key = [0x22, 0x66, 0x69, 0x6C, 0x65, 0x63, 0x68];   // "filech
  outer: for (let i = s; i + 7 <= buf.length; i++) {
    if (buf[i] !== 0x22) continue;
    for (let k = 1; k < 7; k++) if (buf[i + k] !== key[k]) continue outer;
    return true;
  }
  return false;
}
const DR_NUM = /^[-+]?\d+(\.\d+)?([eE][-+]?\d+)?$/;
const drNumber = (s) => { if (!DR_NUM.test(s)) return null; const v = parseFloat(s); return Number.isFinite(v) ? v : null; };
const isDsListLine = (s) => s.length >= 16 && !(s.length & 1) && /^2F010000/i.test(s) && /^[0-9A-Fa-f]*$/.test(s);
function dsListDecode(s) {
  const b = new Uint8Array(s.length / 2);
  for (let i = 0; i < b.length; i++) b[i] = parseInt(s.substr(2 * i, 2), 16);
  const dv = new DataView(b.buffer);
  let p = 4;
  const need = (n) => b.length - p >= n;
  if (!need(4)) return null;
  const count = dv.getUint32(p, true); p += 4;
  if (count > Math.floor((b.length - 8) / 4)) return null;
  const out = [];
  for (let i = 0; i < count; i++) {
    if (!need(4)) return null;
    const type = dv.getUint32(p, true); p += 4;
    if (type === 0 || type === 13) { if (!need(8)) return null; const v = dv.getFloat64(p, true); p += 8; out.push(Number.isFinite(v) ? v : null); }
    else if (type === 1) {
      if (!need(4)) return null;
      const len = dv.getUint32(p, true); p += 4;
      if (len > b.length - p) return null;
      out.push(cut(utf8.decode(b.subarray(p, p + len)))); p += len;
    } else if (type === 5) out.push(null);
    else if (type === 7) { if (!need(4)) return null; out.push(dv.getInt32(p, true)); p += 4; }
    else if (type === 10) { if (!need(8)) return null; out.push(Number(dv.getBigInt64(p, true))); p += 8; }
    else return null;
  }
  return out;
}
function drLines(t) {
  const l = cut(t).split('\n');
  if (l.length && l[l.length - 1] === '') l.pop();
  return l.map(x => x.endsWith('\r') ? x.slice(0, -1) : x);
}
function drLayout(chapter) {
  const one = chapter === 1, l = [['truename', 'S', 1], ['othername', 'L', 1], ['char', 'C', 1], ['char', 'C', 1], ['char', 'C', 1],
    ['gold', 'N', 1], ['xp', 'N', 1], ['lv', 'N', 1], ['inv', 'X', 1], ['invc', 'X', 1], ['darkzone', 'N', 1]];
  for (const n of ['hp', 'maxhp', 'at', 'df', 'mag', 'guts', 'charweapon', 'chararmor1', 'chararmor2', 'weaponstyle']) l.push([n, 'L', 1]);
  l.push(['items', 'X', one ? 4 * (4 * 8 + 12) : 5 * (4 * 10 + 12)], ['bolts', 'X', 3]);
  for (const n of ['item', 'keyitem', 'weapon', 'armor']) l.push([n, 'L', 1]);
  if (!one) l.push(['pocketitem', 'L', 1]);
  for (const n of ['tension', 'maxtension', 'lweapon', 'larmor', 'lxp', 'llv', 'lgold', 'lhp', 'lmaxhp', 'lat', 'ldf', 'lwstrength', 'ladef']) l.push([n, 'N', 1]);
  l.push(['litem', 'L', 1], ['phone', 'L', 1], ['flags', 'F', 1], ['plot', 'N', 1], ['room', 'N', 1], ['time', 'N', 1]);
  return l;
}
function drFlags(v) {
  const o = {};
  v.forEach((x, i) => { if (x !== null && x !== 0 && x !== '') o[String(i)] = x; });
  return o;
}
const drScalar = (line, kind) => { if (kind !== 'S') { const v = drNumber(line); if (v !== null) return v; } return line; };
function drChapterFile(name, chapter, slot, route, text) {
  const f = { name, chapter, slot: slot % 3, completion: slot >= 3 ? 1 : 0, route };
  const lines = drLines(text), lay = drLayout(chapter);
  let at = 0, fits = true;
  for (const [, kind, count] of lay) {
    if (at + count > lines.length) { fits = false; break; }
    if ((kind === 'L' || kind === 'F') !== isDsListLine(lines[at]) && kind !== 'X' && kind !== 'S') { fits = false; break; }
    at += count;
  }
  if (fits) {
    at = 0;
    for (const [n, kind, count] of lay) {
      const ln = lines[at];
      at += count;
      if (kind === 'X') continue;
      if (kind === 'C') { (f.char = f.char || []).push(drScalar(ln, 'N')); continue; }
      if (kind === 'S' || kind === 'N') { f[n] = drScalar(ln, kind); continue; }
      const v = dsListDecode(ln);
      f[n] = v === null ? null : kind === 'F' ? drFlags(v) : v;
    }
    return f;
  }
  f.error = 'unknown file layout';
  for (let i = lines.length - 1; i >= 0; i--) {
    if (!isDsListLine(lines[i])) continue;
    const v = dsListDecode(lines[i]);
    if (!v || v.length < 2500) continue;
    f.flags = drFlags(v);
    ['plot', 'room', 'time'].forEach((k, j) => { if (i + 1 + j < lines.length) f[k] = drScalar(lines[i + 1 + j], 'N'); });
    break;
  }
  return f;
}
function drIni(text) {
  const o = {};
  let sec = null;
  for (let ln of drLines(text)) {
    ln = ln.replace(/^[ \t]+|[ \t]+$/g, '');
    if (!ln) continue;
    if (ln[0] === '[' && ln.endsWith(']')) { const n = ln.slice(1, -1); sec = o[n] = o[n] || {}; continue; }
    const eq = ln.indexOf('=');
    if (!sec || eq <= 0) continue;
    const k = ln.slice(0, eq).replace(/[ \t]+$/, '');
    let v = ln.slice(eq + 1).replace(/^[ \t]+/, '');
    if (v.length >= 2 && v[0] === '"' && v.endsWith('"')) v = v.slice(1, -1);
    delete sec[k];
    const x = drNumber(v);
    sec[k] = x !== null ? x : v;
  }
  return o;
}
function parseDeltarune(buf) {
  if (!isDeltarune(buf)) return null;
  let s = buf[0] === 0xEF ? 3 : 0, e = buf.length;
  while (e > s && (buf[e - 1] === 0 || buf[e - 1] === 0x20 || buf[e - 1] === 0x0D || buf[e - 1] === 0x0A || buf[e - 1] === 0x09)) e--;
  const src = JSON.parse(utf8.decode(buf.subarray(s, e)));
  if (!src || typeof src !== 'object' || Array.isArray(src)) throw new Error(T('not a JSON object', 'pas un objet JSON'));
  const FN = /^filech(\d{1,3})_(\d{1,3})([\s\S]*)$/;
  const names = Object.keys(src).filter(k => typeof src[k] === 'string');
  const chapterFiles = names.filter(k => FN.test(k)).sort(), inis = names.filter(k => !FN.test(k) && k.length > 4 && k.endsWith('.ini')).sort();
  const tree = { files: [], ini: {} };
  for (const k of chapterFiles) { const m = FN.exec(k); tree.files.push(drChapterFile(k, +m[1], +m[2], m[3], src[k])); }
  for (const k of inis) tree.ini[k] = drIni(src[k]);
  const completed = [0, 0, 0, 0, 0, 0], patch = new Array(DR_TROPHIES).fill(0), fb = new Array(DR_TROPHIES).fill(0);
  const flag = (f, i) => { const v = f.flags && f.flags[String(i)]; return typeof v === 'number' ? v : 0; };
  const has = (f, list, value, anyNonZero) => Array.isArray(f[list]) && f[list].some(x => typeof x === 'number' && (anyNonZero ? x !== 0 : x === value));
  for (const f of tree.files) {
    const ch = typeof f.chapter === 'number' ? f.chapter : 0;
    if (ch >= 1 && ch <= 5 && f.completion === 1) completed[ch] = 1;
    for (let i = 0; i < DR_TROPHIES; i++) if (flag(f, DR_FLAG_BASE + i) >= 1) patch[i] = 1;
    if (flag(f, 106) >= 1) fb[9] = 1;
    if (ch === 1 && (has(f, 'chararmor1', 0, true) || has(f, 'chararmor2', 0, true))) fb[14] = 1;
    if (ch >= 2 && flag(f, 325) === 1) fb[18] = 1;
    if (ch >= 3 && flag(f, 1220) >= 1) fb[22] = 1;
    if (ch >= 2 && (has(f, 'weapon', 13) || has(f, 'charweapon', 13) || has(f, 'weapon', 22) || has(f, 'charweapon', 22))) fb[27] = 1;
  }
  for (let n = 1; n <= 4; n++) fb[n] = completed[n];
  const trophy = patch.map((p, i) => (p || fb[i]) ? 1 : 0);
  trophy[0] = patch[0] || trophy.slice(1).every(x => x) ? 1 : 0;
  Object.assign(tree, { completed, patch, trophy });
  const meta = { files: chapterFiles.length, completed: completed.map((c, i) => c ? i : 0).filter(Boolean), trophies: trophy.filter(Boolean).length, patched: patch.filter(Boolean).length };
  return { deltarune: true, tree, meta };
}

// ================================================================== MessagePack (generic tree decoder)
// Reusable by any game that stores MessagePack. Mirror: msgpackToJson in source/save_formats.cpp. Tree mapping:
//   nil -> null · bool · integers (up to 64-bit) -> number · float32 -> number rounded to 6 decimals (like the
//   other readers) · float64 -> number · non-finite -> null · str -> string (cut at NUL) · bin -> uppercase hex
//   array -> list · map -> object: string keys as is, integer keys in decimal (exact, 64-bit), nil/true/false keys
//   by name, other keys "<float>" / "<bin>" / "<array>" / "<map>" / "<ext>"; a repeated key keeps its first place
//   and its last value · ext -1 (timestamp) -> whole seconds since 1970 (nanoseconds dropped) · other ext ->
//   "ext:<type>:<HEX>". JavaScript lists integer-like object keys (0 .. 2^32-2) first in ascending order: the app
//   orders its objects the same way so both trees compare equal.
const MP_MAX_DEPTH = 64;
const hexOf = (b) => { let s = ''; for (let i = 0; i < b.length; i++) s += (b[i] < 16 ? '0' : '') + b[i].toString(16); return s.toUpperCase(); };
function parseMsgpack(buf) {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  let o = 0;
  const meta = { maps: 0, arrays: 0, ext: 0, timestamps: 0 };
  const need = (n) => { if (n < 0 || o + n > buf.length) throw new Error(T('MessagePack: unexpected end at 0x', 'MessagePack : fin inattendue à 0x') + o.toString(16).toUpperCase()); };
  const u8 = () => { need(1); return buf[o++]; };
  const be = (n, signed) => {
    need(n); let v;
    if (n === 1) v = signed ? dv.getInt8(o) : dv.getUint8(o);
    else if (n === 2) v = signed ? dv.getInt16(o) : dv.getUint16(o);
    else if (n === 4) v = signed ? dv.getInt32(o) : dv.getUint32(o);
    else v = signed ? dv.getBigInt64(o) : dv.getBigUint64(o);
    o += n; return v;
  };
  const bytes = (n) => { need(n); const b = buf.subarray(o, o + n); o += n; return b; };
  const str = (n) => cut(utf8.decode(bytes(n)));
  const count = (n) => { if (n > buf.length - o) throw new Error(T('MessagePack: bad length at 0x', 'MessagePack : longueur invalide à 0x') + o.toString(16).toUpperCase()); return n; };
  const ext = (type, n) => {
    const b = bytes(n); meta.ext++;
    if (type === -1 && (n === 4 || n === 8 || n === 12)) {
      meta.timestamps++;
      const d = new DataView(b.buffer, b.byteOffset, n);
      if (n === 4) return d.getUint32(0);
      if (n === 8) return d.getUint32(0) % 4 * 4294967296 + d.getUint32(4);   // low 34 bits
      return Number(d.getBigInt64(4));
    }
    return 'ext:' + type + ':' + hexOf(b);
  };
  // value(depth, asKey): asKey -> the key text (scalars) or a placeholder (containers, skipped)
  const value = (depth, asKey) => {
    if (depth > MP_MAX_DEPTH) throw new Error(T('MessagePack: nested too deep', 'MessagePack : imbrication trop profonde'));
    const t = u8();
    let n;
    if (t <= 0x7f) return asKey ? String(t) : t;
    if (t >= 0xe0) return asKey ? String(t - 256) : t - 256;
    if (t >= 0x80 && t <= 0x8f) return map(t & 15, depth, asKey);
    if (t >= 0x90 && t <= 0x9f) return arr(t & 15, depth, asKey);
    if (t >= 0xa0 && t <= 0xbf) return str(t & 31);
    switch (t) {
      case 0xc0: return asKey ? 'null' : null;
      case 0xc2: return asKey ? 'false' : false;
      case 0xc3: return asKey ? 'true' : true;
      case 0xc4: case 0xc5: case 0xc6: n = be(1 << (t - 0xc4), false); { const h = hexOf(bytes(n)); return asKey ? '<bin>' : h; }
      case 0xc7: case 0xc8: case 0xc9: { n = be(1 << (t - 0xc7), false); const ty = be(1, true); const v = ext(ty, n); return asKey ? '<ext>' : v; }
      case 0xca: { need(4); const v = dv.getFloat32(o); o += 4; return asKey ? '<float>' : fin(+v.toFixed(6)); }
      case 0xcb: { need(8); const v = dv.getFloat64(o); o += 8; return asKey ? '<float>' : fin(v); }
      case 0xcc: case 0xcd: case 0xce: case 0xcf: { const v = be(1 << (t - 0xcc), false); return asKey ? String(v) : Number(v); }
      case 0xd0: case 0xd1: case 0xd2: case 0xd3: { const v = be(1 << (t - 0xd0), true); return asKey ? String(v) : Number(v); }
      case 0xd4: case 0xd5: case 0xd6: case 0xd7: case 0xd8: { const ty = be(1, true); const v = ext(ty, 1 << (t - 0xd4)); return asKey ? '<ext>' : v; }
      case 0xd9: case 0xda: case 0xdb: return str(be(1 << (t - 0xd9), false));
      case 0xdc: case 0xdd: return arr(be(t === 0xdc ? 2 : 4, false), depth, asKey);
      case 0xde: case 0xdf: return map(be(t === 0xde ? 2 : 4, false), depth, asKey);
    }
    throw new Error(T('MessagePack: invalid byte 0x', 'MessagePack : octet invalide 0x') + t.toString(16).toUpperCase() + T(' at 0x', ' à 0x') + (o - 1).toString(16).toUpperCase());
  };
  const arr = (n, depth, asKey) => {
    count(n); meta.arrays++;
    const a = [];
    for (let i = 0; i < n; i++) { const v = value(depth + 1, false); if (!asKey) a.push(v); }
    return asKey ? '<array>' : a;
  };
  const map = (n, depth, asKey) => {
    count(n); meta.maps++;
    const m = {};
    for (let i = 0; i < n; i++) {
      const k = value(depth + 1, true), v = value(depth + 1, false);
      if (asKey) continue;
      if (k === '__proto__') Object.defineProperty(m, k, { value: v, enumerable: true, writable: true, configurable: true });
      else m[k] = v;
    }
    return asKey ? '<map>' : m;
  };
  const tree = value(0, false);
  meta.size = buf.length; meta.trailing = buf.length - o;
  return { tree, meta };
}

// ================================================================== small synchronous crypto + raw inflate
// The Save Lab parses trees synchronously (differences, rules, ⚡ Auto all call parseTree), so WebCrypto and
// DecompressionStream (asynchronous) cannot be used here. Same code in C++ (source/save_formats.cpp).
function sha1(msg) {
  const ml = msg.length, n = ((ml + 8) >> 6) + 1, w = new Uint32Array(80), b = new Uint8Array(n * 64);
  b.set(msg); b[ml] = 0x80;
  const bits = ml * 8; b[n * 64 - 4] = bits >>> 24; b[n * 64 - 3] = bits >>> 16; b[n * 64 - 2] = bits >>> 8; b[n * 64 - 1] = bits;
  let h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  for (let c = 0; c < n; c++) {
    for (let i = 0; i < 16; i++) { const p = c * 64 + i * 4; w[i] = (b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]; }
    for (let i = 16; i < 80; i++) { const x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = (x << 1) | (x >>> 31); }
    let a = h0, bb = h1, cc = h2, d = h3, e = h4;
    for (let i = 0; i < 80; i++) {
      const f = i < 20 ? ((bb & cc) | (~bb & d)) + 0x5A827999 : i < 40 ? (bb ^ cc ^ d) + 0x6ED9EBA1 : i < 60 ? ((bb & cc) | (bb & d) | (cc & d)) + 0x8F1BBCDC : (bb ^ cc ^ d) + 0xCA62C1D6;
      const t = (((a << 5) | (a >>> 27)) + f + e + w[i]) | 0;
      e = d; d = cc; cc = (bb << 30) | (bb >>> 2); bb = a; a = t;
    }
    h0 = (h0 + a) | 0; h1 = (h1 + bb) | 0; h2 = (h2 + cc) | 0; h3 = (h3 + d) | 0; h4 = (h4 + e) | 0;
  }
  const out = new Uint8Array(20);
  [h0, h1, h2, h3, h4].forEach((h, i) => { out[i * 4] = h >>> 24; out[i * 4 + 1] = h >>> 16; out[i * 4 + 2] = h >>> 8; out[i * 4 + 3] = h; });
  return out;
}
function hmacSha1(key, msg) {
  if (key.length > 64) key = sha1(key);
  const ip = new Uint8Array(64 + msg.length), op = new Uint8Array(84);
  for (let i = 0; i < 64; i++) { const k = i < key.length ? key[i] : 0; ip[i] = k ^ 0x36; op[i] = k ^ 0x5c; }
  ip.set(msg, 64); op.set(sha1(ip), 64);
  return sha1(op);
}
// PBKDF2-HMAC-SHA1, dkLen <= 20 (one block)
function pbkdf2Sha1(pw, salt, iter, dkLen) {
  const s = new Uint8Array(salt.length + 4); s.set(salt); s[salt.length + 3] = 1;
  let u = hmacSha1(pw, s); const t = u.slice();
  for (let i = 1; i < iter; i++) { u = hmacSha1(pw, u); for (let j = 0; j < 20; j++) t[j] ^= u[j]; }
  return t.subarray(0, dkLen);
}
const AES = (() => {
  const xt = (a) => ((a << 1) ^ (a & 0x80 ? 0x11b : 0)) & 0xff;
  const mul = (a, b) => { let r = 0; while (b) { if (b & 1) r ^= a; a = xt(a); b >>= 1; } return r; };
  const sbox = new Uint8Array(256), inv = new Uint8Array(256);
  for (let i = 0; i < 256; i++) {
    let x = 0; if (i) for (let j = 1; j < 256; j++) if (mul(i, j) === 1) { x = j; break; }
    let y = x; for (let k = 0; k < 4; k++) { x = ((x << 1) | (x >> 7)) & 0xff; y ^= x; }
    sbox[i] = y ^ 0x63; inv[sbox[i]] = i;
  }
  const m = [9, 11, 13, 14].map(k => Uint8Array.from({ length: 256 }, (_, i) => mul(i, k)));
  return { sbox, inv, m9: m[0], m11: m[1], m13: m[2], m14: m[3], xt };
})();
function aes128CbcDecrypt(key, iv, data) {
  const { sbox, inv, m9, m11, m13, m14, xt } = AES;
  const rk = new Uint8Array(176); rk.set(key);
  for (let i = 16, rcon = 1; i < 176; i += 4) {
    let t = rk.slice(i - 4, i);
    if (i % 16 === 0) { t = Uint8Array.of(sbox[t[1]] ^ rcon, sbox[t[2]], sbox[t[3]], sbox[t[0]]); rcon = xt(rcon); }
    for (let j = 0; j < 4; j++) rk[i + j] = rk[i - 16 + j] ^ t[j];
  }
  const out = new Uint8Array(data.length), s = new Uint8Array(16), u = new Uint8Array(16);
  for (let off = 0; off < data.length; off += 16) {
    for (let i = 0; i < 16; i++) s[i] = data[off + i] ^ rk[160 + i];
    for (let r = 9; r >= 0; r--) {
      for (let c = 0; c < 4; c++) for (let w = 0; w < 4; w++) u[w + 4 * c] = inv[s[w + 4 * ((c - w + 4) % 4)]];
      for (let i = 0; i < 16; i++) u[i] ^= rk[r * 16 + i];
      if (r) for (let c = 0; c < 4; c++) {
        const a0 = u[4 * c], a1 = u[4 * c + 1], a2 = u[4 * c + 2], a3 = u[4 * c + 3];
        s[4 * c] = m14[a0] ^ m11[a1] ^ m13[a2] ^ m9[a3]; s[4 * c + 1] = m9[a0] ^ m14[a1] ^ m11[a2] ^ m13[a3];
        s[4 * c + 2] = m13[a0] ^ m9[a1] ^ m14[a2] ^ m11[a3]; s[4 * c + 3] = m11[a0] ^ m13[a1] ^ m9[a2] ^ m14[a3];
      } else s.set(u);
    }
    for (let i = 0; i < 16; i++) out[off + i] = s[i] ^ (off ? data[off - 16 + i] : iv[i]);
  }
  return out;
}
// Raw DEFLATE (RFC 1951, no zlib header), after Mark Adler's puff.c. Throws on malformed data or > maxOut bytes.
const INF_LBASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258];
const INF_LEXT = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
const INF_DBASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577];
const INF_DEXT = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13];
const INF_ORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];
function inflateRawSync(src, maxOut = 64 << 20) {
  const bad = (m) => new Error(T('DEFLATE: ', 'DEFLATE : ') + m);
  let pos = 0, bitbuf = 0, bitcnt = 0, out = new Uint8Array(Math.max(1024, src.length * 4)), n = 0;
  const bits = (k) => {
    while (bitcnt < k) { if (pos >= src.length) throw bad(T('unexpected end', 'fin inattendue')); bitbuf |= src[pos++] << bitcnt; bitcnt += 8; }
    const v = bitbuf & ((1 << k) - 1); bitbuf >>>= k; bitcnt -= k; return v;
  };
  const put = (b) => {
    if (n >= out.length) { if (n >= maxOut) throw bad(T('output too large', 'sortie trop grande')); const o2 = new Uint8Array(Math.min(maxOut, out.length * 2)); o2.set(out); out = o2; }
    out[n++] = b;
  };
  const build = (lens) => {
    const count = new Uint16Array(16), sym = new Uint16Array(lens.length), offs = new Uint16Array(16);
    for (const l of lens) count[l]++;
    let left = 1;
    for (let l = 1; l < 16; l++) { left = left * 2 - count[l]; if (left < 0) throw bad(T('bad code lengths', 'longueurs de codes invalides')); }
    for (let l = 1; l < 15; l++) offs[l + 1] = offs[l] + count[l];
    for (let s = 0; s < lens.length; s++) if (lens[s]) sym[offs[lens[s]]++] = s;
    return { count, sym };
  };
  const decode = (h) => {
    let code = 0, first = 0, index = 0;
    for (let l = 1; l < 16; l++) {
      code |= bits(1);
      const c = h.count[l];
      if (code - c < first) return h.sym[index + code - first];
      index += c; first = (first + c) << 1; code <<= 1;
    }
    throw bad(T('bad code', 'code invalide'));
  };
  let fixed = null;
  for (;;) {
    const last = bits(1), type = bits(2);
    if (type === 0) {
      bitbuf = 0; bitcnt = 0;
      if (pos + 4 > src.length) throw bad(T('unexpected end', 'fin inattendue'));
      const len = src[pos] | (src[pos + 1] << 8), nlen = src[pos + 2] | (src[pos + 3] << 8); pos += 4;
      if (len !== (~nlen & 0xffff)) throw bad(T('bad stored block', 'bloc stocké invalide'));
      if (pos + len > src.length) throw bad(T('unexpected end', 'fin inattendue'));
      for (let i = 0; i < len; i++) put(src[pos++]);
    } else if (type === 1 || type === 2) {
      let lc, dc;
      if (type === 1) {
        if (!fixed) {
          const l = new Uint8Array(288); l.fill(8, 0, 144); l.fill(9, 144, 256); l.fill(7, 256, 280); l.fill(8, 280, 288);
          fixed = [build(l), build(new Uint8Array(30).fill(5))];
        }
        [lc, dc] = fixed;
      } else {
        const nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) throw bad(T('bad counts', 'comptes invalides'));
        const cl = new Uint8Array(19);
        for (let i = 0; i < ncode; i++) cl[INF_ORDER[i]] = bits(3);
        const ch = build(cl), lens = new Uint8Array(nlen + ndist);
        for (let i = 0; i < nlen + ndist;) {
          const s = decode(ch);
          if (s < 16) { lens[i++] = s; continue; }
          let len = 0, rep;
          if (s === 16) { if (!i) throw bad(T('repeat without length', 'répétition sans longueur')); len = lens[i - 1]; rep = 3 + bits(2); }
          else if (s === 17) rep = 3 + bits(3);
          else rep = 11 + bits(7);
          if (i + rep > nlen + ndist) throw bad(T('too many lengths', 'trop de longueurs'));
          while (rep--) lens[i++] = len;
        }
        if (!lens[256]) throw bad(T('no end-of-block code', 'pas de code de fin de bloc'));
        lc = build(lens.subarray(0, nlen)); dc = build(lens.subarray(nlen));
      }
      for (;;) {
        let s = decode(lc);
        if (s < 256) { put(s); continue; }
        if (s === 256) break;
        s -= 257;
        if (s >= 29) throw bad(T('bad length code', 'code de longueur invalide'));
        const len = INF_LBASE[s] + bits(INF_LEXT[s]);
        const d = decode(dc);
        if (d >= 30) throw bad(T('bad distance code', 'code de distance invalide'));
        const dist = INF_DBASE[d] + bits(INF_DEXT[d]);
        if (dist > n) throw bad(T('distance too far back', 'distance trop grande'));
        for (let i = 0; i < len; i++) put(out[n - dist]);
      }
    } else throw bad(T('bad block type', 'type de bloc invalide'));
    if (last) break;
  }
  return out.slice(0, n);
}

// ================================================================== THEATRHYTHM FINAL BAR LINE (backup.inf, musicBackup.inf, proficaBackup.inf)
// [u32 LE length, PS4 only] + 32-byte header q0 q1 q2 q3 + AES-128-CBC; salt = q0‖q3, iv = q2‖q1,
// key = PBKDF2-HMAC-SHA1(password, salt, 1000, 16); plaintext = raw DEFLATE of a MessagePack map, PKCS#7 padded.
// The password (a string of the game's global-metadata.dat) is never bundled: setPassword('theatrhythm', …)
// (typed in the page, kept in localStorage); without it the module recognises nothing.
// Tree = the MessagePack tree, with the int-keyed records ([Key(n)] classes of the game) given their field names:
//   MusicInfo/MusicRecordDictionary/<music id>/DifficultyRecordDictionary/<0..3>/PlayStyleRecordDictionary/<0..2>
//   ProficaInfo/MyProfica · ProficaInfo/RivalProficaList[*]
const PASSWORDS = {};
function setPassword(game, pw) { PASSWORDS[game] = typeof pw === 'string' ? pw.trim() : ''; }
const TH_FILES = /(^|[\\/])(backup|musicBackup|proficaBackup)\.inf$/i;
const TH_LABELS = {
  playStyle: ['Highscore', 'UpdateDateUniversal', 'BestPlayRank', 'MaxChainCount', 'BestClearMark', 'CriticalBarZones', 'TotalPlayCount', 'TotalClearCount', 'TotalFullChainClearCount', 'MaxDamage'],
  myProfica: ['MyGuid', 'ProficaIllustID', 'ProficaBaseDesignID', 'ProficaBaseColorType', 'SummonStoneIndex', 'LatestPlayedMusics', 'EditedFlag'],
  rival: ['CursorSelectedFlag', 'LockedFlag', 'ObtainUniversalDateTime', 'LastPlayedMusic', '_4', 'ProficaIllustID', 'ProficaBaseDesignID', 'ProficaBaseColorType', 'MyMoogleID', 'SummonStone', 'AccountName', 'InternalAccountID', 'TotalRhythpo', 'TotalPlayTime', 'TotalAchievement', 'GameCompletion', 'PlayMusicRanking', 'PartySet', 'CharacterUseCount', 'CollecaCategory', 'SeriesCompleteCount', 'BestReachedEndlessStage', 'ClearMusicNumber', 'BestDamageRanking', 'TotalMusicPlayCount', 'TotalWinCount', 'CurrentRate', 'LatestPlayedMusics'],
};
function thLabel(a, names) {
  if (!Array.isArray(a) || a.length !== names.length) return a;
  const o = {}; names.forEach((k, i) => { o[k] = a[i]; }); return o;
}
function theatrhythmLabels(t) {
  const isO = (x) => x && typeof x === 'object' && !Array.isArray(x);
  const mr = isO(t.MusicInfo) && isO(t.MusicInfo.MusicRecordDictionary) ? t.MusicInfo.MusicRecordDictionary : null;
  if (mr) for (const m of Object.values(mr)) {
    if (!isO(m) || !isO(m.DifficultyRecordDictionary)) continue;
    for (const d of Object.values(m.DifficultyRecordDictionary)) {
      if (!isO(d) || !isO(d.PlayStyleRecordDictionary)) continue;
      const ps = d.PlayStyleRecordDictionary;
      for (const k of Object.keys(ps)) ps[k] = thLabel(ps[k], TH_LABELS.playStyle);
    }
  }
  if (isO(t.ProficaInfo)) {
    const p = t.ProficaInfo;
    if ('MyProfica' in p) p.MyProfica = thLabel(p.MyProfica, TH_LABELS.myProfica);
    if (Array.isArray(p.RivalProficaList)) p.RivalProficaList = p.RivalProficaList.map(x => thLabel(x, TH_LABELS.rival));
  }
}
// Container check without the password: Switch = 32-byte header + AES blocks; PS4 = u32 LE length first.
function theatrhythmBody(buf) {
  if (buf.length >= 52 && (buf.length - 4) % 16 === 0 && (buf[0] | (buf[1] << 8) | (buf[2] << 16)) + buf[3] * 16777216 === buf.length - 4) return buf.subarray(4);
  if (buf.length >= 48 && buf.length % 16 === 0) return buf;
  return null;
}
function isTheatrhythm(buf, name) { return !!theatrhythmBody(buf) && (!name || TH_FILES.test(name)); }
function decryptTheatrhythm(buf, password) {
  const body = theatrhythmBody(buf);
  if (!body) return null;
  const q = (i) => body.subarray(8 * i, 8 * i + 8);
  const salt = new Uint8Array(16), iv = new Uint8Array(16);
  salt.set(q(0)); salt.set(q(3), 8); iv.set(q(2)); iv.set(q(1), 8);
  const key = pbkdf2Sha1(new TextEncoder().encode(password), salt, 1000, 16);
  const ct = body.subarray(32);
  // cheap check on the last block first: valid PKCS#7 padding, else not this format / wrong password
  const tail = aes128CbcDecrypt(key, ct.subarray(ct.length - 32, ct.length - 16), ct.subarray(ct.length - 16));
  const pad = tail[15];
  if (pad < 1 || pad > 16) return null;
  for (let i = 16 - pad; i < 16; i++) if (tail[i] !== pad) return null;
  const pt = aes128CbcDecrypt(key, iv, ct);
  return { raw: inflateRawSync(pt.subarray(0, pt.length - pad)), ps4: body !== buf };
}
function parseTheatrhythm(buf) {
  const pw = PASSWORDS.theatrhythm;
  if (!pw || !theatrhythmBody(buf)) return null;
  let dec;
  try { dec = decryptTheatrhythm(buf, pw); } catch (e) { return null; }   // wrong key / not this format
  if (!dec) return null;
  const d = parseMsgpack(dec.raw);
  if (!d.tree || typeof d.tree !== 'object' || Array.isArray(d.tree)) return null;
  theatrhythmLabels(d.tree);
  return { tree: d.tree, meta: Object.assign(d.meta, { ps4: dec.ps4, version: typeof d.tree.Version === 'string' ? d.tree.Version : '', top: Object.keys(d.tree).filter(k => k !== 'Version') }) };
}

// Finds the password among the string literals of the game's global-metadata.dat (IL2CPP v24+), trying them on
// one save file, nearest to the known indexes first. Mirror of theatrhythmFindPassword (save_formats.cpp).
// Asynchronous (yields every few candidates so the page stays responsive); onProgress(done, total).
async function theatrhythmFindPassword(meta, save, onProgress) {
  const dv = new DataView(meta.buffer, meta.byteOffset, meta.byteLength);
  if (meta.length < 32 || dv.getUint32(0, true) !== 0xFAB11BAF) throw new Error(T('not a global-metadata.dat file', 'ce n\'est pas un fichier global-metadata.dat'));
  const lo = dv.getUint32(8, true), lsz = dv.getUint32(12, true), dO = dv.getUint32(16, true), dsz = dv.getUint32(20, true);
  if (lo + lsz > meta.length || dO + dsz > meta.length) throw new Error(T('damaged global-metadata.dat', 'global-metadata.dat abîmé'));
  const body = theatrhythmBody(save);
  if (!body) throw new Error(T('not a THEATRHYTHM save file', 'ce n\'est pas une sauvegarde THEATRHYTHM'));
  const n = lsz / 8 | 0, hints = [12425, 13610];
  const order = Array.from({ length: n }, (_, i) => i).sort((a, b) => Math.min(...hints.map(h => Math.abs(a - h))) - Math.min(...hints.map(h => Math.abs(b - h))) || a - b);
  const salt = new Uint8Array(16); salt.set(body.subarray(0, 8)); salt.set(body.subarray(24, 32), 8);
  const ct = body.subarray(32), tried = new Set();
  for (let k = 0; k < order.length; k++) {
    if (k % 16 === 15) { if (onProgress) onProgress(k, order.length); await new Promise(r => setTimeout(r, 0)); }
    const i = order[k], len = dv.getUint32(lo + 8 * i, true), di = dv.getUint32(lo + 8 * i + 4, true);
    if (len < 4 || len > 128 || di + len > dsz) continue;
    const bytes = meta.subarray(dO + di, dO + di + len), s = latin1(bytes);
    if (tried.has(s)) continue;
    tried.add(s);
    const key = pbkdf2Sha1(bytes, salt, 1000, 16);
    const tail = aes128CbcDecrypt(key, ct.subarray(ct.length - 32, ct.length - 16), ct.subarray(ct.length - 16));
    const pad = tail[15];
    if (pad < 1 || pad > 16 || !tail.subarray(16 - pad).every(x => x === pad)) continue;
    const pw = utf8.decode(bytes);
    try { const d = decryptTheatrhythm(save, pw); if (d && parseMsgpack(d.raw).tree) return { password: pw, index: i }; } catch (e) { }
  }
  return null;
}

// ================================================================== RPG Maker MV / MZ (fileN.rpgsave / fileN.rmmzsave)
// Same tree as rpgMakerToJson in source/save_formats.cpp. MV: LZString.compressToBase64(JsonEx JSON); MZ: zlib of
// the JSON (the binary string often written back as UTF-8); plain JSON accepted. JsonEx markers removed:
// {"@a": [...]} -> the list, "@" / "@c" keys dropped, {"@r": n} -> null. Plus _header {encoding}.
const LZ_B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=';
const lzB64 = (c) => { const i = LZ_B64.indexOf(c); return c.length === 1 ? i : -1; };
function lzStringDecompressB64(input, maxOut = 64 << 20) {
  const len = input.length;
  if (!len) return null;
  const dict = [{ lit: true, ch: 0 }, { lit: true, ch: 1 }, { lit: true, ch: 2 }];
  let val = Math.max(0, lzB64(input[0])), position = 32, index = 1;
  const readBits = (n) => {
    let bits = 0;
    for (let i = 0; i < n; i++) {
      const resb = val & position;
      position >>>= 1;
      if (!position) { position = 32; val = index < len ? Math.max(0, lzB64(input[index])) : 0; index++; }
      bits |= (resb ? 1 : 0) << i;
    }
    return bits >>> 0;
  };
  let enlargeIn = 4, numBits = 3;
  const first = readBits(2);
  let c0;
  if (first === 0) c0 = readBits(8); else if (first === 1) c0 = readBits(16); else return null;
  const out = [c0];
  dict.push({ lit: false, off: 0, len: 1 });
  let wOff = 0, wLen = 1;
  for (;;) {
    if (index > len) return null;
    let c = readBits(numBits);
    if (c === 0 || c === 1) { dict.push({ lit: true, ch: readBits(c === 0 ? 8 : 16) }); c = dict.length - 1; enlargeIn--; }
    else if (c === 2) break;
    if (!enlargeIn) { enlargeIn = 2 ** numBits; numBits++; if (numBits > 31) return null; }
    const eOff = out.length; let eLen;
    if (c < dict.length) {
      const e = dict[c];
      if (e.lit) { out.push(e.ch); eLen = 1; } else { for (let k = 0; k < e.len; k++) out.push(out[e.off + k]); eLen = e.len; }
    } else if (c === dict.length) {
      for (let k = 0; k < wLen; k++) out.push(out[wOff + k]);
      out.push(out[wOff]); eLen = wLen + 1;
    } else return null;
    if (out.length > maxOut) return null;
    dict.push({ lit: false, off: wOff, len: wLen + 1 });
    enlargeIn--;
    wOff = eOff; wLen = eLen;
    if (!enlargeIn) { enlargeIn = 2 ** numBits; numBits++; if (numBits > 31) return null; }
  }
  let s = '';
  for (let i = 0; i < out.length; i += 8192) s += String.fromCharCode.apply(null, out.slice(i, i + 8192));
  return s.replace(/[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(^|[^\uD800-\uDBFF])[\uDC00-\uDFFF]/g, (m, p) => (p || '') + '�');
}
function utf8Latin1(d) {
  const out = []; let multi = false;
  for (let i = 0; i < d.length; i++) {
    const c = d[i];
    if (c < 0x80) out.push(c);
    else if ((c === 0xC2 || c === 0xC3) && i + 1 < d.length && (d[i + 1] & 0xC0) === 0x80) { out.push(((c & 3) << 6) | (d[i + 1] & 63)); i++; multi = true; }
    else return null;
  }
  return multi ? Uint8Array.from(out) : null;
}
function zlibInflate(d) {
  if (d.length < 6 || (d[0] & 0x0F) !== 8 || ((d[0] << 8) | d[1]) % 31 !== 0 || (d[1] & 0x20)) return null;
  try { return inflateRawSync(d.subarray(2)); } catch (e) { return null; }
}
const RM_KEYS = ['system', 'screen', 'timer', 'switches', 'variables', 'selfSwitches', 'actors', 'party', 'map', 'player'];
const isWs = (c) => c === 0x20 || c === 0x0D || c === 0x0A || c === 0x09;
function rpgMakerDecode(buf) {
  let s = 0, e = buf.length;
  if (e >= 3 && buf[0] === 0xEF && buf[1] === 0xBB && buf[2] === 0xBF) s = 3;
  while (s < e && isWs(buf[s])) s++;
  while (e > s && (buf[e - 1] === 0 || isWs(buf[e - 1]))) e--;
  if (e - s < 4) return null;
  if (buf[s] === 0x7B || buf[s] === 0x5B) return { json: utf8.decode(buf.subarray(s, e)), encoding: 'json' };
  let out = zlibInflate(buf);
  if (out) return { json: utf8.decode(out), encoding: 'zlib' };
  if (buf[0] === 0x78) { const b = utf8Latin1(buf); if (b && (out = zlibInflate(b))) return { json: utf8.decode(out), encoding: 'zlib' }; }
  if (e - s < 8) return null;
  for (let i = s; i < e; i++) if (lzB64(String.fromCharCode(buf[i])) < 0) return null;
  const txt = lzStringDecompressB64(latin1(buf.subarray(s, e)));
  return txt === null ? null : { json: txt, encoding: 'lzstring' };
}
function isRpgMaker(buf) {
  const d = rpgMakerDecode(buf);
  if (!d) return false;
  const t = d.json.trimStart();
  if (t[0] !== '{' && t[0] !== '[') return false;
  if (d.encoding !== 'json') return true;
  if (d.json.includes('"@c"') || d.json.includes('"@a"')) return true;
  return RM_KEYS.filter(k => d.json.includes('"' + k + '"')).length >= 4;
}
function jsonExClean(n, depth = 0) {
  if (depth > 256) return null;
  if (n && typeof n === 'object' && !Array.isArray(n)) {
    if ('@r' in n) return null;
    if ('@a' in n) return jsonExClean(n['@a'], depth + 1);
    const o = {};
    for (const k of Object.keys(n)) if (k !== '@' && k !== '@c') o[k] = jsonExClean(n[k], depth + 1);
    return o;
  }
  if (Array.isArray(n)) return n.map(v => jsonExClean(v, depth + 1));
  return n;
}
function parseRpgMaker(buf) {
  const d = rpgMakerDecode(buf);
  if (!d) throw new Error(T('not an RPG Maker MV/MZ save', 'pas une sauvegarde RPG Maker MV/MZ'));
  let tree = JSON.parse(d.json);
  if (!tree || typeof tree !== 'object') throw new Error(T('decoded text is not JSON', 'le texte décodé n\'est pas du JSON'));
  tree = jsonExClean(tree);
  if (!Array.isArray(tree)) tree._header = { encoding: d.encoding };
  return { rpgmaker: true, tree, meta: { encoding: d.encoding, jsonBytes: new TextEncoder().encode(d.json).length } };
}

// ================================================================== GameMaker text saves (ini / lines + ds_list / ds_map hex)
// Same tree as gameMakerToJson in source/save_formats.cpp (see include/save_formats.h).
const GM_NUM = /^[-+]?\d+(\.\d+)?([eE][-+]?\d+)?$/;
const gmTrim = (s) => s.replace(/^[ \t]+|[ \t]+$/g, '');
const gmKeyOf = (v) => Number.isInteger(v) && Math.abs(v) < 9007199254740992 ? v.toFixed(0).replace(/^-0$/, '0') : String(v);
function gmDsDecode(s, depth) {
  if (depth > 8 || s.length < 16 || (s.length & 1) || !/^[0-9A-Fa-f]+$/.test(s)) return undefined;
  const b = new Uint8Array(s.length / 2);
  for (let i = 0; i < b.length; i++) b[i] = parseInt(s.substr(2 * i, 2), 16);
  const dv = new DataView(b.buffer);
  let p = 0;
  const need = (n) => b.length - p >= n;
  const u32 = () => { if (!need(4)) throw 0; const v = dv.getUint32(p, true); p += 4; return v; };
  try {
    const head = u32();
    const isMap = head >= 0x191 && head <= 0x193, isList = head >= 0x12E && head <= 0x130;
    if (!isMap && !isList) return undefined;
    const count = u32();
    if (count > Math.floor((b.length - 8) / (isMap ? 8 : 4))) return undefined;
    const read = () => {
      const type = u32();
      if (type === 0 || type === 13) { if (!need(8)) throw 0; const v = dv.getFloat64(p, true); p += 8; return Number.isFinite(v) ? { k: 0, v } : { k: 2 }; }
      if (type === 1) { const len = u32(); if (len > b.length - p) throw 0; const v = cut(utf8.decode(b.subarray(p, p + len))); p += len; return { k: 1, v }; }
      if (type === 5) return { k: 2 };
      if (type === 7) { if (!need(4)) throw 0; const v = dv.getInt32(p, true); p += 4; return { k: 0, v }; }
      if (type === 10) { if (!need(8)) throw 0; const v = Number(dv.getBigInt64(p, true)); p += 8; return { k: 0, v }; }
      throw 0;
    };
    const node = (x) => { if (x.k === 2) return null; if (x.k === 0) return x.v; const inner = gmDsDecode(x.v, depth + 1); return inner === undefined ? x.v : inner; };
    const out = isMap ? {} : [];
    for (let i = 0; i < count; i++) {
      const a = read();
      if (!isMap) { out.push(node(a)); continue; }
      const v = read();
      out[a.k === 1 ? a.v : a.k === 2 ? 'null' : gmKeyOf(a.v)] = node(v);
    }
    if (p !== b.length) return undefined;
    return out;
  } catch (e) { return undefined; }
}
function gmValue(raw, depth = 0) {
  let v = gmTrim(raw);
  if (v.length >= 2 && v[0] === '"' && v[v.length - 1] === '"') v = v.slice(1, -1);
  if (GM_NUM.test(v)) { const n = parseFloat(v); if (Number.isFinite(n)) return n; }
  const ds = gmDsDecode(v, depth);
  return ds === undefined ? v : ds;
}
function gmLines(buf) {
  const s = buf.length >= 3 && buf[0] === 0xEF && buf[1] === 0xBB && buf[2] === 0xBF ? 3 : 0;
  let e = s; while (e < buf.length && buf[e]) e++;
  const lines = utf8.decode(buf.subarray(s, e)).split('\n').map(l => l.endsWith('\r') ? l.slice(0, -1) : l);
  if (lines.length && lines[lines.length - 1] === '') lines.pop();
  return lines;
}
const gmIsSection = (l) => { const t = gmTrim(l); return t.length >= 3 && t[0] === '[' && t[t.length - 1] === ']' && t.indexOf(']') === t.length - 1; };
function gmIsIni(lines) {
  let sections = 0;
  for (const l of lines) {
    const t = gmTrim(l);
    if (!t || t[0] === ';' || t[0] === '#') continue;
    if (gmIsSection(t)) { sections++; continue; }
    if (t.indexOf('=') <= 0) return false;
  }
  return sections > 0;
}
function gmPrintable(d) {
  const s = d.length >= 3 && d[0] === 0xEF && d[1] === 0xBB && d[2] === 0xBF ? 3 : 0;
  let e = s; while (e < d.length && d[e]) e++;
  for (let i = e; i < d.length; i++) if (d[i]) return false;
  if (e === s) return false;
  for (let i = s; i < e; i++) { const c = d[i]; if (c < 32 && c !== 9 && c !== 10 && c !== 13) return false; }
  return true;
}
function gmBase64Ini(lines) {
  const b = lines.map(gmTrim).join('');
  if (b.length < 8 || b.length % 4) return null;
  for (const c of b) if (lzB64(c) < 0) return null;
  const out = []; let acc = 0, bits = 0;
  for (const c of b) {
    const v = lzB64(c);
    if (v === 64) break;
    acc = ((acc << 6) | v) & 0xFFFFFF; bits += 6;
    if (bits >= 8) { bits -= 8; out.push((acc >>> bits) & 0xFF); }
  }
  const d = Uint8Array.from(out);
  if (!gmPrintable(d)) return null;
  return gmIsIni(gmLines(d)) ? d : null;
}
function isGameMaker(buf) {
  if (!gmPrintable(buf)) return false;
  const lines = gmLines(buf);
  if (!lines.length) return false;
  const first = gmTrim(lines[0]);
  if ((first[0] === '{' || first[0] === '[') && !gmIsSection(first)) return false;
  if (gmIsIni(lines) || gmBase64Ini(lines)) return true;
  return lines.some(l => { let v = gmTrim(l); if (v.length >= 2 && v[0] === '"' && v[v.length - 1] === '"') v = v.slice(1, -1); return gmDsDecode(v, 0) !== undefined; });
}
function parseGameMaker(buf) {
  if (!gmPrintable(buf)) throw new Error(T('not a GameMaker text save', 'pas une sauvegarde texte GameMaker'));
  let lines = gmLines(buf), encoding = 'text';
  if (!gmIsIni(lines)) { const d = gmBase64Ini(lines); if (d) { lines = gmLines(d); encoding = 'base64'; } }
  const ini = gmIsIni(lines), tree = {};
  let keys = 0;
  if (ini) {
    let sec = null;
    for (const l of lines) {
      const t = gmTrim(l);
      if (!t || t[0] === ';' || t[0] === '#') continue;
      if (gmIsSection(t)) { const name = t.slice(1, -1); if (!tree[name] || typeof tree[name] !== 'object' || Array.isArray(tree[name])) tree[name] = {}; sec = tree[name]; continue; }
      const eq = t.indexOf('=');
      if (!sec) { if (!tree['']) tree[''] = {}; sec = tree['']; }
      sec[gmTrim(t.slice(0, eq))] = gmValue(t.slice(eq + 1));
      keys++;
    }
  } else { tree.lines = lines.map(l => gmValue(l)); keys = lines.length; }
  delete tree._header;
  tree._header = { format: ini ? 'ini' : 'lines', encoding };
  return { gamemaker: true, tree, meta: { format: ini ? 'ini' : 'lines', encoding, keys } };
}

// ================================================================== Easy Save 3 (Unity asset), encrypted
// Mirror of es3ToJson / es3FindPassword (save_formats.cpp). File = 16-byte IV + AES-128-CBC (PKCS#7),
// key = PBKDF2-HMAC-SHA1(password, IV, 100, 16); plaintext = ES3 JSON, optionally gzip. The password is the game's
// ES3 setting (ES3Defaults in resources.assets), never bundled: setPassword('es3', …).
function decryptEs3(buf, password) {
  if (buf.length < 32 || (buf.length - 16) % 16) return null;
  const iv = buf.subarray(0, 16), ct = buf.subarray(16);
  const key = pbkdf2Sha1(new TextEncoder().encode(password), iv, 100, 16);
  const tail = aes128CbcDecrypt(key, ct.length >= 32 ? ct.subarray(ct.length - 32, ct.length - 16) : iv, ct.subarray(ct.length - 16));
  const pad = tail[15];
  if (pad < 1 || pad > 16) return null;
  for (let i = 16 - pad; i < 16; i++) if (tail[i] !== pad) return null;
  let pt = aes128CbcDecrypt(key, iv, ct).subarray(0, ct.length - pad);
  if (pt.length >= 18 && pt[0] === 0x1f && pt[1] === 0x8b && pt[2] === 8) {
    const fl = pt[3]; let p = 10;
    if (fl & 4) p += 2 + (pt[p] | (pt[p + 1] << 8));
    if (fl & 8) { while (p < pt.length && pt[p]) p++; p++; }
    if (fl & 16) { while (p < pt.length && pt[p]) p++; p++; }
    if (fl & 2) p += 2;
    pt = inflateRawSync(pt.subarray(p));
  }
  let s = 0; while (s < pt.length && (pt[s] === 0x20 || pt[s] === 9 || pt[s] === 10 || pt[s] === 13)) s++;
  if (pt[s] !== 0x7B && pt[s] !== 0x5B) return null;
  for (const c of pt) if (c < 32 && c !== 9 && c !== 10 && c !== 13) return null;
  return new TextDecoder().decode(pt);
}
function parseEs3(buf) {
  const pw = PASSWORDS.es3;
  if (!pw) return null;
  let text;
  try { text = decryptEs3(buf, pw); } catch (e) { return null; }
  if (!text) return null;
  let tree;
  try { tree = JSON.parse(text.replace(/[\s\0]+$/, '')); } catch (e) { return null; }
  return { es3: true, tree, meta: { bytes: text.length } };
}
// Password: the strings (u32 length + text) right after each "ES3Defaults" in the game's resources.assets.
function es3FindPassword(asset, save) {
  const tag = new TextEncoder().encode('ES3Defaults'), tried = new Set();
  for (let i = 0; i + tag.length <= asset.length; i++) {
    let hit = true;
    for (let k = 0; hit && k < tag.length; k++) if (asset[i + k] !== tag[k]) hit = false;
    if (!hit) continue;
    const end = Math.min(asset.length, i + tag.length + 1024);
    for (let o = i + tag.length; o + 4 < end; o++) {
      const len = asset[o] | (asset[o + 1] << 8) | (asset[o + 2] << 16) | (asset[o + 3] << 24);
      if (len < 1 || len > 128 || o + 4 + len > asset.length) continue;
      let text = true;
      for (let k = 0; text && k < len; k++) { const c = asset[o + 4 + k]; text = c >= 32 && c < 127; }
      if (!text) continue;
      const pw = new TextDecoder().decode(asset.subarray(o + 4, o + 4 + len));
      if (tried.has(pw)) continue;
      tried.add(pw);
      let ok = null;
      try { ok = decryptEs3(save, pw); } catch (e) { }
      if (ok) return { password: pw };
    }
  }
  return null;
}

const api = { decryptEs3, parseEs3, es3FindPassword, Reader, parseRpgMaker, isRpgMaker, parseGameMaker, isGameMaker, lzStringDecompressB64, parseGvas, isGvas, parseNrbf, isNrbf, parseSgb, isSgb, parseDeltarune, isDeltarune, lz4Block, KNOWN_XOR_KEYS, xorBytes, looksXorText, guessXorKey, unwrapXor,
  parseMsgpack, inflateRawSync, sha1, pbkdf2Sha1, aes128CbcDecrypt, setPassword, isTheatrhythm, parseTheatrhythm, decryptTheatrhythm, theatrhythmFindPassword, TH_FILES };
if (typeof module !== 'undefined' && module.exports) module.exports = api;
else root.SaveFormats = api;
})(typeof self !== 'undefined' ? self : this);
