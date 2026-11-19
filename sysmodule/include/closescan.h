// Scan on close: when a game closes, its save is no longer locked, so the module reads it with the
// app's own rule engine (source/rules.cpp) and unlocks what is met right away, with popups.
// Covers every game whose definition has a save reader, not only those with a live mode.
#pragma once
#include <switch.h>
#include <cstdint>
#include <string>

enum CloseScanResult { CS_SKIPPED, CS_DONE, CS_LOCKED };

// Definitions: built into the module (romfs/games, see make_data.sh), then
// sdmc:/switch/SwitchAchievements/games/*.json overrides, like the app.
// CS_LOCKED: the save couldn't be mounted (game started again?) - the caller may retry.
CloseScanResult closeScan(uint64_t titleId, const AccountUid& uid, void (*logf)(const char* fmt, ...));
