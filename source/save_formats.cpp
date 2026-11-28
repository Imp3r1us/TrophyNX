// GVAS (Unreal), NRBF (.NET BinaryFormatter), SGB, DELTARUNE, MessagePack and THEATRHYTHM readers -> cJSON trees.
// Mirror of tools/savelab/formats.js: any change here must be made there too (host/test/test_formats.cpp
// compares both on the same files). Built without exceptions: errors are sticky in the Reader.
#include "save_formats.h"
#include "tree_paths.h"
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
    bool tags54 = false;   // UE 5.4+ property tags (complete type names)
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
    std::string innerStruct, keyStruct, valueStruct;   // UE 5.4+: struct names of containers
    int32_t size = 0, index = 0;
    uint8_t boolVal = 0;
};

cJSON* readProps(Reader& r, GvasCtx& c, int depth);
cJSON* readStruct(Reader& r, const std::string& name, GvasCtx& c, int depth, double size);

// UE 5.4+ type name tree: name, parameter count, parameters (see formats.js readTypeName)
struct TypeName { std::string name; std::vector<TypeName> params; };
bool readTypeName(Reader& r, TypeName& tn, int depth = 0) {
    if (depth > 16) { r.fail("property type nested too deeply"); return false; }
    tn.name = fstr(r);
    int32_t n = r.i32();
    if (r.bad) return false;
    if (n < 0 || n > 64) { r.fail("invalid property type"); return false; }
    tn.params.resize(n);
    for (int32_t i = 0; i < n; i++) if (!readTypeName(r, tn.params[i], depth + 1)) return false;
    return true;
}
static const TypeName* tparam(const TypeName* tn, size_t i) { return tn && i < tn->params.size() ? &tn->params[i] : nullptr; }
static std::string tpname(const TypeName* tn, size_t i) { const TypeName* p = tparam(tn, i); return p ? p->name : std::string(); }

bool readTag54(Reader& r, Tag& t) {
    TypeName tn;
    if (!readTypeName(r, tn)) return false;
    t.type = tn.name;
    if (!validType(t.type)) { r.fail("invalid property type"); return false; }
    t.size = r.i32();
    uint8_t flags = r.u8();
    if (flags & 1) t.index = r.i32();
    if (flags & 2) r.skip(16);
    if (flags & 4) { uint8_t ext = r.u8(); if (ext & 2) { r.u8(); r.u32(); } }   // overridable information
    if (t.type == "StructProperty") t.structName = tpname(&tn, 0);
    else if (t.type == "BoolProperty") t.boolVal = (flags & 0x10) ? 1 : 0;
    else if (t.type == "ByteProperty") { t.enumName = tpname(&tn, 0); if (t.enumName.empty()) t.enumName = "None"; }
    else if (t.type == "EnumProperty") t.enumName = tpname(&tn, 0);
    else if (t.type == "ArrayProperty" || t.type == "SetProperty") { t.inner = tpname(&tn, 0); t.innerStruct = tpname(tparam(&tn, 0), 0); }
    else if (t.type == "MapProperty") {
        t.key = tpname(&tn, 0); t.value = tpname(&tn, 1);
        t.keyStruct = tpname(tparam(&tn, 0), 0); t.valueStruct = tpname(tparam(&tn, 1), 0);
    }
    return !r.bad;
}

