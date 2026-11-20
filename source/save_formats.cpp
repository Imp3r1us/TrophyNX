// GVAS (Unreal) and NRBF (.NET BinaryFormatter) readers -> cJSON trees.
// Mirror of tools/savelab/formats.js: any change here must be made there too (host/test/test_formats.cpp
// compares both on the same files). Built without exceptions: errors are sticky in the Reader.
#include "save_formats.h"
#include "cJSON.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

namespace {

const size_t MAX_STR = 1u << 20;

struct Reader {
    const uint8_t* b;
    size_t n, o = 0;
    bool bad = false;
    std::string err;
    Reader(const uint8_t* d, size_t s) : b(d), n(s) {}
    void fail(const std::string& e) { if (!bad) { bad = true; err = e; } }
    bool need(size_t k) {
        if (bad) return false;
        if (k > n - o) { char m[64]; snprintf(m, sizeof m, "unexpected end of file at 0x%zX", o); fail(m); return false; }
        return true;
    }
    void skip(size_t k) { if (need(k)) o += k; }
    template <class T> T get() { T v{}; if (need(sizeof v)) { memcpy(&v, b + o, sizeof v); o += sizeof v; } return v; }
    uint8_t u8() { return get<uint8_t>(); }
    int8_t i8() { return get<int8_t>(); }
    uint16_t u16() { return get<uint16_t>(); }
    int16_t i16() { return get<int16_t>(); }
    uint32_t u32() { return get<uint32_t>(); }
    int32_t i32() { return get<int32_t>(); }
    double i64() { return (double)get<int64_t>(); }
    double u64() { return (double)get<uint64_t>(); }
    double f32() { double v = get<float>(); return std::isfinite(v) ? fixed6(v) : NAN; }
    // Same value as JavaScript's +v.toFixed(6): exact decimal expansion, rounded half away from zero.
    static double fixed6(double v) {
        if (std::fabs(v) >= 1e21) return v;
        char t[400];
        snprintf(t, sizeof t, "%.200f", std::fabs(v));   // exact for any float
        std::string s(t);
        size_t dot = s.find('.');
        std::string digits = s.substr(0, dot) + s.substr(dot + 1, 6);
        if (s[dot + 7] >= '5') {
            int i = (int)digits.size() - 1;
            for (; i >= 0; i--) { if (digits[i] == '9') digits[i] = '0'; else { digits[i]++; break; } }
            if (i < 0) digits = "1" + digits;
        }
        std::string out = digits.substr(0, digits.size() - 6) + "." + digits.substr(digits.size() - 6);
        double r = strtod(out.c_str(), nullptr);
        return v < 0 ? -r : r;
    }
    double f64() { return get<double>(); }
};

// Non-finite numbers become null (same as the Save Lab).
cJSON* num(double v) { return std::isfinite(v) ? cJSON_CreateNumber(v) : cJSON_CreateNull(); }

void putUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

// Decodes our own (valid) UTF-8 back to code points.
std::vector<uint32_t> codepoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = (uint8_t)s[i];
        int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < len && i + k < s.size(); k++) cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string hex8(uint32_t v) { char t[16]; snprintf(t, sizeof t, "%08X", v); return t; }

// =========================================================================== GVAS
struct GvasCtx {
    uint32_t ue4 = 0, ue5 = 0;
    bool lwc = false;
    int skipped = 0;
    long budget = 2000000;
};

// Strings end at the first NUL (same as the Save Lab).
std::string cutNul(std::string s) { size_t z = s.find('\0'); if (z != std::string::npos) s.resize(z); return s; }

std::string fstr(Reader& r) {
    int32_t len = r.i32();
    std::string s;
    if (r.bad || len == 0) return s;
    if (len > 0) {
        if ((size_t)len > MAX_STR) { r.fail("string too long"); return s; }
        if (!r.need((size_t)len)) return s;
        for (int32_t i = 0; i < len - 1; i++) putUtf8(s, r.b[r.o + i]);   // ANSI = Latin-1
        r.o += len;
        return cutNul(s);
    }
    if (len == INT32_MIN || (size_t)(-(int64_t)len) > MAX_STR) { r.fail("string too long"); return s; }
    size_t m = (size_t)(-(int64_t)len);
    if (!r.need(m * 2)) return s;
    const uint8_t* p = r.b + r.o;
    for (size_t i = 0; i + 1 < m; i++) {   // UTF-16LE, lone surrogates -> U+FFFD (like TextDecoder)
        uint32_t c = p[i * 2] | (p[i * 2 + 1] << 8);
        if (c >= 0xD800 && c < 0xDC00 && i + 2 < m) {
            uint32_t d = p[i * 2 + 2] | (p[i * 2 + 3] << 8);
            if (d >= 0xDC00 && d < 0xE000) { putUtf8(s, 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00)); i++; continue; }
        }
        if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
        putUtf8(s, c);
    }
    r.o += m * 2;
    return cutNul(s);
}

std::string guid(Reader& r) { std::string s; for (int i = 0; i < 4; i++) s += hex8(r.u32()); return s; }

bool validName(const std::string& s) {
    size_t units = 0;
    for (uint32_t cp : codepoints(s)) {
        if (!((cp >= 0x20 && cp <= 0x7E) || cp >= 0xA0)) return false;
        units += cp > 0xFFFF ? 2 : 1;
    }
    return units >= 1 && units <= 256;
}
bool validType(const std::string& t) {
    const size_t suf = 8;   // "Property"
    if (t.size() <= suf || t.size() > 64 + suf || t.compare(t.size() - suf, suf, "Property") != 0) return false;
    for (size_t i = 0; i < t.size() - suf; i++) { char c = t[i]; if (!(isalnum((unsigned char)c) || c == '_')) return false; }
    return true;
}

struct Tag {
    std::string name, type, structName, enumName, inner, key, value;
    int32_t size = 0, index = 0;
    uint8_t boolVal = 0;
};

cJSON* readProps(Reader& r, GvasCtx& c, int depth);
cJSON* readStruct(Reader& r, const std::string& name, GvasCtx& c, int depth, double size);

// false at "None" or on error (r.bad)
bool readTag(Reader& r, Tag& t) {
    t.name = fstr(r);
    if (r.bad || t.name == "None") return false;
    if (!validName(t.name)) { char m[64]; snprintf(m, sizeof m, "invalid property name at 0x%zX", r.o); r.fail(m); return false; }
    t.type = fstr(r);
    if (r.bad) return false;
    if (!validType(t.type)) { r.fail("invalid property type"); return false; }
    t.size = r.i32();
    t.index = r.i32();
    if (t.type == "StructProperty") { t.structName = fstr(r); r.skip(16); }
    else if (t.type == "BoolProperty") t.boolVal = r.u8();
    else if (t.type == "ByteProperty" || t.type == "EnumProperty") t.enumName = fstr(r);
    else if (t.type == "ArrayProperty" || t.type == "SetProperty") t.inner = fstr(r);
    else if (t.type == "MapProperty") { t.key = fstr(r); t.value = fstr(r); }
    if (r.u8()) r.skip(16);   // property GUID
    return !r.bad;
}

void addProp(cJSON* obj, std::set<std::string>& multi, const std::string& name, cJSON* v, int32_t index) {
    cJSON* existing = cJSON_GetObjectItemCaseSensitive(obj, name.c_str());
    if (index > 0 || existing) {
        if (!multi.count(name)) {
            cJSON* arr = cJSON_CreateArray();
            if (existing) {   // keeps the original position (like a JavaScript object)
                cJSON_AddItemToArray(arr, cJSON_Duplicate(existing, true));
                cJSON_ReplaceItemInObjectCaseSensitive(obj, name.c_str(), arr);
            } else cJSON_AddItemToObject(obj, name.c_str(), arr);
            multi.insert(name);
        }
        cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(obj, name.c_str()), v);
    } else cJSON_AddItemToObject(obj, name.c_str(), v);
}

