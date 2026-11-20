// Save captures on close (captures.h): when a game the player records closes, copy its whole save.
#pragma once
#include <switch.h>

enum CaptureStep { CAPSTEP_OFF, CAPSTEP_DONE, CAPSTEP_LOCKED };

// CAPSTEP_LOCKED: the save could not be mounted yet (the caller retries a few seconds later).
CaptureStep captureOnClose(u64 titleId, const AccountUid& uid, int sessionMinutes, void (*logf)(const char* fmt, ...));
