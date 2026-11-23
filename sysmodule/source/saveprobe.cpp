// Diagnostics for the live module:
//  - diagnoseAttach(): dmnt:cht turns every attach failure into 0x32C80D; redo its steps here
//    to learn which one fails (loader module list, or another debugger already attached).
// (The old save-image probe is now the live save watch, livesave.cpp.)
#include <switch.h>
#include <cstdio>
#include <cstring>
#include "saveprobe.h"

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
