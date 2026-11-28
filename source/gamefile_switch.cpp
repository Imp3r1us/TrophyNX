// Files of an installed game (its romfs), read in pieces: used to find the password of an encrypted save
// (ES3Defaults in Data/resources.assets, Data/Managed/Metadata/global-metadata.dat) with nothing to dump.
// Shared by the app and the live module (sysmodule/source/shared_gamefile.cpp). The app runs with the
// permissions it was launched with (applet mode can't open other programs' files on some setups); the live
// module has full file system access, so it can when the app can't.
#ifdef __SWITCH__
#include "gamefile.h"
#include <switch.h>
#include <cstdio>
#include <memory>

namespace {

// "2002-3001" plus the raw value (some firmwares return extra high bits).
std::string rcText(Result rc) {
    char b[48];
    snprintf(b, sizeof(b), "%04u-%04u (0x%X)", 2000 + R_MODULE(rc), R_DESCRIPTION(rc), (unsigned)rc);
    return b;
}

// Path of the installed game's program NCA (SD card, then console memory), for fsOpenFileSystemWithId.
bool gameProgramPath(uint64_t titleId, char* path, size_t size, std::string& why) {
    Result rc = ncmInitialize();
    if (R_FAILED(rc)) { why += "ncm " + rcText(rc) + "; "; return false; }
    bool ok = false;
    const NcmStorageId storages[] = {NcmStorageId_SdCard, NcmStorageId_BuiltInUser};
    for (NcmStorageId sid : storages) {
        NcmContentMetaDatabase db;
        if (R_FAILED(ncmOpenContentMetaDatabase(&db, sid))) continue;
        NcmContentMetaKey key;
        NcmContentId cid;
        bool found = R_SUCCEEDED(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, titleId)) &&
                     R_SUCCEEDED(ncmContentMetaDatabaseGetContentIdByType(&db, &cid, &key, NcmContentType_Program));
        ncmContentMetaDatabaseClose(&db);
        if (!found) continue;
        NcmContentStorage cs;
        if (R_FAILED(ncmOpenContentStorage(&cs, sid))) continue;
        ok = R_SUCCEEDED(ncmContentStorageGetPath(&cs, path, size, &cid));
        ncmContentStorageClose(&cs);
        if (ok) break;
    }
    ncmExit();
    if (!ok) why += "program NCA not found on the SD card or console memory (game card?); ";
    return ok;
}

struct Open {
    FsFileSystem fs;
    FsFile f;
};

}  // namespace

// Ways to open the romfs, in order (each refused one is listed in `why`):
// 1. with its update (OpenFileSystemWithPatch; "not implemented" on some firmwares),
// 2. by program id (OpenDataFileSystemByProgramId, what Atmosphère's own tools use),
// 3. the base game's program NCA by path (OpenFileSystemWithId), with content attributes, then without.
// The passwords come from settings of the base game, so 2 and 3 are as good as 1.
bool openGameFile(uint64_t titleId, const std::string& rel, GameFile& out, std::string& why) {
    why.clear();
    FsFileSystem fs;
    Result rc = fsOpenFileSystemWithPatch(&fs, titleId, FsFileSystemType_ContentData);
    if (R_FAILED(rc)) {
        why += "with its update " + rcText(rc) + "; ";
        rc = fsOpenDataFileSystemByProgramId(&fs, titleId);
        if (R_FAILED(rc)) why += "by program id " + rcText(rc) + "; ";
    }
    if (R_FAILED(rc)) {
        char nca[FS_MAX_PATH] = {0};
        if (gameProgramPath(titleId, nca, sizeof(nca), why)) {
            rc = fsOpenFileSystemWithId(&fs, titleId, FsFileSystemType_ContentData, nca, FsContentAttributes_All);
            if (R_FAILED(rc)) {
                why += "base game " + rcText(rc) + "; ";
                rc = fsOpenFileSystemWithId(&fs, titleId, FsFileSystemType_ContentData, nca, FsContentAttributes_None);
                if (R_FAILED(rc)) why += "base game (no attributes) " + rcText(rc) + "; ";
            }
        }
    }
    if (R_FAILED(rc)) {
        why = "the system didn't open the game's files: " + why.substr(0, why.size() >= 2 ? why.size() - 2 : 0);
        return false;
    }
    std::unique_ptr<Open> o(new Open());
    o->fs = fs;
    char path[FS_MAX_PATH] = {0};
    snprintf(path, sizeof(path), "/%s", rel.c_str());
    rc = fsFsOpenFile(&o->fs, path, FsOpenMode_Read, &o->f);
    if (R_FAILED(rc)) {
        why = std::string(path) + " isn't in this game: " + rcText(rc);
        fsFsClose(&o->fs);
        return false;
    }
    s64 size = 0;
    if (R_FAILED(fsFileGetSize(&o->f, &size)) || size <= 0) {
        why = "unexpected file size";
        fsFileClose(&o->f);
        fsFsClose(&o->fs);
        return false;
    }
    Open* h = o.release();
    out.size = (uint64_t)size;
    out.read = [h](uint64_t off, void* buf, size_t n) {
        size_t done = 0;
        while (done < n) {
            u64 got = 0;
            if (R_FAILED(fsFileRead(&h->f, (s64)(off + done), (uint8_t*)buf + done, n - done, FsReadOption_None, &got)) || !got)
                return false;
            done += (size_t)got;
        }
        return true;
    };
    out.close = [h] {
        fsFileClose(&h->f);
        fsFsClose(&h->fs);
        delete h;
    };
    return true;
}
#endif
