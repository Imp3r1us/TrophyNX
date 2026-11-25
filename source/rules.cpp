#include "rules.h"
#include "byml.h"
#include "json_doc.h"
#include "save_formats.h"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

// ---------------------------------------------------------------- crc32
uint32_t crc32str(const std::string& s) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char ch : s) crc = (crc >> 8) ^ table[(crc ^ ch) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

uint32_t murmur3str(const std::string& s) {
    const uint8_t* d = (const uint8_t*)s.data();
    size_t len = s.size(), n = len / 4;
    uint32_t h = 0, c1 = 0xcc9e2d51, c2 = 0x1b873593;
    auto rotl = [](uint32_t x, int r) { return (x << r) | (x >> (32 - r)); };
    for (size_t i = 0; i < n; i++) {
        uint32_t k = d[i * 4] | (d[i * 4 + 1] << 8) | (d[i * 4 + 2] << 16) | ((uint32_t)d[i * 4 + 3] << 24);
        k *= c1; k = rotl(k, 15); k *= c2;
        h ^= k; h = rotl(h, 13); h = h * 5 + 0xe6546b64;
    }
    const uint8_t* t = d + n * 4;
    uint32_t k = 0;
    switch (len & 3) {
        case 3: k ^= t[2] << 16; // fallthrough
        case 2: k ^= t[1] << 8;  // fallthrough
        case 1: k ^= t[0]; k *= c1; k = rotl(k, 15); k *= c2; h ^= k;
    }
    h ^= (uint32_t)len;
    h ^= h >> 16; h *= 0x85ebca6b; h ^= h >> 13; h *= 0xc2b2ae35; h ^= h >> 16;
    return h;
}

// ---------------------------------------------------------------- hash/value save reader
uint32_t GamedataSave::rd(size_t off) const {
    const uint8_t* p = &m_data[off];
    if (m_le) return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

bool GamedataSave::parse(std::vector<uint8_t>&& data, const std::string& format) {
    m_data = std::move(data);
    m_index.clear();
    if (m_data.size() < 0x30) return false;
    if (format == "totk_progress") {
        m_le = true;
        if (rd(0) != 0x01020304u) return false;
        m_index.reserve(40000);
        for (size_t o = 0x28; o + 8 <= m_data.size(); o += 8) {
            uint32_t h = rd(o);
            if (h == 0xa3db7114u) break;   // MetaData.SaveTypeHash: end of the simple-value table
            m_index.emplace(h, (uint32_t)(o + 4));
        }
        return !m_index.empty();
    }
    // Header: u32 version hash (small number), u32 0xFFFFFFFF. Switch saves are little-endian,
    // Wii U saves big-endian; detect from the version field.
    m_le = true;
    if (rd(4) != 0xFFFFFFFFu) return false;
    if (rd(0) >= 0x01000000u) m_le = false;
    m_index.reserve(m_data.size() / 8);
    for (size_t o = 0x0C; o + 8 <= m_data.size(); o += 8) {
        uint32_t h = rd(o);
        m_index.emplace(h, (uint32_t)(o + 4)); // keeps first occurrence (arrays repeat the hash)
    }
    return true;
}

bool GamedataSave::get(uint32_t hash, uint32_t& out) const {
    auto it = m_index.find(hash);
    if (it == m_index.end()) return false;
    out = m_filtered ? it->second : rd(it->second);
    return true;
}

bool GamedataSave::parseStream(const std::function<bool(uint64_t, uint8_t*, size_t)>& read, uint64_t size,
                               const std::string& format, const std::unordered_set<uint32_t>& wanted) {
    m_data.clear();
    m_index.clear();
    m_filtered = true;
    m_pairs = 0;
    if (size < 0x30) return false;
    uint8_t head[0x30];
    if (!read(0, head, sizeof(head))) return false;
    m_data.assign(head, head + sizeof(head));   // rd() works on the header
    bool totk = format == "totk_progress";
    uint64_t start;
    m_le = true;
    if (totk) {
        if (rd(0) != 0x01020304u) return false;
        start = 0x28;
    } else {
        if (rd(4) != 0xFFFFFFFFu) return false;
        if (rd(0) >= 0x01000000u) m_le = false;
        start = 0x0C;
    }
    m_data.clear();
    const size_t CHUNK = 64 * 1024;   // multiple of 8: pairs never straddle two chunks
    std::vector<uint8_t> buf(CHUNK);
    auto u32 = [&](const uint8_t* p) -> uint32_t {
        return m_le ? (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)
                    : ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    };
    for (uint64_t o = start; o + 8 <= size; o += CHUNK) {
        size_t len = (size_t)std::min<uint64_t>(CHUNK, (size - o) & ~(uint64_t)7);
        if (len == 0) break;
        if (!read(o, buf.data(), len)) return false;
        for (size_t i = 0; i + 8 <= len; i += 8) {
            uint32_t h = u32(&buf[i]);
            if (totk && h == 0xa3db7114u) return true;   // MetaData.SaveTypeHash: end of the simple values
            m_pairs++;
            if (wanted.count(h)) m_index.emplace(h, u32(&buf[i + 4]));   // keeps the first occurrence
        }
    }
    return totk ? m_pairs > 0 : true;
}

// ---------------------------------------------------------------- definitions
static std::string jstr(cJSON* o, const char* k, const char* def = "") {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? std::string(v->valuestring) : std::string(def);
}
static double jnum(cJSON* o, const char* k, double def) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}
static bool jbool(cJSON* o, const char* k, bool def) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

typedef uint32_t (*HashFn)(const std::string&);

static uint32_t parseHashOrName(const std::string& s, HashFn fn) {
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return (uint32_t)strtoul(s.c_str(), nullptr, 16);
    return fn(s);
}

static bool parseRule(cJSON* r, Rule& rule, std::string& err, HashFn fn) {
    std::string t = jstr(r, "type");
    if (t == "flag" || t == "value") rule.type = RuleType::Flag;
    else if (t == "count") rule.type = RuleType::Count;
    else if (t == "sum") rule.type = RuleType::Sum;
    else if (t == "playtime_hours") rule.type = RuleType::PlaytimeHours;
    else if (t == "launches") rule.type = RuleType::Launches;
    else if (t == "games_played") rule.type = RuleType::GamesPlayed;
    else if (t == "total_hours") rule.type = RuleType::TotalHours;
    else if (t == "unlocked_total") rule.type = RuleType::UnlockedTotal;
    else if (t == "days_played") rule.type = RuleType::DaysPlayed;
    else if (t == "streak_days") rule.type = RuleType::StreakDays;
    else if (t == "comeback_days") rule.type = RuleType::ComebackDays;
    else if (t == "screenshots") rule.type = RuleType::Screenshots;
    else if (t == "videos") rule.type = RuleType::Videos;
    else if (t == "days_total") rule.type = RuleType::DaysTotal;
    else if (t == "streak_total") rule.type = RuleType::StreakTotal;
    else if (t == "captures_total") rule.type = RuleType::CapturesTotal;
    else if (t == "memory") {
        // live only: the background module reads the game's memory; the app shows the unlocks it stored
        rule.type = RuleType::Memory;
        if (!mem::parseMemRule(r, rule.mem, err)) return false;
        rule.target = rule.mem.all[0].target;
        rule.maxSane = rule.mem.all[0].max;
        rule.showProgress = jbool(r, "show_progress", false);   // the app never sees the live value
        return true;
    }
    else { err = "unknown rule type '" + t + "'"; return false; }

    std::string single = jstr(r, "name");
    if (single.empty()) single = jstr(r, "hash");
    if (!single.empty()) rule.hashes.push_back(parseHashOrName(single, fn));
    cJSON* names = cJSON_GetObjectItemCaseSensitive(r, "names");
    if (cJSON_IsArray(names)) {
        cJSON* n;
        cJSON_ArrayForEach(n, names) if (cJSON_IsString(n)) rule.hashes.push_back(parseHashOrName(n->valuestring, fn));
    }
    std::string pattern = jstr(r, "pattern");   // e.g. "Clear_Dungeon%03d"
    if (!pattern.empty()) {
        int from = (int)jnum(r, "from", 0), to = (int)jnum(r, "to", -1);
        for (int i = from; i <= to; i++) {
            char buf[128];
            snprintf(buf, sizeof(buf), pattern.c_str(), i);
            rule.hashes.push_back(fn(buf));
        }
    }
    cJSON* eq = cJSON_GetObjectItemCaseSensitive(r, "equals");
    if (cJSON_IsString(eq)) { rule.hasEquals = true; rule.equals = parseHashOrName(eq->valuestring, fn); rule.hasEqualsStr = true; rule.equalsStr = eq->valuestring; }
    else if (cJSON_IsNumber(eq)) {
        rule.hasEquals = true; rule.equals = (uint32_t)eq->valuedouble;
        char nb[64]; snprintf(nb, sizeof(nb), "%.17g", eq->valuedouble);
        rule.hasEqualsStr = true; rule.equalsStr = nb;
    } else if (cJSON_IsBool(eq)) { rule.hasEqualsStr = true; rule.equalsStr = cJSON_IsTrue(eq) ? "true" : "false"; }
    std::string path = jstr(r, "path");
    if (!path.empty()) rule.paths.push_back(path);
    cJSON* paths = cJSON_GetObjectItemCaseSensitive(r, "paths");
    if (cJSON_IsArray(paths)) { cJSON* pp; cJSON_ArrayForEach(pp, paths) if (cJSON_IsString(pp)) rule.paths.push_back(pp->valuestring); }
    rule.isFloat = jbool(r, "float", false);
    rule.target = jnum(r, "target", 1);
    rule.divisor = jnum(r, "divisor", 1);
    if (rule.divisor <= 0) rule.divisor = 1;
    rule.showProgress = jbool(r, "show_progress", rule.target > 1);
    auto parseOff = [](cJSON* v) -> uint32_t {
        if (cJSON_IsNumber(v)) return (uint32_t)v->valuedouble;
        if (cJSON_IsString(v)) return (uint32_t)strtoul(v->valuestring, nullptr, 0);
        return 0;
    };
    cJSON* off = cJSON_GetObjectItemCaseSensitive(r, "offset");
    if (off) rule.offsets.push_back(parseOff(off));
    cJSON* offs = cJSON_GetObjectItemCaseSensitive(r, "offsets");
    if (cJSON_IsArray(offs)) { cJSON* o; cJSON_ArrayForEach(o, offs) rule.offsets.push_back(parseOff(o)); }
    rule.size = (int)jnum(r, "size", 4);
    if (rule.size != 1 && rule.size != 2 && rule.size != 4) rule.size = 4;
    rule.maxSane = jnum(r, "max", 0);
    bool saveRule = rule.type == RuleType::Flag || rule.type == RuleType::Count || rule.type == RuleType::Sum;
    if (saveRule && rule.hashes.empty() && rule.offsets.empty() && rule.paths.empty()) { err = "rule needs name, hash, names, pattern, offset(s) or path(s)"; return false; }
    return true;
}

static bool parseAchList(cJSON* arr, std::vector<Achievement>& out, std::string& err, HashFn fn = crc32str) {
    if (!cJSON_IsArray(arr)) return true;
    cJSON* a;
    cJSON_ArrayForEach(a, arr) {
        Achievement ach;
        ach.id = jstr(a, "id");
        ach.name = jstr(a, "name");
        ach.desc = jstr(a, "desc");
        ach.points = (int)jnum(a, "points", 10);
        ach.hidden = jbool(a, "hidden", false);
        ach.tier = jstr(a, "tier");
        cJSON* r = cJSON_GetObjectItemCaseSensitive(a, "rule");
        std::string e;
        if (ach.id.empty() || !cJSON_IsObject(r) || !parseRule(r, ach.rule, e, fn)) {
            err += "achievement '" + ach.id + "': " + (e.empty() ? "missing id or rule" : e) + "\n";
            continue;
        }
        out.push_back(ach);
    }
    return true;
}

static cJSON* loadJson(const std::string& path) {
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) return nullptr;
    buf.push_back(0);
    return cJSON_Parse((const char*)buf.data());
}

