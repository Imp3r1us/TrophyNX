// sys-trophynx: background module that watches games' memory while you play (like RetroAchievements),
// unlocks achievements live and shows popups. BotW: flag objects found by their layout (flagscan.cpp).
// Any game: "memory" rules, values at per-build addresses (include/memrules.h). For every game with a
// save reader it also reads the save each time the game saves, from the save image in NAND ("live save",
// livesave.cpp), and once more as soon as the game closes ("scan on close", closescan.cpp).
#include <switch.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <malloc.h>
#include <map>
#include <unistd.h>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>
#include "cJSON.h"
#include "dmntcht.h"
#include "flagscan.h"
#include "popup.h"
#include "popup_draw.h"
#include "botw_json.h"
#include "saveprobe.h"
#include "closescan.h"
#include "livesave.h"
#include "capture.h"
#include "store.h"
#include "heap.h"
#include "memrules.h"
#include "overlay_data.h"
#include "playlog.h"

#define MODULE_VERSION "0.13.0"
static const u64 BOTW_ID = 0x01007EF00011E000ULL;

extern "C" {
// Sysmodule setup: no applet, small fixed heap.
u32 __nx_applet_type = AppletType_None;
// nv's default transfer memory is 8 MB; the popup layer needs very little (same value as Tesla).
u32 __nx_nv_transfermem_size = 0x15000;
void __libnx_init_time(void);
u32 __nx_fs_num_sessions = 1;
// Heap: __libnx_initheap is in heap.cpp (2 MB while idle, 4 MB only while a popup or a save read needs it).

void __appInit(void) {
    if (R_FAILED(smInitialize())) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }
    if (R_SUCCEEDED(timeInitialize())) __libnx_init_time();
    if (R_FAILED(fsInitialize())) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    fsdevMountSdmc();
}

void __appExit(void) {
    fsdevUnmountAll();
    fsExit();
    timeExit();
    smExit();
}
}

// ---------------------------------------------------------------- log
static void logf(const char* fmt, ...) {
    char path[128];
    snprintf(path, sizeof(path), "%s/live_log.txt", dataDir().c_str());
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 256 * 1024) {   // keep it small: rotate once
        char old[140];
        snprintf(old, sizeof(old), "%s.old", path);
        remove(old);
        rename(path, old);
    }
    FILE* f = fopen(path, "a");
    if (!f) return;
    time_t t = time(NULL);
    char tb[32];
    strftime(tb, sizeof(tb), "%m-%d %H:%M:%S", gmtime(&t));
    fprintf(f, "[%s] ", tb);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---------------------------------------------------------------- definitions
static uint32_t crc32str(const char* s) {
    uint32_t crc = 0xFFFFFFFFu;
    for (; *s; s++) {
        crc ^= (uint8_t)*s;
        for (int k = 0; k < 8; k++) crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}
static uint32_t hashOf(const char* s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return (uint32_t)strtoul(s, nullptr, 16);
    return crc32str(s);
}

enum RuleKind { R_FLAG, R_COUNT, R_SUM };
struct LiveAch {
    std::string id, name, desc;
    int points = 10, tier = 0;
    RuleKind kind = R_FLAG;
    std::vector<uint32_t> hashes;
    double target = 1;
    bool hasEquals = false;
    uint32_t equals = 0;
};

static std::vector<LiveAch> loadDefinition(std::string& gameName) {
    std::vector<LiveAch> out;
    std::string txt((const char*)botw_json, botw_json_size);
    cJSON* root = cJSON_Parse(txt.c_str());
    if (!root) { logf("ERROR: embedded definition is not valid JSON"); return out; }
    cJSON* n = cJSON_GetObjectItem(root, "name");
    if (cJSON_IsString(n)) gameName = n->valuestring;
    cJSON* a;
    cJSON_ArrayForEach(a, cJSON_GetObjectItem(root, "achievements")) {
        cJSON* r = cJSON_GetObjectItem(a, "rule");
        cJSON* type = cJSON_GetObjectItem(r, "type");
        if (!cJSON_IsString(type)) continue;
        LiveAch x;
        std::string t = type->valuestring;
        if (t == "flag" || t == "value") x.kind = R_FLAG;
        else if (t == "count") x.kind = R_COUNT;
        else if (t == "sum") x.kind = R_SUM;
        else continue;   // play-time rules are handled by the app
        x.id = cJSON_GetObjectItem(a, "id")->valuestring;
        x.name = cJSON_GetObjectItem(a, "name")->valuestring;
        cJSON* p = cJSON_GetObjectItem(a, "points");
        if (cJSON_IsNumber(p)) x.points = p->valueint;
        cJSON* d = cJSON_GetObjectItem(a, "desc");
        if (cJSON_IsString(d)) x.desc = d->valuestring;
        cJSON* tr = cJSON_GetObjectItem(a, "tier");   // optional; default from points (<25 bronze, <50 silver, else gold)
        x.tier = popupTierFor(cJSON_IsString(tr) ? tr->valuestring : nullptr, x.points);
        cJSON* v;
        if (cJSON_IsString(v = cJSON_GetObjectItem(r, "name"))) x.hashes.push_back(hashOf(v->valuestring));
        if (cJSON_IsString(v = cJSON_GetObjectItem(r, "hash"))) x.hashes.push_back(hashOf(v->valuestring));
        cJSON* e;
        cJSON_ArrayForEach(e, cJSON_GetObjectItem(r, "names")) if (cJSON_IsString(e)) x.hashes.push_back(hashOf(e->valuestring));
        if (cJSON_IsString(v = cJSON_GetObjectItem(r, "pattern"))) {
            int from = cJSON_GetObjectItem(r, "from")->valueint, to = cJSON_GetObjectItem(r, "to")->valueint;
            for (int i = from; i <= to; i++) {
                char b[128];
                snprintf(b, sizeof(b), v->valuestring, i);
                x.hashes.push_back(crc32str(b));
            }
        }
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(r, "target"))) x.target = v->valuedouble;
        if ((v = cJSON_GetObjectItem(r, "equals"))) {
            x.hasEquals = true;
            x.equals = cJSON_IsString(v) ? hashOf(v->valuestring) : (uint32_t)v->valuedouble;
        }
        out.push_back(x);
    }
    cJSON_Delete(root);
    return out;
}