std::string softPath(Reader& r, GvasCtx& c) {
    std::vector<std::string> parts;
    int n = c.ue5 >= 1007 ? 3 : 2;
    for (int i = 0; i < n; i++) parts.push_back(fstr(r));
    std::string s;
    for (auto& p : parts) if (!p.empty()) { if (!s.empty()) s += ':'; s += p; }
    return s;
}

cJSON* readText(Reader& r, long end, GvasCtx& c, int depth);
cJSON* textArg(Reader& r, GvasCtx& c, int depth) {
    int8_t t = r.i8();
    if (r.bad) return nullptr;
    switch (t) {
        case 0: return num(c.ue5 ? r.i64() : r.i32());
        case 1: return num(c.ue5 ? r.u64() : r.u32());
        case 2: return num(r.f32());
        case 3: return num(r.f64());
        case 4: return readText(r, -1, c, depth + 1);
    }
    r.fail("unsupported text argument");
    return nullptr;
}
// FText -> its displayable source (see formats.js readText)
cJSON* readText(Reader& r, long end, GvasCtx& c, int depth) {
    if (depth > 16) { r.fail("text nested too deeply"); return nullptr; }
    r.u32();
    int8_t h = r.i8();
    if (r.bad) return nullptr;
    auto str = [&](const std::string& s) { return r.bad ? nullptr : cJSON_CreateString(s.c_str()); };
    auto drop = [](cJSON* x) { if (x) cJSON_Delete(x); };
    auto numText = [&]() -> cJSON* {
        cJSON* v = textArg(r, c, depth);
        if (r.u32()) r.skip(25);
        fstr(r);
        if (r.bad) { drop(v); return nullptr; }
        return v;
    };
    switch (h) {
        case -1:
            if (end >= 0 && (long)r.o + 4 > end) return cJSON_CreateString("");
            if (r.i32()) return str(fstr(r));
            return str("");
        case 0: fstr(r); fstr(r); return str(fstr(r));
        case 1: case 2: case 3: {
            cJSON* f = readText(r, -1, c, depth + 1);
            int32_t n = r.i32();
            if (r.bad || n < 0 || n > 1000) { drop(f); r.fail("invalid text"); return nullptr; }
            for (int32_t i = 0; i < n && !r.bad; i++) { if (h != 2) fstr(r); drop(textArg(r, c, depth)); }
            if (r.bad) { drop(f); return nullptr; }
            return f;
        }
        case 4: case 5: return numText();
        case 6: fstr(r); return numText();
        case 7: { double t = r.i64(); r.i8(); fstr(r); return r.bad ? nullptr : num(t); }
        case 8: { double t = r.i64(); r.i8(); fstr(r); fstr(r); return r.bad ? nullptr : num(t); }
        case 9: { double t = r.i64(); r.i8(); r.i8(); fstr(r); fstr(r); return r.bad ? nullptr : num(t); }
        case 10: { cJSON* f = readText(r, -1, c, depth + 1); r.i8(); if (r.bad) { drop(f); return nullptr; } return f; }
        case 11: fstr(r); return str(fstr(r));
    }
    r.fail("unsupported text history");
    return nullptr;
}

cJSON* guessStruct(Reader& r, GvasCtx& c, int depth) {
    size_t at = r.o;
    int skipped = c.skipped;
    long budget = c.budget;
    cJSON* v = readProps(r, c, depth + 1);
    if (!r.bad) return v;
    if (v) cJSON_Delete(v);
    r.bad = false; r.err.clear(); r.o = at; c.skipped = skipped; c.budget = budget;
    std::string g = guid(r);
    return r.bad ? nullptr : cJSON_CreateString(g.c_str());
}

cJSON* readElem(Reader& r, const std::string& type, GvasCtx& c, int depth, const std::string* structName = nullptr) {
    cJSON* v = nullptr;
    if (type == "BoolProperty") { bool b = r.u8() != 0; v = cJSON_CreateBool(b); }
    else if (type == "ByteProperty") v = num(r.u8());
    else if (type == "Int8Property") v = num(r.i8());
    else if (type == "Int16Property") v = num(r.i16());
    else if (type == "IntProperty") v = num(r.i32());
    else if (type == "Int64Property") v = num(r.i64());
    else if (type == "UInt16Property") v = num(r.u16());
    else if (type == "UInt32Property") v = num(r.u32());
    else if (type == "UInt64Property") v = num(r.u64());
    else if (type == "FloatProperty") v = num(r.f32());
    else if (type == "DoubleProperty") v = num(r.f64());
    else if (type == "StrProperty" || type == "NameProperty" || type == "EnumProperty" || type == "ObjectProperty" ||
             type == "ClassProperty" || type == "InterfaceProperty" || type == "WeakObjectProperty") {
        std::string s = fstr(r);
        v = cJSON_CreateString(s.c_str());
    } else if (type == "SoftObjectProperty" || type == "SoftClassProperty") { std::string s = softPath(r, c); v = cJSON_CreateString(s.c_str()); }
    else if (type == "TextProperty") return readText(r, -1, c, 0);
    else if (type == "StructProperty") return structName ? readStruct(r, *structName, c, depth, 0) : guessStruct(r, c, depth);
    else { r.fail("unsupported type " + type); return nullptr; }
    if (r.bad) { cJSON_Delete(v); return nullptr; }
    return v;
}

cJSON* readArray(Reader& r, const std::string& inner, int32_t size, GvasCtx& c, int depth) {
    int32_t n = r.i32();
    if (r.bad) return nullptr;
    if (n < 0 || n > size) { r.fail("invalid array"); return nullptr; }
    cJSON* out = cJSON_CreateArray();
    if (inner == "StructProperty") {
        fstr(r); fstr(r);
        double total = r.i64();
        std::string sname = fstr(r);
        r.skip(16);
        if (r.u8()) r.skip(16);
        double each = n ? total / n : 0;
        for (int32_t i = 0; i < n && !r.bad; i++) { cJSON* e = readStruct(r, sname, c, depth, each); if (e) cJSON_AddItemToArray(out, e); }
    } else if (inner == "ByteProperty" && size - 4 != n) {
        for (int32_t i = 0; i < n && !r.bad; i++) { std::string s = fstr(r); if (!r.bad) cJSON_AddItemToArray(out, cJSON_CreateString(s.c_str())); }
    } else {
        for (int32_t i = 0; i < n && !r.bad; i++) { cJSON* e = readElem(r, inner, c, depth); if (e) cJSON_AddItemToArray(out, e); }
    }
    if (r.bad) { cJSON_Delete(out); return nullptr; }
    return out;
}

cJSON* readStruct(Reader& r, const std::string& name, GvasCtx& c, int depth, double size) {
    auto dbl = [&](int k) { return size ? size == k * 8 : c.lwc; };
    auto obj = [&](std::initializer_list<const char*> keys, bool d) {
        cJSON* o = cJSON_CreateObject();
        for (const char* k : keys) cJSON_AddItemToObject(o, k, num(d ? r.f64() : r.f32()));
        return o;
    };
    cJSON* v = nullptr;
    if (name == "Vector") v = obj({"X", "Y", "Z"}, dbl(3));
    else if (name == "Rotator") v = obj({"Pitch", "Yaw", "Roll"}, dbl(3));
    else if (name == "Vector2D") v = obj({"X", "Y"}, dbl(2));
    else if (name == "Quat" || name == "Vector4" || name == "Plane") v = obj({"X", "Y", "Z", "W"}, dbl(4));
    else if (name == "LinearColor") v = obj({"R", "G", "B", "A"}, false);
    else if (name == "Color") { v = cJSON_CreateObject(); for (const char* k : {"B", "G", "R", "A"}) cJSON_AddItemToObject(v, k, num(r.u8())); }
    else if (name == "IntPoint") { v = cJSON_CreateObject(); for (const char* k : {"X", "Y"}) cJSON_AddItemToObject(v, k, num(r.i32())); }
    else if (name == "IntVector") { v = cJSON_CreateObject(); for (const char* k : {"X", "Y", "Z"}) cJSON_AddItemToObject(v, k, num(r.i32())); }
    else if (name == "Guid") { std::string g = guid(r); v = cJSON_CreateString(g.c_str()); }
    else if (name == "DateTime" || name == "Timespan") v = num(r.i64());
    else if (name == "GameplayTagContainer") {
        int32_t n = r.i32();
        if (r.bad) return nullptr;
        if (n < 0 || n > 100000) { r.fail("invalid GameplayTagContainer"); return nullptr; }
        v = cJSON_CreateArray();
        for (int32_t i = 0; i < n && !r.bad; i++) { std::string s = fstr(r); cJSON_AddItemToArray(v, cJSON_CreateString(s.c_str())); }
    } else if (name == "SoftObjectPath" || name == "SoftClassPath") { std::string s = softPath(r, c); v = cJSON_CreateString(s.c_str()); }
    else if (name == "Box") {
        bool d = size ? size == 49 : c.lwc;
        v = cJSON_CreateObject();
        cJSON_AddItemToObject(v, "Min", obj({"X", "Y", "Z"}, d));
        cJSON_AddItemToObject(v, "Max", obj({"X", "Y", "Z"}, d));
        cJSON_AddItemToObject(v, "IsValid", cJSON_CreateBool(r.u8() != 0));
    } else return readProps(r, c, depth + 1);
    if (r.bad) { cJSON_Delete(v); return nullptr; }
    return v;
}