static bool gameDefFromJson(cJSON* root, GameDef& d, std::string& err) {
    d = GameDef();
    d.id = strtoull(jstr(root, "title_id").c_str(), nullptr, 16);
    d.name = jstr(root, "name");
    d.reader = jstr(root, "reader");
    d.magic = jstr(root, "magic");
    d.xorKey = jstr(root, "xor_key");
    d.keyFile = jstr(root, "key_file");
    d.fileSize = (uint64_t)jnum(root, "file_size", 0);
    d.closeScan = jbool(root, "close_scan", true);
    d.liveSave = jbool(root, "live_save", d.closeScan);   // same reasons to be off: wrong offsets...
    cJSON* sf = cJSON_GetObjectItemCaseSensitive(root, "save_files");
    cJSON* s;
    if (cJSON_IsArray(sf)) cJSON_ArrayForEach(s, sf) if (cJSON_IsString(s)) d.saveFiles.push_back(s->valuestring);
    HashFn fn = d.reader == "totk_progress" ? murmur3str : crc32str;
    parseAchList(cJSON_GetObjectItemCaseSensitive(root, "achievements"), d.achs, err, fn);
    if (!d.id) { err += "missing title_id\n"; return false; }
    return true;
}

bool parseGameDef(const char* text, GameDef& out, std::string& err) {
    cJSON* root = cJSON_Parse(text);
    if (!root) { err += "invalid JSON\n"; return false; }
    bool ok = gameDefFromJson(root, out, err);
    cJSON_Delete(root);
    return ok;
}