// ---------------------------------------------------------------- unlock store (shared with the app): store.cpp
static std::string userKey() { return lastUser(); }

// ---------------------------------------------------------------- memory access
// Normally through Atmosphère's dmnt:cht. When dmnt refuses to attach (it does on some launches,
// see the 0.3.x logs) we debug the game ourselves, sys-botbase style: attach, read, detach. While
// attached the game is paused, so attachments are kept very short (<= 30 ms).
static bool g_own = false;        // own-debugger mode for the current game session
static u64 g_ownPid = 0;
static Handle g_dbg = INVALID_HANDLE;
static u64 g_dbgTick = 0;

static bool ownAttach() {
    if (g_dbg != INVALID_HANDLE) return true;
    if (R_FAILED(svcDebugActiveProcess(&g_dbg, g_ownPid))) { g_dbg = INVALID_HANDLE; return false; }
    g_dbgTick = armGetSystemTick();
    return true;
}
static void ownDetach() {
    if (g_dbg != INVALID_HANDLE) { svcCloseHandle(g_dbg); g_dbg = INVALID_HANDLE; }   // closing resumes the game
}

struct DmntReader : MemReader {
    bool read(uint64_t addr, void* buf, size_t size) override {
        if (!g_own) return R_SUCCEEDED(dmntchtReadCheatProcessMemory(addr, buf, size));
        return ownAttach() && R_SUCCEEDED(svcReadDebugProcessMemory(buf, g_dbg, addr, size));
    }
};

static void scanYield() {
    if (g_own && g_dbg != INVALID_HANDLE && armTicksToNs(armGetSystemTick() - g_dbgTick) > 30000000ULL) {
        ownDetach();                  // let the game run between bursts
        svcSleepThread(15000000ULL);
        return;
    }
    svcSleepThread(100000);           // 0.1 ms between 64 KiB chunks: stay gentle on the CPU
}

// Own mode: mappings + main module range from the game's memory map. Main = 2nd static code
// region (after rtld), up to the next code region (covers its .rodata/.data, where vtables live).
static bool ownMemoryMap(std::vector<MemoryInfo>& out, uint64_t& mainBase, uint64_t& mainSize) {
    out.clear();
    if (!ownAttach()) return false;
    u64 addr = 0;
    std::vector<u64> code;
    while (true) {
        MemoryInfo mi; u32 pi;
        if (R_FAILED(svcQueryDebugProcessMemory(&mi, &pi, g_dbg, addr))) break;
        if (mi.type != MemType_Unmapped) out.push_back(mi);
        if (mi.type == MemType_CodeStatic && mi.perm == Perm_Rx) code.push_back(mi.addr);
        u64 next = mi.addr + mi.size;
        if (next <= addr) break;
        addr = next;
    }
    ownDetach();
    mainBase = mainSize = 0;
    if (code.size() >= 2) { mainBase = code[1]; mainSize = (code.size() >= 3 ? code[2] : code[1] + 0x4000000) - code[1]; }
    else if (code.size() == 1) { mainBase = code[0]; mainSize = 0x4000000; }
    return !out.empty();
}

