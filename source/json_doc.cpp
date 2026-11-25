#include "json_doc.h"
#include "cJSON.h"
#include <cctype>
#include <cstdlib>

static BymlValue leafOf(const cJSON* n, bool textValues = true) {
    BymlValue v;
    if (!n || cJSON_IsNull(n)) return v;
    if (cJSON_IsBool(n)) { v.kind = BymlValue::Bool; v.num = cJSON_IsTrue(n) ? 1 : 0; return v; }
    if (cJSON_IsNumber(n)) {
        double d = n->valuedouble;
        v.kind = (d == (double)(long long)d) ? BymlValue::Int : BymlValue::Float;
        v.num = d;
        return v;
    }
    if (cJSON_IsString(n) && !textValues) { v.kind = BymlValue::String; v.str = n->valuestring ? n->valuestring : ""; return v; }
    if (cJSON_IsString(n)) {
        // values saved as text ("True", "12", "3.5") read as booleans / numbers (same as Save Lab's engine)
        std::string t = n->valuestring ? n->valuestring : "";
        std::string l = t;
        for (char& c : l) c = (char)tolower((unsigned char)c);
        if (l == "true" || l == "false") { v.kind = BymlValue::Bool; v.num = l == "true"; return v; }
        size_t i = t[0] == '-' ? 1 : 0, digits = 0, frac = 0;
        bool dot = false, ok = t.size() > i;
        for (; i < t.size() && ok; i++) {
            if (t[i] >= '0' && t[i] <= '9') (dot ? frac : digits)++;
            else if (t[i] == '.' && !dot && digits) dot = true;
            else ok = false;
        }
        if (ok && digits && digits <= 15 && (!dot || (frac && frac <= 9))) {
            v.num = strtod(t.c_str(), nullptr);
            v.kind = dot ? BymlValue::Float : BymlValue::Int;
            return v;
        }
        v.kind = BymlValue::String; v.str = t; return v;
    }
    if (cJSON_IsArray(n) || cJSON_IsObject(n)) { v.kind = BymlValue::Container; v.num = cJSON_GetArraySize(n); }
    return v;
}

JsonDoc::~JsonDoc() { if (m_root) cJSON_Delete(m_root); }

JsonDoc& JsonDoc::operator=(JsonDoc&& o) noexcept {
    if (this != &o) {
        if (m_root) cJSON_Delete(m_root);
        m_root = o.m_root;
        m_textValues = o.m_textValues;
        o.m_root = nullptr;
        error = std::move(o.error);
    }
    return *this;
}

void JsonDoc::adopt(cJSON* root, bool textValues) {
    if (m_root) cJSON_Delete(m_root);
    m_root = root;
    m_textValues = textValues;
}