void loadGameDefs(const std::string& dir, std::map<uint64_t, GameDef>& defs, std::string& errors, std::vector<std::string>& loaded) {
    for (const std::string& f : listDir(dir)) {
        if (f.size() < 6 || f.substr(f.size() - 5) != ".json") continue;
        cJSON* root = loadJson(dir + "/" + f);
        if (!root) { errors += f + ": invalid JSON\n"; continue; }
        GameDef d;
        std::string e;
        bool ok = gameDefFromJson(root, d, e);
        cJSON_Delete(root);
        if (!e.empty()) errors += f + ": " + e;
        if (ok) { loaded.push_back(dir + "/" + f + " (" + std::to_string(d.achs.size()) + " achievements)"); defs[d.id] = std::move(d); }  // later folders override earlier ones
    }
}

void applyGameDef(Game& g, const GameDef& d) {
    g.hasDefinition = true;
    g.reader = d.reader;
    g.fileSize = d.fileSize;
    g.magic = d.magic;
    g.xorKey = d.xorKey;
    g.keyFile = d.keyFile;
    g.saveFiles = d.saveFiles;
    g.achs = d.achs;
}

void loadUniversal(const std::string& path, std::vector<Achievement>& perGame, std::vector<Achievement>& global, std::string& errors) {
    cJSON* root = loadJson(path);
    if (!root) return;
    perGame.clear();
    global.clear();
    std::string e;
    parseAchList(cJSON_GetObjectItemCaseSensitive(root, "per_game"), perGame, e);
    parseAchList(cJSON_GetObjectItemCaseSensitive(root, "global"), global, e);
    if (!e.empty()) errors += "universal.json: " + e;
    cJSON_Delete(root);
}

