#pragma once
#include <switch.h>

typedef void (*LogFn)(const char* fmt, ...);

// Logs whether the running game's committed save image can be read from NAND (BIS User).
void probeRunningSave(u64 titleId, LogFn log);
// Logs which step of dmnt's attach fails for this process.
void diagnoseAttach(u64 pid, LogFn log);