// false at "None" or on error (r.bad)
bool readTag(Reader& r, Tag& t, const GvasCtx& c) {
    t.name = fstr(r);
    if (r.bad || t.name == "None") return false;
    if (!validName(t.name)) { char m[64]; snprintf(m, sizeof m, "invalid property name at 0x%zX", r.o); r.fail(m); return false; }
    if (c.tags54) return readTag54(r, t);
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

cJSON* readArray(Reader& r, const std::string& inner, int32_t size, GvasCtx& c, int depth, const std::string& innerStruct) {
    int32_t n = r.i32();
    if (r.bad) return nullptr;
    if (n < 0 || n > size) { r.fail("invalid array"); return nullptr; }
    cJSON* out = cJSON_CreateArray();
    if (inner == "StructProperty" && c.tags54) {   // 5.4+: no inner tag, the struct name is in the type
        double each = n ? (double)(size - 4) / n : 0;
        for (int32_t i = 0; i < n && !r.bad; i++) { cJSON* e = readStruct(r, innerStruct, c, depth, each); if (e) cJSON_AddItemToArray(out, e); }
    } else if (inner == "StructProperty") {
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
    if (t.type == "ArrayProperty") return readArray(r, t.inner, t.size, c, depth, t.innerStruct);
    if (t.type == "SetProperty" || t.type == "MapProperty") {
        bool map = t.type == "MapProperty";
        const std::string& kt = map ? t.key : t.inner;
        const std::string& ks = map ? t.keyStruct : t.innerStruct;
        const std::string* kS = ks.empty() ? nullptr : &ks;
        const std::string* vS = t.valueStruct.empty() ? nullptr : &t.valueStruct;
        int32_t rm = r.i32();
        if (r.bad) return nullptr;
        if (rm < 0 || rm > t.size) { r.fail("invalid set/map"); return nullptr; }
        for (int32_t i = 0; i < rm && !r.bad; i++) { cJSON* x = readElem(r, kt, c, depth, kS); if (x) cJSON_Delete(x); }
        int32_t n = r.i32();
        if (r.bad) return nullptr;
        if (n < 0 || n > t.size) { r.fail("invalid set/map"); return nullptr; }
        cJSON* out = cJSON_CreateArray();
        for (int32_t i = 0; i < n && !r.bad; i++) {
            cJSON* k = readElem(r, kt, c, depth, kS);
            if (!map) { if (k) cJSON_AddItemToArray(out, k); continue; }
            cJSON* v = r.bad ? nullptr : readElem(r, t.value, c, depth, vS);
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
        if (!readTag(r, t, c)) break;
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
    c.lwc = c.ue5 >= 1004;
    uint16_t major = r.u16(), minor = r.u16(), patch = r.u16();
    r.u32();
    std::string branch = fstr(r);
    uint32_t cvf = r.u32(), ncv = r.u32();
    if (!r.bad && ncv > 10000) r.fail("invalid GVAS header");
    for (uint32_t i = 0; i < ncv && !r.bad; i++) { if (cvf == 1) r.skip(8); else { r.skip(20); if (cvf == 2) fstr(r); } }
    std::string cls = fstr(r);
    c.tags54 = c.ue5 >= 1012;
    if (c.tags54) r.u8();   // 5.4+: one byte after the class name (0 in every save seen)
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

// ------------------------------------------------------------------ SGB1, streamed and pruned (live module)
// Same tree as sgbToJson, for a heap of a few MB: the file is read 16 KB at a time, LZ4 is decoded through its
// 64 KB window (LZ4 never looks further back), and the Lua state is walked as it comes, building only the values
// the rule paths can reach (a trie of the paths; "[*]" = every value of a table).
namespace {

struct SgbFileIn {   // buffered reads through ReadAtFn
    const ReadAtFn& read;
    uint64_t size, off = 0;   // next file offset to read
    std::vector<uint8_t> buf;   // on the heap: the live module's stack is small
    size_t len = 0, pos = 0;
    bool ok = true;
    SgbFileIn(const ReadAtFn& r, uint64_t s) : read(r), size(s), buf(16384) {}
    uint64_t tell() const { return off - len + pos; }
    bool byte(uint8_t& b) {
        if (pos == len) {
            if (!ok || off >= size) { ok = false; return false; }
            size_t n = (size_t)std::min<uint64_t>(buf.size(), size - off);
            if (!read(off, buf.data(), n)) { ok = false; return false; }
            off += n; len = n; pos = 0;
        }
        b = buf[pos++];
        return true;
    }
    bool u32(uint32_t& v) {
        uint8_t b[4];
        for (int i = 0; i < 4; i++) if (!byte(b[i])) return false;
        v = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
        return true;
    }
    bool str(std::string* out) {   // u32 length + bytes (out == nullptr: skipped)
        uint32_t k;
        if (!u32(k) || k > size - tell()) { ok = false; return false; }
        if (out) out->clear();
        for (uint32_t i = 0; i < k; i++) { uint8_t b; if (!byte(b)) return false; if (out) *out += (char)b; }
        return true;
    }
};

struct Lz4Stream {   // LZ4 block decoder, one byte at a time, 64 KB ring window
    SgbFileIn& in;
    uint64_t inEnd;            // file offset where the compressed block ends
    size_t maxOut, produced = 0;
    std::vector<uint8_t> win;
    size_t litLeft = 0, matchLeft = 0, matchOff = 0;
    uint8_t tok = 0;
    bool afterLit = false, bad = false;
    std::string err;
    Lz4Stream(SgbFileIn& i, uint64_t end, size_t mo) : in(i), inEnd(end), maxOut(mo) { win.resize(65536); }
    void fail(const char* e) { if (!bad) { bad = true; err = e; } }
    bool inByte(uint8_t& b) {
        if (in.tell() >= inEnd || !in.byte(b)) { fail("LZ4 data is corrupted"); return false; }
        return true;
    }
    bool ext(size_t& v) {   // 255-continued length
        uint8_t b;
        do { if (!inByte(b)) return false; v += b; } while (b == 255);
        return true;
    }
    bool put(uint8_t b) {
        if (produced >= maxOut) { fail("LZ4 data is corrupted"); return false; }
        win[produced & 0xFFFF] = b;
        produced++;
        return true;
    }
    // false at the end of the data (bad == false) or on an error (bad == true)
    bool next(uint8_t& b) {
        for (;;) {
            if (bad) return false;
            if (litLeft) {
                if (!inByte(b) || !put(b)) return false;
                litLeft--;
                return true;
            }
            if (matchLeft) {
                b = win[(produced - matchOff) & 0xFFFF];
                if (!put(b)) return false;
                matchLeft--;
                return true;
            }
            if (afterLit) {   // literals done: a match follows, unless this was the last sequence
                afterLit = false;
                if (in.tell() >= inEnd) return false;
                uint8_t lo, hi;
                if (!inByte(lo) || !inByte(hi)) return false;
                size_t off = lo | (hi << 8);
                if (off == 0 || off > produced) { fail("LZ4 data is corrupted"); return false; }
                size_t len = tok & 15;
                if (len == 15 && !ext(len)) return false;
                matchOff = off;
                matchLeft = len + 4;
                continue;
            }
            if (in.tell() >= inEnd) return false;
            if (!inByte(tok)) return false;
            size_t lit = tok >> 4;
            if (lit == 15 && !ext(lit)) return false;
            litLeft = lit;
            afterLit = true;
        }
    }
};

struct LuaStream {
    Lz4Stream& z;
    const PathTrie& trie;
    size_t budget, used = 0;
    std::string err;
    bool byte(uint8_t& b) {
        if (z.next(b)) return true;
        if (err.empty()) err = z.bad ? z.err : "truncated Lua state";
        return false;
    }
    bool u32(uint32_t& v) {
        uint8_t b[4];
        for (int i = 0; i < 4; i++) if (!byte(b[i])) return false;
        v = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
        return true;
    }
    bool f64(double& v) {
        uint8_t b[8];
        for (int i = 0; i < 8; i++) if (!byte(b[i])) return false;
        memcpy(&v, b, 8);
        return true;
    }
    bool str(std::string* out) {
        uint32_t k;
        if (!u32(k)) return false;
        if (k > z.maxOut - z.produced) { err = "truncated string"; return false; }
        if (out) { out->clear(); out->reserve(std::min<uint32_t>(k, 4096)); }
        for (uint32_t i = 0; i < k; i++) {
            uint8_t b;
            if (!byte(b)) { if (!z.bad) err = "truncated string"; return false; }
            if (out) *out += (char)b;
        }
        return true;
    }
    bool charge(size_t n) {   // approximate heap use of the pruned tree
        used += n + 48;
        if (budget && used > budget) { err = "not enough memory for the rule values (" + std::to_string(budget >> 10) + " KB free)"; return false; }
        return true;
    }
    bool tableSize(uint64_t& total) {
        uint32_t na, nh;
        if (!u32(na) || !u32(nh)) return false;
        total = (uint64_t)na + nh;
        if (total > (z.maxOut - z.produced) / 2) { err = "bad Lua table size"; return false; }
        return true;
    }
    bool badTag(uint8_t t) {
        char b[64];
        snprintf(b, sizeof(b), "unknown Lua value tag 0x%02X at %zu", (unsigned)t, z.produced - 1);
        err = b;
        return false;
    }
    bool skip(int depth) {
        if (depth > 64) { err = "Lua tables nested too deep"; return false; }
        uint8_t t;
        if (!byte(t)) return false;
        double d;
        uint64_t n;
        switch (t) {
            case '-': case '0': case '1': return true;
            case 'N': return f64(d);
            case 'S': return str(nullptr);
            case 'T':
                if (!tableSize(n)) return false;
                for (uint64_t i = 0; i < 2 * n; i++) if (!skip(depth + 1)) return false;
                return true;
            default: return badTag(t);
        }
    }
    // A table key as sgbToJson names it (luabinsKey): strings as is, numbers in decimal, booleans by name, else "".
    bool key(std::string& out, int depth) {
        if (depth > 64) { err = "Lua tables nested too deep"; return false; }
        uint8_t t;
        if (!byte(t)) return false;
        out.clear();
        switch (t) {
            case '-': return true;
            case '0': out = "false"; return true;
            case '1': out = "true"; return true;
            case 'N': {
                double v;
                if (!f64(v)) return false;
                char b[40];
                if (v == std::floor(v) && std::fabs(v) < 1e15) snprintf(b, sizeof(b), "%lld", (long long)v);
                else snprintf(b, sizeof(b), "%.17g", v);
                out = b;
                return true;
            }
            case 'S':
                if (!str(&out)) return false;
                out = std::string(out.c_str());   // cJSON strings stop at a NUL, like sgbToJson's keys
                return true;
            case 'T': {
                uint64_t n;
                if (!tableSize(n)) return false;
                for (uint64_t i = 0; i < 2 * n; i++) if (!skip(depth + 1)) return false;
                return true;
            }
            default: return badTag(t);
        }
    }
    // A value under `where` (not dead). nullptr = error (err set).
    cJSON* value(int depth, const PathSet& where) {
        if (depth > 64) { err = "Lua tables nested too deep"; return nullptr; }
        uint8_t t;
        if (!byte(t)) return nullptr;
        cJSON* r = nullptr;
        switch (t) {
            case '-': r = cJSON_CreateNull(); break;
            case '0': r = cJSON_CreateFalse(); break;
            case '1': r = cJSON_CreateTrue(); break;
            case 'N': {
                double v;
                if (!f64(v)) return nullptr;
                r = cJSON_CreateNumber(v);
                break;
            }
            case 'S': {
                std::string s;
                if (!str(&s)) return nullptr;
                if (!charge(strlen(s.c_str()))) return nullptr;
                r = cJSON_CreateString(s.c_str());
                break;
            }
            case 'T': {
                uint64_t n;
                if (!tableSize(n)) return nullptr;
                if (!charge(0)) return nullptr;
                cJSON* o = cJSON_CreateObject();
                if (!o) { err = "out of memory"; return nullptr; }
                std::string k;
                for (uint64_t i = 0; i < n; i++) {
                    if (!key(k, depth + 1)) { cJSON_Delete(o); return nullptr; }
                    PathSet c = childOf(trie, where, k);
                    if (c.dead()) {
                        if (!skip(depth + 1)) { cJSON_Delete(o); return nullptr; }
                        continue;
                    }
                    cJSON* v = value(depth + 1, c);
                    if (!v) { cJSON_Delete(o); return nullptr; }
                    if (!charge(k.size())) { cJSON_Delete(v); cJSON_Delete(o); return nullptr; }
                    cJSON_AddItemToObject(o, k.c_str(), v);
                }
                return o;
            }
            default: badTag(t); return nullptr;
        }
        if (!r) { err = "out of memory"; return nullptr; }
        if (!charge(0)) { cJSON_Delete(r); return nullptr; }
        return r;
    }
};

}  // namespace

cJSON* sgbToJsonStream(const ReadAtFn& read, uint64_t size, const std::vector<std::string>& keepPaths, size_t memoryBudget,
                       std::string& error, SaveFormatInfo* info) {
    uint8_t sig[16];
    if (size < 16 || !read(0, sig, 16) || !isSgb(sig, 16)) { error = "not a Supergiant SGB1 save"; return nullptr; }
    PathTrie trie;
    if (!trie.build(keepPaths)) { error = "unusable rule path"; return nullptr; }
    SgbFileIn h(read, size);
    uint8_t skip8;
    for (int i = 0; i < 8; i++) h.byte(skip8);   // signature + checksum
    uint32_t version = 0, runs = 0, meta = 0, shrine = 0, nkeys = 0, packed = 0, lo, hi;
    h.u32(version);
    if (version >= 16) { h.u32(lo); h.u32(hi); }   // timestamp
    std::string location, map;
    h.str(&location);
    h.u32(runs); h.u32(meta); h.u32(shrine);
    uint8_t god = 0, hell = 0;
    h.byte(god); h.byte(hell);
    h.u32(nkeys);
    for (uint32_t i = 0; i < nkeys && h.ok; i++) h.str(nullptr);
    h.str(&map);
    h.str(nullptr);   // start_next_map
    h.u32(packed);
    if (!h.ok || packed > size - h.tell()) { error = "truncated header"; return nullptr; }
    Lz4Stream z(h, h.tell() + packed, 9388032);   // same cap as sgbToJson
    LuaStream L{z, trie, memoryBudget, 0, std::string()};
    uint8_t count;
    if (!L.byte(count)) { error = "empty Lua state"; return nullptr; }
    PathSet top;
    top.at.push_back(0);
    if (trie.nodes[0].whole) { top.full = true; top.at.clear(); }
    cJSON* root = nullptr;
    for (int i = 0; i < count; i++) {
        if (root) {   // sgbToJson keeps only the first table too
            if (!L.skip(0)) { error = L.err; cJSON_Delete(root); return nullptr; }
            continue;
        }
        cJSON* v = L.value(0, top);
        if (!v) { error = L.err; return nullptr; }
        if (cJSON_IsObject(v)) root = v;
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
                              std::to_string(z.produced / 1024) + " KB (streamed, rule values only)";
    return root;
}

bool SgbDoc::parseStream(const ReadAtFn& read, uint64_t size, const std::vector<std::string>& keepPaths, size_t memoryBudget) {
    cJSON* t = sgbToJsonStream(read, size, keepPaths, memoryBudget, error, &info);
    if (!t) return false;
    adopt(t, false);
    return true;
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

// ================================================================== MessagePack, raw DEFLATE, SHA-1 / AES (THEATRHYTHM)
// Mirror of parseMsgpack / inflateRawSync / sha1 / aes128CbcDecrypt / parseTheatrhythm in tools/savelab/formats.js.
// Self-contained (no zlib / mbedtls): the same file is built into the app, the live module and the host tests.
namespace {

// ---- SHA-1, HMAC, PBKDF2 (one 20-byte block)
struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t buf[64];
    size_t n = 0;
    uint64_t total = 0;
    static uint32_t rol(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f = i < 20 ? ((b & c) | (~b & d)) + 0x5A827999 : i < 40 ? (b ^ c ^ d) + 0x6ED9EBA1
                       : i < 60 ? ((b & c) | (b & d) | (c & d)) + 0x8F1BBCDC : (b ^ c ^ d) + 0xCA62C1D6;
            uint32_t t = rol(a, 5) + f + e + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    void update(const uint8_t* p, size_t len) {
        total += len;
        while (len) {
            size_t k = std::min(len, (size_t)64 - n);
            memcpy(buf + n, p, k);
            n += k; p += k; len -= k;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    void final(uint8_t out[20]) {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80, zero = 0;
        update(&pad, 1);
        while (n != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(len, 8);
        for (int i = 0; i < 20; i++) out[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
    }
};

// The inner/outer HMAC states are computed once: 2 SHA-1 blocks per iteration (the password search runs
// thousands of these on the console).
void pbkdf2Sha1(const std::string& pw, const uint8_t* salt, size_t slen, int iter, uint8_t* out, size_t dkLen) {
    uint8_t k[64] = {0}, hk[20];
    const uint8_t* key = (const uint8_t*)pw.data();
    size_t klen = pw.size();
    if (klen > 64) { Sha1 s; s.update(key, klen); s.final(hk); key = hk; klen = 20; }
    memcpy(k, key, klen);
    uint8_t ip[64], op[64];
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    Sha1 ictx, octx;
    ictx.update(ip, 64);
    octx.update(op, 64);
    auto hmac = [&](const uint8_t* msg, size_t mlen, uint8_t res[20]) {
        uint8_t inner[20];
        Sha1 a = ictx; a.update(msg, mlen); a.final(inner);
        Sha1 b = octx; b.update(inner, 20); b.final(res);
    };
    std::vector<uint8_t> s(salt, salt + slen);
    s.insert(s.end(), {0, 0, 0, 1});
    uint8_t u[20], t[20];
    hmac(s.data(), s.size(), u);
    memcpy(t, u, 20);
    for (int i = 1; i < iter; i++) {
        uint8_t v[20];
        hmac(u, 20, v);
        memcpy(u, v, 20);
        for (int j = 0; j < 20; j++) t[j] ^= u[j];
    }
    memcpy(out, t, std::min<size_t>(dkLen, 20));
}

// ---- AES-128 decryption (CBC)
struct AesTables {
    uint8_t sbox[256], inv[256], m9[256], m11[256], m13[256], m14[256];
    static uint8_t xt(uint8_t a) { return (uint8_t)((a << 1) ^ (a & 0x80 ? 0x1b : 0)); }
    static uint8_t mul(uint8_t a, uint8_t b) { uint8_t r = 0; while (b) { if (b & 1) r ^= a; a = xt(a); b >>= 1; } return r; }
    AesTables() {
        for (int i = 0; i < 256; i++) {
            uint8_t x = 0;
            if (i) for (int j = 1; j < 256; j++) if (mul((uint8_t)i, (uint8_t)j) == 1) { x = (uint8_t)j; break; }
            uint8_t y = x;
            for (int k = 0; k < 4; k++) { x = (uint8_t)((x << 1) | (x >> 7)); y ^= x; }
            sbox[i] = y ^ 0x63;
        }
        for (int i = 0; i < 256; i++) {
            inv[sbox[i]] = (uint8_t)i;
            m9[i] = mul((uint8_t)i, 9); m11[i] = mul((uint8_t)i, 11); m13[i] = mul((uint8_t)i, 13); m14[i] = mul((uint8_t)i, 14);
        }
    }
};
const AesTables& aesTables() { static AesTables t; return t; }

void aes128CbcDecrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t* in, size_t len, uint8_t* out) {
    const AesTables& T = aesTables();
    uint8_t rk[176];
    memcpy(rk, key, 16);
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4] = {rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1]};
        if (i % 16 == 0) {
            uint8_t t0 = t[0];
            t[0] = T.sbox[t[1]] ^ rcon; t[1] = T.sbox[t[2]]; t[2] = T.sbox[t[3]]; t[3] = T.sbox[t0];
            rcon = AesTables::xt(rcon);
        }
        for (int j = 0; j < 4; j++) rk[i + j] = rk[i - 16 + j] ^ t[j];
    }
    uint8_t s[16], u[16];
    for (size_t off = 0; off + 16 <= len; off += 16) {
        for (int i = 0; i < 16; i++) s[i] = in[off + i] ^ rk[160 + i];
        for (int r = 9; r >= 0; r--) {
            for (int c = 0; c < 4; c++) for (int w = 0; w < 4; w++) u[w + 4 * c] = T.inv[s[w + 4 * ((c - w + 4) % 4)]];
            for (int i = 0; i < 16; i++) u[i] ^= rk[r * 16 + i];
            if (r) for (int c = 0; c < 4; c++) {
                uint8_t a0 = u[4 * c], a1 = u[4 * c + 1], a2 = u[4 * c + 2], a3 = u[4 * c + 3];
                s[4 * c] = T.m14[a0] ^ T.m11[a1] ^ T.m13[a2] ^ T.m9[a3];
                s[4 * c + 1] = T.m9[a0] ^ T.m14[a1] ^ T.m11[a2] ^ T.m13[a3];
                s[4 * c + 2] = T.m13[a0] ^ T.m9[a1] ^ T.m14[a2] ^ T.m11[a3];
                s[4 * c + 3] = T.m11[a0] ^ T.m13[a1] ^ T.m9[a2] ^ T.m14[a3];
            } else memcpy(s, u, 16);
        }
        const uint8_t* prev = off ? in + off - 16 : iv;
        for (int i = 0; i < 16; i++) out[off + i] = s[i] ^ prev[i];
    }
}

// ---- raw DEFLATE (after Mark Adler's puff.c)
const uint16_t INF_LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t INF_LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t INF_DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t INF_DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const uint8_t INF_ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct Huff { uint16_t count[16]; uint16_t sym[320]; };

// Output in a malloc'ed buffer: running out of memory (live module heap) is an error, never an abort.
struct Inflater {
    const uint8_t* src; size_t len, pos = 0;
    uint32_t bitbuf = 0; int bitcnt = 0;
    uint8_t* ob = nullptr; size_t on = 0, ocap = 0, maxOut;
    std::string err;
    Inflater(const uint8_t* s, size_t l, size_t m, size_t initial) : src(s), len(l), maxOut(m) {
        ocap = std::min(maxOut, std::max<size_t>(initial, 1024));
        ob = (uint8_t*)malloc(ocap);
        if (!ob) ocap = 0;
    }
    ~Inflater() { free(ob); }
    uint8_t* release() { uint8_t* b = ob; ob = nullptr; return b; }
    bool fail(const char* m) { if (err.empty()) err = std::string("DEFLATE: ") + m; return false; }
    bool bits(int k, int& v) {
        while (bitcnt < k) { if (pos >= len) return fail("unexpected end"); bitbuf |= (uint32_t)src[pos++] << bitcnt; bitcnt += 8; }
        v = (int)(bitbuf & ((1u << k) - 1)); bitbuf >>= k; bitcnt -= k; return true;
    }
    bool put(uint8_t b) {
        if (on == ocap) {
            if (on >= maxOut) return fail("output too large");
            size_t nc = std::min(maxOut, std::max<size_t>(ocap * 2, 1024));
            uint8_t* nb = (uint8_t*)realloc(ob, nc);
            if (!nb) return fail("out of memory");
            ob = nb; ocap = nc;
        }
        ob[on++] = b;
        return true;
    }
    bool build(Huff& h, const uint8_t* lens, int n) {
        uint16_t offs[16] = {0};
        memset(h.count, 0, sizeof h.count);
        for (int s = 0; s < n; s++) h.count[lens[s]]++;
        int left = 1;
        for (int l = 1; l < 16; l++) { left = left * 2 - h.count[l]; if (left < 0) return fail("bad code lengths"); }
        for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h.count[l];
        for (int s = 0; s < n; s++) if (lens[s]) h.sym[offs[lens[s]]++] = (uint16_t)s;
        return true;
    }
    bool decode(const Huff& h, int& sym) {
        int code = 0, first = 0, index = 0, b;
        for (int l = 1; l < 16; l++) {
            if (!bits(1, b)) return false;
            code |= b;
            int c = h.count[l];
            if (code - c < first) { sym = h.sym[index + code - first]; return true; }
            index += c; first = (first + c) << 1; code <<= 1;
        }
        return fail("bad code");
    }
    bool codes(const Huff& lc, const Huff& dc) {
        for (;;) {
            int s, e, d;
            if (!decode(lc, s)) return false;
            if (s < 256) { if (!put((uint8_t)s)) return false; continue; }
            if (s == 256) return true;
            s -= 257;
            if (s >= 29) return fail("bad length code");
            if (!bits(INF_LEXT[s], e)) return false;
            int n = INF_LBASE[s] + e;
            if (!decode(dc, d)) return false;
            if (d >= 30) return fail("bad distance code");
            if (!bits(INF_DEXT[d], e)) return false;
            size_t dist = (size_t)INF_DBASE[d] + e;
            if (dist > on) return fail("distance too far back");
            while (n--) if (!put(ob[on - dist])) return false;
        }
    }
    bool run() {
        Huff fl, fd;
        bool fixedReady = false;
        for (;;) {
            int last, type;
            if (!bits(1, last) || !bits(2, type)) return false;
            if (type == 0) {
                bitbuf = 0; bitcnt = 0;
                if (len - pos < 4) return fail("unexpected end");
                unsigned n = src[pos] | src[pos + 1] << 8, nn = src[pos + 2] | src[pos + 3] << 8;
                pos += 4;
                if (n != (~nn & 0xffff)) return fail("bad stored block");
                if (len - pos < n) return fail("unexpected end");
                for (unsigned i = 0; i < n; i++) if (!put(src[pos++])) return false;
            } else if (type == 1) {
                if (!fixedReady) {
                    uint8_t l[288], dl[30];
                    for (int i = 0; i < 288; i++) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
                    for (int i = 0; i < 30; i++) dl[i] = 5;
                    if (!build(fl, l, 288) || !build(fd, dl, 30)) return false;
                    fixedReady = true;
                }
                if (!codes(fl, fd)) return false;
            } else if (type == 2) {
                int nlen, ndist, ncode, v;
                if (!bits(5, nlen) || !bits(5, ndist) || !bits(4, ncode)) return false;
                nlen += 257; ndist += 1; ncode += 4;
                if (nlen > 286 || ndist > 30) return fail("bad counts");
                uint8_t cl[19] = {0}, lens[316] = {0};
                for (int i = 0; i < ncode; i++) { if (!bits(3, v)) return false; cl[INF_ORDER[i]] = (uint8_t)v; }
                Huff ch, lc, dc;
                if (!build(ch, cl, 19)) return false;
                for (int i = 0; i < nlen + ndist;) {
                    int s;
                    if (!decode(ch, s)) return false;
                    if (s < 16) { lens[i++] = (uint8_t)s; continue; }
                    int l = 0, rep;
                    if (s == 16) { if (!i) return fail("repeat without length"); l = lens[i - 1]; if (!bits(2, rep)) return false; rep += 3; }
                    else if (s == 17) { if (!bits(3, rep)) return false; rep += 3; }
                    else { if (!bits(7, rep)) return false; rep += 11; }
                    if (i + rep > nlen + ndist) return fail("too many lengths");
                    while (rep--) lens[i++] = (uint8_t)l;
                }
                if (!lens[256]) return fail("no end-of-block code");
                if (!build(lc, lens, nlen) || !build(dc, lens + nlen, ndist)) return false;
                if (!codes(lc, dc)) return false;
            } else return fail("bad block type");
            if (last) return true;
        }
    }
};

// ---- MessagePack -> cJSON
const int MP_MAX_DEPTH = 64;

std::string hexUpper(const uint8_t* p, size_t n) {
    static const char* H = "0123456789ABCDEF";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; i++) { s += H[p[i] >> 4]; s += H[p[i] & 15]; }
    return s;
}

// JavaScript lists object keys that are array indices (canonical decimal, 0 .. 2^32-2) first, ascending.
bool jsIndexKey(const char* k, uint32_t& v) {
    if (!k || !*k || (k[0] == '0' && k[1])) return false;
    uint64_t x = 0;
    for (const char* p = k; *p; p++) {
        if (*p < '0' || *p > '9' || p - k >= 10) return false;
        x = x * 10 + (uint64_t)(*p - '0');
    }
    if (x > 4294967294ull) return false;
    v = (uint32_t)x;
    return true;
}
void jsKeyOrder(cJSON* obj) {
    std::vector<std::pair<uint32_t, cJSON*>> idx;
    std::vector<cJSON*> rest;
    for (cJSON* c = obj->child; c; c = c->next) {
        uint32_t v;
        if (jsIndexKey(c->string, v)) idx.push_back({v, c}); else rest.push_back(c);
    }
    if (idx.empty()) return;
    std::stable_sort(idx.begin(), idx.end(), [](const std::pair<uint32_t, cJSON*>& a, const std::pair<uint32_t, cJSON*>& b) { return a.first < b.first; });
    std::vector<cJSON*> all;
    all.reserve(idx.size() + rest.size());
    for (auto& p : idx) all.push_back(p.second);
    all.insert(all.end(), rest.begin(), rest.end());
    obj->child = all[0];
    for (size_t i = 0; i < all.size(); i++) {
        all[i]->prev = i ? all[i - 1] : all.back();
        all[i]->next = i + 1 < all.size() ? all[i + 1] : nullptr;
    }
}

// Moves the key of `from` to `to` (both detached or `from` about to be replaced by `to`).
void moveKey(cJSON* from, cJSON* to) {
    to->string = from->string;
    from->string = nullptr;
    if (from->type & cJSON_StringIsConst) { to->type |= cJSON_StringIsConst; from->type &= ~cJSON_StringIsConst; }
}

// Optional pruning (live module): only the values that rule paths can reach are built; everything else is
// skipped without allocating. State = the (path, atom) pairs still alive at a node; `full` = some path ends at or
// above this node (it is kept whole: a rule may count its children). Arrays keep their length (skipped elements
// become null) so that "[n]" indexes stay right; "[Field=value]" elements are kept whole.
// Fixed size (no heap): in the live module, running out of memory must end in a clean error, so the pruned
// path only allocates through cJSON (which returns NULL) and short strings (no heap with the small-string buffer).
const size_t MP_MAX_PATHS = 128;
struct MpFilter {
    std::pair<uint16_t, uint16_t> at[MP_MAX_PATHS];
    size_t count = 0;
    bool full = false;
    bool dead() const { return !full && count == 0; }
};
typedef std::vector<std::vector<std::string>> MpPaths;

void mpAdvance(const MpPaths& paths, MpFilter& out, size_t p, size_t i) {
    if (i >= paths[p].size()) out.full = true;
    else if (out.count < MP_MAX_PATHS) out.at[out.count++] = {(uint16_t)p, (uint16_t)i};
    else out.full = true;   // (cannot happen: at most MP_MAX_PATHS paths) keep everything rather than lose a value
}
MpFilter mpChildKey(const MpPaths& paths, const MpFilter& f, const std::string& key) {
    MpFilter c;
    for (size_t k = 0; k < f.count; k++) {
        const auto& pi = f.at[k];
        const std::string& a = paths[pi.first][pi.second];
        if (a[0] != '[') { if (a == key) mpAdvance(paths, c, pi.first, pi.second + 1u); }
        else if (a == "[*]") mpAdvance(paths, c, pi.first, pi.second + 1u);   // every value of an object
        if (c.full) break;
    }
    return c;
}
MpFilter mpChildIndex(const MpPaths& paths, const MpFilter& f, uint64_t j) {
    MpFilter c;
    for (size_t k = 0; k < f.count; k++) {
        const auto& pi = f.at[k];
        const std::string& a = paths[pi.first][pi.second];
        if (a[0] != '[') continue;
        if (a == "[*]") mpAdvance(paths, c, pi.first, pi.second + 1u);
        else if (a.find('=') != std::string::npos) c.full = true;   // [Field=value]: the whole element is needed
        else if (strtoull(a.c_str() + 1, nullptr, 10) == j) mpAdvance(paths, c, pi.first, pi.second + 1u);
        if (c.full) break;
    }
    return c;
}

struct MpReader {
    const uint8_t* b; size_t n, o = 0;
    std::string err;
    // pruning (nullptr = whole tree) and field names given to [Key(n)] arrays while reading (pruned mode only)
    const MpPaths* paths = nullptr;
    const char* const* (*labeler)(const std::vector<std::string>& keys, uint64_t len) = nullptr;
    std::vector<std::string> keys;   // keys from the root to the current node (pruned mode)
    MpReader(const uint8_t* d, size_t s) : b(d), n(s) {}
    bool fail(const std::string& m) { if (err.empty()) err = m; return false; }
    bool need(size_t k) {
        if (k > n - o) { char m[64]; snprintf(m, sizeof m, "MessagePack: unexpected end at 0x%zX", o); return fail(m); }
        return true;
    }
    bool be(int k, uint64_t& v) {
        if (!need((size_t)k)) return false;
        v = 0;
        for (int i = 0; i < k; i++) v = v << 8 | b[o + i];
        o += (size_t)k;
        return true;
    }
    bool count(uint64_t c) {
        if (c > n - o) { char m[64]; snprintf(m, sizeof m, "MessagePack: bad length at 0x%zX", o); return fail(m); }
        return true;
    }
    static std::string cutStr(const uint8_t* p, size_t k) { size_t z = 0; while (z < k && p[z]) z++; return std::string((const char*)p, z); }
    cJSON* ext(int8_t type, size_t k) {
        if (!need(k)) return nullptr;
        const uint8_t* p = b + o;
        o += k;
        if (type == -1 && (k == 4 || k == 8 || k == 12)) {   // timestamp -> whole seconds
            auto rd = [&](int at, int len) { uint64_t v = 0; for (int i = 0; i < len; i++) v = v << 8 | p[at + i]; return v; };
            if (k == 4) return cJSON_CreateNumber((double)rd(0, 4));
            if (k == 8) return cJSON_CreateNumber((double)(rd(0, 8) & 0x3FFFFFFFFull));
            return cJSON_CreateNumber((double)(int64_t)rd(4, 8));
        }
        return cJSON_CreateString(("ext:" + std::to_string((int)type) + ":" + hexUpper(p, k)).c_str());
    }
    // Skips one value (no allocation).
    bool skip(int depth) {
        if (depth > MP_MAX_DEPTH) return fail("MessagePack: nested too deep");
        if (!need(1)) return false;
        uint8_t t = b[o++];
        uint64_t c = 0, ty;
        if (t <= 0x7f || t >= 0xe0 || t == 0xc0 || t == 0xc2 || t == 0xc3) return true;
        if ((t >= 0x80 && t <= 0x8f) || t == 0xde || t == 0xdf) {
            if (t >= 0x80 && t <= 0x8f) c = t & 15; else if (!be(t == 0xde ? 2 : 4, c)) return false;
            if (!count(c)) return false;
            for (uint64_t i = 0; i < 2 * c; i++) if (!skip(depth + 1)) return false;
            return true;
        }
        if ((t >= 0x90 && t <= 0x9f) || t == 0xdc || t == 0xdd) {
            if (t >= 0x90 && t <= 0x9f) c = t & 15; else if (!be(t == 0xdc ? 2 : 4, c)) return false;
            if (!count(c)) return false;
            for (uint64_t i = 0; i < c; i++) if (!skip(depth + 1)) return false;
            return true;
        }
        if (t >= 0xa0 && t <= 0xbf) c = t & 31;
        else if (t >= 0xc4 && t <= 0xc6) { if (!be(1 << (t - 0xc4), c)) return false; }
        else if (t >= 0xc7 && t <= 0xc9) { if (!be(1 << (t - 0xc7), c) || !be(1, ty)) return false; }
        else if (t == 0xca) c = 4;
        else if (t == 0xcb) c = 8;
        else if (t >= 0xcc && t <= 0xcf) c = (uint64_t)1 << (t - 0xcc);
        else if (t >= 0xd0 && t <= 0xd3) c = (uint64_t)1 << (t - 0xd0);
        else if (t >= 0xd4 && t <= 0xd8) c = 1 + ((uint64_t)1 << (t - 0xd4));
        else if (t >= 0xd9 && t <= 0xdb) { if (!be(1 << (t - 0xd9), c)) return false; }
        else { char m[80]; snprintf(m, sizeof m, "MessagePack: invalid byte 0x%02X at 0x%zX", t, o - 1); return fail(m); }
        if (!need((size_t)c)) return false;
        o += (size_t)c;
        return true;
    }
    // One value. With `key`, the value is read as a map key: *key receives its text and a dummy node is returned.
    // `f`: pruning state of this node (nullptr = build everything below).
    cJSON* value(int depth, std::string* key, const MpFilter* f = nullptr);
    cJSON* array(uint64_t c, int depth, std::string* key, const MpFilter* f) {
        if (!count(c)) return nullptr;
        if (key) {   // an array used as a map key: read it, keep nothing
            for (uint64_t i = 0; i < c; i++) if (!skip(depth + 1)) return nullptr;
            *key = "<array>";
            return cJSON_CreateNull();
        }
        const char* const* names = f && labeler ? labeler(keys, c) : nullptr;
        cJSON* a = names ? cJSON_CreateObject() : cJSON_CreateArray();
        if (!a) { fail("out of memory"); return nullptr; }
        for (uint64_t i = 0; i < c; i++) {
            MpFilter cf;
            if (f) cf = names ? mpChildKey(*paths, *f, names[i]) : mpChildIndex(*paths, *f, i);
            if (f && cf.dead()) {
                if (!skip(depth + 1)) { cJSON_Delete(a); return nullptr; }
                if (!names) cJSON_AddItemToArray(a, cJSON_CreateNull());
                continue;
            }
            if (f) keys.push_back(names ? std::string(names[i]) : std::to_string(i));
            cJSON* v = value(depth + 1, nullptr, f && !cf.full ? &cf : nullptr);
            if (f) keys.pop_back();
            if (!v) { cJSON_Delete(a); return nullptr; }
            if (names) cJSON_AddItemToObjectCS(a, names[i], v);
            else cJSON_AddItemToArray(a, v);
        }
        return a;
    }
    cJSON* map(uint64_t c, int depth, std::string* key, const MpFilter* f) {
        if (!count(c)) return nullptr;
        if (key) {   // a map used as a map key: read it, keep nothing
            for (uint64_t i = 0; i < 2 * c; i++) if (!skip(depth + 1)) return nullptr;
            *key = "<map>";
            return cJSON_CreateNull();
        }
        cJSON* m = cJSON_CreateObject();
        if (!m) { fail("out of memory"); return nullptr; }
        std::unordered_map<std::string, cJSON*> seen;
        for (uint64_t i = 0; i < c; i++) {
            std::string k;
            cJSON* kd = value(depth + 1, &k);
            if (!kd) { cJSON_Delete(m); return nullptr; }
            cJSON_Delete(kd);
            MpFilter cf;
            if (f) {
                cf = mpChildKey(*paths, *f, k);
                if (cf.dead()) { if (!skip(depth + 1)) { cJSON_Delete(m); return nullptr; } continue; }
                keys.push_back(k);
            }
            cJSON* v = value(depth + 1, nullptr, f && !cf.full ? &cf : nullptr);
            if (f) keys.pop_back();
            if (!v) { cJSON_Delete(m); return nullptr; }
            // repeated key: first place, last value (JavaScript object semantics). Pruned mode (live module): looked up
            // in the object itself, no hash map (no allocation that could abort when the heap is full).
            cJSON* prev = nullptr;
            if (paths) prev = cJSON_GetObjectItemCaseSensitive(m, k.c_str());
            else { auto it = seen.find(k); if (it != seen.end()) prev = it->second; }
            if (prev) {
                moveKey(prev, v);
                cJSON_ReplaceItemViaPointer(m, prev, v);
                if (!paths) seen[k] = v;
            } else {
                if (!cJSON_AddItemToObject(m, k.c_str(), v)) { cJSON_Delete(v); cJSON_Delete(m); fail("out of memory"); return nullptr; }
                if (!paths) seen[k] = v;
            }
        }
        if (!paths) jsKeyOrder(m);   // only the full tree is compared with the Save Lab's
        return m;
    }
};

cJSON* MpReader::value(int depth, std::string* key, const MpFilter* f) {
    if (depth > MP_MAX_DEPTH) { fail("MessagePack: nested too deep"); return nullptr; }
    if (!need(1)) return nullptr;
    uint8_t t = b[o++];
    uint64_t c;
    // scalar: node, and its text when it is a map key
    auto ret = [&](cJSON* node, const std::string& asKey) -> cJSON* {
        if (!node) { fail("out of memory"); return nullptr; }
        if (key) *key = asKey;
        return node;
    };
    auto str = [&](uint64_t k) -> cJSON* {
        if (!need((size_t)k)) return nullptr;
        std::string s = cutStr(b + o, (size_t)k);
        o += (size_t)k;
        return ret(cJSON_CreateString(s.c_str()), s);
    };
    if (t <= 0x7f) return ret(cJSON_CreateNumber(t), key ? std::to_string(t) : std::string());
    if (t >= 0xe0) return ret(cJSON_CreateNumber((int)t - 256), key ? std::to_string((int)t - 256) : std::string());
    if (t >= 0x80 && t <= 0x8f) return map(t & 15, depth, key, f);
    if (t >= 0x90 && t <= 0x9f) return array(t & 15, depth, key, f);
    if (t >= 0xa0 && t <= 0xbf) return str(t & 31);
    switch (t) {
        case 0xc0: return ret(cJSON_CreateNull(), "null");
        case 0xc2: return ret(cJSON_CreateFalse(), "false");
        case 0xc3: return ret(cJSON_CreateTrue(), "true");
        case 0xc4: case 0xc5: case 0xc6: {
            if (!be(1 << (t - 0xc4), c) || !need((size_t)c)) return nullptr;
            std::string h = hexUpper(b + o, (size_t)c);
            o += (size_t)c;
            return ret(cJSON_CreateString(h.c_str()), "<bin>");
        }
        case 0xc7: case 0xc8: case 0xc9: {
            uint64_t ty;
            if (!be(1 << (t - 0xc7), c) || !be(1, ty)) return nullptr;
            cJSON* e = ext((int8_t)(uint8_t)ty, (size_t)c);
            return e ? ret(e, "<ext>") : nullptr;
        }
        case 0xca: {
            uint64_t v;
            if (!be(4, v)) return nullptr;
            uint32_t u = (uint32_t)v;
            float fl;
            memcpy(&fl, &u, 4);
            double d = fl;
            return ret(std::isfinite(d) ? cJSON_CreateNumber(Reader::fixed6(d)) : cJSON_CreateNull(), "<float>");
        }
        case 0xcb: {
            uint64_t v;
            if (!be(8, v)) return nullptr;
            double d;
            memcpy(&d, &v, 8);
            return ret(num(d), "<float>");
        }
        case 0xcc: case 0xcd: case 0xce: case 0xcf: {
            if (!be(1 << (t - 0xcc), c)) return nullptr;
            return ret(cJSON_CreateNumber((double)c), key ? std::to_string(c) : std::string());
        }
        case 0xd0: case 0xd1: case 0xd2: case 0xd3: {
            int k = 1 << (t - 0xd0);
            if (!be(k, c)) return nullptr;
            int64_t v = k == 8 ? (int64_t)c : (int64_t)(c << (64 - 8 * k)) >> (64 - 8 * k);
            return ret(cJSON_CreateNumber((double)v), key ? std::to_string(v) : std::string());
        }
        case 0xd4: case 0xd5: case 0xd6: case 0xd7: case 0xd8: {
            uint64_t ty;
            if (!be(1, ty)) return nullptr;
            cJSON* e = ext((int8_t)(uint8_t)ty, (size_t)1 << (t - 0xd4));
            return e ? ret(e, "<ext>") : nullptr;
        }
        case 0xd9: case 0xda: case 0xdb:
            if (!be(1 << (t - 0xd9), c)) return nullptr;
            return str(c);
        case 0xdc: case 0xdd:
            if (!be(t == 0xdc ? 2 : 4, c)) return nullptr;
            return array(c, depth, key, f);
        case 0xde: case 0xdf:
            if (!be(t == 0xde ? 2 : 4, c)) return nullptr;
            return map(c, depth, key, f);
    }
    char m[80];
    snprintf(m, sizeof m, "MessagePack: invalid byte 0x%02X at 0x%zX", t, o - 1);
    fail(m);
    return nullptr;
}

// ---- THEATRHYTHM FINAL BAR LINE
const char* const TH_PLAYSTYLE[] = {"Highscore", "UpdateDateUniversal", "BestPlayRank", "MaxChainCount", "BestClearMark", "CriticalBarZones",
                                    "TotalPlayCount", "TotalClearCount", "TotalFullChainClearCount", "MaxDamage"};
const char* const TH_MYPROFICA[] = {"MyGuid", "ProficaIllustID", "ProficaBaseDesignID", "ProficaBaseColorType", "SummonStoneIndex", "LatestPlayedMusics", "EditedFlag"};
const char* const TH_RIVAL[] = {"CursorSelectedFlag", "LockedFlag", "ObtainUniversalDateTime", "LastPlayedMusic", "_4", "ProficaIllustID", "ProficaBaseDesignID",
                                "ProficaBaseColorType", "MyMoogleID", "SummonStone", "AccountName", "InternalAccountID", "TotalRhythpo", "TotalPlayTime",
                                "TotalAchievement", "GameCompletion", "PlayMusicRanking", "PartySet", "CharacterUseCount", "CollecaCategory",
                                "SeriesCompleteCount", "BestReachedEndlessStage", "ClearMusicNumber", "BestDamageRanking", "TotalMusicPlayCount",
                                "TotalWinCount", "CurrentRate", "LatestPlayedMusics"};

// [Key(n)] array -> object with the game's field names, in place (only when the length matches).
void thLabelIn(cJSON* parent, cJSON* item, const char* const* names, int n) {
    if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) != n) return;
    cJSON* o = cJSON_CreateObject();
    for (int i = 0; i < n; i++) cJSON_AddItemToObject(o, names[i], cJSON_DetachItemFromArray(item, 0));
    moveKey(item, o);
    cJSON_ReplaceItemViaPointer(parent, item, o);
}
void theatrhythmLabels(cJSON* t) {
    cJSON* mi = cJSON_GetObjectItemCaseSensitive(t, "MusicInfo");
    cJSON* mr = cJSON_IsObject(mi) ? cJSON_GetObjectItemCaseSensitive(mi, "MusicRecordDictionary") : nullptr;
    if (cJSON_IsObject(mr)) for (cJSON* m = mr->child; m; m = m->next) {
        cJSON* dr = cJSON_IsObject(m) ? cJSON_GetObjectItemCaseSensitive(m, "DifficultyRecordDictionary") : nullptr;
        if (!cJSON_IsObject(dr)) continue;
        for (cJSON* d = dr->child; d; d = d->next) {
            cJSON* ps = cJSON_IsObject(d) ? cJSON_GetObjectItemCaseSensitive(d, "PlayStyleRecordDictionary") : nullptr;
            if (!cJSON_IsObject(ps)) continue;
            for (cJSON* s = ps->child; s;) { cJSON* next = s->next; thLabelIn(ps, s, TH_PLAYSTYLE, 10); s = next; }
        }
    }
    cJSON* pi = cJSON_GetObjectItemCaseSensitive(t, "ProficaInfo");
    if (cJSON_IsObject(pi)) {
        cJSON* my = cJSON_GetObjectItemCaseSensitive(pi, "MyProfica");
        if (my) thLabelIn(pi, my, TH_MYPROFICA, 7);
        cJSON* rl = cJSON_GetObjectItemCaseSensitive(pi, "RivalProficaList");
        if (cJSON_IsArray(rl)) for (cJSON* r = rl->child; r;) { cJSON* next = r->next; thLabelIn(rl, r, TH_RIVAL, 28); r = next; }
    }
}

// Ciphertext body (after the PS4 length prefix, if any), or nullptr.
const uint8_t* theatrhythmBody(const uint8_t* d, size_t n, size_t& bodyLen, bool& ps4) {
    ps4 = false;
    if (n >= 52 && (n - 4) % 16 == 0 && ((uint32_t)d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24) == n - 4) {
        ps4 = true;
        bodyLen = n - 4;
        return d + 4;
    }
    if (n >= 48 && n % 16 == 0) { bodyLen = n; return d; }
    return nullptr;
}

// Field names of the game's [Key(n)] arrays, by position in the tree (used while reading, pruned mode).
const char* const* theatrhythmLabeler(const std::vector<std::string>& k, uint64_t len) {
    if (len == 10 && k.size() == 7 && k[0] == "MusicInfo" && k[1] == "MusicRecordDictionary" && k[3] == "DifficultyRecordDictionary" &&
        k[5] == "PlayStyleRecordDictionary") return TH_PLAYSTYLE;
    if (len == 7 && k.size() == 2 && k[0] == "ProficaInfo" && k[1] == "MyProfica") return TH_MYPROFICA;
    if (len == 28 && k.size() == 3 && k[0] == "ProficaInfo" && k[1] == "RivalProficaList") return TH_RIVAL;
    return nullptr;
}

bool splitPaths(const std::vector<std::string>* in, MpPaths& out) {
    if (!in) return false;
    for (const std::string& p : *in) {
        std::vector<std::string> a;
        if (!treeSplitPath(p, a) || a.empty()) return false;   // unusable path: build everything (safe)
        out.push_back(a);
    }
    return true;
}

}  // namespace

bool inflateRaw(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t maxOut, std::string* error) {
    out.clear();
    Inflater f(in, inSize, maxOut, inSize * 4);
    bool ok = f.run();
    if (!ok && error) *error = f.err;
    if (ok) out.assign(f.ob, f.ob + f.on);
    return ok;
}

cJSON* msgpackToJson(const uint8_t* data, size_t size, std::string& error, size_t* used, const std::vector<std::string>* keepPaths) {
    MpReader r(data, size);
    MpPaths paths;
    MpFilter root;
    bool pruned = splitPaths(keepPaths, paths);
    if (pruned && paths.size() <= MP_MAX_PATHS) { r.paths = &paths; r.keys.reserve(MP_MAX_DEPTH + 2); for (size_t p = 0; p < paths.size(); p++) root.at[root.count++] = {(uint16_t)p, 0}; }
    else pruned = false;
    cJSON* t = r.value(0, nullptr, pruned ? &root : nullptr);
    if (!t) { error = r.err.empty() ? "MessagePack: invalid data" : r.err; return nullptr; }
    if (used) *used = r.o;
    return t;
}

bool isTheatrhythm(const uint8_t* data, size_t size) {
    size_t bl;
    bool ps4;
    return theatrhythmBody(data, size, bl, ps4) != nullptr;
}

cJSON* theatrhythmToJson(const uint8_t* data, size_t size, const std::string& password, std::string& error, SaveFormatInfo* info,
                         const std::vector<std::string>* keepPaths, size_t memoryBudget) {
    size_t bl = 0;
    bool ps4 = false;
    const uint8_t* body = theatrhythmBody(data, size, bl, ps4);
    if (!body) { error = "size " + std::to_string(size) + " is not a 32-byte header + AES blocks"; return nullptr; }
    if (password.empty()) { error = "no password"; return nullptr; }
    uint8_t salt[16], iv[16], key[16];
    memcpy(salt, body, 8); memcpy(salt + 8, body + 24, 8);    // q0 || q3
    memcpy(iv, body + 16, 8); memcpy(iv + 8, body + 8, 8);     // q2 || q1
    pbkdf2Sha1(password, salt, 16, 1000, key, 16);
    const uint8_t* ct = body + 32;
    size_t clen = bl - 32;
    uint8_t tail[16];
    aes128CbcDecrypt(key, ct + clen - 32, ct + clen - 16, 16, tail);   // padding check on the last block first
    int pad = tail[15];
    bool padOk = pad >= 1 && pad <= 16;
    for (int i = 16 - pad; padOk && i < 16; i++) if (tail[i] != pad) padOk = false;
    if (!padOk) { error = "wrong password or not a THEATRHYTHM save (bad padding)"; return nullptr; }
    uint8_t* pt = (uint8_t*)malloc(clen);
    if (!pt) { error = "out of memory"; return nullptr; }
    aes128CbcDecrypt(key, iv, ct, clen, pt);
    size_t maxOut = 64u << 20;
    if (memoryBudget) maxOut = std::min(maxOut, memoryBudget > clen ? (memoryBudget - clen) / 4 : 0);   // room left for the tree (x3)
    Inflater inf(pt, clen - (size_t)pad, maxOut, std::min(maxOut, clen * 80));   // ~x75 on real saves: one allocation
    bool inflated = inf.run();
    free(pt);
    if (!inflated) {
        error = inf.err == "DEFLATE: output too large" && memoryBudget ? "not enough memory (" + std::to_string(memoryBudget >> 10) + " KB free)"
                                                                          : "wrong password or damaged file (" + inf.err + ")";
        return nullptr;
    }
    if (memoryBudget && inf.on * 4 + 65536 > memoryBudget) {   // decoded MessagePack + its pruned tree (measured x2.4 + malloc overhead)
        error = "not enough memory (needs ~" + std::to_string((inf.on * 4 + 65536) >> 10) + " KB, " + std::to_string(memoryBudget >> 10) + " KB free)";
        return nullptr;
    }
    MpReader r(inf.ob, inf.on);
    MpPaths paths;
    MpFilter root;
    if (splitPaths(keepPaths, paths) && paths.size() <= MP_MAX_PATHS) {   // else: whole tree (always right, just bigger)
        r.paths = &paths;
        r.labeler = theatrhythmLabeler;
        r.keys.reserve(MP_MAX_DEPTH + 2);
        for (size_t p = 0; p < paths.size(); p++) root.at[root.count++] = {(uint16_t)p, 0};
    }
    cJSON* t = r.value(0, nullptr, r.paths ? &root : nullptr);
    if (!t) { error = r.err.empty() ? "MessagePack: invalid data" : r.err; return nullptr; }
    if (!cJSON_IsObject(t)) { cJSON_Delete(t); error = "MessagePack root is not a map"; return nullptr; }
    theatrhythmLabels(t);
    if (info) {
        cJSON* v = cJSON_GetObjectItemCaseSensitive(t, "Version");
        info->summary = std::string("THEATRHYTHM save (") + (ps4 ? "PS4" : "Switch") + ", MessagePack " + std::to_string(inf.on / 1024) +
                        " KB" + (cJSON_IsString(v) ? std::string(", v") + v->valuestring : std::string()) + (r.paths ? ", rule values only" : "") + ")";
    }
    return t;
}

bool TheatrhythmDoc::parse(std::vector<uint8_t>&& data) {
    cJSON* t = theatrhythmToJson(data.data(), data.size(), password, error, &info, keepPaths.empty() ? nullptr : &keepPaths, memoryBudget);
    std::vector<uint8_t>().swap(data);
    if (!t) return false;
    adopt(t, false);
    return true;
}

// The password is a string literal of the game's global-metadata.dat (IL2CPP, version 24+ header: magic
// 0xFAB11BAF, then stringLiteralOffset / stringLiteralSize / stringLiteralDataOffset). Its index changes with
// game updates, so every literal is tried, nearest to the known indexes first (instant on known versions).
bool theatrhythmFindPasswordIn(const ByteReader& rd, uint64_t metaSize, const uint8_t* save, size_t saveSize, std::string& password, int* index) {
    if (metaSize < 32) return false;
    uint8_t hdr[32];
    if (!rd(0, hdr, 32)) return false;
    auto h32 = [&](size_t o) { uint32_t v; memcpy(&v, hdr + o, 4); return v; };
    if (h32(0) != 0xFAB11BAFu) return false;
    uint32_t lo = h32(8), lsz = h32(12), dO = h32(16), dsz = h32(20);
    if ((uint64_t)lo + lsz > metaSize || (uint64_t)dO + dsz > metaSize) return false;
    if (lsz > (64u << 20)) return false;   // 8 bytes per literal: millions of literals is not a metadata file
    size_t bl = 0;
    bool ps4 = false;
    const uint8_t* body = theatrhythmBody(save, saveSize, bl, ps4);
    if (!body) return false;
    std::vector<uint8_t> table(lsz);
    if (lsz && !rd(lo, table.data(), lsz)) return false;
    auto t32 = [&](size_t o) { uint32_t v; memcpy(&v, table.data() + o, 4); return v; };
    uint8_t salt[16], key[16], tail[16];
    memcpy(salt, body, 8);
    memcpy(salt + 8, body + 24, 8);
    const uint8_t* ct = body + 32;
    size_t clen = bl - 32;
    int n = (int)(lsz / 8);
    const int hints[] = {12425, 13610};   // Switch build of 2023+ / version 1.00 (rcostanza's converter)
    std::vector<std::pair<int, int>> order;   // (distance to the nearest hint, index)
    order.reserve((size_t)n);
    for (int i = 0; i < n; i++) {
        int d = 1 << 30;
        for (int h : hints) d = std::min(d, std::abs(i - h));
        order.push_back({d, i});
    }
    std::sort(order.begin(), order.end());
    std::set<std::string> tried;
    char sbuf[128];
    for (auto& o : order) {
        uint32_t len = t32(8u * (uint32_t)o.second), di = t32(8u * (uint32_t)o.second + 4);
        if (len < 4 || len > 128 || (uint64_t)di + len > dsz) continue;
        if (!rd((uint64_t)dO + di, sbuf, len)) continue;
        std::string pw(sbuf, len);
        if (!tried.insert(pw).second) continue;
        pbkdf2Sha1(pw, salt, 16, 1000, key, 16);
        aes128CbcDecrypt(key, ct + clen - 32, ct + clen - 16, 16, tail);
        int pad = tail[15];
        bool ok = pad >= 1 && pad <= 16;
        for (int i = 16 - pad; ok && i < 16; i++) if (tail[i] != pad) ok = false;
        if (!ok) continue;
        std::string err;
        cJSON* t = theatrhythmToJson(save, saveSize, pw, err);   // confirm: the whole file decodes
        if (!t) continue;
        cJSON_Delete(t);
        password = pw;
        if (index) *index = o.second;
        return true;
    }
    return false;
}

static ByteReader memoryReader(const uint8_t* d, size_t n) {
    return [d, n](uint64_t off, void* buf, size_t len) {
        if (off > n || len > n - off) return false;
        memcpy(buf, d + off, len);
        return true;
    };
}

bool theatrhythmFindPassword(const uint8_t* meta, size_t metaSize, const uint8_t* save, size_t saveSize, std::string& password, int* index) {
    return theatrhythmFindPasswordIn(memoryReader(meta, metaSize), metaSize, save, saveSize, password, index);
}

// ================================================================== Easy Save 3 (Unity asset), encrypted
// Mirror of parseEs3 / es3FindPassword in tools/savelab/formats.js. File = 16-byte IV + AES-128-CBC (PKCS#7) with
// key = PBKDF2-HMAC-SHA1(password, IV, 100, 16); the plaintext is ES3's JSON, optionally gzip-compressed.
// The password is the game's own ES3 setting (Resources/ES3/ES3Defaults in resources.assets): never built in.
namespace {

bool es3Decrypt(const uint8_t* d, size_t n, const std::string& pw, std::vector<uint8_t>& out, std::string& error) {
    if (n < 32 || (n - 16) % 16) { error = "size " + std::to_string(n) + " is not a 16-byte IV + AES blocks"; return false; }
    uint8_t key[16], tail[16];
    pbkdf2Sha1(pw, d, 16, 100, key, 16);
    const uint8_t* ct = d + 16;
    size_t clen = n - 16;
    aes128CbcDecrypt(key, clen >= 32 ? ct + clen - 32 : d, ct + clen - 16, 16, tail);   // padding check first
    int pad = tail[15];
    bool ok = pad >= 1 && pad <= 16;
    for (int i = 16 - pad; ok && i < 16; i++) if (tail[i] != pad) ok = false;
    if (!ok) { error = "wrong password or not an Easy Save 3 file (bad padding)"; return false; }
    out.resize(clen);
    aes128CbcDecrypt(key, d, ct, clen, out.data());
    out.resize(clen - (size_t)pad);
    if (out.size() >= 18 && out[0] == 0x1F && out[1] == 0x8B && out[2] == 8) {   // gzip (ES3 compression)
        uint8_t fl = out[3];
        size_t p = 10;
        if (fl & 4) { if (p + 2 > out.size()) { error = "bad gzip header"; return false; } p += 2 + (out[p] | out[p + 1] << 8); }
        if (fl & 8) { while (p < out.size() && out[p]) p++; p++; }
        if (fl & 16) { while (p < out.size() && out[p]) p++; p++; }
        if (fl & 2) p += 2;
        if (p >= out.size()) { error = "bad gzip header"; return false; }
        std::vector<uint8_t> raw;
        if (!inflateRaw(out.data() + p, out.size() - p, raw, 64u << 20, &error)) return false;
        out.swap(raw);
    }
    return true;
}

bool es3LooksLikeJson(const std::vector<uint8_t>& t) {
    size_t s = 0, bad = 0;
    while (s < t.size() && (t[s] == ' ' || t[s] == '\t' || t[s] == '\r' || t[s] == '\n')) s++;
    if (s >= t.size() || (t[s] != '{' && t[s] != '[')) return false;
    for (uint8_t c : t) if (c < 32 && c != 9 && c != 10 && c != 13) bad++;
    return bad == 0;
}

}  // namespace

cJSON* es3ToJson(const uint8_t* data, size_t size, const std::string& password, std::string& error, SaveFormatInfo* info) {
    std::vector<uint8_t> pt;
    if (!es3Decrypt(data, size, password, pt, error)) return nullptr;
    if (!es3LooksLikeJson(pt)) { error = "decrypted, but not JSON (wrong password?)"; return nullptr; }
    size_t e = pt.size();
    while (e && (pt[e - 1] == 0 || pt[e - 1] == ' ' || pt[e - 1] == '\n' || pt[e - 1] == '\r' || pt[e - 1] == '\t')) e--;
    cJSON* t = cJSON_ParseWithLength((const char*)pt.data(), e);
    if (!t) { error = "decrypted, but JSON syntax error"; return nullptr; }
    if (info) info->summary = "Easy Save 3 (AES), " + std::to_string(pt.size()) + " bytes of JSON";
    return t;
}

bool Es3Doc::parse(std::vector<uint8_t>&& data) {
    cJSON* t = es3ToJson(data.data(), data.size(), password, error, &info);
    std::vector<uint8_t>().swap(data);
    if (!t) return false;
    adopt(t, true);   // JSON: "true" / "12" as text read like the json reader
    return true;
}

// The password is a field of the ES3Defaults MonoBehaviour (Unity strings: u32 length + bytes, 4-aligned) a few
// fields after its name; every length-prefixed string just after each "ES3Defaults" is tried on the save.
bool es3FindPasswordIn(const ByteReader& rd, uint64_t assetSize, const uint8_t* save, size_t saveSize, std::string& password) {
    static const char tag[] = "ES3Defaults";
    const size_t tl = sizeof(tag) - 1;
    const size_t chunk = 256 * 1024, reach = tl + 1024 + 4 + 128;   // a tag near the end of a piece needs this much after it
    std::vector<uint8_t> buf(chunk + reach);
    std::set<std::string> tried;
    for (uint64_t pos = 0; pos < assetSize; pos += chunk) {
        size_t n = (size_t)std::min<uint64_t>(assetSize - pos, chunk + reach);
        if (!rd(pos, buf.data(), n)) return false;
        const uint8_t* a = buf.data();
        size_t starts = std::min(n, chunk);   // tags starting in this piece (the rest is the next piece's)
        for (size_t i = 0; i < starts && i + tl <= n; i++) {
            if (a[i] != 'E' || memcmp(a + i, tag, tl) != 0) continue;
            size_t end = (size_t)std::min<uint64_t>(assetSize - pos, i + tl + 1024);
            for (size_t o = i + tl; o + 4 < end; o++) {
                uint32_t len;
                memcpy(&len, a + o, 4);
                if (len < 1 || len > 128 || o + 4 + len > n) continue;
                bool text = true;
                for (uint32_t k = 0; text && k < len; k++) { uint8_t c = a[o + 4 + k]; text = c >= 32 && c < 127; }
                if (!text) continue;
                std::string pw((const char*)a + o + 4, len);
                if (!tried.insert(pw).second) continue;
                std::vector<uint8_t> pt;
                std::string err;
                if (!es3Decrypt(save, saveSize, pw, pt, err) || !es3LooksLikeJson(pt)) continue;
                password = pw;
                return true;
            }
        }
    }
    return false;
}

bool es3FindPassword(const uint8_t* asset, size_t assetSize, const uint8_t* save, size_t saveSize, std::string& password) {
    return es3FindPasswordIn(memoryReader(asset, assetSize), assetSize, save, saveSize, password);
}

// ================================================================== RPG Maker MV / MZ (fileN.rpgsave / fileN.rmmzsave)
// Mirror of parseRpgMaker in tools/savelab/formats.js. MV: LZString.compressToBase64(JsonEx.stringify(contents)).
// MZ: zlib (pako level 1) of the same JSON, the binary string often written back as UTF-8. Plain JSON is accepted too.
// JsonEx markers are removed: {"@a": [...]} -> the list, "@" (class) and "@c" (object id) keys dropped,
// {"@r": n} (circular reference) -> null. Tree: the save contents (system, switches, variables, actors, party...)
// plus "_header": {encoding: "lzstring"|"zlib"|"json"} (MV writes LZString, MZ and some MV ports zlib).
namespace {

int lzB64(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return 64;
    return -1;
}

// LZString.decompressFromBase64 -> UTF-16 code units. Dictionary entries are slices of the output (or one literal).
bool lzStringDecompressB64(const char* in, size_t len, std::u16string& out, size_t maxOut) {
    out.clear();
    if (!len) return false;
    struct Ent { uint32_t off, len; char16_t ch; bool lit; };
    std::vector<Ent> dict;
    dict.reserve(4096);
    for (int i = 0; i < 3; i++) dict.push_back({0, 0, (char16_t)i, true});
    uint32_t val = (uint32_t)std::max(0, lzB64(in[0])), position = 32;
    size_t index = 1;
    auto readBits = [&](int n) -> uint32_t {
        uint32_t bits = 0;
        for (int i = 0; i < n; i++) {
            uint32_t resb = val & position;
            position >>= 1;
            if (!position) {
                position = 32;
                val = index < len ? (uint32_t)std::max(0, lzB64(in[index])) : 0;
                index++;
            }
            bits |= (resb ? 1u : 0u) << i;
        }
        return bits;
    };
    uint32_t enlargeIn = 4, numBits = 3;
    uint32_t first = readBits(2);
    char16_t c0;
    if (first == 0) c0 = (char16_t)readBits(8);
    else if (first == 1) c0 = (char16_t)readBits(16);
    else return false;
    dict.push_back({0, 1, c0, false});
    out.push_back(c0);
    uint32_t wOff = 0, wLen = 1;
    for (;;) {
        if (index > len) return false;
        uint32_t c = readBits((int)numBits);
        if (c == 0 || c == 1) {
            dict.push_back({0, 0, (char16_t)readBits(c == 0 ? 8 : 16), true});
            c = (uint32_t)dict.size() - 1;
            enlargeIn--;
        } else if (c == 2) return true;
        if (!enlargeIn) { enlargeIn = 1u << numBits; numBits++; if (numBits > 31) return false; }
        uint32_t eOff = (uint32_t)out.size(), eLen;
        if (c < dict.size()) {
            const Ent e = dict[c];
            if (e.lit) { out.push_back(e.ch); eLen = 1; }
            else { for (uint32_t k = 0; k < e.len; k++) out.push_back(out[e.off + k]); eLen = e.len; }
        } else if (c == dict.size()) {
            for (uint32_t k = 0; k < wLen; k++) out.push_back(out[wOff + k]);
            out.push_back(out[wOff]);
            eLen = wLen + 1;
        } else return false;
        if (out.size() > maxOut) return false;
        // w + entry[0]: w is just before the entry in the output, so it is the slice wOff .. wLen + 1
        dict.push_back({wOff, wLen + 1, 0, false});
        enlargeIn--;
        wOff = eOff;
        wLen = eLen;
        if (!enlargeIn) { enlargeIn = 1u << numBits; numBits++; if (numBits > 31) return false; }
    }
}

void utf16ToUtf8(const std::u16string& s, std::string& o) {
    o.clear();
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | cp >> 6); o += (char)(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | cp >> 12); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
        else { o += (char)(0xF0 | cp >> 18); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
    }
}

// The MZ zip string written as UTF-8: every code point <= 0xFF becomes one byte again. false if it is not that.
bool utf8Latin1(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(n);
    bool multi = false;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = d[i];
        if (c < 0x80) out.push_back(c);
        else if ((c == 0xC2 || c == 0xC3) && i + 1 < n && (d[i + 1] & 0xC0) == 0x80) { out.push_back((uint8_t)((c & 3) << 6 | (d[i + 1] & 63))); i++; multi = true; }
        else return false;
    }
    return multi;
}

bool zlibInflate(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
    if (n < 6 || (d[0] & 0x0F) != 8 || ((d[0] << 8) | d[1]) % 31 != 0 || (d[1] & 0x20)) return false;
    return inflateRaw(d + 2, n - 2, out, 64u << 20);
}

const char* const RM_KEYS[] = {"system", "screen", "timer", "switches", "variables", "selfSwitches", "actors", "party", "map", "player"};

cJSON* jsonExClean(cJSON* n, int depth) {
    if (depth > 256) { cJSON_Delete(n); return cJSON_CreateNull(); }
    if (cJSON_IsObject(n)) {
        if (cJSON_GetObjectItemCaseSensitive(n, "@r")) { cJSON_Delete(n); return cJSON_CreateNull(); }
        cJSON* a = cJSON_GetObjectItemCaseSensitive(n, "@a");
        if (a) {
            cJSON* arr = cJSON_DetachItemViaPointer(n, a);
            cJSON_Delete(n);
            return jsonExClean(arr, depth + 1);
        }
        cJSON_DeleteItemFromObjectCaseSensitive(n, "@");
        cJSON_DeleteItemFromObjectCaseSensitive(n, "@c");
    }
    if (!cJSON_IsObject(n) && !cJSON_IsArray(n)) return n;
    bool obj = cJSON_IsObject(n);
    std::vector<cJSON*> kids;
    while (n->child) kids.push_back(cJSON_DetachItemViaPointer(n, n->child));
    for (cJSON* k : kids) {
        std::string name = k->string ? k->string : "";
        cJSON* r = (cJSON_IsObject(k) || cJSON_IsArray(k)) ? jsonExClean(k, depth + 1) : k;
        if (obj) cJSON_AddItemToObject(n, name.c_str(), r);
        else {
            if (r->string && !(r->type & cJSON_StringIsConst)) cJSON_free(r->string);
            r->string = nullptr;
            cJSON_AddItemToArray(n, r);
        }
    }
    return n;
}

}  // namespace

static bool rpgMakerDecode(const uint8_t* data, size_t size, std::string& json, std::string& encoding) {
    size_t s = 0, e = size;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) s = 3;
    while (s < e && (data[s] == ' ' || data[s] == '\r' || data[s] == '\n' || data[s] == '\t')) s++;
    while (e > s && (data[e - 1] == 0 || data[e - 1] == ' ' || data[e - 1] == '\r' || data[e - 1] == '\n' || data[e - 1] == '\t')) e--;
    if (e - s < 4) return false;
    if (data[s] == '{' || data[s] == '[') { json.assign((const char*)data + s, e - s); encoding = "json"; return true; }
    std::vector<uint8_t> out;
    if (zlibInflate(data, size, out)) { json.assign(out.begin(), out.end()); encoding = "zlib"; return true; }
    std::vector<uint8_t> bytes;
    if (data[0] == 0x78 && utf8Latin1(data, size, bytes) && zlibInflate(bytes.data(), bytes.size(), out)) {
        json.assign(out.begin(), out.end()); encoding = "zlib"; return true;
    }
    for (size_t i = s; i < e; i++) if (lzB64((char)data[i]) < 0) return false;
    if (e - s < 8) return false;
    std::u16string u;
    if (!lzStringDecompressB64((const char*)data + s, e - s, u, 64u << 20)) return false;
    utf16ToUtf8(u, json);
    encoding = "lzstring";
    return true;
}

bool isRpgMaker(const uint8_t* data, size_t size) {
    std::string json, enc;
    if (!rpgMakerDecode(data, size, json, enc)) return false;
    size_t i = 0;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\r' || json[i] == '\n' || json[i] == '\t')) i++;
    if (i >= json.size() || (json[i] != '{' && json[i] != '[')) return false;
    if (enc != "json") return true;   // LZString / zlib text that decodes to JSON: RPG Maker
    // plain JSON: only with RPG Maker's save keys or JsonEx markers
    if (json.find("\"@c\"") != std::string::npos || json.find("\"@a\"") != std::string::npos) return true;
    int n = 0;
    for (const char* k : RM_KEYS) if (json.find(std::string("\"") + k + "\"") != std::string::npos) n++;
    return n >= 4;
}

cJSON* rpgMakerToJson(const uint8_t* data, size_t size, std::string& error, SaveFormatInfo* info) {
    std::string json, enc;
    if (!rpgMakerDecode(data, size, json, enc)) { error = "not an RPG Maker MV/MZ save (LZString base64, zlib or JSON)"; return nullptr; }
    cJSON* t = cJSON_ParseWithLength(json.c_str(), json.size());
    if (!t || (!cJSON_IsObject(t) && !cJSON_IsArray(t))) { if (t) cJSON_Delete(t); error = "decoded text is not JSON"; return nullptr; }
    t = jsonExClean(t, 0);
    if (cJSON_IsObject(t)) {
        cJSON* h = cJSON_CreateObject();
        cJSON_AddStringToObject(h, "encoding", enc.c_str());
        cJSON_AddItemToObject(t, "_header", h);
    }
    if (info) info->summary = "RPG Maker MV/MZ save (" + enc + ", " + std::to_string(json.size()) + " bytes of JSON)";
    return t;
}

bool RpgMakerDoc::parse(std::vector<uint8_t>&& data) {
    cJSON* t = rpgMakerToJson(data.data(), data.size(), error, &info);
    if (!t) return false;
    adopt(t, false);
    return true;
}

// ================================================================== GameMaker text saves (ini / lines + ds_list / ds_map hex)
// Mirror of parseGameMaker in tools/savelab/formats.js. Text cut at the first NUL, BOM and \r removed.
//   INI (every non-empty line is "[section]", "key=value" or a ;/# comment, at least one section), also when the whole
//   file is base64 of such a text: {"<section>": {"<key>": value}}, keys before any section under "".
//   Otherwise: {"lines": [value per line]} (a final empty line dropped).
// Values: one pair of surrounding quotes removed; numbers (-12, 3.5, 1e3) become numbers; ds_list_write hex -> list,
// ds_map_write hex -> object (number keys written as integers, e.g. "3"), nested hex strings decoded too (depth 8).
// Plus "_header": {format: "ini"|"lines", encoding: "text"|"base64"}.
namespace {

bool gmNumber(const std::string& s, double& v) {
    size_t i = 0, n = s.size();
    if (i < n && (s[i] == '-' || s[i] == '+')) i++;
    size_t d0 = i;
    while (i < n && s[i] >= '0' && s[i] <= '9') i++;
    if (i == d0) return false;
    if (i < n && s[i] == '.') {
        size_t f0 = ++i;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        if (i == f0) return false;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '-' || s[i] == '+')) i++;
        size_t e0 = i;
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
        if (i == e0) return false;
    }
    if (i != n) return false;
    v = strtod(s.c_str(), nullptr);
    return std::isfinite(v);
}