cJSON* readValue(Reader& r, const Tag& t, GvasCtx& c, int depth, long end) {
    if (t.type == "ByteProperty") {
        if (t.enumName == "None" || t.size == 1) return num(r.u8());
        std::string s = fstr(r);
        return r.bad ? nullptr : cJSON_CreateString(s.c_str());
    }
    if (t.type == "StructProperty") return readStruct(r, t.structName, c, depth, t.size);
    if (t.type == "ArrayProperty") return readArray(r, t.inner, t.size, c, depth);
    if (t.type == "SetProperty" || t.type == "MapProperty") {
        bool map = t.type == "MapProperty";
        const std::string& kt = map ? t.key : t.inner;
        int32_t rm = r.i32();
        if (r.bad) return nullptr;
        if (rm < 0 || rm > t.size) { r.fail("invalid set/map"); return nullptr; }
        for (int32_t i = 0; i < rm && !r.bad; i++) { cJSON* x = readElem(r, kt, c, depth); if (x) cJSON_Delete(x); }
        int32_t n = r.i32();
        if (r.bad) return nullptr;
        if (n < 0 || n > t.size) { r.fail("invalid set/map"); return nullptr; }
        cJSON* out = cJSON_CreateArray();
        for (int32_t i = 0; i < n && !r.bad; i++) {
            cJSON* k = readElem(r, kt, c, depth);
            if (!map) { if (k) cJSON_AddItemToArray(out, k); continue; }
            cJSON* v = r.bad ? nullptr : readElem(r, t.value, c, depth);
            if (r.bad) { if (k) cJSON_Delete(k); if (v) cJSON_Delete(v); break; }
            cJSON* kv = cJSON_CreateObject();
            cJSON_AddItemToObject(kv, "Key", k);
            cJSON_AddItemToObject(kv, "Value", v);
            cJSON_AddItemToArray(out, kv);
        }
        if (r.bad) { cJSON_Delete(out); return nullptr; }
        return out;
    }
    if (t.type == "TextProperty") return readText(r, end, c, 0);
    if (t.type == "DelegateProperty" || t.type == "MulticastInlineDelegateProperty" || t.type == "MulticastSparseDelegateProperty" ||
        t.type == "FieldPathProperty")
        return cJSON_CreateNull();   // engine bindings, never game progress
    return readElem(r, t.type, c, depth);
}

cJSON* readProps(Reader& r, GvasCtx& c, int depth) {
    if (depth > 48) { r.fail("structure nested too deeply"); return nullptr; }
    cJSON* obj = cJSON_CreateObject();
    std::set<std::string> multi;
    for (;;) {
        Tag t;
        if (!readTag(r, t)) break;
        if (t.size < 0 || (size_t)t.size > r.n - r.o) { r.fail("invalid size for " + t.name); break; }
        size_t start = r.o, end = start + t.size;
        cJSON* v = nullptr;
        if (t.type == "BoolProperty") v = cJSON_CreateBool(t.boolVal != 0);
        else {
            v = readValue(r, t, c, depth, (long)end);
            if (r.bad || r.o > end) {   // undecodable value: null, continue after it
                if (v) cJSON_Delete(v);
                v = cJSON_CreateNull();
                c.skipped++;
                r.bad = false; r.err.clear();
            }
            if (!v) v = cJSON_CreateNull();
        }
        r.o = end;
        if (--c.budget < 0) { cJSON_Delete(v); r.fail("document too large"); break; }
        addProp(obj, multi, t.name, v, t.index);
    }
    if (r.bad) { cJSON_Delete(obj); return nullptr; }
    return obj;
}

// =========================================================================== NRBF
struct NVal {
    enum K { Null, Bool, Num, Str, Ref, Nulls, End } k = Null;
    double d = 0;
    std::string s;
    int32_t id = 0;   // Ref: object id, Nulls: count
};
struct NObj {
    enum K { String, Array, Class } k = Class;
    std::string s;             // String value / class name
    std::vector<std::string> names;
    std::vector<NVal> vals;    // Array elements / class member values
};
struct NMeta {
    std::string name;
    std::vector<std::string> names;
    bool typed = false;
    std::vector<uint8_t> bt, extra;
};

struct Nrbf {
    Reader r;
    std::unordered_map<int32_t, NObj> objs;
    std::unordered_map<int32_t, NMeta> metas;
    long budget = 2000000;
    Nrbf(const uint8_t* d, size_t n) : r(d, n) {}

