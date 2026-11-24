#include "closescan.h"
#include "popup.h"
#include "popup_draw.h"
#include "rules.h"
#include "store.h"
#include "games_bin.h"
#include "overlay_data.h"
#include <malloc.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ctime>

extern "C" char* fake_heap_end;   // current end of the module heap (heap.cpp: 2 MB idle, 4 MB when it could grow)

// Free bytes in the heap as it is now (free blocks + never used tail), minus a safety margin.
size_t moduleMemoryAvailable() {
    struct mallinfo mi = mallinfo();
    char* top = (char*)sbrk(0);
    size_t tail = (top && fake_heap_end > top) ? (size_t)(fake_heap_end - top) : 0;
    size_t total = (size_t)mi.fordblks + tail;
    const size_t margin = 192 * 1024;
    return total > margin ? total - margin : 0;
}

// ---------------------------------------------------------------- save access
// Streaming source: small reads, never more in memory than the heap can take.
class SysSaveSource : public SaveSource {
public:
    explicit SysSaveSource(const AccountUid& uid) : m_uid(uid) {}
    ~SysSaveSource() override { close(); }
    bool open(uint64_t titleId) override {
        close();
        Result rc = fsdevMountSaveDataReadOnly("acsave", titleId, m_uid);
        m_mounted = R_SUCCEEDED(rc);
        m_rc = rc;
        if (!m_mounted) {
            char b[120];
            snprintf(b, sizeof(b), "mount failed: rc=0x%X%s", rc, rc == 0x7D402 ? " = no save data for this user" : "");
            m_err = b;
        }
        return m_mounted;
    }
    void close() override {
        if (m_file) { fclose(m_file); m_file = nullptr; m_fileRel.clear(); }
        if (m_mounted) fsdevUnmountDevice("acsave");
        m_mounted = false;
    }
    std::string lastError() override { return m_err; }
    Result lastRc() const { return m_rc; }
    bool streaming() const override { return true; }
    bool fileSize(const std::string& rel, uint64_t& size) override {
        struct stat st;
        if (!m_mounted || stat(path(rel).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
        size = (uint64_t)st.st_size;
        return true;
    }
    bool readAt(const std::string& rel, uint64_t off, void* buf, size_t len) override {
        if (!m_mounted) return false;
        if (!m_file || m_fileRel != rel) {   // keep the current file open: binary_le reads many small values
            if (m_file) fclose(m_file);
            m_file = fopen(path(rel).c_str(), "rb");
            m_fileRel = m_file ? rel : "";
            if (!m_file) return false;
        }
        return fseek(m_file, (long)off, SEEK_SET) == 0 && fread(buf, 1, len, m_file) == len;
    }
    bool readAll(const std::string& rel, std::vector<uint8_t>& out) override {
        uint64_t size;
        if (!fileSize(rel, size) || size + 64 * 1024 > memoryAvailable()) return false;
        return readFile(path(rel), out);
    }
    size_t memoryAvailable() override { return moduleMemoryAvailable(); }

private:
    std::string path(const std::string& rel) const { return "acsave:/" + rel; }
    AccountUid m_uid;
    bool m_mounted = false;
    Result m_rc = 0;
    std::string m_err;
    FILE* m_file = nullptr;
    std::string m_fileRel;
};

// ---------------------------------------------------------------- definitions
// games.bin = for each romfs/games file: "<file name>\n<JSON text>\0"
static bool builtinDef(uint64_t tid, GameDef& out, std::string& err) {
    char want[24];
    snprintf(want, sizeof(want), "%016llX.json", (unsigned long long)tid);
    const char* p = (const char*)games_bin;
    const char* end = p + games_bin_size;
    while (p < end) {
        const char* nl = (const char*)memchr(p, '\n', end - p);
        if (!nl) break;
        const char* text = nl + 1;
        const char* z = (const char*)memchr(text, '\0', end - text);
        if (!z) break;
        if ((size_t)(nl - p) == strlen(want) && strncasecmp(p, want, nl - p) == 0) return parseGameDef(text, out, err);
        p = z + 1;
    }
    return false;
}

// Case-insensitive substring test (newlib has no strcasestr).
static bool containsNoCase(const char* hay, const char* needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++) if (strncasecmp(hay, needle, n) == 0) return true;
    return false;
}

bool findDef(uint64_t tid, GameDef& out, std::string& source, void (*logf)(const char*, ...)) {
    std::string err;
    bool found = builtinDef(tid, out, err);
    if (found) source = "built-in";
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llX", (unsigned long long)tid);
    std::string dir = dataDir() + "/games";
    for (const std::string& f : listDir(dir)) {   // SD overrides, like the app (later files win)
        if (f.size() < 6 || f.substr(f.size() - 5) != ".json") continue;
        std::vector<uint8_t> buf;
        if (!readFile(dir + "/" + f, buf)) continue;
        buf.push_back(0);
        if (!containsNoCase((const char*)buf.data(), hex)) continue;   // cheap filter before parsing
        GameDef d;
        std::string e;
        if (parseGameDef((const char*)buf.data(), d, e) && d.id == tid) { out = std::move(d); found = true; source = dir + "/" + f; err = e; }
    }
    if (!err.empty()) logf("close scan: definition warnings: %s", err.c_str());
    return found;
}

// ---------------------------------------------------------------- unlocks
int unlockMet(const Game& g, const GameDef& def, const std::string& defSource, const AccountUid& uid, const char* tag,
              unsigned long long ms, const char* reportName, void (*logf)(const char*, ...)) {
    const bool live = strcmp(tag, "live") == 0;
    std::string user = userKeyOf(uid);
    // Unlock what is met (re-read the store right before writing: the app or the live mode may have changed it)
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "%016llX/", (unsigned long long)g.id);
    std::map<std::string, uint64_t> store = loadStore(user);
    std::vector<const Achievement*> fresh;
    int met = 0, saveAchs = 0;
    uint64_t now = (uint64_t)time(NULL);
    for (const Achievement& a : g.achs) {
        if (!isSaveRule(a.rule.type)) continue;
        saveAchs++;
        if (a.value + 1e-9 < a.rule.target) continue;
        met++;
        std::string key = prefix + a.id;
        if (store.count(key)) continue;
        store[key] = now;
        fresh.push_back(&a);
    }
    if (!fresh.empty()) saveStore(user, store);

