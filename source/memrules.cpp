// "memory" rules: see include/memrules.h
#include "memrules.h"
#include "cJSON.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mem {

static std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool parseValType(const std::string& s0, ValType& out) {
    static const struct { const char* n; ValType t; } T[] = {
        {"u8", ValType::U8}, {"u16", ValType::U16}, {"u32", ValType::U32}, {"u64", ValType::U64},
        {"s8", ValType::S8}, {"s16", ValType::S16}, {"s32", ValType::S32}, {"s64", ValType::S64},
        {"i8", ValType::S8}, {"i16", ValType::S16}, {"i32", ValType::S32}, {"i64", ValType::S64},
        {"f32", ValType::F32}, {"f64", ValType::F64}, {"float", ValType::F32}, {"double", ValType::F64},
    };
    std::string s = lower(s0);
    for (const auto& e : T) if (s == e.n) { out = e.t; return true; }
    return false;
}

const char* valTypeName(ValType t) {
    static const char* N[] = {"u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64"};
    return N[(int)t];
}

int valSize(ValType t) {
    switch (t) {
        case ValType::U8: case ValType::S8: return 1;
        case ValType::U16: case ValType::S16: return 2;
        case ValType::U32: case ValType::S32: case ValType::F32: return 4;
        default: return 8;
    }
}

bool parseCmp(const std::string& s, Cmp& out) {
    if (s == ">=" || s.empty()) out = Cmp::Ge;
    else if (s == ">") out = Cmp::Gt;
    else if (s == "<=") out = Cmp::Le;
    else if (s == "<") out = Cmp::Lt;
    else if (s == "==" || s == "=") out = Cmp::Eq;
    else if (s == "!=") out = Cmp::Ne;
    else if (s == "&") out = Cmp::BitsAny;
    else return false;
    return true;
}

const char* cmpName(Cmp c) {
    static const char* N[] = {">=", ">", "<=", "<", "==", "!=", "&"};
    return N[(int)c];
}

// ---------------------------------------------------------------- addresses
namespace {
struct Cursor {
    const char* p;
    void ws() { while (*p == ' ' || *p == '\t') p++; }
};
bool hexNumber(Cursor& c, int64_t& out) {
    c.ws();
    if (c.p[0] == '0' && (c.p[1] == 'x' || c.p[1] == 'X')) c.p += 2;
    if (!isxdigit((unsigned char)*c.p)) return false;
    uint64_t v = 0;
    int digits = 0;
    while (isxdigit((unsigned char)*c.p)) {
        if (++digits > 16) return false;
        char ch = (char)tolower((unsigned char)*c.p++);
        v = v * 16 + (uint64_t)(ch <= '9' ? ch - '0' : ch - 'a' + 10);
    }
    out = (int64_t)v;
    return true;
}
// Optional "+hex" / "-hex"; nothing = 0.
bool signedOffset(Cursor& c, int64_t& out, std::string& err) {
    c.ws();
    out = 0;
    if (*c.p != '+' && *c.p != '-') return true;
    bool neg = *c.p++ == '-';
    if (!hexNumber(c, out)) { err = "expected a hex number after + or -"; return false; }
    if (neg) out = -out;
    return true;
}
}  // namespace

bool parseAddress(const std::string& text, Address& out, std::string& err) {
    out = Address();
    Cursor c{text.c_str()};
    int depth = 0;
    c.ws();
    while (*c.p == '[') { depth++; c.p++; c.ws(); }
    std::string word;
    while (isalpha((unsigned char)*c.p)) word += (char)tolower((unsigned char)*c.p++);
    if (word == "main") out.region = Region::Main;
    else if (word == "heap") out.region = Region::Heap;
    else if (word == "alias") out.region = Region::Alias;
    else { err = "address must start with main, heap or alias: '" + text + "'"; return false; }
    int64_t off;
    if (!signedOffset(c, off, err)) return false;
    out.offsets.push_back(off);
    for (int i = 0; i < depth; i++) {
        c.ws();
        if (*c.p != ']') { err = "missing ']' in '" + text + "'"; return false; }
        c.p++;
        if (!signedOffset(c, off, err)) return false;
        out.offsets.push_back(off);
    }
    c.ws();
    if (*c.p) { err = "unexpected '" + std::string(c.p) + "' in '" + text + "'"; return false; }
    if (depth > 16) { err = "too many pointers in '" + text + "'"; return false; }
    return true;
}

