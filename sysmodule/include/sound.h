// Unlock sound: the chime or fanfare (include/jingle.h) or the player's WAV, played on the console's audio output while
// the popup is on screen. Its own thread; the popup thread starts it and waits for it before freeing the heap.
#pragma once
#include <string>

// Starts playing for a tier (0 bronze .. 3 platinum). sound: "chime" | "fanfare" | "custom" | "off". reserve = heap bytes
// the caller still needs (the popup card): the sound is skipped when it would not leave that much.
// Returns false (logged) when nothing plays.
bool soundStart(const std::string& sound, int volume, int tier, size_t reserve);
// Waits for the sound started by soundStart() and frees its buffer. Safe to call when nothing plays.
void soundJoin();
void soundSetLog(void (*logFn)(const char* fmt, ...));
