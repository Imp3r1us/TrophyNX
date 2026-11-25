// Scan on close: when a game closes, its save is no longer locked, so the module reads it with the
// app's own rule engine (source/rules.cpp) and unlocks what is met right away, with popups.
// Covers every game whose definition has a save reader, not only those with a live mode.
#pragma once
#include <switch.h>
#include <cstdint>
#include <string>
#include "rules.h"

enum CloseScanResult { CS_SKIPPED, CS_DONE, CS_LOCKED };

// Definitions: built into the module (romfs/games, see make_data.sh), then
// sdmc:/switch/TrophyNX/games/*.json overrides, like the app.
// CS_LOCKED: the save couldn't be mounted (game started again?) - the caller may retry.
// The definition of a title: built-in, then the SD card's games/*.json (later wins). source = where it came from.
bool findDef(uint64_t titleId, GameDef& out, std::string& source, void (*logf)(const char* fmt, ...));

// Unlocks the save achievements of g (already evaluated) that are met, with popups; writes the report to
// <data dir>/<reportName>. tag: "on close" or "live" (log lines). Returns the number of new unlocks.
int unlockMet(const Game& g, const GameDef& def, const std::string& defSource, const AccountUid& uid, const char* tag,
              unsigned long long ms, const char* reportName, void (*logf)(const char* fmt, ...));

// Free bytes in the module's fixed heap, minus a safety margin.
size_t moduleMemoryAvailable();
// Grows the module heap so that moduleMemoryAvailable() >= bytes, if the system has room (heap.h).
// Whoever reads saves calls heapUnreserve() when done.
bool moduleReserveMemory(size_t bytes);

CloseScanResult closeScan(uint64_t titleId, const AccountUid& uid, void (*logf)(const char* fmt, ...));
