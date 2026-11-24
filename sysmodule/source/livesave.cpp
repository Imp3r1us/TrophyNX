// Live save watch, see include/livesave.h.
#include "livesave.h"
#include "closescan.h"
#include "livepopups.h"
#include "popup.h"
#include "rawfat.h"
#include "rules.h"
#include "saveimage.h"
#include "store.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const u64 FIRST_LOOK = 20;   // s after the game starts (let it boot and mount its save first)
const u64 POLL = 4;          // s between two looks at the header
const u64 SETTLE = 2;        // s a new commit must stay the same before it is read (the game may save several files)

struct State {
    bool active = false;
    u64 tid = 0, nextAt = 0, pendingSince = 0;
    GameDef def;
    std::string defSource;
    AccountUid uid = {};
    u64 saveId = 0;
    bool located = false;
    rawfat::File file;
    std::string lastCommit, pending;
    int failures = 0, headerMisses = 0, locates = 0, changedDuringRead = 0;
    LiveFollow follow;   // live check / progress popups
};
State S;

// BIS User partition, raw. Reads are rounded to whole sectors.
struct Bis {
    FsStorage st = {};
    bool open = false;
    Result rc = 0;
    Bis() { rc = fsOpenBisStorage(&st, FsBisPartitionId_User); open = R_SUCCEEDED(rc); }
    ~Bis() { if (open) fsStorageClose(&st); }
    bool read(uint64_t off, void* buf, size_t len) {
        if (!open) return false;
        if (off % 512 == 0 && len % 512 == 0) return R_SUCCEEDED(fsStorageRead(&st, (s64)off, buf, len));
        uint64_t a = off / 512 * 512, b = (off + len + 511) / 512 * 512;
        std::vector<uint8_t> tmp((size_t)(b - a));
        if (R_FAILED(fsStorageRead(&st, (s64)a, tmp.data(), tmp.size()))) return false;
        memcpy(buf, tmp.data() + (off - a), len);
        return true;
    }
};

// Save of this title for this user (account save, else device save).
bool findSaveId(u64 tid, const AccountUid& uid, u64& id) {
    FsSaveDataInfoReader r;
    if (R_FAILED(fsOpenSaveDataInfoReader(&r, FsSaveDataSpaceId_User))) return false;
    FsSaveDataInfo buf[16];
    bool found = false, haveDevice = false;
    u64 device = 0;
    s64 n = 0;
    while (!found && R_SUCCEEDED(fsSaveDataInfoReaderRead(&r, buf, 16, &n)) && n > 0) {
        for (s64 i = 0; i < n; i++) {
            const FsSaveDataInfo& s = buf[i];
            if (s.application_id != tid) continue;
            if (s.save_data_type == FsSaveDataType_Account && s.uid.uid[0] == uid.uid[0] && s.uid.uid[1] == uid.uid[1]) {
                id = s.save_data_id; found = true; break;
            }
            if (s.save_data_type == FsSaveDataType_Device && !haveDevice) { device = s.save_data_id; haveDevice = true; }
        }
    }
    fsSaveDataInfoReaderClose(&r);
    if (!found && haveDevice) { id = device; found = true; }
    return found;
}

void stopWith(LiveLogFn log, const char* why) {
    log("live save: %s - %s; popups at close only for this session", S.def.name.c_str(), why);
    S.active = false;
}

bool locate(Bis& bis, LiveLogFn log) {
    S.located = false;
    if (!bis.open) { log("live save: raw NAND User unavailable 0x%X", bis.rc); return false; }
    rawfat::Volume vol;
    if (!vol.mount([&](uint64_t o, void* b, size_t l) { return bis.read(o, b, l); })) {
        log("live save: NAND User: %s", vol.error().c_str());
        return false;
    }
    char path[40];
    snprintf(path, sizeof(path), "/save/%016llx", (unsigned long long)S.saveId);
    if (!vol.open(path, S.file)) { log("live save: %s: %s", path, vol.error().c_str()); return false; }
    S.located = true;
    log("live save: %s - save image %s, %llu KB in %u pieces", S.def.name.c_str(), path,
        (unsigned long long)(S.file.size >> 10), (unsigned)S.file.runs.size());
    return true;
}

