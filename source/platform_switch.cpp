#ifdef __SWITCH__
#include "platform.h"
#include "history.h"
#include <switch.h>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <memory>
#include <algorithm>
#include "render.h"
#include <curl/curl.h>

static AccountUid g_uid;
static std::string g_userName = "Player";
static PadState g_pad;
static Framebuffer g_fb;
static bool g_fbReady = false;
static bool g_saveMounted = false;
static bool g_pl = false, g_pdm = false, g_ns = false, g_acc = false, g_romfs = false;
static uint64_t g_down = 0, g_held = 0;
static std::string g_lastError;

static uint32_t mapButtons(uint64_t k) {
    uint32_t b = 0;
    if (k & HidNpadButton_A) b |= plat::BTN_A;
    if (k & HidNpadButton_B) b |= plat::BTN_B;
    if (k & HidNpadButton_X) b |= plat::BTN_X;
    if (k & HidNpadButton_Y) b |= plat::BTN_Y;
    if (k & (HidNpadButton_Up | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) b |= plat::BTN_UP;
    if (k & (HidNpadButton_Down | HidNpadButton_StickLDown | HidNpadButton_StickRDown)) b |= plat::BTN_DOWN;
    if (k & (HidNpadButton_Left | HidNpadButton_StickLLeft)) b |= plat::BTN_LEFT;
    if (k & (HidNpadButton_Right | HidNpadButton_StickLRight)) b |= plat::BTN_RIGHT;
    if (k & HidNpadButton_Plus) b |= plat::BTN_PLUS;
    if (k & HidNpadButton_Minus) b |= plat::BTN_MINUS;
    if (k & HidNpadButton_L) b |= plat::BTN_L;
    if (k & HidNpadButton_ZL) b |= plat::BTN_ZL;
    if (k & HidNpadButton_R) b |= plat::BTN_R;
    if (k & HidNpadButton_ZR) b |= plat::BTN_ZR;
    if (k & HidNpadButton_StickL) b |= plat::BTN_STICK;
    if (k & HidNpadButton_StickR) b |= plat::BTN_RSTICK;
    return b;
}

bool plat::init(std::string& err) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    if (R_FAILED(framebufferCreate(&g_fb, nwindowGetDefault(), 1280, 720, PIXEL_FORMAT_RGBA_8888, 2))) {
        err = "Could not create the framebuffer.";
        return false;
    }
    framebufferMakeLinear(&g_fb);
    g_fbReady = true;

    g_romfs = R_SUCCEEDED(romfsInit());
    g_pl = R_SUCCEEDED(plInitialize(PlServiceType_User));
    g_ns = R_SUCCEEDED(nsInitialize());
    g_pdm = R_SUCCEEDED(pdmqryInitialize());
    g_acc = R_SUCCEEDED(accountInitialize(AccountServiceType_Administrator));
    if (!g_acc) g_acc = R_SUCCEEDED(accountInitialize(AccountServiceType_Application));
    if (!g_acc) { err = "Account service unavailable."; return false; }
    if (!g_ns) { err = "NS service unavailable."; return false; }

    // Pick the user: preselected (title override launch) or ask with the system selector.
    Result rc = accountGetPreselectedUser(&g_uid);
    if (R_FAILED(rc) || !accountUidIsValid(&g_uid)) {
        PselUserSelectionSettings s;
        memset(&s, 0, sizeof(s));
        rc = pselShowUserSelector(&g_uid, &s);
        if (R_FAILED(rc) || !accountUidIsValid(&g_uid)) {
            // Fall back to the most recently opened user, then the first user.
            rc = accountGetLastOpenedUser(&g_uid);
            if (R_FAILED(rc) || !accountUidIsValid(&g_uid)) {
                AccountUid users[ACC_USER_LIST_SIZE];
                s32 n = 0;
                if (R_SUCCEEDED(accountListAllUsers(users, ACC_USER_LIST_SIZE, &n)) && n > 0) g_uid = users[0];
                else { err = "No user selected."; return false; }
            }
        }
    }
    AccountProfile prof;
    if (R_SUCCEEDED(accountGetProfile(&prof, g_uid))) {
        AccountProfileBase base;
        memset(&base, 0, sizeof(base));
        if (R_SUCCEEDED(accountProfileGet(&prof, NULL, &base))) g_userName = std::string(base.nickname, strnlen(base.nickname, sizeof(base.nickname)));
        accountProfileClose(&prof);
    }
    return true;
}