    // Progress for the Tesla overlay (<data>/overlay/<TID>.live.json): the values just read from the save
    if (g.saveStatus == SaveStatus::Ok && saveAchs > 0) {
        std::map<std::string, double> vals;
        for (const Achievement& a : g.achs) if (isSaveRule(a.rule.type)) vals[a.id] = a.value;
        std::string path = ov::livePath(dataDir(), g.id);
        std::vector<uint8_t> old;
        readFile(path, old);
        mkdir((dataDir() + "/overlay").c_str(), 0777);
        writeFile(path, ov::mergeLive(std::string(old.begin(), old.end()), vals));
    }

    // Full report for the X debug view / troubleshooting, overwritten each time
    std::string rep = std::string(live ? "Live save read: " : "Scan on close: ") + def.name + " (definition: " + defSource + ", user " + user + ")\n";
    for (const std::string& l : g.debug) rep += l + "\n";
    for (const Achievement* a : fresh) rep += "NEW UNLOCK: " + a->id + " (" + a->name + ")\n";
    writeFile(dataDir() + "/" + reportName, rep);

    const char* status = g.saveStatus == SaveStatus::Ok ? "save read" : g.saveStatus == SaveStatus::NoSave ? "no save" : "save NOT recognised";
    logf("%s: %s - %s in %llu ms, %d/%d met, %u new", live ? "live save" : "close scan", def.name.c_str(), status, ms, met,
         saveAchs, (unsigned)fresh.size());

    // Popups: a first scan of an advanced save can unlock dozens at once -> 3 cards + a summary
    const size_t SHOW = fresh.size() > 4 ? 3 : fresh.size();
    for (size_t i = 0; i < fresh.size(); i++) {
        const Achievement& a = *fresh[i];
        logf("UNLOCKED %s (%s) value %g >= %g [%s]", a.id.c_str(), a.name.c_str(), a.value, a.rule.target, tag);
        if (i >= SHOW) continue;
        PopupRequest pr;
        pr.titleId = g.id; pr.achId = a.id; pr.title = a.name; pr.desc = a.desc; pr.game = def.name;
        pr.points = a.points; pr.tier = popupTierFor(a.tier.empty() ? nullptr : a.tier.c_str(), a.points);
        popupEnqueue(pr);
    }
    if (fresh.size() > SHOW) {
        PopupRequest pr;
        int pts = 0;
        for (size_t i = SHOW; i < fresh.size(); i++) pts += fresh[i]->points;
        char t[64];
        snprintf(t, sizeof(t), "+%u more achievements", (unsigned)(fresh.size() - SHOW));
        pr.titleId = g.id; pr.achId = "_summary"; pr.title = t; pr.desc = "Open TrophyNX to see them all";
        pr.game = def.name; pr.points = pts; pr.tier = popupTierFor(nullptr, 0);
        popupEnqueue(pr);
    }
    return (int)fresh.size();
}

// ---------------------------------------------------------------- scan
CloseScanResult closeScan(uint64_t tid, const AccountUid& uid, void (*logf)(const char*, ...)) {
    GameDef def;
    std::string defSource;
    if (!findDef(tid, def, defSource, logf)) return CS_SKIPPED;   // no definition: nothing to read
    if (def.reader.empty()) return CS_SKIPPED;                     // play-log achievements only (the app handles them)
    if (!def.closeScan) { logf("close scan: %016llX skipped (\"close_scan\": false)", (unsigned long long)tid); return CS_SKIPPED; }

    Game g;
    g.id = tid;
    g.name = def.name;
    applyGameDef(g, def);
    u64 t0 = armGetSystemTick();
    {
        SysSaveSource src(uid);
        evaluateSave(g, src);
        if (g.saveStatus == SaveStatus::NoSave && src.lastRc() != 0x7D402) {
            logf("close scan: %s - %s", def.name.c_str(), src.lastError().c_str());
            return CS_LOCKED;
        }
    }
    u64 ms = armTicksToNs(armGetSystemTick() - t0) / 1000000;
    unlockMet(g, def, defSource, uid, "on close", ms, "close_log.txt", logf);
    return CS_DONE;
}
