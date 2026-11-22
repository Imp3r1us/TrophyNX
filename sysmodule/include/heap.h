// The module's heap, sized on demand.
//
// Every background module shares one "system" memory group (~306 MB), and on a console with a few
// sysmodules it can be almost full: when a game launches, Nintendo's own modules then fail with
// 2001-0132 (LimitReached) and the console shuts down. So the module keeps a small heap while idle
// (HEAP_IDLE_SIZE) and only grows it (HEAP_BIG_SIZE) while a popup is on screen or a save is read,
// when there is room for it, then gives the memory back.
#pragma once
#include <cstddef>

#define HEAP_IDLE_SIZE 0x200000   // 2 MB (svcSetHeapSize granularity): definitions, flag scanner, logs
#define HEAP_BIG_SIZE  0x400000   // 4 MB: popup layer buffers, scan on close (one save file at a time)

// Takes the big heap for `why` (logged on failure). False: not enough free system memory, skip the task.
// Nested/parallel leases (popup thread + main loop) are counted; each successful acquire needs a release.
bool heapAcquire(const char* why);
void heapRelease();
// Current heap size, for the health log.
size_t heapSize();
// Logger for the heap (main.cpp's logf).
void heapSetLog(void (*logFn)(const char* fmt, ...));