std::string gmKeyOf(double v) {
    char b[40];
    if (v == std::floor(v) && std::fabs(v) < 9007199254740992.0) snprintf(b, sizeof(b), "%.0f", v);
    else {
        for (int p = 1; p <= 17; p++) { snprintf(b, sizeof(b), "%.*g", p, v); if (strtod(b, nullptr) == v) break; }
    }
    if (!strcmp(b, "-0")) return "0";
    return b;
}

cJSON* gmValue(const std::string& raw, int depth);

// ds_list_write (u32 0x12E..0x130) / ds_map_write (u32 0x191..0x193) hex text -> list / object, nullptr if not one
cJSON* gmDsDecode(const std::string& s, int depth) {
    if (depth > 8 || s.size() < 16 || (s.size() & 1)) return nullptr;
    for (char c : s) if (hexDigit(c) < 0) return nullptr;
    std::vector<uint8_t> b(s.size() / 2);
    for (size_t i = 0; i < b.size(); i++) b[i] = (uint8_t)(hexDigit(s[2 * i]) * 16 + hexDigit(s[2 * i + 1]));
    size_t p = 0;
    auto u32 = [&](uint32_t& v) {
        if (b.size() - p < 4) return false;
        v = b[p] | b[p + 1] << 8 | b[p + 2] << 16 | (uint32_t)b[p + 3] << 24;
        p += 4;
        return true;
    };
    uint32_t head, count;
    if (!u32(head)) return nullptr;
    bool isMap = head >= 0x191 && head <= 0x193, isList = head >= 0x12E && head <= 0x130;
    if (!isMap && !isList) return nullptr;
    if (!u32(count) || count > (b.size() - 8) / (isMap ? 8 : 4)) return nullptr;
    // one value: kind 0 number, 1 string, 2 null
    auto read = [&](int& kind, double& num, std::string& str) {
        uint32_t type;
        if (!u32(type)) return false;
        kind = 0;
        if (type == 0 || type == 13) {
            if (b.size() - p < 8) return false;
            memcpy(&num, b.data() + p, 8);
            p += 8;
            if (!std::isfinite(num)) kind = 2;
        } else if (type == 1) {
            uint32_t len;
            if (!u32(len) || len > b.size() - p) return false;
            kind = 1;
            str.assign((const char*)b.data() + p, len);
            p += len;
            size_t z = str.find('\0');
            if (z != std::string::npos) str.resize(z);
        } else if (type == 5) kind = 2;
        else if (type == 7) { uint32_t x; if (!u32(x)) return false; num = (int32_t)x; }
        else if (type == 10) { uint32_t lo, hi; if (!u32(lo) || !u32(hi)) return false; num = (double)(int64_t)((uint64_t)hi << 32 | lo); }
        else return false;
        return true;
    };
    auto node = [&](int kind, double num, const std::string& str) -> cJSON* {
        if (kind == 2) return cJSON_CreateNull();
        if (kind == 0) return cJSON_CreateNumber(num);
        cJSON* inner = gmDsDecode(str, depth + 1);
        return inner ? inner : cJSON_CreateString(str.c_str());
    };
    cJSON* out = isMap ? cJSON_CreateObject() : cJSON_CreateArray();
    for (uint32_t i = 0; i < count; i++) {
        int k1, k2; double n1 = 0, n2 = 0; std::string s1, s2;
        if (!read(k1, n1, s1)) { cJSON_Delete(out); return nullptr; }
        if (!isMap) { cJSON_AddItemToArray(out, node(k1, n1, s1)); continue; }
        if (!read(k2, n2, s2)) { cJSON_Delete(out); return nullptr; }
        std::string key = k1 == 1 ? s1 : k1 == 2 ? "null" : gmKeyOf(n1);
        cJSON* v = node(k2, n2, s2);
        if (cJSON_GetObjectItemCaseSensitive(out, key.c_str())) cJSON_ReplaceItemInObjectCaseSensitive(out, key.c_str(), v);
        else cJSON_AddItemToObject(out, key.c_str(), v);
    }
    if (p != b.size()) { cJSON_Delete(out); return nullptr; }
    if (isMap) jsKeyOrder(out);
    return out;
}