static bool collectHeap(std::vector<MemRegion>& regions, uint64_t& total) {
    regions.clear();
    total = 0;
    std::vector<MemoryInfo> maps;
    u64 got = 0;
    if (g_own) {
        uint64_t b, sz;
        if (!ownMemoryMap(maps, b, sz)) return false;
        got = maps.size();
    } else {
        u64 count = 0;
        if (R_FAILED(dmntchtGetCheatProcessMappingCount(&count)) || count == 0) return false;
        maps.resize(count);
        if (R_FAILED(dmntchtGetCheatProcessMappings(maps.data(), count, 0, &got))) return false;
    }
    for (u64 i = 0; i < got; i++) {
        const MemoryInfo& m = maps[i];
        if (m.type == MemType_Heap && (m.perm & Perm_Rw) == Perm_Rw) {
            regions.push_back({m.addr, m.size});
            total += m.size;
        }
    }
    return !regions.empty();
}

// ---------------------------------------------------------------- pending app update
// The app can't replace its own .nro while it runs (it keeps the file open). Since 0.10.1 it releases it
// first; older versions left the new file next to it as "TrophyNX.nro.new". At boot the app isn't
// running yet, so finish that swap here (the .new file was written only after its SHA-256 matched).
static void finishPendingAppUpdate() {
    static const char* const APPS[] = {"sdmc:/switch/TrophyNX.nro", "sdmc:/switch/TrophyNX/TrophyNX.nro"};
    for (const char* app : APPS) {
        std::string nro = app, tmp = nro + ".new", old = nro + ".old";
        struct stat st;
        if (stat(tmp.c_str(), &st) != 0 || st.st_size < 4096) continue;
        remove(old.c_str());
        bool had = stat(nro.c_str(), &st) == 0;
        if (had && rename(nro.c_str(), old.c_str()) != 0) { logf("pending update: %s is in use, left %s", app, tmp.c_str()); continue; }
        if (rename(tmp.c_str(), nro.c_str()) != 0) {
            if (had) rename(old.c_str(), nro.c_str());
            logf("pending update: could not install %s", tmp.c_str());
            continue;
        }
        remove(old.c_str());
        logf("pending update: installed %s", app);
    }
}

// ---------------------------------------------------------------- safety
// The module only touches real, installed games. Homebrew started "as a game" (hbmenu as an
// application, forwarders, launchers like homeNX, title takeover with R) is ignored completely:
// no live mode, no save probe, no capture, no scan on close.
static bool isRealGameId(u64 tid) {
    // base application ids: 0x01xxxxxxxxxxx000 with the low 13 bits clear (patches are +0x800,
    // add-ons +0x1000...). Forwarders use other prefixes (0x05...), hbmenu ends in ...3000.
    return (tid >> 56) == 0x01 && (tid & 0x1FFF) == 0;
}

// Atmosphère extension pm:dmnt 65000 (AtmosphereGetProcessInfo): its override status says whether the
// application process is really hbloader (homebrew launched in place of / through a game).
static bool isHomebrewProcess(u64 pid) {
    struct { u64 program_id; u8 storage_id; u8 pad[7]; u64 keys_held; u64 flags; } out = {};
    Handle h = INVALID_HANDLE;
    Result rc = serviceDispatchInOut(pmdmntGetServiceSession(), 65000, pid, out,
        .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
        .out_handles = &h,
    );
    if (R_SUCCEEDED(rc) && h != INVALID_HANDLE) svcCloseHandle(h);   // never keep the process handle
    return R_SUCCEEDED(rc) && (out.flags & 1);                         // OverrideStatusFlag_Hbl
}

// Health check: usage of the "system" resource group (shared by every background module) read
// from our own resource limit, plus our own heap and open popup layers. Logged regularly; when a
// shared resource gets close to its limit the module goes into safe mode (no display layers, no
// save mounts, no probes) until it drops again, so it can never be the one that tips it over.
static const char* const RES_NAMES[5] = {"memory", "threads", "events", "transfer", "sessions"};
static Handle g_resLimit = INVALID_HANDLE;

static bool readUsage(s64 cur[5], s64 lim[5]) {
    if (g_resLimit == INVALID_HANDLE) {
        u64 h = 0;
        // InfoType_ResourceLimit takes INVALID_HANDLE (the kernel rejects any process handle, even the
        // pseudo-handle CUR_PROCESS_HANDLE, with InvalidHandle): it always means the current process.
        if (R_FAILED(svcGetInfo(&h, InfoType_ResourceLimit, INVALID_HANDLE, 0)) || h == 0) return false;
        g_resLimit = (Handle)h;   // kept for the whole run: one handle, opened once
    }
    for (int i = 0; i < 5; i++) {
        if (R_FAILED(svcGetResourceLimitCurrentValue(&cur[i], g_resLimit, (LimitableResource)i)) ||
            R_FAILED(svcGetResourceLimitLimitValue(&lim[i], g_resLimit, (LimitableResource)i))) return false;
    }
    return true;
}