static bool g_net = false;      // sockets + curl, started by the first httpGet
static std::string g_self;

void plat::exit() {
    closeSave();
    if (g_net) { curl_global_cleanup(); socketExit(); }
    if (g_fbReady) framebufferClose(&g_fb);
    if (g_acc) accountExit();
    if (g_pdm) pdmqryExit();
    if (g_ns) nsExit();
    if (g_pl) plExit();
    if (g_romfs) romfsExit();
}

std::string plat::userName() { return g_userName; }

std::string plat::userKey() {
    char buf[40];
    snprintf(buf, sizeof(buf), "%016lx%016lx", (unsigned long)g_uid.uid[1], (unsigned long)g_uid.uid[0]);
    return buf;
}

void plat::listGames(std::vector<GameInfo>& out, ProgressFn progress, void* ctx) {
    out.clear();
    std::vector<NsApplicationRecord> recs;
    NsApplicationRecord buf[64];
    s32 offset = 0;
    while (true) {
        s32 n = 0;
        if (R_FAILED(nsListApplicationRecord(buf, 64, offset, &n)) || n <= 0) break;
        recs.insert(recs.end(), buf, buf + n);
        offset += n;
    }
    std::unique_ptr<NsApplicationControlData> ctl(new NsApplicationControlData);
    for (size_t i = 0; i < recs.size(); i++) {
        if (progress) progress((int)i, (int)recs.size(), ctx);
        GameInfo g;
        g.id = recs[i].application_id;
        u64 sz = 0;
        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, g.id, ctl.get(), sizeof(NsApplicationControlData), &sz))) {
            NacpLanguageEntry* le = NULL;
            int idx = -1;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&ctl->nacp, &le)) && le) idx = (int)(le - ctl->nacp.lang_data.lang);
            std::vector<std::string> names;
            if (ctl->nacp.titles_data_format == 1) {
                // [21.0.0+] compressed titles: the plain lang[] array would read as garbage
                decodeCompressedTitles((const uint8_t*)&ctl->nacp.lang_data, sizeof(ctl->nacp.lang_data), names);
            } else {
                for (int li = 0; li < 16; li++)
                    names.push_back(std::string(ctl->nacp.lang_data.lang[li].name, strnlen(ctl->nacp.lang_data.lang[li].name, 0x200)));
            }
            if (idx >= 0 && idx < (int)names.size() && isCleanText(names[idx])) g.name = names[idx];
            for (size_t li = 0; li < names.size() && !isCleanText(g.name); li++) g.name = names[li];   // any valid language
            if (!isCleanText(g.name)) g.name.clear();
            if (sz > sizeof(NacpStruct)) g.iconJpeg.assign(ctl->icon, ctl->icon + (sz - sizeof(NacpStruct)));
        }
        if (g.name.empty()) {
            char t[32];
            snprintf(t, sizeof(t), "%016lX", (unsigned long)g.id);
            g.name = t;
        }
        out.push_back(std::move(g));
    }
    if (progress) progress((int)recs.size(), (int)recs.size(), ctx);
}

PlayStats plat::playStats(uint64_t id) {
    PlayStats p;
    if (!g_pdm) return p;
    PdmPlayStatistics s;
    memset(&s, 0, sizeof(s));
    if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(id, g_uid, false, &s))) {
        p.valid = true;
        p.playtimeNs = s.playtime;
        p.launches = s.total_launches;
        p.lastPlayed = s.last_timestamp_user;
    }
    return p;
}

// Local UTC offset (seconds) from the console's time zone, so play days start at local midnight.
static int64_t localUtcOffset() {
    u64 now = 0;
    if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &now))) return 0;
    TimeCalendarTime cal;
    TimeCalendarAdditionalInfo info;
    if (R_FAILED(timeToCalendarTimeWithMyRule(now, &cal, &info))) return 0;
    return info.offset;
}