std::string gmTrim(const std::string& s) {
    size_t a = 0, e = s.size();
    while (a < e && (s[a] == ' ' || s[a] == '\t')) a++;
    while (e > a && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
    return s.substr(a, e - a);
}

cJSON* gmValue(const std::string& raw, int depth) {
    std::string v = gmTrim(raw);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    double num;
    if (gmNumber(v, num)) return cJSON_CreateNumber(num);
    cJSON* ds = gmDsDecode(v, depth);
    if (ds) return ds;
    return cJSON_CreateString(v.c_str());
}

void gmLines(const uint8_t* data, size_t size, std::vector<std::string>& lines) {
    size_t s = size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF ? 3 : 0;
    size_t e = s;
    while (e < size && data[e]) e++;
    std::string t((const char*)data + s, e - s);
    lines.clear();
    size_t st = 0;
    for (;;) {
        size_t nl = t.find('\n', st);
        std::string ln = t.substr(st, nl == std::string::npos ? std::string::npos : nl - st);
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        lines.push_back(ln);
        if (nl == std::string::npos) break;
        st = nl + 1;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
}

bool gmIsSection(const std::string& l) {
    std::string t = gmTrim(l);
    return t.size() >= 3 && t.front() == '[' && t.back() == ']' && t.find(']') == t.size() - 1;
}

bool gmIsIni(const std::vector<std::string>& lines) {
    int sections = 0;
    for (const std::string& l : lines) {
        std::string t = gmTrim(l);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (gmIsSection(t)) { sections++; continue; }
        size_t eq = t.find('=');
        if (eq == std::string::npos || eq == 0) return false;
    }
    return sections > 0;
}

bool gmPrintable(const uint8_t* d, size_t n) {
    size_t s = n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF ? 3 : 0, e = s, bad = 0;
    while (e < n && d[e]) e++;
    for (size_t i = e; i < n; i++) if (d[i]) return false;   // only trailing NULs
    if (e == s) return false;
    for (size_t i = s; i < e; i++) { uint8_t c = d[i]; if (c < 32 && c != 9 && c != 10 && c != 13) bad++; }
    return bad == 0;
}

// whole text = base64 (line breaks allowed) of an INI text
bool gmBase64Ini(const std::vector<std::string>& lines, std::vector<uint8_t>& out) {
    std::string b;
    for (const std::string& l : lines) b += gmTrim(l);
    if (b.size() < 8 || b.size() % 4) return false;
    for (char c : b) if (lzB64(c) < 0) return false;
    out.clear();
    uint32_t acc = 0; int bits = 0;
    for (char c : b) {
        int v = lzB64(c);
        if (v == 64) break;
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)(acc >> bits)); }
    }
    if (!gmPrintable(out.data(), out.size())) return false;
    std::vector<std::string> inner;
    gmLines(out.data(), out.size(), inner);
    return gmIsIni(inner);
}

}  // namespace