// Returns true when the system is close to a limit (>= 90 %) on something the module's tasks use:
// sessions, events, transfer memory (popup layer: vi/nv; save reads: fs). Threads are logged but don't
// count: after start the module creates none (the popup thread exists from boot). Memory is checked by
// heapAcquire() right before each task that needs more of it.
static bool healthCheck(const char* why, bool log) {
    s64 cur[5] = {}, lim[5] = {};
    bool ok = readUsage(cur, lim);
    bool tight = false;
    std::string worst;
    if (ok) {
        for (int i = 1; i < 5; i++) {
            if (lim[i] > 0 && cur[i] * 10 >= lim[i] * 9) {
                worst += std::string(" ") + RES_NAMES[i];
                if (i != 1) tight = true;   // 1 = threads: high, but the module never adds any
            }
        }
    }
    if (log) {
        if (!ok) logf("health (%s): resource limit unreadable; heap free %u KB, popup layers open %d", why,
                      (unsigned)(heapFree() >> 10), popupLayersOpen());
        else logf("health (%s): sessions %lld/%lld, events %lld/%lld, threads %lld/%lld, transfer %lld/%lld, memory %lld/%lld MB "
                  "(system group); heap %u KB, free %u KB, popup layers open %d%s%s", why,
                  (long long)cur[4], (long long)lim[4], (long long)cur[2], (long long)lim[2], (long long)cur[1], (long long)lim[1],
                  (long long)cur[3], (long long)lim[3], (long long)(cur[0] >> 20), (long long)(lim[0] >> 20),
                  (unsigned)(heapSize() >> 10), (unsigned)(heapFree() >> 10), popupLayersOpen(), worst.empty() ? "" : " - high:", worst.c_str());
    }
    return tight;
}