// Per-user play days from the account events of the play log: on 10.0.0+ each "open" / "close" of the
// user account by an application carries its program id. Older firmware: application events, all users.
void plat::playHistory(PlayHistory& out) {
    out = PlayHistory();
    if (!g_pdm) { out.notes.push_back("pdm:qry unavailable"); return; }
    int64_t off = localUtcOffset();
    s32 total = 0, start = 0, end = 0;
    Result rc = pdmqryGetAvailablePlayEventRange(&total, &start, &end);
    char b[200];
    snprintf(b, sizeof(b), "play log range: rc=0x%X, %d entries, index %d..%d, UTC offset %+ld min", rc, total, start, end, (long)(off / 60));
    out.notes.push_back(b);
    if (R_FAILED(rc) || total <= 0) return;

    const s32 batch = 128;
    std::vector<PdmAccountEvent> ev(batch);
    std::map<uint64_t, uint64_t> openAt;   // program id -> open time of the current session
    int read = 0, mine = 0, opens = 0, closes = 0, withProgram = 0, otherTypes = 0;
    s32 idx = start;
    for (int guard = 0; guard < 4096 && idx <= end; guard++) {
        s32 n = 0;
        rc = pdmqryQueryAccountEvent(idx, ev.data(), batch, &n);
        if (R_FAILED(rc) || n <= 0) break;
        for (s32 i = 0; i < n; i++) {
            const PdmAccountEvent& e = ev[i];
            read++;
            if (memcmp(&e.uid, &g_uid, sizeof(AccountUid)) != 0) continue;
            mine++;
            uint64_t pid = e.program_id;
            if (pid) withProgram++;
            if (!pid) continue;
            if (e.type == 0) {
                opens++;
                auto it = openAt.find(pid);
                if (it != openAt.end()) addSessionDays(out.days[pid], it->second, it->second, off);   // never closed
                openAt[pid] = e.timestamp_user;
            } else if (e.type == 1) {
                closes++;
                auto it = openAt.find(pid);
                if (it != openAt.end()) { addSessionDays(out.days[pid], it->second, e.timestamp_user, off); openAt.erase(it); }
                else addSessionDays(out.days[pid], e.timestamp_user, e.timestamp_user, off);
            } else otherTypes++;
        }
        s32 next = (s32)ev[n - 1].entry_index + 1;
        if (next <= idx) break;
        idx = next;
    }
    for (auto& kv : openAt) addSessionDays(out.days[kv.first], kv.second, kv.second, off);   // still running / not closed
    snprintf(b, sizeof(b), "account events: %d read, %d for this user (%d with a program id: %d open, %d close, %d other)",
             read, mine, withProgram, opens, closes, otherTypes);
    out.notes.push_back(b);
    if (!out.days.empty()) { out.valid = true; out.source = "account events (this user)"; return; }

    // Fallback: application launch / focus events (every user).
    std::vector<PdmAppletEvent> ae(batch);
    int appEvents = 0;
    idx = start;
    for (int guard = 0; guard < 4096 && idx <= end; guard++) {
        s32 n = 0;
        rc = pdmqryQueryAppletEvent(idx, false, ae.data(), batch, &n);
        if (R_FAILED(rc) || n <= 0) break;
        for (s32 i = 0; i < n; i++) {
            const PdmAppletEvent& e = ae[i];
            if (e.event_type != PdmAppletEventType_Launch && e.event_type != PdmAppletEventType_InFocus) continue;
            if (!e.program_id || !e.timestamp_user) continue;
            appEvents++;
            addSessionDays(out.days[e.program_id], e.timestamp_user, e.timestamp_user, off);
        }
        s32 next = (s32)ae[n - 1].entry_index + 1;
        if (next <= idx) break;
        idx = next;
    }
    snprintf(b, sizeof(b), "application events (all users): %d launch/focus events, %d games", appEvents, (int)out.days.size());
    out.notes.push_back(b);
    if (!out.days.empty()) { out.valid = true; out.source = "application events (all users)"; }
}