// ---------------------------------------------------------------- evaluation
static double ruleValueOnSave(const Rule& r, const GamedataSave& s) {
    double total = 0;
    for (uint32_t h : r.hashes) {
        uint32_t raw;
        if (!s.get(h, raw)) continue;
        double v;
        if (r.isFloat) { float f; memcpy(&f, &raw, 4); v = f; }
        else v = (double)(int32_t)raw;
        bool hit = r.hasEquals ? raw == r.equals : raw != 0;
        if (r.type == RuleType::Count) total += hit ? 1 : 0;
        else if (r.type == RuleType::Sum) total += v;
        else return r.hasEquals ? (hit ? 1 : 0) : v;   // Flag: single value
    }
    return total;
}

static std::string fmtd(double v) {
    char b[32];
    if (v == (long long)v) snprintf(b, sizeof(b), "%lld", (long long)v);
    else snprintf(b, sizeof(b), "%.2f", v);
    return b;
}

// Fixed-offset little-endian saves (Smash Ultimate system_data.bin, MK8D userdata.dat).
static void evaluateBinary(Game& g, SaveSource& src) {
    if (!src.open(g.id)) {
        g.debug.push_back("Save " + src.lastError());
        g.saveStatus = SaveStatus::NoSave;
        return;
    }
    g.debug.push_back("Save mounted (read-only).");
    const bool stream = src.streaming();
    std::vector<uint8_t> data;   // whole file (app)
    std::string used;            // file in use
    uint64_t usedSize = 0;
    for (const std::string& f : g.saveFiles) {
        std::vector<uint8_t> buf;
        uint64_t size = 0;
        if (stream) {
            if (!src.fileSize(f, size)) { g.debug.push_back("  " + f + ": not present"); continue; }
            if (!g.magic.empty()) {
                buf.resize(g.magic.size());
                if (size < g.magic.size() || !src.readAt(f, 0, buf.data(), buf.size())) buf.clear();
            }
        } else {
            if (!src.readAll(f, buf)) { g.debug.push_back("  " + f + ": not present"); continue; }
            size = buf.size();
        }
        std::string why;
        if (g.fileSize && size != g.fileSize)
            why = "size " + std::to_string(size) + ", expected " + std::to_string(g.fileSize) + " (other game version?)";
        else if (!g.magic.empty() && (buf.size() < g.magic.size() || memcmp(buf.data(), g.magic.data(), g.magic.size()) != 0))
            why = "header is not '" + g.magic + "'";
        if (!why.empty()) { g.debug.push_back("  " + f + ": " + why + " -> NOT RECOGNISED"); continue; }
        g.debug.push_back("  " + f + ": " + std::to_string(size) + " bytes -> OK");
        if (!stream) data = std::move(buf);
        used = f;
        usedSize = size;
        break;
    }
    if (used.empty()) {
        src.close();
        g.saveStatus = SaveStatus::Unreadable;
        g.debug.push_back("No usable save file: nothing is unlocked from the save (safety).");
        return;
    }
    g.saveStatus = SaveStatus::Ok;
    g.debug.push_back("");
    g.debug.push_back("Achievement values:");
    for (Achievement& a : g.achs) {
        RuleType t = a.rule.type;
        if (!isSaveRule(t)) continue;
        double total = 0;
        bool bad = false;
        for (uint32_t off : a.rule.offsets) {
            if ((uint64_t)off + a.rule.size > usedSize) { bad = true; continue; }
            uint8_t b[4] = {0, 0, 0, 0};
            if (stream) { if (!src.readAt(used, off, b, a.rule.size)) { bad = true; continue; } }
            else memcpy(b, &data[off], a.rule.size);
            uint32_t v = 0;
            for (int i = 0; i < a.rule.size; i++) v |= (uint32_t)b[i] << (8 * i);
            if (a.rule.maxSane > 0 && v > a.rule.maxSane) { bad = true; continue; }
            if (t == RuleType::Count) total += v ? 1 : 0;
            else total += v;
        }
        a.value = bad ? 0 : total;   // a suspicious value never unlocks anything
        std::string line = "  " + a.id + ": " + fmtd(a.value) + " / " + fmtd(a.rule.target);
        line += a.value + 1e-9 >= a.rule.target ? "  [MET]" : "  [not yet]";
        if (bad) line += "  !! value out of range: offset probably moved in a game update";
        g.debug.push_back(line);
    }
    src.close();
}

