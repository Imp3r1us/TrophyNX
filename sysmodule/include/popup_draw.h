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