bool isGameMaker(const uint8_t* data, size_t size) {
    if (!gmPrintable(data, size)) return false;
    std::vector<std::string> lines;
    gmLines(data, size, lines);
    if (lines.empty()) return false;
    std::string first = gmTrim(lines[0]);
    if (!first.empty() && (first[0] == '{' || first[0] == '[') && !gmIsSection(first)) return false;   // JSON
    if (gmIsIni(lines)) return true;
    std::vector<uint8_t> dec;
    if (gmBase64Ini(lines, dec)) return true;
    for (const std::string& l : lines) {
        std::string v = gmTrim(l);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        cJSON* d = gmDsDecode(v, 0);
        if (d) { cJSON_Delete(d); return true; }
    }
    return false;
}

cJSON* gameMakerToJson(const uint8_t* data, size_t size, std::string& error, SaveFormatInfo* info) {
    if (!gmPrintable(data, size)) { error = "not a GameMaker text save (binary data)"; return nullptr; }
    std::vector<std::string> lines;
    gmLines(data, size, lines);
    std::string encoding = "text";
    std::vector<uint8_t> dec;
    if (!gmIsIni(lines) && gmBase64Ini(lines, dec)) { gmLines(dec.data(), dec.size(), lines); encoding = "base64"; }
    cJSON* t = cJSON_CreateObject();
    bool ini = gmIsIni(lines);
    int keys = 0;
    if (ini) {
        cJSON* sec = nullptr;
        for (const std::string& l : lines) {
            std::string tl = gmTrim(l);
            if (tl.empty() || tl[0] == ';' || tl[0] == '#') continue;
            if (gmIsSection(tl)) {
                std::string name = tl.substr(1, tl.size() - 2);
                sec = cJSON_GetObjectItemCaseSensitive(t, name.c_str());
                if (!cJSON_IsObject(sec)) {
                    if (sec) cJSON_DeleteItemFromObjectCaseSensitive(t, name.c_str());
                    sec = cJSON_AddObjectToObject(t, name.c_str());
                }
                continue;
            }
            size_t eq = tl.find('=');
            if (!sec) { sec = cJSON_GetObjectItemCaseSensitive(t, ""); if (!sec) sec = cJSON_AddObjectToObject(t, ""); }
            std::string key = gmTrim(tl.substr(0, eq));
            cJSON* v = gmValue(tl.substr(eq + 1), 0);
            if (cJSON_GetObjectItemCaseSensitive(sec, key.c_str())) cJSON_ReplaceItemInObjectCaseSensitive(sec, key.c_str(), v);
            else cJSON_AddItemToObject(sec, key.c_str(), v);
            keys++;
        }
        jsKeyOrder(t);
        for (cJSON* s = t->child; s; s = s->next) jsKeyOrder(s);
    } else {
        cJSON* arr = cJSON_AddArrayToObject(t, "lines");
        for (const std::string& l : lines) cJSON_AddItemToArray(arr, gmValue(l, 0));
        keys = (int)lines.size();
    }
    cJSON* h = cJSON_CreateObject();
    cJSON_AddStringToObject(h, "format", ini ? "ini" : "lines");
    cJSON_AddStringToObject(h, "encoding", encoding.c_str());
    if (cJSON_GetObjectItemCaseSensitive(t, "_header")) cJSON_DeleteItemFromObjectCaseSensitive(t, "_header");
    cJSON_AddItemToObject(t, "_header", h);
    if (info) info->summary = std::string("GameMaker ") + (ini ? "INI" : "text") + (encoding == "base64" ? " (base64)" : "") + ", " +
                              std::to_string(keys) + (ini ? " keys" : " lines");
    return t;
}

bool GameMakerDoc::parse(std::vector<uint8_t>&& data) {
    cJSON* t = gameMakerToJson(data.data(), data.size(), error, &info);
    if (!t) return false;
    adopt(t, false);
    return true;
}