// ---------------------------------------------------------------- BYML reader (Super Mario Odyssey File*.bin, ...)
// Value of a rule on one document: flag/value -> largest matched value, count -> matched values that
// are truthy (or equal to `equals`), sum -> sum of matched values. `found` = paths that matched something.
// Works for any path-addressed document (BymlDoc, JsonDoc).
template <class Doc>
double treeRuleValue(const Rule& rule, const Doc& doc, size_t& found) {
    double total = 0, best = 0;
    bool any = false;
    found = 0;
    for (const std::string& p : rule.paths) {
        std::vector<BymlValue> vals;
        if (!doc.query(p, vals) || vals.empty()) continue;
        found++;
        for (const BymlValue& v : vals) {
            double x;
            if (rule.hasEqualsStr) {
                BymlValue w = v;
                bool eq;
                if (w.kind == BymlValue::String) eq = w.str == rule.equalsStr;
                else if (rule.equalsStr == "true") eq = w.num == 1;
                else if (rule.equalsStr == "false") eq = w.num == 0;
                else eq = w.num == strtod(rule.equalsStr.c_str(), nullptr);
                x = eq ? 1 : 0;
            } else {
                x = v.kind == BymlValue::String ? (v.str.empty() ? 0 : 1) : v.num;
            }
            if (rule.type == RuleType::Count) total += (rule.hasEqualsStr ? x : (v.truthy() ? 1 : 0));
            else if (rule.type == RuleType::Sum) total += x;
            else if (!any || x > best) best = x;
            any = true;
        }
    }
    return rule.type == RuleType::Flag ? best : total;
}

static std::string describeDoc(const BymlDoc& d) {
    if (d.streamed()) return "BYML v" + std::to_string(d.version()) + (d.bigEndian() ? " BE" : " LE") + " (read in pages, " + std::to_string(d.pageReads()) + " page reads)";
    char info[64];
    snprintf(info, sizeof(info), "BYML v%d %s at 0x%zX", d.version(), d.bigEndian() ? "BE" : "LE", d.base());
    return info;
}
static std::string describeDoc(const JsonDoc& d) { return "JSON, " + std::to_string(d.topLevelCount()) + " top-level entries"; }
static std::string describeDoc(const GvasDoc& d) {
    return "GVAS " + d.info.summary + (d.info.skipped ? ", " + std::to_string(d.info.skipped) + " value(s) not decoded" : "");
}
static std::string describeDoc(const NrbfDoc& d) { return "NRBF, " + d.info.summary; }
static std::string describeDoc(const SgbDoc& d) { return d.info.summary; }
static std::string describeDoc(const DeltaruneDoc& d) { return d.info.summary; }
static std::string describeDoc(const TheatrhythmDoc& d) { return d.info.summary; }
static std::string describeDoc(const RpgMakerDoc& d) { return d.info.summary; }
static std::string describeDoc(const GameMakerDoc& d) { return d.info.summary; }

// Rough peak memory of a parsed document per byte of file (BymlDoc keeps the file; the others build
// a cJSON tree, ~10 bytes of tree per byte of text, more for dense binary formats).
static size_t memoryFactor(const BymlDoc*) { return 1; }
static size_t memoryFactor(const JsonDoc*) { return 12; }
static size_t memoryFactor(const GvasDoc*) { return 16; }
static size_t memoryFactor(const NrbfDoc*) { return 16; }
static size_t memoryFactor(const SgbDoc*) { return 160; }   // LZ4 (x4..x40) then a Lua state tree
static size_t memoryFactor(const DeltaruneDoc*) { return 3; }   // parsed JSON text (~1x) + a small tree (flags kept sparse)
static size_t memoryFactor(const RpgMakerDoc*) { return 48; }   // LZString / zlib (x3..x10) then a JSON tree
static size_t memoryFactor(const GameMakerDoc*) { return 16; }
static size_t memoryFactor(const TheatrhythmDoc*) { return 4; }   // pre-check only: the reader checks its real need after decrypting
// (live module: only the rule values are built, ~2.3 MB peak for musicBackup.inf instead of ~11 MB)