    std::string lps() {
        uint32_t len = 0;
        int shift = 0;
        uint8_t b;
        do {
            if (shift > 28) { r.fail("invalid string length"); return ""; }
            b = r.u8();
            if (r.bad) return "";
            len |= (uint32_t)(b & 0x7F) << shift;
            shift += 7;
        } while (b & 0x80);
        if (len > MAX_STR) { r.fail("string too long"); return ""; }
        if (!r.need(len)) return "";
        std::string s((const char*)r.b + r.o, len);
        r.o += len;
        return cutNul(s);
    }
    NVal numv(double d) { NVal v; if (std::isfinite(d)) { v.k = NVal::Num; v.d = d; } return v; }
    NVal strv(const std::string& s) { NVal v; v.k = NVal::Str; v.s = s; return v; }
    NVal prim(uint8_t t) {
        switch (t) {
            case 1: { NVal v; v.k = NVal::Bool; v.d = r.u8() != 0; return v; }
            case 2: return numv(r.u8());
            case 3: {
                if (!r.need(1)) return NVal();
                uint8_t c = r.b[r.o];
                size_t k = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                if (!r.need(k)) return NVal();
                std::string s((const char*)r.b + r.o, k);
                r.o += k;
                return strv(cutNul(s));
            }
            case 5: {
                std::string s = lps();
                char* e = nullptr;
                double d = strtod(s.c_str(), &e);
                return e != s.c_str() && std::isfinite(d) ? numv(d) : strv(s);
            }
            case 6: return numv(r.f64());
            case 7: return numv(r.i16());
            case 8: return numv(r.i32());
            case 9: case 12: return numv(r.i64());
            case 10: return numv(r.i8());
            case 11: return numv(r.f32());
            case 13: { uint32_t lo = r.u32(), hi = r.u32() & 0x3FFFFFFF; return numv(hi * 4294967296.0 + lo); }
            case 14: return numv(r.u16());
            case 15: return numv(r.u32());
            case 16: return numv(r.u64());
            case 17: return NVal();
            case 18: return strv(lps());
        }
        r.fail("unknown primitive type");
        return NVal();
    }
    NVal ref(int32_t id) { NVal v; v.k = NVal::Ref; v.id = id; return v; }
    uint8_t extraFor(uint8_t bt) {
        if (bt == 0 || bt == 7) return r.u8();
        if (bt == 3) lps();
        else if (bt == 4) { lps(); r.i32(); }
        return 0;
    }
    void members(const NMeta& m, std::vector<NVal>& vals) {
        for (size_t i = 0; i < m.names.size() && !r.bad; i++) {
            if (m.typed && m.bt[i] == 0) vals.push_back(prim(m.extra[i]));
            else {
                NVal v = record();
                if (v.k == NVal::Nulls) { r.fail("multiple nulls in a class"); return; }
                vals.push_back(v);
            }
        }
    }
    void elements(size_t n, int prim_t, std::vector<NVal>& out) {
        while (out.size() < n && !r.bad) {
            if (--budget < 0) { r.fail("document too large"); return; }
            if (prim_t >= 0) { out.push_back(prim((uint8_t)prim_t)); continue; }
            NVal v = record();
            if (v.k == NVal::Nulls) {
                if (v.id < 0 || out.size() + (size_t)v.id > n) { r.fail("invalid array"); return; }
                for (int32_t k = 0; k < v.id; k++) out.push_back(NVal());
            } else out.push_back(v);
        }
    }
    NVal record() {
        for (;;) {
            if (--budget < 0) { r.fail("document too large"); return NVal(); }
            uint8_t t = r.u8();
            if (r.bad) return NVal();
            switch (t) {
                case 1: {
                    int32_t id = r.i32(), mid = r.i32();
                    if (r.bad) return NVal();
                    auto it = metas.find(mid);
                    if (it == metas.end()) { r.fail("unknown class reference"); return NVal(); }
                    NMeta m = it->second;
                    NObj o; o.k = NObj::Class; o.s = m.name; o.names = m.names;
                    members(m, o.vals);
                    objs[id] = std::move(o);
                    return ref(id);
                }
                case 2: case 3: case 4: case 5: {
                    int32_t id = r.i32();
                    NMeta m;
                    m.name = lps();
                    int32_t n = r.i32();
                    if (r.bad) return NVal();
                    if (n < 0 || n > 100000) { r.fail("invalid class"); return NVal(); }
                    for (int32_t i = 0; i < n && !r.bad; i++) m.names.push_back(lps());
                    if (t >= 4) {
                        m.typed = true;
                        for (int32_t i = 0; i < n; i++) m.bt.push_back(r.u8());
                        for (int32_t i = 0; i < n; i++) m.extra.push_back(extraFor(m.bt[i]));
                    }
                    if (t == 3 || t == 5) r.i32();
                    if (r.bad) return NVal();
                    metas[id] = m;
                    NObj o; o.k = NObj::Class; o.s = m.name; o.names = m.names;
                    members(m, o.vals);
                    objs[id] = std::move(o);
                    return ref(id);
                }
                case 6: { int32_t id = r.i32(); NObj o; o.k = NObj::String; o.s = lps(); objs[id] = std::move(o); return ref(id); }
                case 7: {
                    int32_t id = r.i32();
                    uint8_t at = r.u8();
                    int32_t rank = r.i32();
                    if (r.bad) return NVal();
                    if (rank < 1 || rank > 32) { r.fail("invalid array"); return NVal(); }
                    double n = 1;
                    for (int32_t i = 0; i < rank; i++) { int32_t l = r.i32(); if (l < 0) { r.fail("invalid array"); return NVal(); } n *= l; }
                    if (at >= 3 && at <= 5) r.skip(4 * (size_t)rank);
                    uint8_t bt = r.u8();
                    uint8_t ex = extraFor(bt);
                    if (r.bad) return NVal();
                    if (n > (double)r.n) { r.fail("array too large"); return NVal(); }
                    NObj o; o.k = NObj::Array;
                    elements((size_t)n, bt == 0 ? ex : -1, o.vals);
                    objs[id] = std::move(o);
                    return ref(id);
                }
                case 8: return prim(r.u8());
                case 9: return ref(r.i32());
                case 10: return NVal();
                case 11: { NVal v; v.k = NVal::End; return v; }
                case 12: r.i32(); lps(); continue;
                case 13: { NVal v; v.k = NVal::Nulls; v.id = r.u8(); return v; }
                case 14: { NVal v; v.k = NVal::Nulls; v.id = r.i32(); return v; }
                case 15: case 16: case 17: {
                    int32_t id = r.i32(), n = r.i32();
                    int p = t == 15 ? r.u8() : -1;
                    if (r.bad) return NVal();
                    if (n < 0 || (size_t)n > r.n) { r.fail("invalid array"); return NVal(); }
                    NObj o; o.k = NObj::Array;
                    elements((size_t)n, p, o.vals);
                    objs[id] = std::move(o);
                    return ref(id);
                }
            }
            char m[64];
            snprintf(m, sizeof m, "unknown NRBF record %u at 0x%zX", t, r.o - 1);
            r.fail(m);
            return NVal();
        }
    }

    // ---- tree building
    long buildBudget = 2000000;
    std::set<int32_t> stack;
    bool tooBig = false;
    cJSON* conv(const NVal& v, int depth) {
        if (--buildBudget < 0) { tooBig = true; return cJSON_CreateNull(); }
        switch (v.k) {
            case NVal::Bool: return cJSON_CreateBool(v.d != 0);
            case NVal::Num: return cJSON_CreateNumber(v.d);
            case NVal::Str: return cJSON_CreateString(v.s.c_str());
            case NVal::Ref: {
                auto it = objs.find(v.id);
                if (stack.count(v.id) || depth > 64 || it == objs.end()) return cJSON_CreateNull();
                stack.insert(v.id);
                cJSON* x = node(it->second, depth);
                stack.erase(v.id);
                return x;
            }
            default: return cJSON_CreateNull();
        }
    }
    static bool startsWith(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }
    const NVal* member(const NObj& o, const char* name) {
        for (size_t i = 0; i < o.names.size(); i++) if (o.names[i] == name) return &o.vals[i];
        return nullptr;
    }
    cJSON* list(const NVal* v, int depth, double limit = -1) {
        cJSON* x = v ? conv(*v, depth + 1) : nullptr;
        if (!x || !cJSON_IsArray(x)) { if (x) cJSON_Delete(x); return cJSON_CreateArray(); }
        if (limit >= 0) while (cJSON_GetArraySize(x) > limit) cJSON_DeleteItemFromArray(x, cJSON_GetArraySize(x) - 1);
        return x;
    }
    cJSON* node(const NObj& o, int depth) {
        if (o.k == NObj::String) return cJSON_CreateString(o.s.c_str());
        if (o.k == NObj::Array) {
            cJSON* a = cJSON_CreateArray();
            for (const NVal& e : o.vals) cJSON_AddItemToArray(a, conv(e, depth + 1));
            return a;
        }
        if (o.names.size() == 1 && o.names[0] == "value__") return conv(o.vals[0], depth + 1);
        const std::string& cls = o.s;
        if (startsWith(cls, "System.Collections.Generic.List`1") && member(o, "_items")) {
            const NVal* sz = member(o, "_size");
            double size = sz && sz->k == NVal::Num ? sz->d : 0;
            return list(member(o, "_items"), depth, size < 0 ? 0 : size);
        }
        if (startsWith(cls, "System.Collections.Generic.HashSet`1")) return list(member(o, "Elements"), depth);
        if (startsWith(cls, "System.Collections.Generic.Dictionary`2") || startsWith(cls, "System.Collections.Generic.SortedDictionary`2"))
            return list(member(o, "KeyValuePairs"), depth);
        if (startsWith(cls, "System.Collections.Generic.KeyValuePair`2")) {
            const NVal* k = member(o, "key");
            const NVal* v = member(o, "value");
            cJSON* kv = cJSON_CreateObject();
            cJSON_AddItemToObject(kv, "Key", k ? conv(*k, depth + 1) : cJSON_CreateNull());
            cJSON_AddItemToObject(kv, "Value", v ? conv(*v, depth + 1) : cJSON_CreateNull());
            return kv;
        }
        cJSON* out = cJSON_CreateObject();
        for (size_t i = 0; i < o.names.size(); i++) {
            std::string n = o.names[i];
            const char* suf = ">k__BackingField";
            size_t sl = strlen(suf);
            if (n.size() > sl + 1 && n[0] == '<' && n.compare(n.size() - sl, sl, suf) == 0) n = n.substr(1, n.size() - sl - 1);
            cJSON* v = conv(o.vals[i], depth + 1);
            if (cJSON_GetObjectItemCaseSensitive(out, n.c_str())) cJSON_ReplaceItemInObjectCaseSensitive(out, n.c_str(), v);
            else cJSON_AddItemToObject(out, n.c_str(), v);
        }
        return out;
    }
};

}  // namespace

