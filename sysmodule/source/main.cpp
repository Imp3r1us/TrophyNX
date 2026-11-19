// sys-achievements: background module that watches Breath of the Wild's memory while you play
// (like RetroAchievements), unlocks achievements live and shows popups. For every game with a
// save reader it also reads the save as soon as the game closes ("scan on close", closescan.cpp).
#include <switch.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
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
#include "store.h"

#define MODULE_VERSION "0.5.0"
static const u64 BOTW_ID = 0x01007EF00011E000ULL;

extern "C" {
// Sysmodule setup: no applet, small fixed heap.
u32 __nx_applet_type = AppletType_None;
// nv's default transfer memory is 8 MB; the popup layer needs very little (same value as Tesla).
u32 __nx_nv_transfermem_size = 0x15000;
void __libnx_init_time(void);
u32 __nx_fs_num_sessions = 1;
#define INNER_HEAP_SIZE 0x400000   // 4 MB: flag scanner + popup layer (two 16-bit 1080p-scale buffers) + scan on close (one save file at a time)
void __libnx_initheap(void) {
    static char inner_heap[INNER_HEAP_SIZE];
    extern char* fake_heap_start;
    extern char* fake_heap_end;
    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

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
    snprintf(path, sizeof(path), "%s/live_log.txt", DATA_DIR);
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

// ---------------------------------------------------------------- main loop
int main(int, char**) {
    mkdir(DATA_DIR, 0777);
    logf("sys-achievements " MODULE_VERSION " started");
    popupStart(logf);
    Result rc;
    if (R_FAILED(rc = pmdmntInitialize())) logf("ERROR pmdmnt init 0x%X", rc);
    if (R_FAILED(rc = pminfoInitialize())) logf("ERROR pminfo init 0x%X", rc);
    if (R_FAILED(rc = accountInitialize(AccountServiceType_Administrator))) logf("ERROR account init 0x%X", rc);
    bool dmnt = R_SUCCEEDED(rc = dmntchtInitialize());
    if (!dmnt) logf("ERROR dmnt:cht init 0x%X (is Atmosphere's dmnt enabled?)", rc);

    std::string gameName = "Breath of the Wild";
    std::vector<LiveAch> achs = loadDefinition(gameName);
    std::vector<uint32_t> wanted;
    for (auto& a : achs) wanted.insert(wanted.end(), a.hashes.begin(), a.hashes.end());
    logf("loaded %u live achievements (%u flags) for %s", (unsigned)achs.size(), (unsigned)wanted.size(), gameName.c_str());

    FlagScanner scanner;
    scanner.setWanted(wanted);
    DmntReader mem;

    u64 curPid = 0, curTid = 0, startedAt = 0;
    bool watching = false, ready = false;
    u64 seenAt = 0, lastScan = 0, lastEval = 0;
    int attempts = 0, probes = 0;
    bool attached = false, diagnosed = false;
    std::string user;
    // scan on close: the save of the game that just closed is read a few seconds later
    u64 closedTid = 0, closedAt = 0;
    int closedTries = 0;
    AccountUid closedUid = {};
    auto gameEnded = [&](u64 tid, u64 when) {
        if (!tid) return;
        AccountUid uid = {};
        if (lastUser(&uid).empty()) { logf("close scan: no user"); return; }
        closedTid = tid; closedUid = uid; closedAt = when + 3; closedTries = 0;
    };
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "%016llX/", (unsigned long long)BOTW_ID);

    while (true) {
        svcSleepThread(1000000000ULL);
        u64 now = (u64)time(NULL);
        popupWatchdog();
        u64 pid = 0;
        bool running = R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)) && pid != 0;
        if (closedTid && now >= closedAt && popupIdle()) {   // after any popup: its buffers share the heap
            CloseScanResult r = closeScan(closedTid, closedUid, logf);
            if (r == CS_LOCKED && ++closedTries < 4) closedAt = now + 5;   // save still in use: try again
            else closedTid = 0;
        }
        if (!running) {
            if (curPid) { logf("game closed (pid %llu)", (unsigned long long)curPid); gameEnded(curTid, now); }
            if (attached) dmntchtForceCloseCheatProcess();   // never leave dmnt pointing at a dead process
            ownDetach(); g_own = false;
            curPid = 0; curTid = 0; watching = ready = attached = false;
            continue;
        }
        if (pid != curPid) {
            if (curPid) { logf("game changed (pid %llu -> %llu)", (unsigned long long)curPid, (unsigned long long)pid); gameEnded(curTid, now); }
            curPid = pid;
            u64 tid = 0;
            pminfoGetProgramId(&tid, pid);
            curTid = tid;
            probes = 0; diagnosed = false; startedAt = now;
            if (attached) { dmntchtForceCloseCheatProcess(); attached = false; }
            ownDetach(); g_own = false;
            watching = (tid == BOTW_ID) && dmnt && !achs.empty();
            ready = false; attempts = 0; seenAt = now; lastScan = 0;
            logf("game started: %016llX%s", (unsigned long long)tid, watching ? " -> watching (live achievements)" : "");
        }
        // Save-image probe (every game): once after 60 s, again after 5 min (the game has usually saved by then).
        if ((probes == 0 && now - startedAt >= 60) || (probes == 1 && now - startedAt >= 300)) {
            probes++;
            probeRunningSave(curTid, logf);
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
