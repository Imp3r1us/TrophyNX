#include "achievements.h"
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

// The app reads saves through the platform layer, whole files at a time.
class PlatSaveSource : public SaveSource {
public:
    bool open(uint64_t titleId) override { return plat::openSave(titleId); }
    void close() override { plat::closeSave(); }
    std::string lastError() override { return plat::lastError(); }
    bool readAll(const std::string& rel, std::vector<uint8_t>& out) override { return plat::readSaveFile(rel, out); }
};

static std::string fmtd(double v) {
    char b[32];
    if (v == (long long)v) snprintf(b, sizeof(b), "%lld", (long long)v);
    else snprintf(b, sizeof(b), "%.2f", v);
    return b;
}

static cJSON* loadJson(const std::string& path) {
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) return nullptr;
    buf.push_back(0);
    return cJSON_Parse((const char*)buf.data());
}

// ---------------------------------------------------------------- unlock store
static std::string storePath() { return plat::dataDir() + "/unlocks/" + plat::userKey() + ".json"; }

static void loadStore(std::map<std::string, uint64_t>& store) {
    store.clear();
    cJSON* root = loadJson(storePath());
    if (!root) return;
    cJSON* it;
    cJSON_ArrayForEach(it, root) if (cJSON_IsNumber(it) && it->string) store[it->string] = (uint64_t)it->valuedouble;
    cJSON_Delete(root);
}

static void saveStore(const std::map<std::string, uint64_t>& store) {
    makeDirs(plat::dataDir() + "/unlocks");
    std::string out = "{\n";
    size_t i = 0;
    for (auto& kv : store) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)kv.second);
        out += "  \"" + kv.first + "\": " + buf + (++i < store.size() ? ",\n" : "\n");
    }
    out += "}\n";
    writeFile(storePath(), out);
}

static void applyUnlocksGame(Game& g, bool global, std::map<std::string, uint64_t>& store, bool& changed) {
    g.unlocked = g.points = g.maxPoints = 0;
    char prefix[32];
    if (global) snprintf(prefix, sizeof(prefix), "global/");
    else snprintf(prefix, sizeof(prefix), "%016llX/", (unsigned long long)g.id);
    for (Achievement& a : g.achs) {
        std::string key = std::string(prefix) + a.id;
        auto it = store.find(key);
        if (it != store.end()) {
            a.unlocked = true;
            a.unlockTime = it->second;
        } else if (a.value + 1e-9 >= a.rule.target) {
            a.unlocked = a.isNew = true;   // unlocks are permanent, even if the save changes later
            a.unlockTime = plat::now();
            store[key] = a.unlockTime;
            changed = true;
            g.debug.push_back("NEW UNLOCK: " + a.id + " (" + a.name + ")");
        }
        g.maxPoints += a.points;
        if (a.unlocked) { g.unlocked++; g.points += a.points; }
    }
}

// Tail of the background module's log (sys-trophynx), so it can be read on the console.
static void appendLiveLog(std::vector<std::string>& out) {
    std::vector<uint8_t> buf;
    out.push_back("");
    if (!readFile(plat::dataDir() + "/live_log.txt", buf)) {
        out.push_back("Live module: no log yet (sys-trophynx not installed, or not started since boot).");
        return;
    }
    std::string txt(buf.begin(), buf.end());
    std::vector<std::string> lines;
    size_t st = 0, e;
    while ((e = txt.find('\n', st)) != std::string::npos) { lines.push_back(txt.substr(st, e - st)); st = e + 1; }
    size_t from = lines.size() > 25 ? lines.size() - 25 : 0;
    out.push_back("Live module log (last " + std::to_string(lines.size() - from) + " lines):");
    for (size_t i = from; i < lines.size(); i++) out.push_back("  " + lines[i]);
}

// Full scan report, overwritten each scan: sdmc:/switch/TrophyNX/log.txt
static void writeLog(const Library& lib) {
    std::string out;
    time_t t = (time_t)plat::now();
    char tb[64];
    strftime(tb, sizeof(tb), "%Y-%m-%d %H:%M:%S UTC", gmtime(&t));
    out += std::string("TrophyNX scan log - ") + tb + "\n\n";
    for (size_t gi = 0; gi < lib.games.size(); gi++) {
        const Game& g = lib.games[gi];
        if (gi > 0 && !g.hasDefinition && g.stats.launches == 0) continue;   // keep the log short
        out += "== " + g.name + "  [" + std::to_string(g.unlocked) + "/" + std::to_string(g.achs.size()) + "]\n";
        for (const std::string& l : g.debug) out += l + "\n";
        out += "\n";
    }
    makeDirs(plat::dataDir());
    writeFile(plat::dataDir() + "/log.txt", out);
}