bool JsonDoc::parse(std::vector<uint8_t>&& data) {
    if (m_root) { cJSON_Delete(m_root); m_root = nullptr; }
    m_textValues = true;
    size_t s = 0, e = data.size();
    if (e >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) s = 3;
    auto ws = [](uint8_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (s < e && ws(data[s])) s++;
    while (e > s && (data[e - 1] == 0 || ws(data[e - 1]))) e--;
    if (s >= e || (data[s] != '{' && data[s] != '[')) { error = "not a JSON document"; return false; }
    m_root = cJSON_ParseWithLength(reinterpret_cast<const char*>(data.data() + s), e - s);
    if (!m_root) { error = "JSON syntax error"; return false; }
    return true;
}

size_t JsonDoc::topLevelCount() const { return m_root ? (size_t)cJSON_GetArraySize(m_root) : 0; }

void JsonDoc::walk(const cJSON* n, const std::vector<std::string>& atoms, size_t i, std::vector<BymlValue>& out, int depth) const {
    if (!n || depth > 64) return;
    if (i == atoms.size()) { out.push_back(leafOf(n, m_textValues)); return; }
    const std::string& a = atoms[i];
    if (a[0] != '[') {
        if (cJSON_IsObject(n)) walk(cJSON_GetObjectItemCaseSensitive(n, a.c_str()), atoms, i + 1, out, depth + 1);
        return;
    }
    std::string sel = a.substr(1, a.size() - 2);
    const cJSON* c;
    if (sel == "*" && cJSON_IsObject(n)) {   // every value of an object (Lua tables, dictionaries)
        cJSON_ArrayForEach(c, n) walk(c, atoms, i + 1, out, depth + 1);
        return;
    }
    if (!cJSON_IsArray(n)) return;
    if (sel == "*") {
        cJSON_ArrayForEach(c, n) walk(c, atoms, i + 1, out, depth + 1);
        return;
    }
    size_t eq = sel.find('=');
    if (eq == std::string::npos) {
        char* end = nullptr;
        unsigned long idx = strtoul(sel.c_str(), &end, 10);
        if (end && *end == 0 && !sel.empty() && idx < (unsigned long)cJSON_GetArraySize(n))
            walk(cJSON_GetArrayItem(n, (int)idx), atoms, i + 1, out, depth + 1);
        return;
    }
    // [Field=value]: object elements whose field matches; [=value]: plain elements equal to value
    std::string field = sel.substr(0, eq), want = sel.substr(eq + 1);
    cJSON_ArrayForEach(c, n) {
        if (field.empty()) {
            if (treeValueEquals(leafOf(c, m_textValues), want)) walk(c, atoms, i + 1, out, depth + 1);
            continue;
        }
        if (!cJSON_IsObject(c)) continue;
        const cJSON* f = cJSON_GetObjectItemCaseSensitive(c, field.c_str());
        if (f && treeValueEquals(leafOf(f, m_textValues), want)) walk(c, atoms, i + 1, out, depth + 1);
    }
}

bool JsonDoc::query(const std::string& path, std::vector<BymlValue>& out) const {
    std::vector<std::string> atoms;
    if (!m_root || !treeSplitPath(path, atoms)) return false;
    walk(m_root, atoms, 0, out, 0);
    return true;
}

// ---------------------------------------------------------------- streamed, pruned parse (live module)
#include "tree_paths.h"
#include <cstring>

namespace {

struct JsonStream {
    const std::function<bool(uint64_t, void*, size_t)>& read;
    uint64_t size, off = 0;
    std::vector<uint8_t> buf;
    size_t len = 0, pos = 0;
    const PathTrie& trie;
    size_t budget, used = 0;
    std::string err;
    JsonStream(const std::function<bool(uint64_t, void*, size_t)>& r, uint64_t s, const PathTrie& t, size_t b)
        : read(r), size(s), buf(16384), trie(t), budget(b) {}

    bool fill() {
        if (pos < len) return true;
        if (off >= size) return false;
        size_t n = (size_t)std::min<uint64_t>(buf.size(), size - off);
        if (!read(off, buf.data(), n)) { if (err.empty()) err = "read error"; off = size; return false; }
        off += n; len = n; pos = 0;
        return true;
    }
    int peek() { return fill() ? buf[pos] : -1; }
    int get() { return fill() ? buf[pos++] : -1; }
    void ws() { for (int c = peek(); c == ' ' || c == '\t' || c == '\n' || c == '\r'; c = peek()) pos++; }
    bool fail(const char* e) { if (err.empty()) err = e; return false; }
    bool charge(size_t n) {
        used += n + sizeof(cJSON) + 48;
        if (budget && used > budget) return fail("not enough memory");
        return true;
    }
    bool lit(const char* w) {
        for (const char* p = w; *p; p++) if (get() != *p) return fail("JSON syntax error");
        return true;
    }
    int hex4() {
        int v = 0;
        for (int i = 0; i < 4; i++) {
            int c = get(), d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return -1;
            v = v * 16 + d;
        }
        return v;
    }
    // After the opening quote. out == nullptr: skipped.
    bool str(std::string* out) {
        if (out) out->clear();
        for (;;) {
            int c = get();
            if (c < 0) return fail("JSON syntax error");
            if (c == '"') return true;
            if (c != '\\') { if (out) *out += (char)c; continue; }
            c = get();
            uint32_t cp;
            switch (c) {
                case '"': case '\\': case '/': if (out) *out += (char)c; continue;
                case 'b': if (out) *out += '\b'; continue;
                case 'f': if (out) *out += '\f'; continue;
                case 'n': if (out) *out += '\n'; continue;
                case 'r': if (out) *out += '\r'; continue;
                case 't': if (out) *out += '\t'; continue;
                case 'u': {
                    int h = hex4();
                    if (h < 0) return fail("JSON syntax error");
                    cp = (uint32_t)h;
                    if (cp >= 0xDC00 && cp <= 0xDFFF) return fail("JSON syntax error");
                    if (cp >= 0xD800 && cp <= 0xDBFF) {   // surrogate pair
                        if (get() != '\\' || get() != 'u') return fail("JSON syntax error");
                        int lo = hex4();
                        if (lo < 0xDC00 || lo > 0xDFFF) return fail("JSON syntax error");
                        cp = 0x10000 + (((cp & 0x3FF) << 10) | ((uint32_t)lo & 0x3FF));
                    }
                    break;
                }
                default: return fail("JSON syntax error");
            }
            if (!out) continue;
            if (cp < 0x80) *out += (char)cp;
            else if (cp < 0x800) { *out += (char)(0xC0 | (cp >> 6)); *out += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { *out += (char)(0xE0 | (cp >> 12)); *out += (char)(0x80 | ((cp >> 6) & 0x3F)); *out += (char)(0x80 | (cp & 0x3F)); }
            else { *out += (char)(0xF0 | (cp >> 18)); *out += (char)(0x80 | ((cp >> 12) & 0x3F)); *out += (char)(0x80 | ((cp >> 6) & 0x3F)); *out += (char)(0x80 | (cp & 0x3F)); }
        }
    }
    // where == nullptr: parse and build nothing. *out: the built value (only when where != nullptr).
    bool value(const PathSet* where, cJSON** out, int depth) {
        if (depth > 200) return fail("JSON nested too deeply");
        ws();
        int c = get();
        bool build = where && !where->dead();
        if (out) *out = nullptr;
        if (c == '{' || c == '[') {
            bool obj = c == '{';
            cJSON* node = nullptr;
            if (build) {
                if (!charge(0)) return false;
                node = obj ? cJSON_CreateObject() : cJSON_CreateArray();
                if (!node) return fail("not enough memory");
            }
            auto done = [&](bool ok) { if (!ok && node) cJSON_Delete(node); else if (out) *out = node; return ok; };
            ws();
            if (peek() == (obj ? '}' : ']')) { pos++; return done(true); }
            std::string key;
            for (;;) {
                ws();
                if (obj) {
                    if (get() != '"' || !str(build ? &key : nullptr)) return done(fail("JSON syntax error"));
                    ws();
                    if (get() != ':') return done(fail("JSON syntax error"));
                }
                PathSet cs;
                if (build) cs = childOf(trie, *where, obj ? key : std::string());
                bool keep = build && !cs.dead();
                cJSON* child = nullptr;
                if (!value(keep ? &cs : nullptr, keep ? &child : nullptr, depth + 1)) return done(false);
                if (child) {
                    if (obj) { if (!charge(key.size())) { cJSON_Delete(child); return done(false); } cJSON_AddItemToObject(node, key.c_str(), child); }
                    else cJSON_AddItemToArray(node, child);
                }
                ws();
                int e = get();
                if (e == ',') continue;
                if (e == (obj ? '}' : ']')) return done(true);
                return done(fail("JSON syntax error"));
            }
        }
        if (c == '"') {
            std::string s;
            if (!str(build ? &s : nullptr)) return false;
            if (!build) return true;
            if (!charge(s.size())) return false;
            *out = cJSON_CreateString(s.c_str());
            return *out ? true : fail("not enough memory");
        }
        if (c == 't' || c == 'f' || c == 'n') {
            if (!lit(c == 't' ? "rue" : c == 'f' ? "alse" : "ull")) return false;
            if (!build) return true;
            if (!charge(0)) return false;
            *out = c == 'n' ? cJSON_CreateNull() : cJSON_CreateBool(c == 't');
            return *out ? true : fail("not enough memory");
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char t[64];
            size_t n = 0;
            t[n++] = (char)c;
            for (int d = peek(); (d >= '0' && d <= '9') || d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-'; d = peek()) {
                if (n + 1 >= sizeof(t)) return fail("JSON syntax error");
                t[n++] = (char)d; pos++;
            }
            t[n] = 0;
            char* end = nullptr;
            double v = strtod(t, &end);
            if (end == t) return fail("JSON syntax error");
            if (!build) return true;
            if (!charge(0)) return false;
            *out = cJSON_CreateNumber(v);
            return *out ? true : fail("not enough memory");
        }
        return fail(c < 0 && !err.empty() ? err.c_str() : "JSON syntax error");
    }
};

}  // namespace

bool JsonDoc::parseStream(const std::function<bool(uint64_t, void*, size_t)>& read, uint64_t size,
                          const std::vector<std::string>& keepPaths, size_t memoryBudget) {
    if (m_root) { cJSON_Delete(m_root); m_root = nullptr; }
    m_textValues = true;
    PathTrie trie;
    if (!trie.build(keepPaths)) { error = "unusable rule path"; return false; }
    if (memoryBudget && memoryBudget < 16384 + 4096) { error = "not enough memory"; return false; }
    JsonStream js(read, size, trie, memoryBudget ? memoryBudget - 16384 : 0);
    if (js.peek() == 0xEF) {   // UTF-8 BOM
        js.pos++;
        if (js.get() != 0xBB || js.get() != 0xBF) { error = "not a JSON document"; return false; }
    }
    js.ws();
    int c = js.peek();
    if (c != '{' && c != '[') { error = js.err.empty() ? "not a JSON document" : js.err; return false; }
    PathSet root = trieRoot(trie);
    cJSON* t = nullptr;
    if (!js.value(&root, &t, 0)) {
        error = js.err == "not enough memory" ? "not enough memory for the rule values (" + std::to_string(memoryBudget >> 10) + " KB free)" : js.err;
        if (t) cJSON_Delete(t);
        return false;
    }
    m_root = t ? t : cJSON_CreateObject();
    return true;
}
