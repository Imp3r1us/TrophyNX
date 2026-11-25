// Data for the Tesla overlay: see include/overlay_data.h
#include "overlay_data.h"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace ov {

int tierFor(const std::string& name, int points) {
    if (name == "platinum") return TIER_PLATINUM;
    if (name == "gold") return TIER_GOLD;
    if (name == "silver") return TIER_SILVER;
    if (name == "bronze") return TIER_BRONZE;
    return points >= 50 ? TIER_GOLD : points >= 25 ? TIER_SILVER : TIER_BRONZE;
}

const char* tierName(int tier) {
    static const char* N[] = {"bronze", "silver", "gold", "platinum"};
    return N[tier < 0 ? 0 : tier > 3 ? 3 : tier];
}

std::string titleHex(uint64_t titleId) {
    char b[17];
    snprintf(b, sizeof(b), "%016llX", (unsigned long long)titleId);
    return b;
}

std::string snapshotPath(const std::string& dataDir, uint64_t titleId) { return dataDir + "/overlay/" + titleHex(titleId) + ".json"; }
std::string livePath(const std::string& dataDir, uint64_t titleId) { return dataDir + "/overlay/" + titleHex(titleId) + ".live.json"; }
std::string memTestPath(const std::string& dataDir, uint64_t titleId) { return dataDir + "/overlay/" + titleHex(titleId) + ".memtest.json"; }

static std::string printAndFree(cJSON* root) {
    char* s = cJSON_PrintUnformatted(root);
    std::string out = s ? s : "";
    if (s) cJSON_free(s);
    cJSON_Delete(root);
    return out + "\n";
}

// ---------------------------------------------------------------- snapshot
std::string snapshotJson(const GameData& g) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "title_id", titleHex(g.titleId).c_str());
    cJSON_AddStringToObject(root, "name", g.name.c_str());
    cJSON* arr = cJSON_AddArrayToObject(root, "achievements");
    for (const Ach& a : g.achs) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", a.id.c_str());
        cJSON_AddStringToObject(o, "name", a.name.c_str());
        cJSON_AddStringToObject(o, "desc", a.desc.c_str());
        cJSON_AddNumberToObject(o, "points", a.points);
        cJSON_AddStringToObject(o, "tier", tierName(a.tier));
        if (a.hidden) cJSON_AddTrueToObject(o, "hidden");
        if (a.showProgress) cJSON_AddTrueToObject(o, "progress");
        cJSON_AddNumberToObject(o, "target", a.target);
        if (a.divisor != 1) cJSON_AddNumberToObject(o, "divisor", a.divisor);
        if (a.value != 0) cJSON_AddNumberToObject(o, "value", a.value);
        cJSON_AddItemToArray(arr, o);
    }
    return printAndFree(root);
}

static std::string str(const cJSON* o, const char* k) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static double num(const cJSON* o, const char* k, double def) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

bool parseSnapshot(const char* json, GameData& out) {
    out = GameData();
    cJSON* root = json ? cJSON_Parse(json) : nullptr;
    if (!root) return false;
    std::string tid = str(root, "title_id");
    out.titleId = strtoull(tid.c_str(), nullptr, 16);
    out.name = str(root, "name");
    const cJSON* arr = cJSON_GetObjectItemCaseSensitive(root, "achievements");
    const cJSON* o;
    cJSON_ArrayForEach(o, arr) {
        Ach a;
        a.id = str(o, "id");
        if (a.id.empty()) continue;
        a.name = str(o, "name");
        a.desc = str(o, "desc");
        a.points = (int)num(o, "points", 10);
        a.tier = tierFor(str(o, "tier"), a.points);
        a.hidden = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "hidden"));
        a.showProgress = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "progress"));
        a.target = num(o, "target", 1);
        a.divisor = num(o, "divisor", 1);
        if (a.divisor == 0) a.divisor = 1;
        a.value = num(o, "value", 0);
        out.achs.push_back(a);
    }
    cJSON_Delete(root);
    return out.titleId != 0;
}