static void evaluateAll(Library& lib, plat::ProgressFn progress, void* ctx) {
    std::map<std::string, uint64_t> store;
    loadStore(store);
    for (Game& g : lib.games) for (Achievement& a : g.achs) { a.isNew = false; a.unlocked = false; a.unlockTime = 0; }

    int n = (int)lib.games.size();
    double totalHours = 0;
    int played = 0;
    for (int i = 1; i < n; i++) {
        if (progress) progress(i, n, ctx);
        Game& g = lib.games[i];
        g.debug.clear();
        g.stats = plat::playStats(g.id);
        char idb[24];
        snprintf(idb, sizeof(idb), "%016llX", (unsigned long long)g.id);
        g.debug.push_back(std::string("Title ID ") + idb);
        g.debug.push_back(g.stats.valid ? "Play log: " + fmtd(floor(g.stats.playtimeNs / 3.6e10) / 100.0) + " h, " + std::to_string(g.stats.launches) + " launches"
                                        : "Play log: unavailable (pdm query failed)");
        double hours = g.stats.playtimeNs / 3.6e12;
        if (g.stats.launches > 0) played++;
        totalHours += hours;
        for (Achievement& a : g.achs) {
            if (a.rule.type == RuleType::PlaytimeHours) a.value = hours;
            else if (a.rule.type == RuleType::Launches) a.value = g.stats.launches;
        }
        PlatSaveSource src;
        evaluateSave(g, src);
    }

    bool changed = false;
    int unlockedInGames = 0;
    for (int i = 1; i < n; i++) {
        applyUnlocksGame(lib.games[i], false, store, changed);
        unlockedInGames += lib.games[i].unlocked;
    }
    // Global achievements depend on the per-game results.
    Game& ov = lib.games[0];
    ov.debug.clear();
    ov.debug.push_back("User: " + plat::userName() + "  (" + plat::userKey() + ")");
    ov.debug.push_back("Installed games: " + std::to_string(n - 1) + ", played: " + std::to_string(played) + ", total " + fmtd(floor(totalHours * 10) / 10) + " h");
    int withSave = 0;
    for (const Game& g : lib.games) if (!g.reader.empty()) withSave++;
    ov.debug.push_back("Definitions loaded: " + std::to_string(lib.defsLoaded.size()) + " files (" + std::to_string(withSave) +
                       " installed games with save support)");
    if (!lib.loadErrors.empty()) {
        ov.debug.push_back("Definition errors:");
        size_t st = 0, e;
        while ((e = lib.loadErrors.find('\n', st)) != std::string::npos) { ov.debug.push_back("  " + lib.loadErrors.substr(st, e - st)); st = e + 1; }
    }
    ov.debug.push_back("Unlock file: " + plat::dataDir() + "/unlocks/" + plat::userKey() + ".json");
    for (Achievement& a : ov.achs) {
        if (a.rule.type == RuleType::GamesPlayed) a.value = played;
        else if (a.rule.type == RuleType::TotalHours) a.value = totalHours;
        else if (a.rule.type == RuleType::UnlockedTotal) a.value = unlockedInGames;
    }
    applyUnlocksGame(ov, true, store, changed);
    if (changed) saveStore(store);
    ov.debug.push_back(changed ? "Unlock file updated." : "No new unlocks this scan.");
    ov.debug.push_back("");
    ov.debug.push_back("Live module (sys-trophynx): " + lib.liveStatus);
    appendLiveLog(ov.debug);
    for (Game& g : lib.games) if (g.id == 0x01007EF00011E000ULL) {
        g.debug.push_back("");
        g.debug.push_back("Live module (sys-trophynx): " + lib.liveStatus);
        appendLiveLog(g.debug);
    }

    // Order: games with their own achievements first, then most recently played.
    if (n > 2) {
        std::stable_sort(lib.games.begin() + 1, lib.games.end(), [](const Game& a, const Game& b) {
            if (a.hasDefinition != b.hasDefinition) return a.hasDefinition;
            return a.stats.lastPlayed > b.stats.lastPlayed;
        });
    }
    lib.totalUnlocked = lib.totalCount = lib.totalPoints = 0;
    lib.newlyUnlocked.clear();
    for (size_t gi = 0; gi < lib.games.size(); gi++) {
        Game& g = lib.games[gi];
        lib.totalCount += (int)g.achs.size();
        lib.totalUnlocked += g.unlocked;
        lib.totalPoints += g.points;
        for (size_t ai = 0; ai < g.achs.size(); ai++)
            if (g.achs[ai].isNew) lib.newlyUnlocked.push_back({(int)gi, (int)ai});
    }
    writeLog(lib);
}