// Screenshots and videos per game, from both album storages (console + SD card).
void plat::albumCounts(AlbumCounts& out) {
    out = AlbumCounts();
    Result rc = capsaInitialize();
    if (R_FAILED(rc)) {
        char b[64]; snprintf(b, sizeof(b), "caps:a unavailable (rc=0x%X)", rc);
        out.notes.push_back(b);
        return;
    }
    const CapsAlbumStorage stores[2] = {CapsAlbumStorage_Nand, CapsAlbumStorage_Sd};
    for (CapsAlbumStorage st : stores) {
        u64 count = 0;
        rc = capsaGetAlbumFileCount(st, &count);
        char b[160];
        if (R_FAILED(rc)) {
            snprintf(b, sizeof(b), "album %s: count failed rc=0x%X", st == CapsAlbumStorage_Sd ? "SD" : "NAND", rc);
            out.notes.push_back(b);
            continue;
        }
        if (count > 100000) count = 100000;
        std::vector<CapsAlbumEntry> list(count ? count : 1);
        u64 got = 0;
        if (count) rc = capsaGetAlbumFileList(st, &got, list.data(), count);
        if (R_FAILED(rc)) got = 0;
        int shots = 0, movies = 0;
        for (u64 i = 0; i < got && i < count; i++) {
            const CapsAlbumFileId& f = list[i].file_id;
            if (f.content == CapsAlbumFileContents_ScreenShot || f.content == CapsAlbumFileContents_ExtraScreenShot) { out.screenshots[f.application_id]++; shots++; }
            else if (f.content == CapsAlbumFileContents_Movie || f.content == CapsAlbumFileContents_ExtraMovie) { out.videos[f.application_id]++; movies++; }
        }
        snprintf(b, sizeof(b), "album %s: %llu files listed (rc=0x%X), %d screenshots, %d videos", st == CapsAlbumStorage_Sd ? "SD" : "NAND",
                 (unsigned long long)got, rc, shots, movies);
        out.notes.push_back(b);
        out.valid = true;
    }
    capsaExit();
}

bool plat::openSave(uint64_t id) {
    closeSave();
    Result rc = fsdevMountSaveDataReadOnly("achsave", id, g_uid);
    g_saveMounted = R_SUCCEEDED(rc);
    if (!g_saveMounted) {
        char b[160];
        // 0x7D402 (module 2 / desc 1002) = no save for this user; other codes often mean the game is running
        snprintf(b, sizeof(b), "mount failed: rc=0x%X (module %u, desc %u)%s", rc, R_MODULE(rc), R_DESCRIPTION(rc),
                 rc == 0x7D402 ? " = no save data for this user" : " (is the game still running/suspended?)");
        g_lastError = b;
    } else g_lastError.clear();
    return g_saveMounted;
}

std::string plat::lastError() { return g_lastError; }

// The live module is "on" when atmosphere/contents/420000000000AC01/flags/boot2.flag exists (what Atmosphère
// and Ultrahand use to start it at boot). Without it the app never starts the module itself.
static bool liveModuleEnabled() {
    FILE* flag = fopen("sdmc:/atmosphere/contents/420000000000AC01/flags/boot2.flag", "rb");
    if (!flag) return false;
    fclose(flag);
    return true;
}

std::string plat::liveModuleStatus() {
    const u64 LIVE_ID = 0x420000000000AC01ULL;
    char b[200];
    u64 pid = 0;
    bool pmd = R_SUCCEEDED(pmdmntInitialize());
    if (pmd && R_SUCCEEDED(pmdmntGetProcessId(&pid, LIVE_ID)) && pid) {
        pmdmntExit();
        snprintf(b, sizeof(b), "running (pid %llu)", (unsigned long long)pid);
        return b;
    }
    if (pmd) pmdmntExit();
    FILE* f = fopen("sdmc:/atmosphere/contents/420000000000AC01/exefs.nsp", "rb");
    if (!f) return "not installed (atmosphere/contents/420000000000AC01/exefs.nsp missing)";
    fclose(f);
    // No boot2.flag = the player turned the module off (Ultrahand or by renaming it): never start it from here.
    if (!liveModuleEnabled()) return "turned off (no flags/boot2.flag): not started";
    Result rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        snprintf(b, sizeof(b), "not running; pm:shell unavailable rc=0x%X", rc);
        return b;
    }
    NcmProgramLocation loc;
    memset(&loc, 0, sizeof(loc));
    loc.program_id = LIVE_ID;
    loc.storageID = NcmStorageId_None;
    rc = pmshellLaunchProgram(0, &loc, &pid);
    pmshellExit();
    if (R_SUCCEEDED(rc)) snprintf(b, sizeof(b), "was not running -> started now (pid %llu)", (unsigned long long)pid);
    else snprintf(b, sizeof(b), "not running and could not start: rc=0x%X (module %u, desc %u)", rc, R_MODULE(rc),
                  R_DESCRIPTION(rc));
    return b;
}