// =========================================================================== public API
const std::vector<std::string>& knownXorKeys() {
    static const std::vector<std::string> keys = {
        "ALL WATCHED OVER BY MACHINES OF LOVING GRACE",   // SIGNALIS (lstr.replika)
    };
    return keys;
}

bool isGvas(const uint8_t* d, size_t n) { return n >= 24 && memcmp(d, "GVAS", 4) == 0; }

bool isNrbf(const uint8_t* d, size_t n) {
    if (n < 17 || d[0] != 0) return false;
    int32_t a, b;
    memcpy(&a, d + 9, 4);
    memcpy(&b, d + 13, 4);
    return a == 1 && b == 0;
}

cJSON* gvasToJson(const uint8_t* data, size_t size, std::string& error, SaveFormatInfo* info) {
    if (!isGvas(data, size)) { error = "not a GVAS file"; return nullptr; }
    Reader r(data, size);
    r.skip(4);
    uint32_t sgv = r.u32();
    if (sgv < 2 || sgv > 3) { error = "GVAS version " + std::to_string(sgv) + " not supported"; return nullptr; }
    GvasCtx c;
    c.ue4 = r.u32();
    c.ue5 = sgv >= 3 ? r.u32() : 0;
    if (c.ue5 >= 1012) { error = "Unreal Engine 5.4+ save (new property format, not supported yet)"; return nullptr; }
    c.lwc = c.ue5 >= 1004;
    uint16_t major = r.u16(), minor = r.u16(), patch = r.u16();
    r.u32();
    std::string branch = fstr(r);
    uint32_t cvf = r.u32(), ncv = r.u32();
    if (!r.bad && ncv > 10000) r.fail("invalid GVAS header");
    for (uint32_t i = 0; i < ncv && !r.bad; i++) { if (cvf == 1) r.skip(8); else { r.skip(20); if (cvf == 2) fstr(r); } }
    std::string cls = fstr(r);
    cJSON* tree = r.bad ? nullptr : readProps(r, c, 0);
    if (r.bad || !tree) { error = r.err.empty() ? "invalid GVAS file" : r.err; if (tree) cJSON_Delete(tree); return nullptr; }
    if (info) {
        char v[32];
        snprintf(v, sizeof v, "%u.%u.%u", major, minor, patch);
        info->summary = std::string("UE ") + v + ", class " + cls;
        info->skipped = c.skipped;
    }
    return tree;
}

// Byte arrays that hold JSON text (e.g. Blasphemous keeps its whole save as JSON in byte[] values) are
// replaced by the parsed JSON, so their content can be addressed by path. Same rule as the Save Lab.
static cJSON* jsonFromBytes(const cJSON* arr) {
    int n = cJSON_GetArraySize(arr);
    if (n < 2 || n > (16 << 20)) return nullptr;
    std::string s;
    s.reserve(n);
    const cJSON* c;
    cJSON_ArrayForEach(c, arr) {
        if (!cJSON_IsNumber(c)) return nullptr;
        double v = c->valuedouble;
        if (v < 0 || v > 255 || v != (int)v) return nullptr;
        s.push_back((char)(int)v);
        if (s.size() == 1 && s[0] != '{' && s[0] != '[') return nullptr;
    }
    return cJSON_ParseWithLength(s.data(), s.size());
}
static void decodeJsonByteArrays(cJSON* n, int depth) {
    if (!n || depth > 64) return;
    cJSON* c = n->child;
    while (c) {
        cJSON* next = c->next;
        if (cJSON_IsArray(c) && c->child && cJSON_IsNumber(c->child)) {
            if (cJSON* j = jsonFromBytes(c)) {
                if (cJSON_IsObject(n) && c->string) cJSON_ReplaceItemInObjectCaseSensitive(n, c->string, j);   // keeps the key
                else cJSON_ReplaceItemViaPointer(n, c, j);
                c = next;
                continue;
            }
        }
        if (cJSON_IsArray(c) || cJSON_IsObject(c)) decodeJsonByteArrays(c, depth + 1);
        c = next;
    }
}

cJSON* nrbfToJson(const uint8_t* data, size_t size, std::string& error, SaveFormatInfo* info) {
    if (!isNrbf(data, size)) { error = "not a .NET BinaryFormatter stream"; return nullptr; }
    std::unique_ptr<Nrbf> p(new Nrbf(data, size));
    Reader& r = p->r;
    r.u8();
    int32_t rootId = r.i32();
    r.i32();
    r.i32(); r.i32();
    for (;;) {
        NVal v = p->record();
        if (r.bad) { error = r.err; return nullptr; }
        if (v.k == NVal::End) break;
    }
    NVal root;
    root.k = NVal::Ref;
    root.id = rootId;
    cJSON* tree = p->conv(root, 0);
    if (tree) decodeJsonByteArrays(tree, 0);
    if (p->tooBig) { cJSON_Delete(tree); error = "document too large"; return nullptr; }
    if (!cJSON_IsObject(tree) && !cJSON_IsArray(tree)) {
        cJSON* w = cJSON_CreateObject();
        cJSON_AddItemToObject(w, "value", tree);
        tree = w;
    }
    if (info) {
        auto it = p->objs.find(rootId);
        std::string cls = it == p->objs.end() ? "" : it->second.k == NObj::String ? "System.String" : it->second.k == NObj::Class ? it->second.s : "";
        info->summary = "root class " + cls.substr(0, cls.find('[')) + ", " + std::to_string(p->objs.size()) + " objects";
    }
    return tree;
}

bool GvasDoc::parse(std::vector<uint8_t>&& data) {
    cJSON* t = gvasToJson(data.data(), data.size(), error, &info);
    if (!t) return false;
    adopt(t, false);
    return true;
}

bool NrbfDoc::parse(std::vector<uint8_t>&& data) {
    cJSON* t = nrbfToJson(data.data(), data.size(), error, &info);
    if (!t) return false;
    adopt(t, false);
    return true;
}

// ================================================================== Supergiant "SGB1" (Hades)
bool lz4BlockDecompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t maxOut) {
    out.clear();
    size_t p = 0;
    while (p < n) {
        uint8_t tok = in[p++];
        size_t lit = tok >> 4;
        if (lit == 15) { uint8_t b; do { if (p >= n) return false; b = in[p++]; lit += b; } while (b == 255); }
        if (lit > n - p || out.size() + lit > maxOut) return false;
        out.insert(out.end(), in + p, in + p + lit);
        p += lit;
        if (p >= n) break;   // the last sequence has literals only
        if (p + 2 > n) return false;
        size_t off = in[p] | (in[p + 1] << 8);
        p += 2;
        if (off == 0 || off > out.size()) return false;
        size_t len = tok & 15;
        if (len == 15) { uint8_t b; do { if (p >= n) return false; b = in[p++]; len += b; } while (b == 255); }
        len += 4;
        if (out.size() + len > maxOut) return false;
        size_t from = out.size() - off;
        for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);   // may overlap
    }
    return true;
}

bool isSgb(const uint8_t* d, size_t n) {
    if (n < 16 || memcmp(d, "SGB1", 4) != 0) return false;
    uint32_t v = d[8] | (d[9] << 8) | (d[10] << 16) | ((uint32_t)d[11] << 24);
    return v == 15 || v == 16;
}