// Password of an encrypted save ("key_file"), given to the readers that need one.
static std::string g_keyDir;
static std::vector<std::string> g_metaDirs;
void setSaveKeyDir(const std::string& dir) { g_keyDir = dir; }
void setSaveKeySearchDirs(const std::vector<std::string>& dirs) { g_metaDirs = dirs; }

// App only: no password file yet -> look for the game's global-metadata.dat (dumped from the game with nxdumptool,
// or copied to the keys folder), find the password in it with one of the save files, and save it as the key file.
static std::string findKeyFromGame(Game& g, SaveSource& src, const std::string& keyPath) {
    if (g.reader != "theatrhythm") return "";
    std::vector<std::string> metas;
    for (const std::string& root : g_metaDirs) findFilesNamed(root, "global-metadata.dat", metas);
    if (metas.empty()) return "";
    std::vector<uint8_t> save;
    for (const std::string& f : g.saveFiles) if (src.readAll(f, save)) break;
    if (save.empty()) return "";
    for (const std::string& m : metas) {
        std::vector<uint8_t> meta;
        if (!readFile(m, meta)) continue;
        std::string pw;
        int idx = -1;
        if (!theatrhythmFindPassword(meta.data(), meta.size(), save.data(), save.size(), pw, &idx)) {
            g.debug.push_back("Password search: " + m + " -> no string decrypts this save (other game or version?)");
            continue;
        }
        makeDirs(g_keyDir);
        bool saved = writeFile(keyPath, pw);
        g.debug.push_back("Password found in " + m + " (string #" + std::to_string(idx) + ")" + (saved ? ", saved to " + keyPath : ", but " + keyPath + " could not be written"));
        return pw;
    }
    return "";
}
template <class Doc> static void giveKey(Doc&, const std::string&) {}
static void giveKey(TheatrhythmDoc& d, const std::string& k) { d.password = k; }
// Live module (small heap): readers that can, build only what the rule paths reach, within `budget` bytes.
template <class Doc> static void givePruning(Doc&, const std::vector<std::string>&, size_t) {}
static void givePruning(TheatrhythmDoc& d, const std::vector<std::string>& paths, size_t budget) { d.keepPaths = paths; d.memoryBudget = budget; }
// Live module: readers that read a file in pieces and keep only the rule values, so any save size fits.
template <class Doc> static bool readsInPieces(const Doc*) { return false; }
static bool readsInPieces(const SgbDoc*) { return true; }
static bool readsInPieces(const BymlDoc*) { return true; }
template <class Doc> static bool parseInPieces(Doc&, SaveSource&, const std::string&, uint64_t, const std::vector<std::string>&) { return false; }
static bool parseInPieces(SgbDoc& d, SaveSource& src, const std::string& f, uint64_t size, const std::vector<std::string>& paths) {
    size_t avail = src.memoryAvailable();   // already less a safety margin; the reader itself takes ~100 KB
    size_t budget = avail > 160 * 1024 ? avail - 160 * 1024 : 1;
    return d.parseStream([&](uint64_t off, void* buf, size_t len) { return src.readAt(f, off, buf, len); }, size, paths, budget);
}
// BYML (Super Mario Odyssey, ~2 MB per file): offsets everywhere, so the file is read in 16 KB pages as the
// rule paths are walked; every path is walked here once (results kept by the document) to catch read errors.
static bool parseInPieces(BymlDoc& d, SaveSource& src, const std::string& f, uint64_t size, const std::vector<std::string>& paths) {
    size_t avail = src.memoryAvailable();
    size_t budget = avail > 96 * 1024 ? avail - 96 * 1024 : 1;
    if (!d.parseStream([&src, f](uint64_t off, void* buf, size_t len) { return src.readAt(f, off, buf, len); }, size, budget)) return false;
    std::vector<BymlValue> v;
    for (const std::string& p : paths) { v.clear(); d.query(p, v); }
    if (d.readFailed()) { d.error = "read error"; return false; }
    return true;
}

// Best value of each achievement over the save files (several slots), shared by every reader.
// Files are read and evaluated one at a time, so only one document is in memory.
struct BestValues {
    std::vector<double> best;
    std::vector<int> bestFile;     // -1 = no usable file yet
    std::vector<size_t> foundMax;  // paths / hashes found in the best-covered file
    std::vector<std::string> files;
    void init(size_t n) { best.assign(n, 0); bestFile.assign(n, -1); foundMax.assign(n, 0); files.clear(); }
    void add(size_t i, double v, size_t found) {
        if (bestFile[i] < 0 || v > best[i]) { best[i] = v; bestFile[i] = (int)files.size() - 1; }
        if (found > foundMax[i]) foundMax[i] = found;
    }
};

