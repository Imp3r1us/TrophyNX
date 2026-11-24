// Achievement popup renderer (NinBadges-style card). Portable: no libnx, so it is tested on PC.
// Everything is designed in 1280x720 units and drawn at POPUP_SCALE (1.5 = native 1080p, no blur).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#define POPUP_SCALE 1.5f
// Canvas in 720p units: wide enough for long names, tall enough for icon + medal.
static const int POPUP_W720 = 480;
static const int POPUP_H720 = 100;
// Layer top in 720p units (the card's text bar starts 16 units lower, at y = 78 like the concept).
static const int POPUP_Y720 = 62;

enum PopupTier { TIER_BRONZE = 0, TIER_SILVER = 1, TIER_GOLD = 2, TIER_PLATINUM = 3 };

struct PopupImage {   // straight RGBA8
    int w = 0, h = 0;
    std::vector<uint8_t> px;
};

struct PopupInfo {
    std::string title, desc;
    int tier = TIER_BRONZE;
    bool dark = true;        // dark translucent bar + white text, or white bar + dark text
    bool iconLeft = false;   // false: text then icon (concept 2); true: icon at the screen edge (concept 3)
    const PopupImage* icon = nullptr;   // nullptr: neutral placeholder
};

// The font must stay in memory (the Switch shared font does).
bool popupFontInit(const uint8_t* ttf);

// Renders the full card once into a straight-RGBA8 image of the canvas size (scaled).
// Returns the card's width in canvas pixels (for the slide distance).
int popupRenderCard(const PopupInfo& info, PopupImage& out);

// Scales an RGBA8 image (box filter when shrinking).
void popupScaleImage(const uint8_t* src, int sw, int sh, int dw, int dh, PopupImage& out);

// Tier from an explicit name ("bronze", "silver", "gold", "platinum") or from the points value.
int popupTierFor(const char* name, int points);

// Slide position for time t (ms) in an animation of total duration d: 0 = hidden, 1 = fully shown.
float popupSlide(uint64_t t, uint64_t d);

// ---------------------------------------------------------------- compact toast ("card": "compact")
// Small dark card, designed directly in 1080p pixels (the layer's space): rounded, thin light border,
// 44 px icon on the left, then up to 3 lines: an uppercase label in the accent colour, the title, a short line.
static const int TOAST_W = 360, TOAST_H = 70;
static const int TOAST_SLIDE = 24;    // px the card slides in from its screen edge (the layer is this much wider)
static const int TOAST_MARGIN = 24;   // px from the screen edges
static const uint32_t ACCENT_CYAN = 0x38BDF8, ACCENT_GOLD = 0xFBBF24, ACCENT_VIOLET = 0xC084FC;
enum PopupCorner { CORNER_TOP_LEFT = 0, CORNER_TOP_RIGHT, CORNER_BOTTOM_LEFT, CORNER_BOTTOM_RIGHT };

struct ToastInfo {
    std::string label;   // e.g. "ACHIEVEMENT UNLOCKED" (drawn as is: pass it in capitals)
    std::string title;   // cut with "..." to fit
    std::string line;    // optional short line, e.g. "+25 pts" or "5 / 10"
    uint32_t accent = ACCENT_CYAN;
    const PopupImage* icon = nullptr;   // nullptr: a neutral tile with a star
};

// Renders the card into a TOAST_W x TOAST_H straight-RGBA8 image.
void popupRenderToast(const ToastInfo& info, PopupImage& out);
// Bronze / silver: cyan. Gold, or a hidden (secret) achievement: gold. Platinum: violet.
uint32_t popupAccentFor(int tier, bool hidden);
// "top-left", "top-right", "bottom-left", "bottom-right"; false (out unchanged) for anything else.
bool popupParseCorner(const char* name, int& out);
// Layer rectangle on the 1920x1080 screen for a card of w x h with the slide room on the screen-edge side.
void popupToastLayer(int corner, int w, int h, int& x, int& y, int& lw, int& lh);
// Where the card sits inside that layer (px from the layer's left) when fully shown, and when hidden.
int popupToastCardX(int corner, float shown);
// Timing: 220 ms in (slide + fade), hold, 180 ms out. hold = holdMs, or 1700 ms when other popups wait.
static const int TOAST_IN_MS = 220, TOAST_OUT_MS = 180, TOAST_HOLD_MS = 3300, TOAST_HOLD_BUSY_MS = 1700;
int popupHoldMs(int holdMs, bool othersWaiting);
// Visibility at time t (ms) of a toast lasting `total` ms: 0 = hidden, 1 = fully shown (eased).
// Used for both the slide and the opacity.
float popupToastShown(uint64_t t, uint64_t total);