// ---------------------------------------------------------------- updates
struct HttpCtx {
    std::vector<uint8_t>* out;
    plat::ProgressFn progress;
    void* ctx;
};

static size_t httpWrite(char* p, size_t sz, size_t n, void* user) {
    HttpCtx* h = (HttpCtx*)user;
    h->out->insert(h->out->end(), (uint8_t*)p, (uint8_t*)p + sz * n);
    return sz * n;
}

static int httpProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    HttpCtx* h = (HttpCtx*)user;
    if (h->progress && total > 0) h->progress((int)(now >> 10), (int)(total >> 10), h->ctx);   // KB
    return 0;
}

bool plat::netInit(std::string& err) {
    if (g_net) return true;
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) { char b[64]; snprintf(b, sizeof(b), "network unavailable (rc=0x%X)", rc); err = b; return false; }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) { socketExit(); err = "curl init failed"; return false; }
    g_net = true;
    return true;
}

void plat::keepAwake(bool on) { appletSetMediaPlaybackState(on); }

std::string plat::localIp() {
    if (R_FAILED(nifmInitialize(NifmServiceType_User))) return "";
    u32 ip = 0;
    Result rc = nifmGetCurrentIpAddress(&ip);
    nifmExit();
    if (R_FAILED(rc) || ip == 0) return "";
    char b[20];
    snprintf(b, sizeof(b), "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24);
    return b;
}

int plat::httpGet(const std::string& url, const std::vector<std::string>& headers, std::vector<uint8_t>& out,
                  ProgressFn progress, void* ctx, std::string& err) {
    out.clear();
    if (!netInit(err)) return 0;
    CURL* c = curl_easy_init();
    if (!c) { err = "curl init failed"; return 0; }
    struct curl_slist* hl = nullptr;
    for (const std::string& h : headers) hl = curl_slist_append(hl, h.c_str());
    HttpCtx hc = {&out, progress, ctx};
    char ebuf[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, httpWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &hc);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, httpProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &hc);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, ebuf);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 100L);   // give up after 30 s below 100 B/s
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    if (r == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    else err = std::string(curl_easy_strerror(r)) + (ebuf[0] ? std::string(": ") + ebuf : "");
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    return (int)code;
}

void plat::setSelfPath(const std::string& p) { g_self = p; }
std::string plat::selfPath() { return g_self; }
std::string plat::sdRoot() { return "sdmc:/"; }

// The game's romfs, update applied (OpenFileSystemWithPatch), read-only: used to find a save password (ES3Defaults in
// Data/resources.assets, global-metadata.dat) without asking the player to dump anything.
bool plat::readGameFile(uint64_t titleId, const std::string& rel, std::vector<uint8_t>& out, std::string& why) {
    char msg[96];
    FsFileSystem fs;
    Result rc = fsOpenFileSystemWithPatch(&fs, titleId, FsFileSystemType_ContentData);
    if (R_FAILED(rc)) {
        snprintf(msg, sizeof(msg), "the system didn't open the game's files: 0x%X", (unsigned)rc);
        why = msg;
        return false;
    }
    char path[FS_MAX_PATH] = {0};
    snprintf(path, sizeof(path), "/%s", rel.c_str());
    FsFile f;
    rc = fsFsOpenFile(&fs, path, FsOpenMode_Read, &f);
    if (R_FAILED(rc)) {
        snprintf(msg, sizeof(msg), "%s isn't in this game: 0x%X", path, (unsigned)rc);
        why = msg;
        fsFsClose(&fs);
        return false;
    }
    s64 size = 0;
    bool ok = R_SUCCEEDED(fsFileGetSize(&f, &size)) && size > 0 && size <= (s64)(256u << 20);
    if (!ok) why = "unexpected file size";
    if (ok) out.resize((size_t)size);
    for (s64 off = 0; ok && off < size;) {
        u64 got = 0;
        u64 want = (u64)std::min<s64>(size - off, 1 << 20);
        if (R_FAILED(fsFileRead(&f, off, out.data() + off, want, FsReadOption_None, &got)) || got == 0) { ok = false; why = "read error"; }
        off += (s64)got;
    }
    fsFileClose(&f);
    fsFsClose(&fs);
    if (!ok) out.clear();
    return ok;
}

