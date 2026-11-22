// In-game achievement popups: own display layer on top of the game, Ultrahand toast as fallback.
#pragma once
#include <cstdint>
#include <string>

struct PopupRequest {
    uint64_t titleId = 0;
    std::string achId, title, desc, game;
    int points = 0, tier = 0;
};

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
