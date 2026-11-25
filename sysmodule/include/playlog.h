// Play-log achievements while you play: "Play 10 hours", "Launch 25 times"... (rules "playtime_hours" and
// "launches", from the game's definition and universal.json). The app only checks them on its scans; the module
// reads the console's play log (pdm) once a minute while a game runs and unlocks them with a popup.
#pragma once
#include <cstdint>

void playlogInit(void (*logFn)(const char* fmt, ...));   // once, after the other services
void playlogStart(uint64_t titleId, uint64_t now);       // a game started (real games only, not homebrew)
void playlogStop();                                      // no game, or homebrew
void playlogTick(uint64_t now);                          // every loop turn; acts once a minute
