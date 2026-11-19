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
    if (!cJSON_IsArray(n)) return;
    std::string sel = a.substr(1, a.size() - 2);
    const cJSON* c;
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
