#include "capture.h"
#include "captures.h"
#include "store.h"
#include <ctime>
#include <string>

CaptureStep captureOnClose(u64 tid, const AccountUid& uid, int sessionMinutes, void (*logf)(const char*, ...)) {
    CaptureSettings s = loadCaptureSettings(dataDir());
    if (!captureEnabled(s, tid)) return CAPSTEP_OFF;
    Result rc = fsdevMountSaveDataReadOnly("capsave", tid, uid);
    if (R_FAILED(rc)) {
        if (rc == 0x7D402) { logf("capture: %016llX has no save data for this user", (unsigned long long)tid); return CAPSTEP_DONE; }
        logf("capture: %016llX save not available yet (rc=0x%X)", (unsigned long long)tid, rc);
        return CAPSTEP_LOCKED;
    }
    std::string msg;
    u64 t0 = armGetSystemTick();
    captureFromDir("capsave:/", dataDir(), tid, "", (uint64_t)time(NULL), sessionMinutes, s.keep, msg);
    fsdevUnmountDevice("capsave");
    logf("%s [%016llX, %llu ms]", msg.c_str(), (unsigned long long)tid,
         (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000));
    return CAPSTEP_DONE;
}