std::string formatAddress(const Address& a) {
    static const char* R[] = {"main", "heap", "alias"};
    auto off = [](int64_t v) {
        char b[24];
        snprintf(b, sizeof(b), "%s%llX", v < 0 ? "-" : "+", (unsigned long long)(v < 0 ? -v : v));
        return std::string(b);
    };
    if (a.offsets.empty()) return R[(int)a.region];
    std::string s(a.offsets.size() - 1, '[');
    s += R[(int)a.region] + off(a.offsets[0]);
    for (size_t i = 1; i < a.offsets.size(); i++) s += "]" + (a.offsets[i] ? off(a.offsets[i]) : std::string());
    return s;
}

std::string buildHex(const uint8_t* id) {
    char b[17];
    for (int i = 0; i < 8; i++) snprintf(b + i * 2, 3, "%02X", id[i]);
    return std::string(b, 16);
}

const BuildAddrs* forBuild(const std::vector<BuildAddrs>& builds, const std::string& running) {
    const BuildAddrs* best = nullptr;
    for (const BuildAddrs& b : builds)
        if (!b.build.empty() && b.build.size() <= running.size() && running.compare(0, b.build.size(), b.build) == 0 &&
            (!best || b.build.size() > best->build.size()))
            best = &b;
    return best;
}

bool resolve(const Address& a, const Bases& b, const Reader& read, uint64_t& out) {
    uint64_t base = a.region == Region::Main ? b.main : a.region == Region::Heap ? b.heap : b.alias;
    if (!base || a.offsets.empty()) return false;
    uint64_t addr = base + (uint64_t)a.offsets[0];
    for (size_t i = 1; i < a.offsets.size(); i++) {
        uint64_t ptr = 0;
        if (!read(addr, &ptr, sizeof(ptr)) || ptr == 0) return false;
        addr = ptr + (uint64_t)a.offsets[i];
    }
    out = addr;
    return true;
}

bool readValue(ValType t, uint64_t addr, const Reader& read, double& out) {
    uint8_t buf[8] = {};
    if (!read(addr, buf, (size_t)valSize(t))) return false;
    switch (t) {   // the Switch is little-endian, like the hosts the tests run on
        case ValType::U8: out = buf[0]; break;
        case ValType::S8: out = (int8_t)buf[0]; break;
        case ValType::U16: { uint16_t v; memcpy(&v, buf, 2); out = v; break; }
        case ValType::S16: { int16_t v; memcpy(&v, buf, 2); out = v; break; }
        case ValType::U32: { uint32_t v; memcpy(&v, buf, 4); out = v; break; }
        case ValType::S32: { int32_t v; memcpy(&v, buf, 4); out = v; break; }
        case ValType::U64: { uint64_t v; memcpy(&v, buf, 8); out = (double)v; break; }
        case ValType::S64: { int64_t v; memcpy(&v, buf, 8); out = (double)v; break; }
        case ValType::F32: { float v; memcpy(&v, buf, 4); out = v; break; }
        case ValType::F64: { double v; memcpy(&v, buf, 8); out = v; break; }
    }
    return out == out;   // NaN (a float read from garbage) never holds
}

bool holds(Cmp c, double v, double target, double max) {
    if (max > 0 && v > max) return false;
    switch (c) {
        case Cmp::Ge: return v + 1e-9 >= target;
        case Cmp::Gt: return v > target;
        case Cmp::Le: return v <= target + 1e-9;
        case Cmp::Lt: return v < target;
        case Cmp::Eq: return v == target;
        case Cmp::Ne: return v != target;
        case Cmp::BitsAny: return ((uint64_t)v & (uint64_t)target) != 0;
    }
    return false;
}

// ---------------------------------------------------------------- definitions
static bool addAddress(BuildAddrs& b, const cJSON* v, std::string& err) {
    if (!cJSON_IsString(v)) { err = "build " + b.build + ": addresses must be strings"; return false; }
    Address a;
    if (!parseAddress(v->valuestring, a, err)) return false;
    b.addrs.push_back(a);
    return true;
}

