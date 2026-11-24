// Popup settings: sdmc:/switch/TrophyNX/popup.json. Read by the live module before every popup, edited in
// the app (right stick: Popup settings). Portable: shared by the app, the module and the host tests.
#pragma once
#include "popup_draw.h"
#include <string>
#include <vector>

// What a popup announces. Unlock is the achievement popup; the others show that the live module is following
// the game ("live_check" and "progress").
enum PopupKind {
    POPUP_UNLOCK = 0,    // a new achievement
    POPUP_PROGRESS,      // a counter went up while the achievement is still locked (line: "5 / 10")
    POPUP_LIVE_CHECK,    // first live-save read of a session (title: "Working: <game>", line: what was read)
    POPUP_KNOWN,         // already unlocked, met again in the save (first read of a session only)
};

// What a popup says, whatever the card (the module fills it from a PopupRequest, the app's preview from samples).
struct PopupText {
    int kind = POPUP_UNLOCK;
    std::string title, desc, game, line;
    int points = 0, tier = 0;
    bool hidden = false;
};

struct PopupSettings {
    // where: "custom" (own layer), "both" (+ Ultrahand toast), "ultrahand" (toast only), "off"
    std::string style = "custom";
    bool compact = false;          // "card": "compact" or "classic"
    int durationMs = 0;            // "duration_ms": 0 = the card's default (classic 4 s, compact 3.3 s + animation)
    bool liveCheck = true;         // "live_check"
    bool progress = true;          // "progress"
    // classic card
    bool classicDark = true;       // "theme": "dark" / "light"
    bool classicIconLeft = false;  // "icon": "left" / "right"
    // compact card
    int corner = CORNER_TOP_LEFT;  // "position"
    std::string toastTheme = "dark";   // "compact_theme": see popupToastThemes()
    std::string accent = "tier";       // "accent": "tier" or a colour name from popupAccents()
    int opacity = 88;              // "opacity": card background, 50..100 %
    bool border = true;            // "border"
    bool icon = true;              // "show_icon"
    bool label = true;             // "show_label": the small uppercase line
};

struct NamedColor { const char* name; const char* label; uint32_t rgb; };
// Compact card themes: name, label shown in the app, background colour (text colours follow the theme).
const std::vector<NamedColor>& popupToastThemes();
// Fixed accents (besides "tier"): name, label, colour.
const std::vector<NamedColor>& popupAccents();
// Durations offered in the app (ms, 0 = auto).
const std::vector<int>& popupDurations();
const char* popupStyleLabel(const std::string& style);   // "On screen", "On screen + Ultrahand", ...
const char* popupCornerName(int corner);                 // "top-left"...
const char* popupCornerLabel(int corner);                // "Top left"...

// Parses popup.json text (missing keys keep their defaults, invalid values are reported in `warnings`).
// false only when the text is not JSON at all (settings then stay as they were).
bool popupSettingsParse(const char* json, PopupSettings& out, std::string* warnings = nullptr);
// The whole file, every key written (readable, one per line).
std::string popupSettingsJson(const PopupSettings& s);

// Texts of each card for a popup (same in the module and in the app's preview).
ToastInfo popupToastTexts(const PopupText& t, const PopupSettings& s);
void popupClassicTexts(const PopupText& t, std::string& title, std::string& desc);

// The compact card's look from the settings, and its accent for an achievement.
ToastStyle popupToastStyle(const PopupSettings& s);
uint32_t popupAccent(const PopupSettings& s, int tier, bool hidden);