// ---------------------------------------------------------------- live memory test
std::string memTestJson(const MemTest& t) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "title_id", titleHex(t.titleId).c_str());
    cJSON_AddStringToObject(root, "build", t.build.c_str());
    cJSON_AddNumberToObject(root, "watched", t.watched);
    cJSON_AddNumberToObject(root, "total", t.total);
    cJSON_AddBoolToObject(root, "reading", t.reading);
    if (!t.note.empty()) cJSON_AddStringToObject(root, "note", t.note.c_str());
    cJSON* arr = cJSON_AddArrayToObject(root, "items");
    for (const MemTestItem& i : t.items) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", i.id.c_str());
        cJSON_AddStringToObject(o, "cmp", i.cmp.c_str());
        cJSON_AddNumberToObject(o, "target", i.target);
        if (i.read) cJSON_AddNumberToObject(o, "value", i.value);
        if (i.holds) cJSON_AddTrueToObject(o, "holds");
        if (i.done) cJSON_AddTrueToObject(o, "done");
        cJSON_AddItemToArray(arr, o);
    }
    return printAndFree(root);
}

bool parseMemTest(const char* json, MemTest& out) {
    out = MemTest();
    cJSON* root = json ? cJSON_Parse(json) : nullptr;
    if (!root) return false;
    out.titleId = strtoull(str(root, "title_id").c_str(), nullptr, 16);
    out.build = str(root, "build");
    out.watched = (int)num(root, "watched", 0);
    out.total = (int)num(root, "total", 0);
    out.reading = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "reading"));
    out.note = str(root, "note");
    const cJSON* o;
    cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(root, "items")) {
        MemTestItem i;
        i.id = str(o, "id");
        if (i.id.empty()) continue;
        i.cmp = str(o, "cmp");
        if (i.cmp.empty()) i.cmp = ">=";
        i.target = num(o, "target", 0);
        const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, "value");
        i.read = cJSON_IsNumber(v);
        i.value = i.read ? v->valuedouble : 0;
        i.holds = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "holds"));
        i.done = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "done"));
        out.items.push_back(i);
    }
    cJSON_Delete(root);
    return out.titleId != 0;
}

std::string valueText(double v) {
    char b[48];
    if (v != v) return "?";
    if (std::fabs(v - std::round(v)) < 1e-6 && std::fabs(v) < 1e15) {
        long long n = std::llround(v);
        std::string digits = std::to_string(n < 0 ? -n : n), out;
        for (size_t i = 0; i < digits.size(); i++) {
            if (i && (digits.size() - i) % 3 == 0) out += ',';
            out += digits[i];
        }
        return (n < 0 ? "-" : "") + out;
    }
    snprintf(b, sizeof(b), "%.2f", v);
    return b;
}

// ---------------------------------------------------------------- live progress
std::map<std::string, double> parseLive(const char* json) {
    std::map<std::string, double> m;
    cJSON* root = json ? cJSON_Parse(json) : nullptr;
    if (!root) return m;
    const cJSON* it;
    cJSON_ArrayForEach(it, root) if (cJSON_IsNumber(it) && it->string) m[it->string] = it->valuedouble;
    cJSON_Delete(root);
    return m;
}

std::string mergeLive(const std::string& existing, const std::map<std::string, double>& updates) {
    std::map<std::string, double> m = parseLive(existing.c_str());
    for (const auto& kv : updates) m[kv.first] = kv.second;
    cJSON* root = cJSON_CreateObject();
    for (const auto& kv : m) cJSON_AddNumberToObject(root, kv.first.c_str(), kv.second);
    return printAndFree(root);
}

// ---------------------------------------------------------------- unlock store
std::map<std::string, uint64_t> storeEntriesFor(const std::string& text, uint64_t titleId) {
    std::map<std::string, uint64_t> m;
    const std::string key = "\"" + titleHex(titleId) + "/";
    size_t pos = 0;
    while ((pos = text.find(key, pos)) != std::string::npos) {
        size_t idStart = pos + key.size();
        size_t idEnd = text.find('"', idStart);
        if (idEnd == std::string::npos) break;
        size_t colon = text.find_first_not_of(" \t", idEnd + 1);
        pos = idEnd + 1;
        if (colon == std::string::npos || text[colon] != ':') continue;   // a value, not a key
        const char* p = text.c_str() + colon + 1;
        while (*p == ' ' || *p == '\t') p++;
        if (*p < '0' || *p > '9') continue;
        m[text.substr(idStart, idEnd - idStart)] = strtoull(p, nullptr, 10);
    }
    return m;
}

// ---------------------------------------------------------------- merge and order
void merge(GameData& g, const std::map<std::string, uint64_t>& unlocks, const std::map<std::string, double>& live) {
    g.unlocked = g.points = g.maxPoints = 0;
    for (Ach& a : g.achs) {
        auto lv = live.find(a.id);
        if (lv != live.end()) a.value = lv->second;
        auto u = unlocks.find(a.id);
        a.unlocked = u != unlocks.end();
        a.unlockTime = a.unlocked ? u->second : 0;
        g.maxPoints += a.points;
        if (a.unlocked) { g.unlocked++; g.points += a.points; }
    }
}

