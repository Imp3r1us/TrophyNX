// Play-log achievements while you play: see include/playlog.h
#include "playlog.h"
#include "closescan.h"     // findDef
#include "overlay_data.h"  // live progress for the Tesla overlay
#include "popup.h"
#include "popup_draw.h"
#include "rules.h"
#include "store.h"
#include "universal_json.h"
#include <switch.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
void (*g_log)(const char*, ...) = nullptr;
bool g_pdm = false;

struct State {
    uint64_t tid = 0;
    std::string name;
    std::vector<Achievement> achs;   // play-log rules only (definition + universal per-game)
    std::set<std::string> done;
    bool queried = false;            // first play-log read done
    uint64_t startedAt = 0, nextCheck = 0;
    uint64_t baseNs = 0, baseAt = 0; // play time from the log at baseAt
    double launches = 0;
    double lastHours = -1, lastLaunches = -1;
    uint64_t liveWrittenAt = 0;
} S;

// universal.json: the SD card's copy wins (like the app), else the one built into the module
void loadUniversalPerGame(std::vector<Achievement>& out) {
    std::vector<Achievement> global;
    std::string err;
    std::vector<uint8_t> buf;
    if (readFile(dataDir() + "/universal.json", buf)) {
        buf.push_back(0);
        if (parseUniversal((const char*)buf.data(), out, global, err)) return;
    }
    std::string text((const char*)universal_json, universal_json_size);
    parseUniversal(text.c_str(), out, global, err);
}

bool readStats(uint64_t tid, PdmPlayStatistics& st) {
    AccountUid uid = {};
    if (!g_pdm || lastUser(&uid).empty()) return false;
    memset(&st, 0, sizeof(st));
    return R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(tid, uid, false, &st));
}

void writeLive(double hours, double launches) {
    std::map<std::string, double> vals;
    for (const Achievement& a : S.achs) vals[a.id] = a.rule.type == RuleType::PlaytimeHours ? hours : launches;
    std::string path = ov::livePath(dataDir(), S.tid);
    std::vector<uint8_t> old;
    readFile(path, old);
    mkdir((dataDir() + "/overlay").c_str(), 0777);
    writeFile(path, ov::mergeLive(std::string(old.begin(), old.end()), vals));
}
}  // namespace

void playlogInit(void (*logFn)(const char*, ...)) {
    g_log = logFn;
    Result rc = pdmqryInitialize();
    g_pdm = R_SUCCEEDED(rc);
    if (!g_pdm && g_log) g_log("play log: pdm:qry unavailable 0x%X - play time popups off", rc);
}

void playlogStop() { S = State(); }

void playlogStart(uint64_t tid, uint64_t now) {
    S = State();
    if (!g_pdm) return;
    GameDef def;
    std::string src;
    bool hasDef = findDef(tid, def, src, g_log);
    for (const Achievement& a : def.achs) if (isPlayStatsRule(a.rule.type)) S.achs.push_back(a);
    std::vector<Achievement> universal;
    loadUniversalPerGame(universal);
    for (const Achievement& a : universal) if (isPlayStatsRule(a.rule.type)) S.achs.push_back(a);
    if (S.achs.empty()) return;
    std::string user = lastUser();
    char pre[24];
    snprintf(pre, sizeof(pre), "%016llX/", (unsigned long long)tid);
    if (!user.empty())
        for (const auto& kv : loadStore(user))
            if (kv.first.compare(0, 17, pre) == 0) S.done.insert(kv.first.substr(17));
    size_t pending = 0;
    for (const Achievement& a : S.achs) if (!S.done.count(a.id)) pending++;
    if (!pending) { S = State(); return; }
    S.tid = tid;
    S.name = hasDef && !def.name.empty() ? def.name : "";
    S.startedAt = now;
    S.nextCheck = now + 15;   // the play log records the launch as the game starts: give it a moment
    if (g_log) g_log("play log: %u play time / launch achievements to go", (unsigned)pending);
}

void playlogTick(uint64_t now) {
    if (!S.tid || now < S.nextCheck) return;
    S.nextCheck = now + 60;
    PdmPlayStatistics st;
    if (!readStats(S.tid, st)) { if (!S.queried && g_log) g_log("play log: no statistics yet"); return; }
    // The log may count the running session only when it ends: play time = the larger of what the log says now
    // and what it said at the first read plus the time since.
    if (!S.queried) { S.queried = true; S.baseNs = st.playtime; S.baseAt = now; }
    double logged = st.playtime / 3.6e12;
    double grown = (S.baseNs + (now - S.baseAt) * 1000000000ULL) / 3.6e12;
    double hours = logged > grown ? logged : grown;
    S.launches = st.total_launches;
    std::vector<const Achievement*> met = playStatsMet(S.achs, hours, S.launches, S.done);
    if (!met.empty()) {
        std::string user = lastUser();
        if (!user.empty()) {
            char pre[24];
            snprintf(pre, sizeof(pre), "%016llX/", (unsigned long long)S.tid);
            std::map<std::string, uint64_t> store = loadStore(user);   // re-read: the app may have changed it
            bool changed = false;
            for (const Achievement* a : met) {
                S.done.insert(a->id);
                if (store.count(pre + a->id)) continue;
                store[pre + a->id] = now;
                changed = true;
                if (g_log) g_log("UNLOCKED %s (%s) %s %.2f >= %g [play log]", a->id.c_str(), a->name.c_str(),
                                 a->rule.type == RuleType::PlaytimeHours ? "hours" : "launches",
                                 a->rule.type == RuleType::PlaytimeHours ? hours : S.launches, a->rule.target);
                PopupRequest pr;
                pr.titleId = S.tid; pr.achId = a->id; pr.title = a->name; pr.desc = a->desc; pr.game = S.name;
                pr.points = a->points; pr.tier = popupTierFor(a->tier.empty() ? nullptr : a->tier.c_str(), a->points);
                pr.hidden = a->hidden;
                popupEnqueue(pr);
            }
            if (changed) saveStore(user, store);
        }
    }
    // Progress for the Tesla overlay: at the first read, then every 5 minutes (the hours grow every minute)
    if ((S.lastHours < 0 || now - S.liveWrittenAt >= 300 || S.launches != S.lastLaunches) &&
        (hours != S.lastHours || S.launches != S.lastLaunches)) {
        writeLive(hours, S.launches);
        S.lastHours = hours;
        S.lastLaunches = S.launches;
        S.liveWrittenAt = now;
    }
    bool pending = false;
    for (const Achievement& a : S.achs) if (!S.done.count(a.id)) pending = true;
    if (!pending) {
        if (g_log) g_log("play log: every play time / launch achievement of this game is unlocked");
        S.tid = 0;   // nothing left to watch for this session
    }
}
