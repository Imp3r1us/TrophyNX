// In-game achievement popups: own display layer on top of the game, Ultrahand toast as fallback.
#pragma once
#include <cstdint>
#include <string>
#include "popup_config.h"   // PopupKind

struct PopupRequest {
    uint64_t titleId = 0;
    std::string achId, title, desc, game;
    std::string line;    // short extra line: progress "5 / 10", live check details...
    int points = 0, tier = 0;
    int kind = POPUP_UNLOCK;
    bool hidden = false; // secret achievement (gold accent on the compact card)
};

// The popup.json switches the rest of the module needs.
struct PopupOptions {
    bool liveCheck = true;   // "live_check": the live save check + already-unlocked popups
    bool progress = true;    // "progress": a popup when a locked achievement's counter goes up
};
PopupOptions popupOptions();

// Starts the popup thread (call once, after fs/sm are up). logf is the module's logger.
void popupStart(void (*logFn)(const char* fmt, ...));
// Queues a popup; returns immediately. Several unlocks at once are shown one after another.
void popupEnqueue(const PopupRequest& r);
// Call about once a second from the main loop: logs (once) if the popup thread is stuck and
// switches to Ultrahand toasts so unlocks are still announced.
void popupWatchdog();
// True when nothing is queued or on screen (the popup layer's buffers are free).
bool popupIdle();
// Display layers currently open (0 between popups; logged by the health check).
int popupLayersOpen();
// Safe mode: the system is close to its limits -> no display layer, Ultrahand toast only.
void popupSetSafeMode(bool on);
