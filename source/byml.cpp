#include "byml.h"
#include <cstring>
#include <cstdlib>

enum : uint8_t {
    T_STRING = 0xA0, T_BINARY = 0xA1, T_ARRAY = 0xC0, T_HASH = 0xC1, T_STRTAB = 0xC2,
    T_BOOL = 0xD0, T_INT = 0xD1, T_FLOAT = 0xD2, T_UINT = 0xD3, T_INT64 = 0xD4, T_UINT64 = 0xD5, T_DOUBLE = 0xD6, T_NULL = 0xFF,
};

uint32_t BymlDoc::rd32(size_t off) const {
    if (!inRange(off, 4)) return 0;
    const uint8_t* p = &m_data[off];
    if (m_be) return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint32_t BymlDoc::rd24(size_t off) const {
    if (!inRange(off, 3)) return 0;
    const uint8_t* p = &m_data[off];
    if (m_be) return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

bool BymlDoc::readStringTable(uint32_t off, std::vector<std::string>& out) {
    out.clear();
    if (!off) return true;
    size_t o = m_base + off;
    if (!inRange(o, 4) || m_data[o] != T_STRTAB) return false;
    uint32_t n = rd24(o + 1);
    if (!inRange(o + 4, (size_t)(n + 1) * 4)) return false;
    out.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        size_t s = o + rd32(o + 4 + 4 * i);
        if (s >= m_data.size()) return false;
        const void* end = memchr(&m_data[s], 0, m_data.size() - s);
        if (!end) return false;
        out.emplace_back((const char*)&m_data[s], (const uint8_t*)end - &m_data[s]);
    }
    return true;
}

bool BymlDoc::parse(std::vector<uint8_t>&& data) {
    m_data = std::move(data);
    for (size_t b : {(size_t)0, (size_t)0x10}) {
        if (!inRange(b, 0x10)) continue;
        if (m_data[b] == 'Y' && m_data[b + 1] == 'B') { m_be = false; m_base = b; }
        else if (m_data[b] == 'B' && m_data[b + 1] == 'Y') { m_be = true; m_base = b; }
        else continue;
        m_version = m_be ? (m_data[b + 2] << 8 | m_data[b + 3]) : (m_data[b + 3] << 8 | m_data[b + 2]);
        if (m_version < 1 || m_version > 10) continue;
        if (!readStringTable(rd32(b + 4), m_keys) || !readStringTable(rd32(b + 8), m_strings)) { error = "bad string table"; return false; }
        uint32_t root = rd32(b + 12);
        if (!root || !inRange(m_base + root, 4)) { error = "empty document"; return false; }
        uint8_t t = m_data[m_base + root];
        if (t != T_HASH && t != T_ARRAY) { error = "root is not a container"; return false; }
        m_root = Node{t, root};
        return true;
    }
    error = "no BYML header (YB/BY) at offset 0 or 0x10";
    return false;
}

BymlValue BymlDoc::leaf(const Node& n) const {
    BymlValue v;
    switch (n.type) {
        case T_STRING: v.kind = BymlValue::String; if (n.raw < m_strings.size()) v.str = m_strings[n.raw]; break;
        case T_BOOL: v.kind = BymlValue::Bool; v.num = n.raw ? 1 : 0; break;
        case T_INT: v.kind = BymlValue::Int; v.num = (int32_t)n.raw; break;
        case T_UINT: v.kind = BymlValue::Int; v.num = n.raw; break;
        case T_FLOAT: { float f; memcpy(&f, &n.raw, 4); v.kind = BymlValue::Float; v.num = f; break; }
        case T_INT64: case T_UINT64: case T_DOUBLE: {
            size_t o = m_base + n.raw;
            if (!inRange(o, 8)) break;
            uint64_t lo = rd32(o), hi = rd32(o + 4);
            uint64_t x = m_be ? (lo << 32 | hi) : (hi << 32 | lo);
            if (n.type == T_DOUBLE) { double d; memcpy(&d, &x, 8); v.num = d; v.kind = BymlValue::Float; }
            else { v.num = n.type == T_INT64 ? (double)(int64_t)x : (double)x; v.kind = BymlValue::Int; }
            break;
        }
        case T_ARRAY: case T_HASH: v.kind = BymlValue::Container; v.num = (double)listSize(n.raw); break;
        default: v.kind = BymlValue::Null; break;
    }
    return v;
}

size_t BymlDoc::listSize(uint32_t off) const { return rd24(m_base + off + 1); }

bool BymlDoc::listItem(uint32_t off, size_t i, Node& out) const {
    size_t o = m_base + off;
    if (!inRange(o, 4) || m_data[o] != T_ARRAY) return false;
    size_t n = rd24(o + 1);
    if (i >= n || !inRange(o + 4, n)) return false;
    size_t vals = o + 4 + ((n + 3) & ~(size_t)3);
    if (!inRange(vals + 4 * i, 4)) return false;
    out = Node{m_data[o + 4 + i], rd32(vals + 4 * i)};
    return true;
}

bool BymlDoc::dictGet(uint32_t off, const std::string& key, Node& out) const {
    size_t o = m_base + off;
    if (!inRange(o, 4) || m_data[o] != T_HASH) return false;
    size_t n = rd24(o + 1);
    // Entries are sorted by key, but a linear scan keeps this robust and saves are small.
    for (size_t i = 0; i < n; i++) {
        size_t e = o + 4 + 8 * i;
        if (!inRange(e, 8)) return false;
        uint32_t k = m_be ? rd24(e) : (m_data[e] | (m_data[e + 1] << 8) | (m_data[e + 2] << 16));
        if (k < m_keys.size() && m_keys[k] == key) { out = Node{m_data[e + 3], rd32(e + 4)}; return true; }
    }
    return false;
}

static bool valueEquals(const BymlValue& v, const std::string& want) {
    if (v.kind == BymlValue::String) return v.str == want;
    char* end = nullptr;
    double d = strtod(want.c_str(), &end);
    if (end && *end == 0 && !want.empty()) return v.num == d;
    if (want == "true") return v.num == 1;
    if (want == "false") return v.num == 0;
    return false;
}

// A segment is "Key", "Key[sel]", "[sel]" or "Key[sel][sel]"; we split it into atoms.
static bool splitAtoms(const std::string& seg, std::vector<std::string>& atoms) {
    size_t i = 0;
    size_t lb = seg.find('[');
    std::string key = seg.substr(0, lb);
    if (!key.empty()) atoms.push_back(key);
    i = lb;
    while (i != std::string::npos && i < seg.size()) {
        if (seg[i] != '[') return false;
        size_t rb = seg.find(']', i);
        if (rb == std::string::npos) return false;
        atoms.push_back(seg.substr(i, rb - i + 1));
        i = rb + 1;
    }
    return true;
}

void BymlDoc::walk(const Node& n, const std::vector<std::string>& atoms, size_t i, std::vector<BymlValue>& out, int depth) const {
    if (depth > 64) return;
    if (i == atoms.size()) { out.push_back(leaf(n)); return; }
    const std::string& a = atoms[i];
    if (a[0] != '[') {
        Node c;
        if (n.type == T_HASH && dictGet(n.raw, a, c)) walk(c, atoms, i + 1, out, depth + 1);
        return;
    }
    if (n.type != T_ARRAY) return;
    std::string sel = a.substr(1, a.size() - 2);
    size_t count = listSize(n.raw);
    if (sel == "*") {
        for (size_t k = 0; k < count; k++) { Node c; if (listItem(n.raw, k, c)) walk(c, atoms, i + 1, out, depth + 1); }
        return;
    }
    size_t eq = sel.find('=');
    if (eq == std::string::npos) {
        char* end = nullptr;
        unsigned long idx = strtoul(sel.c_str(), &end, 10);
        Node c;
        if (end && *end == 0 && listItem(n.raw, idx, c)) walk(c, atoms, i + 1, out, depth + 1);
        return;
    }
    // [Field=value]: keep dictionary elements whose field matches; [=value]: plain elements equal to value
    std::string field = sel.substr(0, eq), want = sel.substr(eq + 1);
    for (size_t k = 0; k < count; k++) {
        Node c;
        if (!listItem(n.raw, k, c)) continue;
        if (field.empty()) {
            if (valueEquals(leaf(c), want)) walk(c, atoms, i + 1, out, depth + 1);
            continue;
        }
        Node f;
        if (c.type == T_HASH && dictGet(c.raw, field, f) && valueEquals(leaf(f), want)) walk(c, atoms, i + 1, out, depth + 1);
    }
}

// Shared with the JSON reader (json_doc.cpp): same path language, same comparisons.
bool treeValueEquals(const BymlValue& v, const std::string& want) { return valueEquals(v, want); }

bool treeSplitPath(const std::string& path, std::vector<std::string>& atoms) {
    size_t s = 0;
    while (s <= path.size()) {
        size_t e = path.find('/', s);
        if (e == std::string::npos) e = path.size();
        std::string seg = path.substr(s, e - s);
        if (!seg.empty() && !splitAtoms(seg, atoms)) return false;
        s = e + 1;
    }
    return !atoms.empty();
}

bool BymlDoc::query(const std::string& path, std::vector<BymlValue>& out) const {
    std::vector<std::string> atoms;
    if (!treeSplitPath(path, atoms)) return false;
    walk(m_root, atoms, 0, out, 0);
    return true;
}