double progressRatio(const Ach& a) {
    if (a.unlocked) return 1;
    if (a.target <= 0) return 0;
    double r = a.value / a.target;
    return r < 0 ? 0 : r > 1 ? 1 : r;
}

Sections sections(const GameData& g) {
    Sections s;
    for (size_t i = 0; i < g.achs.size(); i++) {
        const Ach& a = g.achs[i];
        if (a.unlocked) s.unlocked.push_back(i);
        else if (a.showProgress && !a.hidden && a.value > 0) s.inProgress.push_back(i);
        else s.locked.push_back(i);
    }
    std::stable_sort(s.inProgress.begin(), s.inProgress.end(),
                     [&](size_t x, size_t y) { return progressRatio(g.achs[x]) > progressRatio(g.achs[y]); });
    std::stable_sort(s.unlocked.begin(), s.unlocked.end(),
                     [&](size_t x, size_t y) { return g.achs[x].unlockTime > g.achs[y].unlockTime; });
    return s;
}

// ---------------------------------------------------------------- texts
static std::string fmtInt(long long v) {
    std::string s = std::to_string(v), out;
    int n = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        out.insert(out.begin(), s[i]);
        if (++n % 3 == 0 && i > 0 && s[i - 1] != '-') out.insert(out.begin(), ',');
    }
    return out;
}

static std::string fmtNum(double v) {
    if (std::fabs(v - std::llround(v)) < 0.05) return fmtInt(std::llround(v));
    char b[32];
    snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

std::string progressText(const Ach& a) {   // as in the app's game page
    double cur = std::min(a.value, a.target) / a.divisor, tgt = a.target / a.divisor;
    return fmtNum(std::floor(cur * 10) / 10) + " / " + fmtNum(tgt);
}

std::string dateText(uint64_t t) {
    if (!t) return "";
    time_t tt = (time_t)t;
    struct tm* tmv = gmtime(&tt);
    if (!tmv) return "";
    char b[32];
    strftime(b, sizeof(b), "%d %b %Y", tmv);   // same as the app
    return b;
}

std::string descText(const Ach& a) {
    if (a.hidden && !a.unlocked) return "Hidden achievement - keep playing!";
    return a.desc;
}

// Byte length of the UTF-8 sequence starting with c.
static size_t u8len(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1; }

std::vector<std::string> wrap(const std::string& text, int maxWidth, const std::function<int(const std::string&)>& measure, size_t maxLines) {
    std::vector<std::string> lines;
    std::vector<std::string> words;
    {
        std::string w;
        for (char c : text) {
            if (c == ' ' || c == '\n') { if (!w.empty()) words.push_back(w); w.clear(); if (c == '\n') words.push_back("\n"); }
            else w += c;
        }
        if (!w.empty()) words.push_back(w);
    }
    std::string line;
    auto flush = [&]() { lines.push_back(line); line.clear(); };
    for (size_t wi = 0; wi < words.size() && lines.size() < maxLines; wi++) {
        std::string w = words[wi];
        if (w == "\n") { flush(); continue; }
        std::string cand = line.empty() ? w : line + " " + w;
        if (measure(cand) <= maxWidth) { line = cand; continue; }
        if (!line.empty()) { flush(); if (lines.size() >= maxLines) break; }
        // the word alone is too wide: cut it on character boundaries
        while (measure(w) > maxWidth && lines.size() < maxLines) {
            size_t cut = 0;
            for (size_t i = 0; i < w.size();) {
                size_t n = u8len((unsigned char)w[i]);
                if (measure(w.substr(0, i + n)) > maxWidth) break;
                i += n;
                cut = i;
            }
            if (cut == 0) cut = u8len((unsigned char)w[0]);
            line = w.substr(0, cut);
            flush();
            w = w.substr(cut);
        }
        line = w;
    }
    if (!line.empty() && lines.size() < maxLines) lines.push_back(line);
    if (lines.size() == maxLines && !lines.empty()) {   // more text than lines: mark the cut
        size_t total = 0;
        for (char c : text) if (c != ' ' && c != '\n') total++;
        size_t shownNoSpace = 0;
        for (const std::string& l : lines) for (char c : l) if (c != ' ') shownNoSpace++;
        if (shownNoSpace < total) lines.back() += "...";
    }
    return lines;
}

}  // namespace ov