bool parseCondition(const void* rule, Condition& out, std::string& err) {
    const cJSON* r = (const cJSON*)rule;
    out = Condition();
    const cJSON* vt = cJSON_GetObjectItemCaseSensitive(r, "value_type");
    if (vt && (!cJSON_IsString(vt) || !parseValType(vt->valuestring, out.type))) {
        err = "value_type must be u8, u16, u32, u64, s8, s16, s32, s64, f32 or f64";
        return false;
    }
    const cJSON* cmp = cJSON_GetObjectItemCaseSensitive(r, "cmp");
    if (cmp && (!cJSON_IsString(cmp) || !parseCmp(cmp->valuestring, out.cmp))) {
        err = "cmp must be >=, >, <=, <, ==, != or &";
        return false;
    }
    const cJSON* t = cJSON_GetObjectItemCaseSensitive(r, "target");
    if (cJSON_IsNumber(t)) out.target = t->valuedouble;
    else if (cJSON_IsString(t)) out.target = (double)strtoull(t->valuestring, nullptr, 0);   // "0x4" for bit masks
    const cJSON* mx = cJSON_GetObjectItemCaseSensitive(r, "max");
    if (cJSON_IsNumber(mx)) out.max = mx->valuedouble;
    const cJSON* builds = cJSON_GetObjectItemCaseSensitive(r, "builds");
    if (!cJSON_IsObject(builds) || !builds->child) { err = "memory rule needs \"builds\": { \"<build id>\": \"<address>\" }"; return false; }
    for (const cJSON* b = builds->child; b; b = b->next) {
        BuildAddrs ba;
        for (const char* p = b->string; p && *p; p++) {
            if (!isxdigit((unsigned char)*p)) { err = std::string("build id '") + b->string + "' is not hex"; return false; }
            ba.build += (char)toupper((unsigned char)*p);
        }
        if (ba.build.empty() || ba.build.size() > 64) { err = "empty or too long build id"; return false; }
        if (cJSON_IsArray(b)) {
            for (const cJSON* v = b->child; v; v = v->next) if (!addAddress(ba, v, err)) return false;
        } else if (!addAddress(ba, b, err)) return false;
        if (ba.addrs.empty()) { err = "build " + ba.build + " has no address"; return false; }
        out.builds.push_back(ba);
    }
    return true;
}

bool anyHolds(const Condition& c, const std::string& build, const Bases& b, const Reader& read, double& value, bool& readFailed) {
    const BuildAddrs* ba = forBuild(c.builds, build);
    if (!ba) return false;
    bool any = false;
    for (const Address& a : ba->addrs) {
        uint64_t addr;
        double v;
        if (!resolve(a, b, read, addr) || !readValue(c.type, addr, read, v)) { readFailed = true; continue; }
        value = v;
        if (holds(c.cmp, v, c.target, c.max)) { any = true; break; }
    }
    return any;
}

// ---------------------------------------------------------------- watcher
void Watcher::reset(const std::vector<Item>& items, const std::set<std::string>& alreadyUnlocked) {
    m_items = items;
    m_done = alreadyUnlocked;
    m_streak.clear();
    m_values.clear();
    m_readFailed = false;
}

size_t Watcher::coverage(const std::string& build) const {
    size_t n = 0;
    for (const Item& i : m_items) if (i.cond && forBuild(i.cond->builds, build)) n++;
    return n;
}

size_t Watcher::pending() const {
    size_t n = 0;
    for (const Item& i : m_items) if (!m_done.count(i.id)) n++;
    return n;
}

std::vector<std::string> Watcher::step(const std::string& build, const Bases& b, const Reader& read) {
    std::vector<std::string> now;
    m_readFailed = false;
    for (const Item& i : m_items) {
        if (!i.cond || m_done.count(i.id)) continue;
        double v = 0;
        bool failed = false;
        bool ok = anyHolds(*i.cond, build, b, read, v, failed);
        if (failed) m_readFailed = true;
        if (!failed || ok) m_values[i.id] = v;
        if (!ok) { m_streak.erase(i.id); continue; }
        if (++m_streak[i.id] >= kStreak) {
            m_done.insert(i.id);
            m_streak.erase(i.id);
            now.push_back(i.id);
        }
    }
    return now;
}

}  // namespace mem
