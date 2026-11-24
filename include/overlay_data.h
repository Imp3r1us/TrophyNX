// Data for the Tesla overlay (overlay/): what it shows for the running game, read from the SD card.
//
//   <data>/overlay/<TID>.json       snapshot written by the app after each scan: every achievement of the game
//                                   (names, descriptions, points, tier, target, progress at scan time)
//   <data>/overlay/<TID>.live.json  progress written by the live module while the game runs: {"<id>": value}
//   <data>/unlocks/<user>.json      the unlock store (the truth for unlocked / when)
//
// The overlay merges the three, so unlocks and progress made during play show without opening the app.
// Pure code (no Switch services): shared by the app, the live module, the overlay and the host tests.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ov {

enum Tier { TIER_BRONZE = 0, TIER_SILVER = 1, TIER_GOLD = 2, TIER_PLATINUM = 3 };

struct Ach {
    std::string id, name, desc;
    int points = 10;
    int tier = TIER_BRONZE;
    bool hidden = false;         // description hidden until unlocked
    bool showProgress = false;   // "37 / 100" while locked
    double target = 1, divisor = 1, value = 0;
    // merged in by the overlay
    bool unlocked = false;
    uint64_t unlockTime = 0;
};

struct GameData {
    uint64_t titleId = 0;
    std::string name;
    std::vector<Ach> achs;
    int unlocked = 0, points = 0, maxPoints = 0;
};

// Same default as the live module's popups: 50+ points gold, 25+ silver, otherwise bronze.
int tierFor(const std::string& name, int points);
const char* tierName(int tier);

std::string titleHex(uint64_t titleId);   // "0100ABCD12345000"
std::string snapshotPath(const std::string& dataDir, uint64_t titleId);
std::string livePath(const std::string& dataDir, uint64_t titleId);

// Snapshot (app side writes, overlay reads).
std::string snapshotJson(const GameData& g);
bool parseSnapshot(const char* json, GameData& out);

// Live progress: {"<id>": value}. mergeLive() returns `existing` with `updates` applied (existing may be empty or broken).
std::string mergeLive(const std::string& existing, const std::map<std::string, double>& updates);
std::map<std::string, double> parseLive(const char* json);

// Unlock times of one game from the unlock store's text ("<TID>/<id>": time), without parsing the whole file:
// the store holds every game and the overlay's memory is small.
std::map<std::string, uint64_t> storeEntriesFor(const std::string& storeText, uint64_t titleId);

// Applies unlocks and live values, then counts unlocked / points.
void merge(GameData& g, const std::map<std::string, uint64_t>& unlocks, const std::map<std::string, double>& live);

// What the overlay lists: indexes into g.achs.
struct Sections {
    std::vector<size_t> inProgress;   // locked, with progress shown and started: closest to done first
    std::vector<size_t> locked;       // other locked ones, file order
    std::vector<size_t> unlocked;     // most recent first
};
Sections sections(const GameData& g);

// Texts shared by the list and the detail page.
std::string progressText(const Ach& a);   // "37 / 100" (same rounding as the app)
double progressRatio(const Ach& a);       // 0..1
std::string dateText(uint64_t unixTime);  // "2026-11-23"
std::string descText(const Ach& a);       // the hidden placeholder while hidden and locked

// Word wrap with a width measure (pixels in the overlay, characters in tests). Long words are cut.
std::vector<std::string> wrap(const std::string& text, int maxWidth, const std::function<int(const std::string&)>& measure,
                              size_t maxLines = 8);

}  // namespace ov