// Reads the save out of the image and unlocks what is met. false = the game saved meanwhile (try again).
bool readSave(const saveimg::ReadFn& rd, LiveLogFn log) {
    Game g;
    g.id = S.tid;
    g.name = S.def.name;
    applyGameDef(g, S.def);
    u64 t0 = armGetSystemTick();
    std::string seen, openError;
    {
        saveimg::ImageSaveSource src(rd, moduleMemoryAvailable);
        evaluateSave(g, src);
        seen = src.commit();
        openError = src.lastError();
    }
    u64 ms = armTicksToNs(armGetSystemTick() - t0) / 1000000;
    // The image could not be opened (seen is empty). This used to return false without a word and retry at
    // every save, forever: say why, and count it as a failure unless the game saved meanwhile.
    if (seen.empty()) {
        if (saveimg::commitId(rd) != S.pending) return false;
        log("live save: %s - save image not readable: %s", S.def.name.c_str(), openError.empty() ? "unknown error" : openError.c_str());
        if (++S.failures >= 3) stopWith(log, "the save image could not be read 3 times");
        S.lastCommit = S.pending;   // next try at the next save
        return true;
    }
    // Only trust values read from one commit: same header before, during and after.
    if (seen != S.pending || saveimg::commitId(rd) != S.pending) {
        if (++S.changedDuringRead == 5)   // once: a silent loop here is what hid problems before
            log("live save: %s - the save changed during each of the last 5 reads (%llu ms each); still trying", S.def.name.c_str(),
                (unsigned long long)ms);
        return false;
    }
    S.changedDuringRead = 0;
    if (g.saveStatus != SaveStatus::Ok) {
        std::string why = g.debug.empty() ? "save not recognised" : g.debug.front();
        log("live save: %s - %s", S.def.name.c_str(), why.c_str());
        if (++S.failures >= 3) stopWith(log, "the save image could not be read 3 times");
        S.lastCommit = S.pending;   // don't retry the same commit forever
        return true;
    }
    S.failures = 0;
    // what was already unlocked before this read (unlockMet adds the new ones): for the "already unlocked" popups
    std::set<std::string> before;
    {
        char pre[24];
        snprintf(pre, sizeof(pre), "%016llX/", (unsigned long long)S.tid);
        for (const auto& kv : loadStore(userKeyOf(S.uid)))
            if (kv.first.compare(0, 17, pre) == 0) before.insert(kv.first.substr(17));
    }
    unlockMet(g, S.def, S.defSource, S.uid, "live", ms, "live_save_log.txt", log);
    std::vector<PopupRequest> extra = livePopups(S.follow, g, before, popupOptions(), ms);
    for (const PopupRequest& pr : extra) popupEnqueue(pr);
    if (!extra.empty()) log("live save: %s - %u follow-up popup(s) (live check / already unlocked / progress)", S.def.name.c_str(), (unsigned)extra.size());
    S.lastCommit = S.pending;
    return true;
}

}  // namespace

void liveSaveStart(u64 tid, u64 now, LiveLogFn log) {
    S = State();
    if (!findDef(tid, S.def, S.defSource, log)) return;
    if (S.def.reader.empty()) return;   // play-log achievements only
    if (!S.def.liveSave) { log("live save: %s skipped (\"live_save\": false)", S.def.name.c_str()); return; }
    S.tid = tid;
    S.active = true;
    S.nextAt = now + FIRST_LOOK;
}

void liveSaveStop() { S = State(); }

void liveSaveTick(u64 now, bool canRead, LiveLogFn log) {
    if (!S.active || now < S.nextAt) return;
    S.nextAt = now + POLL;
    if (!S.saveId) {
        if (lastUser(&S.uid).empty()) return;   // no user yet: look again later
        if (!findSaveId(S.tid, S.uid, S.saveId)) { S.saveId = 0; S.nextAt = now + 30; return; }   // the game creates it later
    }
    Bis bis;
    if (!S.located) {
        if (++S.locates > 3) { stopWith(log, "save image not found in NAND"); return; }
        if (!locate(bis, log)) { S.nextAt = now + 30; return; }
    }
    saveimg::ReadFn rd = [&](uint64_t o, void* b, size_t l) {
        return S.file.read([&](uint64_t po, void* pb, size_t pl) { return bis.read(po, pb, pl); }, o, b, l);
    };
    std::string commit = saveimg::commitId(rd);
    if (commit.empty()) {   // mid-commit, or the image moved (resized): find it again
        if (++S.headerMisses >= 3) { S.headerMisses = 0; S.located = false; }
        return;
    }
    S.headerMisses = 0;
    if (commit == S.lastCommit) { S.pending.clear(); return; }
    if (commit != S.pending) {
        S.pending = commit;
        S.pendingSince = now;
        if (!S.lastCommit.empty()) { S.nextAt = now + SETTLE; return; }   // the first read happens at once
    }
    if (!S.lastCommit.empty() && now - S.pendingSince < SETTLE) return;
    if (!canRead) { S.nextAt = now + 1; return; }
    if (!readSave(rd, log)) { S.pending.clear(); S.nextAt = now + SETTLE; }
}
