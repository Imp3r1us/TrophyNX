// Diagnostics for the live module:
//  - probeRunningSave(): can the committed save of the RUNNING game be read from NAND?
//    A running game's save can't be mounted (fs answers 0x7D402), but its raw image lives at
//    BIS User:/save/<SaveDataId>. If it opens read-only while the game runs, every supported
//    game can get pop-ups at save time (the image format is then parsed like hactool's save.c).
//  - diagnoseAttach(): dmnt:cht turns every attach failure into 0x32C80D; redo its steps here
//    to learn which one fails (loader module list, or another debugger already attached).
#include <switch.h>
#include <cstdio>
#include <cstring>
#include "saveprobe.h"

static bool findSaveDataId(u64 tid, const AccountUid& uid, FsSaveDataInfo& out, LogFn log) {
    FsSaveDataInfoReader r;
    Result rc = fsOpenSaveDataInfoReader(&r, FsSaveDataSpaceId_User);
    if (R_FAILED(rc)) { log("probe: save list failed 0x%X", rc); return false; }
    FsSaveDataInfo buf[16];
    bool found = false, haveDevice = false;
    FsSaveDataInfo device = {};
    s64 n = 0;
    while (!found && R_SUCCEEDED(fsSaveDataInfoReaderRead(&r, buf, 16, &n)) && n > 0) {
        for (s64 i = 0; i < n; i++) {
            const FsSaveDataInfo& s = buf[i];
            if (s.application_id != tid) continue;
            if (s.save_data_type == FsSaveDataType_Account && accountUidIsValid(&uid) &&
                s.uid.uid[0] == uid.uid[0] && s.uid.uid[1] == uid.uid[1]) { out = s; found = true; break; }
            if (s.save_data_type == FsSaveDataType_Device && !haveDevice) { device = s; haveDevice = true; }
        }
    }
    fsSaveDataInfoReaderClose(&r);
    if (!found && haveDevice) { out = device; found = true; }
    return found;
}

// Plan B when the file is locked: read the User partition as raw sectors (FAT32) and find the save
// image ourselves. This only checks that the raw storage opens and holds a FAT32 boot sector.
static void probeRawUser(LogFn log) {
    static bool done = false;   // once per boot is enough
    if (done) return;
    done = true;
    FsStorage st;
    Result rc = fsOpenBisStorage(&st, FsBisPartitionId_User);
    if (R_FAILED(rc)) { log("probe: raw User storage failed 0x%X", rc); return; }
    s64 size = 0;
    fsStorageGetSize(&st, &size);
    u8 sec[0x200] = {};
    rc = fsStorageRead(&st, 0, sec, sizeof(sec));
    fsStorageClose(&st);
    bool fat32 = memcmp(sec + 0x52, "FAT32", 5) == 0 && sec[0x1FE] == 0x55 && sec[0x1FF] == 0xAA;
    unsigned bps = sec[0x0B] | (sec[0x0C] << 8), spc = sec[0x0D];
    log("probe: raw User storage %ld MB, read rc 0x%X, %s (%u bytes/sector, %u sectors/cluster)", (long)(size >> 20), rc,
        fat32 ? "FAT32 boot sector OK" : "no FAT32 boot sector", bps, spc);
}

void probeRunningSave(u64 tid, LogFn log) {
    AccountUid uid = {};
    accountGetLastOpenedUser(&uid);
    FsSaveDataInfo info = {};
    if (!findSaveDataId(tid, uid, info, log)) { log("probe %016lX: no save found for this user", tid); return; }
    log("probe %016lX: save id %016lX type %u size %lu KB", tid, info.save_data_id, info.save_data_type,
        (unsigned long)(info.size >> 10));

    FsFileSystem bis;
    Result rc = fsOpenBisFileSystem(&bis, FsBisPartitionId_User, "");
    if (R_FAILED(rc)) { log("probe: open NAND User failed 0x%X", rc); return; }
    char path[40];
    snprintf(path, sizeof(path), "/save/%016lx", (unsigned long)info.save_data_id);
    FsFile f;
    u64 t0 = armGetSystemTick();
    rc = fsFsOpenFile(&bis, path, FsOpenMode_Read, &f);
    if (R_FAILED(rc)) {
        log("probe: open %s failed 0x%X%s", path, rc, rc == 0xE02 ? " (TargetLocked: the game has its save open)" : "");
        fsFsClose(&bis);
        probeRawUser(log);
        return;
    }
    // Read as little as possible and close at once: never hold the file while the game may save.
    u8 head[0x200] = {};
    s64 size = 0;
    u64 got = 0;
    fsFileGetSize(&f, &size);
    Result rr = fsFileRead(&f, 0, head, sizeof(head), FsReadOption_None, &got);
    fsFileClose(&f);
    fsFsClose(&bis);
    u64 us = armTicksToNs(armGetSystemTick() - t0) / 1000;
    bool disf = got >= 0x104 && memcmp(head + 0x100, "DISF", 4) == 0;
    log("probe: %s readable while running! size %ld, read rc 0x%X (%lu bytes, %lu us), magic %s", path,
        (long)size, rr, (unsigned long)got, (unsigned long)us, disf ? "DISF (save image OK)" : "unknown");
}

void diagnoseAttach(u64 pid, LogFn log) {
    Result rc = ldrDmntInitialize();
    if (R_FAILED(rc)) log("diag: ldr:dmnt unavailable 0x%X", rc);
    else {
        LoaderModuleInfo mods[4];
        s32 n = 0;
        rc = ldrDmntGetProcessModuleInfo(pid, mods, 4, &n);
        if (R_FAILED(rc)) log("diag: loader has no module list for pid %lu (0x%X)", (unsigned long)pid, rc);
        else log("diag: loader lists %d module(s) (dmnt needs 1 or 2 of the first two)", n);
        ldrDmntExit();
    }
    Handle h = INVALID_HANDLE;
    rc = svcDebugActiveProcess(&h, pid);
    if (R_SUCCEEDED(rc)) {
        svcCloseHandle(h);   // detach at once: we only wanted to know if it is possible
        log("diag: debugging the game is possible (no other debugger) - dmnt failed elsewhere");
    } else {
        log("diag: cannot debug the game 0x%X%s", rc,
            (rc == 0xD401 || rc == 0xFA01) ? " -> another tool is already attached (EdiZon, sys-botbase, a cheat overlay...)" : "");
    }
}