// ---------------------------------------------------------------- main loop
int main(int, char**) {
    mkdir(dataDir().c_str(), 0777);
    logf("sys-trophynx " MODULE_VERSION " started");
    heapSetLog(logf);
    finishPendingAppUpdate();
    popupStart(logf);
    Result rc;
    if (R_FAILED(rc = pmdmntInitialize())) logf("ERROR pmdmnt init 0x%X", rc);
    if (R_FAILED(rc = pminfoInitialize())) logf("ERROR pminfo init 0x%X", rc);
    if (R_FAILED(rc = accountInitialize(AccountServiceType_Administrator))) logf("ERROR account init 0x%X", rc);
    bool dmnt = R_SUCCEEDED(rc = dmntchtInitialize());
    playlogInit(logf);
    if (!dmnt) logf("ERROR dmnt:cht init 0x%X (is Atmosphere's dmnt enabled?)", rc);

    std::string gameName = "Breath of the Wild";
    std::vector<LiveAch> achs = loadDefinition(gameName);
    std::vector<uint32_t> wanted;
    for (auto& a : achs) wanted.insert(wanted.end(), a.hashes.begin(), a.hashes.end());
    logf("loaded %u live achievements (%u flags) for %s", (unsigned)achs.size(), (unsigned)wanted.size(), gameName.c_str());

    FlagScanner scanner;
    scanner.setWanted(wanted);
    DmntReader mem;

    bool safe = healthCheck("start", true);
    popupSetSafeMode(safe);
    u64 lastHealth = (u64)time(NULL);
    bool curReal = false;

    u64 curPid = 0, curTid = 0;
    bool watching = false, ready = false;
    u64 seenAt = 0, lastScan = 0, lastEval = 0;
    int attempts = 0;
    bool attached = false, diagnosed = false;
    std::string user;
    // scan on close: the save of the game that just closed is read a few seconds later
    u64 closedTid = 0, closedAt = 0;
    int closedTries = 0, closedMinutes = 0;
    bool closedCaptured = false;
    AccountUid closedUid = {};
    u64 startedAt = 0;
    auto gameEnded = [&](u64 tid, u64 when) {
        if (!tid) return;
        AccountUid uid = {};
        if (lastUser(&uid).empty()) { logf("close scan: no user"); return; }
        closedTid = tid; closedUid = uid; closedAt = when + 3; closedTries = 0;
        closedMinutes = startedAt && when > startedAt ? (int)((when - startedAt + 30) / 60) : 0;
        closedCaptured = false;
    };
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "%016llX/", (unsigned long long)BOTW_ID);

    // ---- memory rules (any game with "type": "memory" achievements)
    GameDef memDef;                 // definition of the running game (memWatch points into its rules)
    mem::Watcher memWatch;
    mem::Bases memBases;
    std::string memBuild;
    bool memMode = false, memReading = false, memLogged = false;
    u64 memRetryAt = 0;
    int memTries = 0, memFailRounds = 0;
    std::map<std::string, double> memWritten;   // memory values last written for the overlay
    u64 memWrittenAt = 0;
    const mem::Reader memRead = [](uint64_t addr, void* out, size_t size) {
        return R_SUCCEEDED(dmntchtReadCheatProcessMemory(addr, out, size));
    };
    auto memStart = [&](u64 tid, u64 now) -> bool {
        std::string src;
        memDef = GameDef();
        if (!findDef(tid, memDef, src, logf)) return false;
        std::vector<mem::Watcher::Item> items;
        for (const Achievement& a : memDef.achs)
            if (a.rule.type == RuleType::Memory) items.push_back({a.id, &a.rule.mem});
        if (items.empty()) return false;
        user = userKey();
        char pre[24];
        snprintf(pre, sizeof(pre), "%016llX/", (unsigned long long)tid);
        std::set<std::string> done;
        if (!user.empty())
            for (const auto& kv : loadStore(user))
                if (kv.first.compare(0, 17, pre) == 0) done.insert(kv.first.substr(17));
        memWatch.reset(items, done);
        memWritten.clear();
        memWrittenAt = 0;
        if (!memWatch.pending()) {
            logf("memory: %s - all %u live achievements already unlocked", memDef.name.c_str(), (unsigned)items.size());
            return false;
        }
        // Not during the game's first 20 s: attaching while Atmosphere sets the game up is what made
        // dmnt crash for HomeNX (their issue #43).
        memRetryAt = now + 20;
        memTries = memFailRounds = 0;
        memReading = memLogged = false;
        logf("memory: %s (%s) - %u live achievements, %u to go; attaching in 20 s", memDef.name.c_str(), src.c_str(),
             (unsigned)items.size(), (unsigned)memWatch.pending());
        return true;
    };
    // Live memory test for the Tesla overlay (<data>/overlay/<TID>.memtest.json): version, coverage and each value.
    auto memTestWrite = [&](bool reading, const char* note) {
        ov::MemTest t;
        t.titleId = curTid;
        t.build = memBuild;
        t.reading = reading;
        t.note = note ? note : "";
        for (const Achievement& a : memDef.achs) {
            if (a.rule.type != RuleType::Memory || a.rule.mem.all.empty()) continue;
            t.total++;
            ov::MemTestItem i;
            i.id = a.id;
            const mem::Condition& c0 = a.rule.mem.all[0];
            i.cmp = c0.change != mem::Change::None ? mem::changeName(c0.change) : mem::cmpName(c0.cmp);
            i.target = c0.target;
            bool covered = a.rule.mem.covers(memBuild);
            if (covered) t.watched++;
            auto v = memWatch.lastValues().find(a.id);
            i.read = covered && v != memWatch.lastValues().end();
            i.value = i.read ? v->second : 0;
            i.holds = memWatch.lastHeld(a.id);
            i.done = memWatch.isDone(a.id);
            t.items.push_back(i);
        }
        mkdir((dataDir() + "/overlay").c_str(), 0777);
        writeFile(ov::memTestPath(dataDir(), curTid), ov::memTestJson(t));
    };
    auto memTestClear = [&](u64 tid) { if (tid) remove(ov::memTestPath(dataDir(), tid).c_str()); };
    auto memStop = [&](const char* why) {
        if (attached) dmntchtForceCloseCheatProcess();   // only when we opened it: never stop the player's cheats
        attached = memReading = memMode = false;
        logf("memory: stopped watching (%s)", why);
        memTestClear(curTid);
    };

    while (true) {
        svcSleepThread(1000000000ULL);
        u64 now = (u64)time(NULL);
        popupWatchdog();
        u64 pid = 0;
        bool running = R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)) && pid != 0;
        // health: every 30 min in the log, checked every minute (safe mode on/off)
        if (now - lastHealth >= 60 || now < lastHealth) {
            static int minutes = 0;
            bool log = (++minutes % 30) == 0;
            lastHealth = now;
            bool s = healthCheck("periodic", log);
            if (s != safe) {
                safe = s;
                popupSetSafeMode(safe);
                healthCheck(safe ? "safe mode on" : "safe mode off", true);
                logf(safe ? "SAFE MODE: system resources low - no popup layers, scans, live save or new memory watches until it recovers"
                          : "safe mode off: system resources back to normal");
            }
        }
        if (closedTid && now >= closedAt && popupIdle()) {   // after any popup: its buffers share the heap
            if (healthCheck("before close scan", true)) {   // fresh reading: the game that just closed freed a lot
                logf("close scan: %016llX skipped (system resources low)", (unsigned long long)closedTid);
                closedTid = 0;
                continue;
            }
            // 1) save capture (games the player records, see captures.h), 2) scan on close
            bool locked = false;
            bool big = heapAcquire("scan on close");   // without it: files that don't fit are skipped (logged)
            if (!closedCaptured) {
                if (captureOnClose(closedTid, closedUid, closedMinutes, logf) == CAPSTEP_LOCKED) locked = true;
                else closedCaptured = true;
            }
            if (!locked) locked = closeScan(closedTid, closedUid, logf) == CS_LOCKED;
            if (big) heapRelease();
            if (locked && ++closedTries < 4) closedAt = now + 5;   // save still in use: try again
            else { closedTid = 0; healthCheck("after close scan", true); }
        }
        if (!running) {
            if (curPid) {
                logf("game closed (pid %llu)", (unsigned long long)curPid);
                healthCheck("game closed", true);
                if (curReal) gameEnded(curTid, now);
            }
            liveSaveStop();
            playlogStop();
            if (memMode) memTestClear(curTid);
            memMode = memReading = false;
            if (attached) dmntchtForceCloseCheatProcess();   // never leave dmnt pointing at a dead process
            ownDetach(); g_own = false;
            curPid = 0; curTid = 0; watching = ready = attached = false;
            continue;
        }
        if (pid != curPid) {
            if (curPid) {
                logf("game changed (pid %llu -> %llu)", (unsigned long long)curPid, (unsigned long long)pid);
                if (memMode) memTestClear(curTid);
                if (curReal) gameEnded(curTid, now);
            }
            curPid = pid;
            u64 tid = 0;
            pminfoGetProgramId(&tid, pid);
            curTid = tid;
            diagnosed = false; startedAt = now;
            if (attached) { dmntchtForceCloseCheatProcess(); attached = false; }
            ownDetach(); g_own = false;
            bool hbl = isHomebrewProcess(pid);
            curReal = isRealGameId(tid) && !hbl;
            watching = curReal && (tid == BOTW_ID) && dmnt && !achs.empty();
            ready = false; attempts = 0; seenAt = now; lastScan = 0;
            logf("game started: %016llX%s", (unsigned long long)tid,
                 watching ? " -> watching (live achievements)"
                 : curReal ? "" : hbl ? " (homebrew through this game: ignored)" : " (homebrew/forwarder: ignored)");
            healthCheck("game started", true);   // system group usage at each launch: where crashes happen
            memReading = false;
            // homebrew / forwarders: no memory rules, no live save (same as the scan on close); not in safe mode
            memMode = curReal && !watching && dmnt && !safe && memStart(tid, now);
            if (curReal) liveSaveStart(tid, now, logf);
            if (curReal) playlogStart(tid, now);
            else playlogStop();
        }
        // Live save (every game with a save reader): reads the save image each time the game saves.
        // Paused in safe mode: each look opens the raw NAND storage (an fs session) and reads the save in our heap.
        if (!safe) liveSaveTick(now, popupIdle() && !closedTid, logf);
        playlogTick(now);   // play time / launches: popups while playing (every minute)
        if (memMode) {
            // 1) attach (dmnt:cht, read-only) and learn the build ID and region bases
            if (!memReading) {
                if (now < memRetryAt) continue;
                memRetryAt = now + 15;
                if (++memTries > 20) { memStop("dmnt never attached"); continue; }
                bool had = false;
                dmntchtHasCheatProcess(&had);   // the player's cheats already opened it: share it, never close it
                if (!had) {
                    if (R_FAILED(rc = dmntchtForceOpenCheatProcess())) {
                        dmntchtForceCloseCheatProcess();
                        svcSleepThread(2000000000ULL);
                        rc = dmntchtForceOpenCheatProcess();
                    }
                    if (R_FAILED(rc)) { logf("memory: attach failed 0x%X (try %d), again in 15 s", rc, memTries); continue; }
                    attached = true;
                }
                DmntCheatProcessMetadata md = {};
                if (R_FAILED(rc = dmntchtGetCheatProcessMetadata(&md)) || md.process_id != pid) {
                    logf("memory: metadata 0x%X / not the running game, again in 15 s", rc);
                    if (attached) { dmntchtForceCloseCheatProcess(); attached = false; }
                    continue;
                }
                memBuild = mem::buildHex(md.main_nso_build_id);
                memBases.main = md.main_nso_extents.base;
                memBases.heap = md.heap_extents.base;
                memBases.alias = md.alias_extents.base;
                size_t cov = memWatch.coverage(memBuild);
                logf("memory: %s build %s%s: %u achievements have addresses for this version", memDef.name.c_str(),
                     memBuild.c_str(), had ? " (sharing the player's cheats)" : "", (unsigned)cov);
                if (!cov) {
                    logf("memory: add this version under \"builds\" as \"%s\" to watch it", memBuild.c_str());
                    memStop("no addresses for this version");
                    memTestWrite(false, "no addresses for this version");   // the overlay shows which build to add
                    continue;
                }
                memReading = true;
                memTestWrite(true, nullptr);
                memFailRounds = 0;
            }
            // 2) every second: read, unlock what held 3 checks in a row
            std::vector<std::string> got = memWatch.step(memBuild, memBases, memRead);
            if (!memLogged) {   // first values, for definition authors
                memLogged = true;
                for (const auto& kv : memWatch.lastValues()) logf("  %s = %g", kv.first.c_str(), kv.second);
            }
            // Progress for the Tesla overlay: when values changed, at most every 5 s (SD writes)
            if (memWatch.lastValues() != memWritten && (now - memWrittenAt >= 5 || now < memWrittenAt)) {
                std::string path = ov::livePath(dataDir(), curTid);
                std::vector<uint8_t> old;
                readFile(path, old);
                mkdir((dataDir() + "/overlay").c_str(), 0777);
                writeFile(path, ov::mergeLive(std::string(old.begin(), old.end()), memWatch.lastValues()));
                memWritten = memWatch.lastValues();
                memWrittenAt = now;
                memTestWrite(true, nullptr);
            }
            if (memWatch.lastReadFailed()) {
                if (++memFailRounds == 30) logf("memory: some reads keep failing (loading screen, or an address that is wrong for this version)");
                bool has = false;
                if (memFailRounds % 10 == 0 && (R_FAILED(dmntchtHasCheatProcess(&has)) || !has)) {
                    logf("memory: dmnt lost the game, attaching again");
                    attached = memReading = false;
                    memRetryAt = now + 5;
                }
            } else memFailRounds = 0;
            if (!got.empty()) {
                if (user.empty()) user = userKey();
                if (!user.empty()) {
                    char pre[24];
                    snprintf(pre, sizeof(pre), "%016llX/", (unsigned long long)curTid);
                    std::map<std::string, uint64_t> store = loadStore(user);   // re-read: the app may have re-locked things
                    bool changed = false;
                    for (const std::string& id : got) {
                        const Achievement* a = nullptr;
                        for (const Achievement& x : memDef.achs) if (x.id == id) { a = &x; break; }
                        if (!a || store.count(pre + id)) continue;
                        store[pre + id] = now;
                        changed = true;
                        auto it = memWatch.lastValues().find(id);
                        const mem::Condition& c0 = a->rule.mem.all[0];
                        logf("UNLOCKED %s (%s) value %g %s %g%s%s [memory%s]", id.c_str(), a->name.c_str(),
                             it == memWatch.lastValues().end() ? 0.0 : it->second, mem::cmpName(c0.cmp), c0.target,
                             c0.change != mem::Change::None ? " " : "", mem::changeName(c0.change),
                             a->rule.mem.all.size() > 1 ? ", all conditions" : "");
                        PopupRequest pr;
                        pr.titleId = curTid; pr.achId = id; pr.title = a->name; pr.desc = a->desc; pr.game = memDef.name;
                        pr.points = a->points; pr.tier = popupTierFor(a->tier.empty() ? nullptr : a->tier.c_str(), a->points);
                        pr.hidden = a->hidden;
                        popupEnqueue(pr);
                    }
                    if (changed) saveStore(user, store);
                }
            }
            if (!memWatch.pending()) memStop("every live achievement is unlocked");
            continue;
        }
        if (!watching) continue;

        // 1) find the flag objects (after the game has had time to boot), retry until found
        if (!ready) {
            // every 30 s for the first 20 tries, then every 2 min (never give up while the game runs)
            if (now - seenAt < 25 || (lastScan && now - lastScan < (attempts >= 20 ? 120u : 30u))) continue;
            lastScan = now;
            attempts++;
            if (g_own) rc = 0;   // already using our own debugger this session
            else if (R_FAILED(rc = dmntchtForceOpenCheatProcess())) {
                // 0x32C80D = dmnt "CheatNotAttached": reset dmnt's state and try once more right away
                dmntchtForceCloseCheatProcess();
                svcSleepThread(2000000000ULL);
                rc = dmntchtForceOpenCheatProcess();
            }
            if (R_FAILED(rc)) {
                logf("attach failed 0x%X (try %d)", rc, attempts);
                if (!diagnosed) { diagnosed = true; diagnoseAttach(pid, logf); }
                g_ownPid = pid;
                g_own = true;
                if (!ownAttach()) { g_own = false; continue; }
                ownDetach();
                logf("dmnt refused: reading the game with our own debugger (short pauses while reading)");
            }
            if (!g_own && !attached) logf("attached to the game (try %d)", attempts);
            if (!g_own) attached = true;
            DmntCheatProcessMetadata md = {};
            char bid[17] = "own-debugger";
            if (g_own) {
                std::vector<MemoryInfo> maps;
                if (!ownMemoryMap(maps, md.main_nso_extents.base, md.main_nso_extents.size)) { logf("own: memory map failed"); continue; }
            } else {
                if (R_FAILED(rc = dmntchtGetCheatProcessMetadata(&md))) { logf("metadata failed 0x%X", rc); continue; }
                if (md.title_id != BOTW_ID) { logf("attached to %016llX, not BotW", (unsigned long long)md.title_id); continue; }
                for (int i = 0; i < 8; i++) snprintf(bid + i * 2, 3, "%02X", md.main_nso_build_id[i]);
            }
            std::vector<MemRegion> regions;
            uint64_t total = 0;
            if (!collectHeap(regions, total)) { logf("no heap mappings found"); continue; }
            logf("scan #%d: build %s, %u heap regions, %llu MB", attempts, bid, (unsigned)regions.size(),
                 (unsigned long long)(total >> 20));
            logf("main 0x%llX size 0x%llX", (unsigned long long)md.main_nso_extents.base, (unsigned long long)md.main_nso_extents.size);
            scanner.setMainRange(md.main_nso_extents.base, md.main_nso_extents.size);
            u64 t0 = armGetSystemTick();
            ScanStats st = scanner.scan(mem, regions, scanYield);
            ownDetach();
            u64 ms = armTicksToNs(armGetSystemTick() - t0) / 1000000;
            logf("scan #%d done: %llu ms, %llu MB read, %llu read errors", attempts, (unsigned long long)ms,
                 (unsigned long long)(st.bytesScanned >> 20), (unsigned long long)st.readErrors);
            logf("found %u/%u flags (%u objects), %llu candidates, %llu rejected", st.hashesFound, st.hashesWanted,
                 (unsigned)scanner.hits().size(), (unsigned long long)st.candidates, (unsigned long long)st.rejected);
            // Anchors: flags that exist from the very start of every BotW game.
            bool anchors = scanner.has(crc32str("IsGet_Obj_DRStone_Get")) && scanner.has(crc32str("HiddenKorok_Number"));
            if (!anchors) { logf("anchor flags not found yet (game still loading?) - will retry"); continue; }
            ready = true;
            user = userKey();
            logf("ready. user %s", user.empty() ? "(unknown!)" : user.c_str());
            for (auto& a : achs) {   // first values, for the log
                double v = 0; bool stale;
                if (a.kind == R_FLAG && scanner.value(mem, a.hashes[0], v, stale)) logf("  %s = %g (target %g)", a.id.c_str(), v, a.target);
            }
            ownDetach();
        }

        // 2) every 3 s: read values, unlock what is met
        if (now - lastEval < 3) continue;
        lastEval = now;
        if (user.empty()) { user = userKey(); if (user.empty()) continue; }
        std::map<std::string, uint64_t> store = loadStore(user);   // re-read: the app may have re-locked things
        bool changed = false, anyStale = false;
        for (auto& a : achs) {
            std::string key = prefix + a.id;
            if (store.count(key)) continue;
            double total = 0;
            bool foundAny = false;
            for (uint32_t h : a.hashes) {
                double v; bool stale;
                if (!scanner.value(mem, h, v, stale)) { if (stale) anyStale = true; continue; }
                foundAny = true;
                bool hit = a.hasEquals ? ((uint32_t)v == a.equals) : (v != 0);
                if (a.kind == R_COUNT) total += hit ? 1 : 0;
                else if (a.kind == R_SUM) total += v;
                else { total = a.hasEquals ? (hit ? 1 : 0) : v; break; }
            }
            if (foundAny && total + 1e-9 >= a.target) {
                store[key] = now;
                changed = true;
                logf("UNLOCKED %s (%s) value %g >= %g", a.id.c_str(), a.name.c_str(), total, a.target);
                PopupRequest pr;
                pr.titleId = BOTW_ID; pr.achId = a.id; pr.title = a.name; pr.desc = a.desc;
                pr.game = gameName; pr.points = a.points; pr.tier = a.tier;
                popupEnqueue(pr);
            }
        }
        ownDetach();   // own mode: all values of this round were read in one short pause
        if (changed) saveStore(user, store);
        if (anyStale) {
            logf("some flag objects moved/freed (save reload?) - rescanning");
            ready = false; attempts = 0; lastScan = 0; seenAt = now - 25;
        }
    }
    return 0;
}