namespace {
struct SgbReader {
    const uint8_t* d; size_t n; size_t p = 0; bool ok = true;
    bool need(size_t k) { if (!ok || k > n - p) { ok = false; return false; } return true; }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v = d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24); p += 4; return v; }
    uint64_t u64() { uint64_t lo = u32(); return lo | ((uint64_t)u32() << 32); }
    std::string str() { uint32_t k = u32(); if (!need(k)) return ""; std::string s((const char*)d + p, k); p += k; return s; }
};

// luabins: '-' nil, '0' false, '1' true, 'N' double, 'S' u32 length + bytes, 'T' u32 array size, u32 hash size, pairs
cJSON* luabinsValue(SgbReader& r, int depth, std::string& err);
std::string luabinsKey(cJSON* k) {
    if (cJSON_IsString(k)) return k->valuestring ? k->valuestring : "";
    if (cJSON_IsNumber(k)) {
        char b[40];
        double v = k->valuedouble;
        if (v == std::floor(v) && std::fabs(v) < 1e15) snprintf(b, sizeof(b), "%lld", (long long)v);
        else snprintf(b, sizeof(b), "%.17g", v);
        return b;
    }
    if (cJSON_IsBool(k)) return cJSON_IsTrue(k) ? "true" : "false";
    return "";
}
cJSON* luabinsValue(SgbReader& r, int depth, std::string& err) {
    if (depth > 64) { err = "Lua tables nested too deep"; r.ok = false; return nullptr; }
    if (!r.need(1)) { err = "truncated Lua state"; return nullptr; }
    char t = (char)r.d[r.p++];
    switch (t) {
        case '-': return cJSON_CreateNull();
        case '0': return cJSON_CreateFalse();
        case '1': return cJSON_CreateTrue();
        case 'N': {
            if (!r.need(8)) { err = "truncated number"; return nullptr; }
            double v;
            memcpy(&v, r.d + r.p, 8);
            r.p += 8;
            return cJSON_CreateNumber(v);
        }
        case 'S': {
            std::string s = r.str();
            if (!r.ok) { err = "truncated string"; return nullptr; }
            return cJSON_CreateString(s.c_str());
        }
        case 'T': {
            uint64_t na = r.u32(), nh = r.u32();
            if (!r.ok || na + nh > (r.n - r.p) / 2) { err = "bad Lua table size"; r.ok = false; return nullptr; }
            cJSON* o = cJSON_CreateObject();
            for (uint64_t i = 0; i < na + nh; i++) {
                cJSON* k = luabinsValue(r, depth + 1, err);
                if (!k) { cJSON_Delete(o); return nullptr; }
                cJSON* v = luabinsValue(r, depth + 1, err);
                if (!v) { cJSON_Delete(k); cJSON_Delete(o); return nullptr; }
                std::string key = luabinsKey(k);
                cJSON_Delete(k);
                cJSON_AddItemToObject(o, key.c_str(), v);
            }
            return o;
        }
        default: {
            char b[64];
            snprintf(b, sizeof(b), "unknown Lua value tag 0x%02X at %zu", (unsigned)(uint8_t)t, r.p - 1);
            err = b;
            r.ok = false;
            return nullptr;
        }
    }
}
}  // namespace

cJSON* sgbToJson(const uint8_t* data, size_t size, std::string& error, SaveFormatInfo* info) {
    if (!isSgb(data, size)) { error = "not a Supergiant SGB1 save"; return nullptr; }
    SgbReader h{data, size};
    h.p = 8;   // signature + checksum
    uint32_t version = h.u32();
    if (version >= 16) h.u64();   // timestamp
    std::string location = h.str();
    uint32_t runs = h.u32(), meta = h.u32(), shrine = h.u32();
    if (!h.need(2)) { error = "truncated header"; return nullptr; }
    int god = data[h.p], hell = data[h.p + 1];
    h.p += 2;
    uint32_t nkeys = h.u32();
    for (uint32_t i = 0; i < nkeys && h.ok; i++) h.str();
    std::string map = h.str();
    h.str();   // start_next_map
    uint32_t packed = h.u32();
    if (!h.ok || !h.need(packed)) { error = "truncated header"; return nullptr; }
    std::vector<uint8_t> raw;
    if (!lz4BlockDecompress(data + h.p, packed, raw, 9388032)) { error = "LZ4 data is corrupted"; return nullptr; }
    SgbReader r{raw.data(), raw.size()};
    if (!r.need(1)) { error = "empty Lua state"; return nullptr; }
    int count = raw[r.p++];
    cJSON* root = nullptr;
    for (int i = 0; i < count; i++) {
        cJSON* v = luabinsValue(r, 0, error);
        if (!v) { if (root) cJSON_Delete(root); return nullptr; }
        if (!root && cJSON_IsObject(v)) root = v;
        else cJSON_Delete(v);
    }
    if (!root) root = cJSON_CreateObject();
    cJSON* hd = cJSON_CreateObject();
    cJSON_AddNumberToObject(hd, "version", version);
    cJSON_AddStringToObject(hd, "location", location.c_str());
    cJSON_AddNumberToObject(hd, "runs", runs);
    cJSON_AddNumberToObject(hd, "meta_points", meta);
    cJSON_AddNumberToObject(hd, "shrine_points", shrine);
    cJSON_AddNumberToObject(hd, "god_mode", god);
    cJSON_AddNumberToObject(hd, "hell_mode", hell);
    cJSON_AddStringToObject(hd, "map", map.c_str());
    cJSON_AddItemToObject(root, "_header", hd);
    if (info) info->summary = "Supergiant SGB1 v" + std::to_string(version) + ", " + std::to_string(runs) + " runs, Lua state " +
                              std::to_string(raw.size() / 1024) + " KB";
    return root;
}

bool SgbDoc::parse(std::vector<uint8_t>&& data) {
    cJSON* t = sgbToJson(data.data(), data.size(), error, &info);
    if (!t) return false;
    adopt(t, false);
    return true;
}

// ================================================================== DELTARUNE (deltarune.sav)
// Mirror of parseDeltarune in tools/savelab/formats.js. The save is a JSON object {file name: file text}; the
// chapter files "filechN_S" are the lines written by scr_saveprocess (console branch), arrays as ds_list_write hex.
namespace {

const int DR_FLAG_BASE = 2400, DR_TROPHIES = 30;

struct DsVal {
    int kind = 0;   // 0 number, 1 string, 2 null
    double num = 0;
    std::string str;
};

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// GameMaker ds_list_write: hex text of u32 0x12F, u32 count, then per entry u32 type and the value
// (0 real: f64, 1 string: u32 length + bytes, 5 undefined: nothing, 7 int32, 10 int64, 13 bool: f64).
bool isDsListLine(const std::string& s) {
    if (s.size() < 16 || (s.size() & 1)) return false;
    if (s.compare(0, 8, "2F010000") != 0 && s.compare(0, 8, "2f010000") != 0) return false;
    for (char c : s) if (hexDigit(c) < 0) return false;
    return true;
}
bool dsListDecode(const std::string& s, std::vector<DsVal>& out) {
    out.clear();
    std::vector<uint8_t> b(s.size() / 2);
    for (size_t i = 0; i < b.size(); i++) b[i] = (uint8_t)(hexDigit(s[2 * i]) * 16 + hexDigit(s[2 * i + 1]));
    size_t p = 4;
    auto u32 = [&](uint32_t& v) {
        if (b.size() - p < 4) return false;
        v = b[p] | b[p + 1] << 8 | b[p + 2] << 16 | (uint32_t)b[p + 3] << 24;
        p += 4;
        return true;
    };
    auto f64 = [&](double& v) { if (b.size() - p < 8) return false; memcpy(&v, b.data() + p, 8); p += 8; return true; };
    uint32_t count;
    if (!u32(count) || count > (b.size() - 8) / 4) return false;
    out.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t type;
        if (!u32(type)) return false;
        DsVal v;
        if (type == 0 || type == 13) {
            if (!f64(v.num)) return false;
            if (!std::isfinite(v.num)) v.kind = 2;
        } else if (type == 1) {
            uint32_t len;
            if (!u32(len) || len > b.size() - p) return false;
            v.kind = 1;
            v.str.assign((const char*)b.data() + p, len);
            p += len;
            size_t z = v.str.find('\0');
            if (z != std::string::npos) v.str.resize(z);
        } else if (type == 5) {
            v.kind = 2;
        } else if (type == 7) {
            uint32_t x;
            if (!u32(x)) return false;
            v.num = (int32_t)x;
        } else if (type == 10) {
            uint32_t lo, hi;
            if (!u32(lo) || !u32(hi)) return false;
            v.num = (double)(int64_t)((uint64_t)hi << 32 | lo);
        } else {
            return false;
        }
        out.push_back(std::move(v));
    }
    return true;
}