static void reportValues(Game& g, const BestValues& bv, const char* header, const char* fileLabel, bool paths) {
    g.debug.push_back(std::to_string(bv.files.size()) + " usable save file(s).");
    g.debug.push_back("");
    g.debug.push_back(header);
    for (size_t i = 0; i < g.achs.size(); i++) {
        Achievement& a = g.achs[i];
        if (!isSaveRule(a.rule.type)) continue;
        a.value = bv.best[i];
        std::string line = "  " + a.id + ": " + fmtd(a.value) + " / " + fmtd(a.rule.target);
        line += a.value + 1e-9 >= a.rule.target ? "  [MET]" : "  [not yet]";
        if (bv.bestFile[i] >= 0 && bv.files.size() > 1) line += std::string("  ") + fileLabel + bv.files[bv.bestFile[i]];
        size_t want = paths ? a.rule.paths.size() : a.rule.hashes.size();
        if ((paths ? !bv.files.empty() : true) && bv.foundMax[i] < want)
            line += "  !! " + std::to_string(want - bv.foundMax[i]) + "/" + std::to_string(want) +
                    (paths ? " path(s) not found" : " flag(s) missing in save");
        g.debug.push_back(line);
    }
}

template <class Doc>
static void evaluateTree(Game& g, SaveSource& src) {
    if (!src.open(g.id)) {
        g.debug.push_back("Save " + src.lastError());
        g.saveStatus = SaveStatus::NoSave;
        return;
    }
    g.debug.push_back("Save mounted (read-only).");
    std::string key;
    if (!g.keyFile.empty()) {   // encrypted save: the password is never shipped, the player provides it
        std::string kp = (g_keyDir.empty() ? std::string("keys") : g_keyDir) + "/" + g.keyFile;
        std::vector<uint8_t> kb;
        if (!g_keyDir.empty() && readFile(kp, kb)) key.assign(kb.begin(), kb.end());
        auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0; };
        while (!key.empty() && blank(key.back())) key.pop_back();
        size_t lead = 0;
        while (lead < key.size() && blank(key[lead])) lead++;
        key = key.substr(lead);
        if (key.empty() && !src.streaming() && !g_keyDir.empty()) key = findKeyFromGame(g, src, kp);
        if (key.empty()) {
            src.close();
            g.saveStatus = SaveStatus::Unreadable;
            g.debug.push_back("Encrypted save: password file " + kp + " is missing or empty.");
            g.debug.push_back("  Put the game's save password in it (one line), or the game's global-metadata.dat in the keys folder");
            g.debug.push_back("  (TrophyNX then finds the password itself; see the TrophyNX docs). Save skipped.");
            return;
        }
        g.debug.push_back("Save password: " + kp + " (" + std::to_string(key.size()) + " bytes).");
    }
    BestValues bv;
    bv.init(g.achs.size());
    std::vector<std::string> rulePaths;
    for (const Achievement& a : g.achs) if (isSaveRule(a.rule.type)) rulePaths.insert(rulePaths.end(), a.rule.paths.begin(), a.rule.paths.end());
    auto use = [&](Doc& d, const std::string& f, uint64_t size) {
        g.debug.push_back("  " + f + ": " + std::to_string(size) + " bytes, " + (g.xorKey.empty() ? "" : "XOR key, ") + describeDoc(d) + " -> OK");
        bv.files.push_back(f);
        for (size_t i = 0; i < g.achs.size(); i++) {
            if (!isSaveRule(g.achs[i].rule.type)) continue;
            size_t found = 0;
            double v = treeRuleValue(g.achs[i].rule, d, found);
            bv.add(i, v, found);
        }
    };
    for (const std::string& f : g.saveFiles) {
        if (src.streaming() && g.xorKey.empty() && readsInPieces((const Doc*)nullptr)) {
            uint64_t size = 0;
            if (!src.fileSize(f, size)) { g.debug.push_back("  " + f + ": not present"); continue; }
            Doc d;
            if (!parseInPieces(d, src, f, size, rulePaths)) {
                g.debug.push_back("  " + f + ": " + std::to_string(size) + " bytes -> NOT RECOGNISED (" + d.error + ")");
                continue;
            }
            use(d, f, size);
            continue;
        }
        if (src.streaming()) {   // background module: don't start what can't fit in its heap
            uint64_t size = 0;
            if (!src.fileSize(f, size)) { g.debug.push_back("  " + f + ": not present"); continue; }
            uint64_t need = size * memoryFactor((const Doc*)nullptr) + size + 128 * 1024;
            size_t avail = src.memoryAvailable();
            if (need > avail) {
                g.debug.push_back("  " + f + ": " + std::to_string(size) + " bytes -> SKIPPED (needs ~" + std::to_string(need >> 10) +
                                  " KB, " + std::to_string(avail >> 10) + " KB free; the app reads it)");
                continue;
            }
        }
        std::vector<uint8_t> buf;
        if (!src.readAll(f, buf)) { g.debug.push_back("  " + f + ": not present"); continue; }
        size_t size = buf.size();
        if (!g.xorKey.empty()) {   // e.g. SIGNALIS: JSON XORed with a fixed phrase
            const std::string& k = g.xorKey;
            for (size_t i = 0; i < buf.size(); i++) buf[i] ^= (uint8_t)k[i % k.size()];
        }
        Doc d;
        giveKey(d, key);
        if (src.streaming()) givePruning(d, rulePaths, src.memoryAvailable() > size ? src.memoryAvailable() - size : 0);
        if (!d.parse(std::move(buf))) {
            g.debug.push_back("  " + f + ": " + std::to_string(size) + " bytes -> NOT RECOGNISED (" + d.error + ")");
            continue;
        }
        use(d, f, size);
    }
    src.close();
    g.saveStatus = bv.files.empty() ? SaveStatus::Unreadable : SaveStatus::Ok;
    reportValues(g, bv, "Achievement values (best file):", "", true);
}

