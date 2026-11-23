#pragma once
#include <switch.h>

typedef void (*LogFn)(const char* fmt, ...);

// Logs which step of dmnt's attach fails for this process.
void diagnoseAttach(u64 pid, LogFn log);