void buildLibrary(Library& lib, plat::ProgressFn progress, void* ctx) {
    lib = Library();
    std::map<uint64_t, GameDef> defs;
    setSaveKeyDir(plat::dataDir() + "/keys");   // passwords of encrypted saves (keys/theatrhythm.txt...), never shipped
    loadGameDefs(plat::romfsDir() + "/games", defs, lib.loadErrors, lib.defsLoaded);
    loadGameDefs(plat::dataDir() + "/games", defs, lib.loadErrors, lib.defsLoaded);   // SD card overrides/additions
    std::vector<Achievement> perGame, global;
    loadUniversal(plat::romfsDir() + "/universal.json", perGame, global, lib.loadErrors);
    loadUniversal(plat::dataDir() + "/universal.json", perGame, global, lib.loadErrors);

    lib.liveStatus = plat::liveModuleStatus();   // check/start early so it has time to boot before we read its log
    Game overview;
    overview.name = "Overview";
    overview.achs = global;
    lib.games.push_back(overview);

    std::vector<GameInfo> infos;
    plat::listGames(infos, progress, ctx);
    for (GameInfo& gi : infos) {
        Game g;
        g.id = gi.id;
        g.name = gi.name;
        g.iconJpeg = std::move(gi.iconJpeg);
        auto it = defs.find(g.id);
        if (it != defs.end() && !it->second.name.empty() &&
            (!isCleanText(g.name) || (g.name.size() == 16 && g.name.find_first_not_of("0123456789ABCDEF") == std::string::npos)))
            g.name = it->second.name;   // console gave no usable name
        if (it != defs.end()) applyGameDef(g, it->second);
        g.achs.insert(g.achs.end(), perGame.begin(), perGame.end());
        lib.games.push_back(std::move(g));
    }
    evaluateAll(lib, progress, ctx);
}

static std::string storeKey(const Library& lib, int gi, const Achievement& a) {
    char prefix[32];
    if (gi == 0) snprintf(prefix, sizeof(prefix), "global/");
    else snprintf(prefix, sizeof(prefix), "%016llX/", (unsigned long long)lib.games[gi].id);
    return std::string(prefix) + a.id;
}

int relockAchievements(Library& lib, int gameIndex, int achIndex) {
    std::map<std::string, uint64_t> store;
    loadStore(store);
    int removed = 0;
    for (int gi = 0; gi < (int)lib.games.size(); gi++) {
        if (gameIndex >= 0 && gi != gameIndex) continue;
        Game& g = lib.games[gi];
        for (int ai = 0; ai < (int)g.achs.size(); ai++) {
            if (achIndex >= 0 && ai != achIndex) continue;
            Achievement& a = g.achs[ai];
            if (store.erase(storeKey(lib, gi, a))) removed++;
            if (a.unlocked) { g.unlocked--; g.points -= a.points; lib.totalUnlocked--; lib.totalPoints -= a.points; }
            a.unlocked = a.isNew = false;
            a.unlockTime = 0;
        }
        g.debug.push_back("RELOCKED " + std::string(achIndex >= 0 ? g.achs[achIndex].id : "all achievements") + " (rescan with Y to unlock again)");
    }
    if (gameIndex < 0) {   // also drop keys of games that are no longer installed
        removed += (int)store.size();
        store.clear();
    }
    saveStore(store);
    return removed;
}

void rescanLibrary(Library& lib, plat::ProgressFn progress, void* ctx) {
    for (Game& g : lib.games) for (Achievement& a : g.achs) a.value = 0;
    evaluateAll(lib, progress, ctx);
}