static void evaluateHashSave(Game& g, SaveSource& src) {
    if (!src.open(g.id)) {
        g.debug.push_back("Save " + src.lastError());
        g.saveStatus = SaveStatus::NoSave;
        return;
    }
    g.debug.push_back("Save mounted (read-only).");
    const bool stream = src.streaming();
    std::unordered_set<uint32_t> wanted;
    if (stream) for (const Achievement& a : g.achs) if (isSaveRule(a.rule.type)) wanted.insert(a.rule.hashes.begin(), a.rule.hashes.end());
    BestValues bv;
    bv.init(g.achs.size());
    for (const std::string& f : g.saveFiles) {
        GamedataSave s;
        bool ok;
        uint64_t size = 0;
        if (stream) {
            if (!src.fileSize(f, size)) { g.debug.push_back("  " + f + ": not present"); continue; }
            ok = s.parseStream([&](uint64_t off, uint8_t* b, size_t len) { return src.readAt(f, off, b, len); }, size, g.reader, wanted);
        } else {
            std::vector<uint8_t> buf;
            if (!src.readAll(f, buf)) { g.debug.push_back("  " + f + ": not present"); continue; }
            size = buf.size();
            ok = s.parse(std::move(buf), g.reader);
        }
        if (!ok) {
            g.debug.push_back("  " + f + ": " + std::to_string(size) + " bytes -> NOT RECOGNISED (unknown header)");
            continue;
        }
        g.debug.push_back("  " + f + ": " + std::to_string(size) + " bytes, " + (s.littleEndian() ? "LE" : "BE") + ", " +
                          std::to_string(s.entryCount()) + (stream ? " pairs (streamed)" : " values") + "  -> OK");
        bv.files.push_back(f);
        for (size_t i = 0; i < g.achs.size(); i++) {
            const Achievement& a = g.achs[i];
            if (!isSaveRule(a.rule.type)) continue;
            size_t found = 0;
            uint32_t raw;
            for (uint32_t h : a.rule.hashes) if (s.get(h, raw)) found++;
            bv.add(i, ruleValueOnSave(a.rule, s), found);
        }
    }
    src.close();
    g.saveStatus = bv.files.empty() ? SaveStatus::Unreadable : SaveStatus::Ok;
    reportValues(g, bv, "Achievement values (best slot):", "slot ", false);
}

void evaluateSave(Game& g, SaveSource& src) {
    g.saveStatus = SaveStatus::None;
    if (g.reader.empty()) {
        g.debug.push_back(g.hasDefinition ? "Themed set based on the play log (this game's save format isn't supported yet)."
                                          : "No game-specific definition: play-time achievements only.");
        return;
    }
    g.debug.push_back("Reader: " + g.reader);
    if (g.reader == "binary_le") evaluateBinary(g, src);
    else if (g.reader == "byml") evaluateTree<BymlDoc>(g, src);
    else if (g.reader == "json") evaluateTree<JsonDoc>(g, src);
    else if (g.reader == "gvas") evaluateTree<GvasDoc>(g, src);
    else if (g.reader == "nrbf") evaluateTree<NrbfDoc>(g, src);
    else if (g.reader == "sgb") evaluateTree<SgbDoc>(g, src);
    else if (g.reader == "deltarune") evaluateTree<DeltaruneDoc>(g, src);
    else if (g.reader == "theatrhythm") evaluateTree<TheatrhythmDoc>(g, src);
    else if (g.reader == "rpgmaker") evaluateTree<RpgMakerDoc>(g, src);
    else if (g.reader == "gamemaker") evaluateTree<GameMakerDoc>(g, src);
    else if (g.reader == "gamedata_hash" || g.reader == "totk_progress") evaluateHashSave(g, src);
    else {
        g.debug.push_back("ERROR: unknown reader");
        g.saveStatus = SaveStatus::Unreadable;
    }
}