bool plat::restartLiveModule(std::string& msg) {
    const u64 LIVE_ID = 0x420000000000AC01ULL;
    Result rc = pmshellInitialize();
    if (R_FAILED(rc)) { char b[80]; snprintf(b, sizeof(b), "pm:shell unavailable (rc=0x%X)", rc); msg = b; return false; }
    u64 oldPid = liveModulePid();
    pmshellTerminateProgram(LIVE_ID);   // fails harmlessly if it wasn't running
    // termination is asynchronous: launching again while the old process still exists fails
    for (int i = 0; i < 50 && liveModulePid() != 0; i++) svcSleepThread(100000000ULL);
    if (liveModulePid() != 0) {
        pmshellExit();
        char b[120];
        snprintf(b, sizeof(b), "the old live module (pid %llu) did not stop: reboot to use the new one", (unsigned long long)oldPid);
        msg = b;
        return false;
    }
    if (!liveModuleEnabled()) {   // turned off: the new file is installed, but it is not started
        pmshellExit();
        msg = "live module updated, not started (turned off: no boot2.flag)";
        return true;
    }
    NcmProgramLocation loc;
    memset(&loc, 0, sizeof(loc));
    loc.program_id = LIVE_ID;
    loc.storageID = NcmStorageId_None;
    u64 pid = 0;
    rc = pmshellLaunchProgram(0, &loc, &pid);
    pmshellExit();
    char b[80];
    if (R_SUCCEEDED(rc)) { snprintf(b, sizeof(b), "live module restarted (pid %llu)", (unsigned long long)pid); msg = b; return true; }
    snprintf(b, sizeof(b), "live module not restarted (rc=0x%X): reboot to use the new one", rc);
    msg = b;
    return false;
}

void plat::releaseSelfFile() {
    if (g_romfs) romfsExit();
    g_romfs = false;
}

uint64_t plat::liveModulePid() {
    u64 pid = 0;
    if (R_FAILED(pmdmntInitialize())) return 0;
    if (R_FAILED(pmdmntGetProcessId(&pid, 0x420000000000AC01ULL))) pid = 0;
    pmdmntExit();
    return pid;
}

void plat::relaunchSelf() {
    if (!g_self.empty()) envSetNextLoad(g_self.c_str(), g_self.c_str());
}

bool plat::readSaveFile(const std::string& rel, std::vector<uint8_t>& out) {
    if (!g_saveMounted) return false;
    return readFile("achsave:/" + rel, out);
}

void plat::listSaveFiles(std::vector<std::pair<std::string, uint64_t>>& out) {
    out.clear();
    if (g_saveMounted) listFilesRecursive("achsave:/", "", out);
}

void plat::closeSave() {
    if (g_saveMounted) fsdevUnmountDevice("achsave");
    g_saveMounted = false;
}

std::string plat::dataDir() { return "sdmc:/switch/TrophyNX"; }
std::string plat::romfsDir() { return "romfs:"; }

bool plat::fontData(int which, const uint8_t** data, size_t* size) {
    if (!g_pl) return false;
    PlFontData f;
    if (R_FAILED(plGetSharedFontByType(&f, which == 0 ? PlSharedFontType_Standard : PlSharedFontType_NintendoExt))) return false;
    *data = (const uint8_t*)f.address;
    *size = f.size;
    return true;
}

uint64_t plat::now() { return (uint64_t)time(NULL); }

bool plat::poll() {
    if (!appletMainLoop()) return false;
    padUpdate(&g_pad);
    g_down = padGetButtonsDown(&g_pad);
    g_held = padGetButtons(&g_pad);
    return true;
}

uint32_t* plat::beginDraw(int& stride) {
    u32 s = 0;
    uint32_t* p = (uint32_t*)framebufferBegin(&g_fb, &s);
    stride = (int)(s / 4);
    return p;
}

void plat::endDraw() { framebufferEnd(&g_fb); }
uint32_t plat::buttonsDown() { return mapButtons(g_down); }
uint32_t plat::buttonsHeld() { return mapButtons(g_held); }
void plat::sleepMs(int ms) { svcSleepThread((s64)ms * 1000000LL); }
#endif