// Number written by GameMaker's string(real) (or an ini value): [-+]digits[.digits][e[-+]digits], nothing else.
bool drNumber(const std::string& s, double& v) {
    size_t i = 0, n = s.size();
    if (i < n && (s[i] == '-' || s[i] == '+')) i++;
    size_t d = i;
    while (i < n && isdigit((unsigned char)s[i])) i++;
    if (i == d) return false;
    if (i < n && s[i] == '.') {
        size_t f = ++i;
        while (i < n && isdigit((unsigned char)s[i])) i++;
        if (i == f) return false;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '-' || s[i] == '+')) i++;
        size_t e = i;
        while (i < n && isdigit((unsigned char)s[i])) i++;
        if (i == e) return false;
    }
    if (i != n) return false;
    v = strtod(s.c_str(), nullptr);
    return std::isfinite(v);
}

cJSON* dsValJson(const DsVal& v) {
    if (v.kind == 1) return cJSON_CreateString(v.str.c_str());
    if (v.kind == 2) return cJSON_CreateNull();
    return cJSON_CreateNumber(v.num);
}

std::vector<std::string> splitLines(const char* t) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* c = t; *c; c++) {
        if (*c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur += *c;
        }
    }
    if (!cur.empty()) {
        if (cur.back() == '\r') cur.pop_back();
        out.push_back(cur);
    }
    return out;
}

// scr_saveprocess, console branch. Kinds: S string, N number, C character (into "char"), L list, F flag list
// (sparse "flags"), X skipped line(s).
struct DrField { const char* name; char kind; int count; };
std::vector<DrField> drLayout(int chapter) {
    bool one = chapter == 1;
    std::vector<DrField> l = {{"truename", 'S', 1}, {"othername", 'L', 1}, {"char", 'C', 1}, {"char", 'C', 1}, {"char", 'C', 1},
                              {"gold", 'N', 1}, {"xp", 'N', 1}, {"lv", 'N', 1}, {"inv", 'X', 1}, {"invc", 'X', 1}, {"darkzone", 'N', 1}};
    for (const char* n : {"hp", "maxhp", "at", "df", "mag", "guts", "charweapon", "chararmor1", "chararmor2", "weaponstyle"})
        l.push_back({n, 'L', 1});
    l.push_back({"items", 'X', one ? 4 * (4 * 8 + 12) : 5 * (4 * 10 + 12)});   // itemat..itemspecial(/element) and spells
    l.push_back({"bolts", 'X', 3});                                             // boltspeed, grazeamt, grazesize
    for (const char* n : {"item", "keyitem", "weapon", "armor"}) l.push_back({n, 'L', 1});
    if (!one) l.push_back({"pocketitem", 'L', 1});
    for (const char* n : {"tension", "maxtension", "lweapon", "larmor", "lxp", "llv", "lgold", "lhp", "lmaxhp", "lat", "ldf", "lwstrength", "ladef"})
        l.push_back({n, 'N', 1});
    l.push_back({"litem", 'L', 1});
    l.push_back({"phone", 'L', 1});
    l.push_back({"flags", 'F', 1});
    l.push_back({"plot", 'N', 1});
    l.push_back({"room", 'N', 1});
    l.push_back({"time", 'N', 1});
    return l;
}

cJSON* drFlags(const std::vector<DsVal>& v) {
    cJSON* o = cJSON_CreateObject();
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].kind == 2 || (v[i].kind == 0 && v[i].num == 0) || (v[i].kind == 1 && v[i].str.empty())) continue;
        cJSON_AddItemToObject(o, std::to_string(i).c_str(), dsValJson(v[i]));
    }
    return o;
}

cJSON* drScalar(const std::string& line, char kind) {
    double v;
    if (kind != 'S' && drNumber(line, v)) return cJSON_CreateNumber(v);
    return cJSON_CreateString(line.c_str());
}

// One "filechN_S" text -> object (see save_formats.h).
cJSON* drChapterFile(const std::string& name, int chapter, int slot, const std::string& route, const char* text) {
    cJSON* f = cJSON_CreateObject();
    cJSON_AddStringToObject(f, "name", name.c_str());
    cJSON_AddNumberToObject(f, "chapter", chapter);
    cJSON_AddNumberToObject(f, "slot", slot % 3);
    cJSON_AddNumberToObject(f, "completion", slot >= 3 ? 1 : 0);
    cJSON_AddStringToObject(f, "route", route.c_str());
    std::vector<std::string> lines = splitLines(text);
    std::vector<DrField> lay = drLayout(chapter);
    // the layout fits when list fields are on ds_list lines and number fields are not
    size_t at = 0;
    bool fits = true;
    for (const DrField& d : lay) {
        if (at + d.count > lines.size()) { fits = false; break; }
        bool list = isDsListLine(lines[at]);
        if ((d.kind == 'L' || d.kind == 'F') != list && d.kind != 'X' && d.kind != 'S') { fits = false; break; }
        at += d.count;
    }
    std::vector<DsVal> vals;
    if (fits) {
        at = 0;
        cJSON* chars = nullptr;
        for (const DrField& d : lay) {
            const std::string& ln = lines[at];
            at += d.count;
            if (d.kind == 'X') continue;
            if (d.kind == 'C') {
                if (!chars) chars = cJSON_AddArrayToObject(f, "char");
                cJSON_AddItemToArray(chars, drScalar(ln, 'N'));
                continue;
            }
            if (d.kind == 'S' || d.kind == 'N') { cJSON_AddItemToObject(f, d.name, drScalar(ln, d.kind)); continue; }
            if (!dsListDecode(ln, vals)) { cJSON_AddNullToObject(f, d.name); continue; }
            if (d.kind == 'F') { cJSON_AddItemToObject(f, d.name, drFlags(vals)); continue; }
            cJSON* a = cJSON_AddArrayToObject(f, d.name);
            for (const DsVal& v : vals) cJSON_AddItemToArray(a, dsValJson(v));
        }
        return f;
    }
    // Unknown layout (another game version): keep the flags (the last list of 2500+ values) and the 3 lines after.
    cJSON_AddStringToObject(f, "error", "unknown file layout");
    for (size_t i = lines.size(); i-- > 0;) {
        if (!isDsListLine(lines[i]) || !dsListDecode(lines[i], vals) || vals.size() < 2500) continue;
        cJSON_AddItemToObject(f, "flags", drFlags(vals));
        const char* tail[] = {"plot", "room", "time"};
        for (size_t k = 0; k < 3 && i + 1 + k < lines.size(); k++) cJSON_AddItemToObject(f, tail[k], drScalar(lines[i + 1 + k], 'N'));
        break;
    }
    return f;
}

