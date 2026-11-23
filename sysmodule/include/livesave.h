// Live save watch: popups while the game runs, for every game with a save reader.
// A running game keeps its save mounted (locked), but each time it saves, FS commits the new state to the save
// image in NAND (User:/save/<SaveDataId>). The module reads that image from the partition's raw sectors
// (rawfat.h: never opening the file, so never competing with the game's lock), notices a new commit from the
// header hash (one 32 KB read every few seconds), then reads the save's files out of the image (saveimage.h)
// with the same rule engine as the app and the scan on close, and unlocks what is met with popups.
#pragma once
#include <switch.h>

typedef void (*LiveLogFn)(const char* fmt, ...);

// A game started (its definition is looked up here; games without a save reader are ignored).
void liveSaveStart(u64 titleId, u64 now, LiveLogFn log);
// The game closed or changed.
void liveSaveStop();
// Called every second while the game runs. Does nothing most of the time.
// canRead: no popup on screen (popup buffers and save reading share the module's heap).
void liveSaveTick(u64 now, bool canRead, LiveLogFn log);