// GameMaker ini text: [Section] lines, key="value" lines (a repeated key keeps the last value).
cJSON* drIni(const char* text) {
    cJSON* o = cJSON_CreateObject();
    cJSON* sec = nullptr;
    for (std::string ln : splitLines(text)) {
        size_t s = ln.find_first_not_of(" \t"), e = ln.find_last_not_of(" \t");
        if (s == std::string::npos) continue;
        ln = ln.substr(s, e - s + 1);
        if (ln[0] == '[' && ln.back() == ']') {
            std::string n = ln.substr(1, ln.size() - 2);
            sec = cJSON_GetObjectItemCaseSensitive(o, n.c_str());
            if (!sec) sec = cJSON_AddObjectToObject(o, n.c_str());
            continue;
        }
        size_t eq = ln.find('=');
        if (!sec || eq == std::string::npos || eq == 0) continue;
        std::string k = ln.substr(0, eq), v = ln.substr(eq + 1);
        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
        size_t vs = v.find_first_not_of(" \t");
        v = vs == std::string::npos ? "" : v.substr(vs);
        if (v.size() >= 2 && v[0] == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        cJSON_DeleteItemFromObjectCaseSensitive(sec, k.c_str());
        double d;
        if (drNumber(v, d)) cJSON_AddNumberToObject(sec, k.c_str(), d);
        else cJSON_AddStringToObject(sec, k.c_str(), v.c_str());
    }
    return o;
}

// "filechN_S" + optional route suffix (chapter 5: "_b")
bool drFileName(const std::string& k, int& chapter, int& slot, std::string& route) {
    if (k.compare(0, 6, "filech") != 0) return false;
    size_t i = 6, d = i;
    while (i < k.size() && isdigit((unsigned char)k[i])) i++;
    if (i == d || i - d > 3 || i >= k.size() || k[i] != '_') return false;
    chapter = atoi(k.substr(d, i - d).c_str());
    size_t s = ++i;
    while (i < k.size() && isdigit((unsigned char)k[i])) i++;
    if (i == s || i - s > 3) return false;
    slot = atoi(k.substr(s, i - s).c_str());
    route = k.substr(i);
    return true;
}

double drNum(const cJSON* o, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}
double drFlag(const cJSON* file, int index) {
    return drNum(cJSON_GetObjectItemCaseSensitive(file, "flags"), std::to_string(index).c_str());
}
// any number of the list equal to `value` (or, with anyNonZero, any non-zero number)
bool drListHas(const cJSON* file, const char* list, double value, bool anyNonZero = false) {
    const cJSON* a = cJSON_GetObjectItemCaseSensitive(file, list);
    const cJSON* c;
    cJSON_ArrayForEach(c, a) {
        if (cJSON_IsNumber(c) && (anyNonZero ? c->valuedouble != 0 : c->valuedouble == value)) return true;
    }
    return false;
}
}  // namespace

bool isDeltarune(const uint8_t* data, size_t size) {
    size_t s = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) s = 3;
    while (s < size && (data[s] == ' ' || data[s] == '\t' || data[s] == '\r' || data[s] == '\n')) s++;
    if (s >= size || data[s] != '{') return false;
    static const char key[] = "\"filech";
    for (size_t i = s; i + 7 <= size; i++)
        if (data[i] == '"' && memcmp(data + i, key, 7) == 0) return true;
    return false;
}

cJSON* deltaruneToJson(const uint8_t* data, size_t size, std::string& error, SaveFormatInfo* info) {
    if (!isDeltarune(data, size)) { error = "not a DELTARUNE save (deltarune.sav)"; return nullptr; }
    size_t s = data[0] == 0xEF ? 3 : 0, e = size;
    while (e > s && (data[e - 1] == 0 || data[e - 1] == ' ' || data[e - 1] == '\r' || data[e - 1] == '\n' || data[e - 1] == '\t')) e--;
    cJSON* src = cJSON_ParseWithLength(reinterpret_cast<const char*>(data + s), e - s);
    if (!cJSON_IsObject(src)) { if (src) cJSON_Delete(src); error = "JSON syntax error"; return nullptr; }
    struct Entry { std::string name; const char* text; };
    std::vector<Entry> chapterFiles, inis;
    const cJSON* c;
    cJSON_ArrayForEach(c, src) {
        if (!cJSON_IsString(c) || !c->string) continue;
        std::string k = c->string, route;
        int ch, sl;
        if (drFileName(k, ch, sl, route)) chapterFiles.push_back({k, c->valuestring});
        else if (k.size() > 4 && k.compare(k.size() - 4, 4, ".ini") == 0) inis.push_back({k, c->valuestring});
    }
    auto byName = [](const Entry& a, const Entry& b) { return a.name < b.name; };
    std::sort(chapterFiles.begin(), chapterFiles.end(), byName);
    std::sort(inis.begin(), inis.end(), byName);
    cJSON* root = cJSON_CreateObject();
    cJSON* files = cJSON_AddArrayToObject(root, "files");
    for (const Entry& f : chapterFiles) {
        int ch = 0, sl = 0;
        std::string route;
        drFileName(f.name, ch, sl, route);
        cJSON_AddItemToArray(files, drChapterFile(f.name, ch, sl, route, f.text));
    }
    cJSON* ini = cJSON_AddObjectToObject(root, "ini");
    for (const Entry& f : inis) cJSON_AddItemToObject(ini, f.name.c_str(), drIni(f.text));
    cJSON_Delete(src);

    // Derived values (same in formats.js). Fallbacks: what the save shows without the patch.
    double completed[6] = {0, 0, 0, 0, 0, 0}, patch[DR_TROPHIES] = {0}, fb[DR_TROPHIES] = {0};
    cJSON_ArrayForEach(c, files) {
        int ch = (int)drNum(c, "chapter");
        if (ch >= 1 && ch <= 5 && drNum(c, "completion") == 1) completed[ch] = 1;
        for (int i = 0; i < DR_TROPHIES; i++) if (drFlag(c, DR_FLAG_BASE + i) >= 1) patch[i] = 1;
        if (drFlag(c, 106) >= 1) fb[9] = 1;   // CONSUMER (Chapter 1): ate the moss
        // ARMOR (Chapter 1): nobody wears armor when Chapter 1 starts
        if (ch == 1 && (drListHas(c, "chararmor1", 0, true) || drListHas(c, "chararmor2", 0, true))) fb[14] = 1;
        if (ch >= 2 && drFlag(c, 325) == 1) fb[18] = 1;     // CLOSENESS: the Chapter 2 photo
        if (ch >= 3 && drFlag(c, 1220) >= 1) fb[22] = 1;    // BEAST: Susiezilla won (Chapter 3)
        if (ch >= 2 && (drListHas(c, "weapon", 13) || drListHas(c, "charweapon", 13) ||   // RING: ThornRing
                        drListHas(c, "weapon", 22) || drListHas(c, "charweapon", 22))) fb[27] = 1;   // or FreezeRing
    }
    for (int n = 1; n <= 4; n++) fb[n] = completed[n];   // CHAPTER n END
    double trophy[DR_TROPHIES];
    bool all = true;
    for (int i = 1; i < DR_TROPHIES; i++) {
        trophy[i] = patch[i] != 0 || fb[i] != 0 ? 1 : 0;
        all = all && trophy[i] != 0;
    }
    trophy[0] = patch[0] != 0 || all ? 1 : 0;   // CONNECTION
    cJSON_AddItemToObject(root, "completed", cJSON_CreateDoubleArray(completed, 6));
    cJSON_AddItemToObject(root, "patch", cJSON_CreateDoubleArray(patch, DR_TROPHIES));
    cJSON_AddItemToObject(root, "trophy", cJSON_CreateDoubleArray(trophy, DR_TROPHIES));
    if (info) {
        int got = 0, patched = 0;
        for (int i = 0; i < DR_TROPHIES; i++) { got += trophy[i] != 0; patched += patch[i] != 0; }
        std::string done;
        for (int n = 1; n <= 5; n++) if (completed[n]) done += (done.empty() ? "" : ",") + std::to_string(n);
        info->summary = "DELTARUNE save, " + std::to_string(chapterFiles.size()) + " chapter files, chapters completed: " +
                        (done.empty() ? "none" : done) + ", trophies " + std::to_string(got) + "/30 (" + std::to_string(patched) + " from the patch)";
    }
    return root;
}

bool DeltaruneDoc::parse(std::vector<uint8_t>&& data) {
    cJSON* t = deltaruneToJson(data.data(), data.size(), error, &info);
    if (!t) return false;
    adopt(t, false);
    return true;
}
