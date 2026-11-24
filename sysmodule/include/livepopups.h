// Popups that show live save is following the game, besides the unlock popups (closescan.cpp unlockMet):
//   - first read of a session: a "live save working" card (what was read, how fast), then the already
//     unlocked achievements the save meets (at most 3, highest points first): proof it really reads it
//   - later reads: a "progress" popup when a locked achievement's counter went up (Pet Cerberus 4/10 -> 5/10),
//     at most 3 per read (closest to done first) + one "+N more" card
// popup.json: "live_check": false turns off the first, "progress": false the second.
// Portable (no libnx): tested on PC by sysmodule/test/test_livepopups.cpp.
#pragma once
#include "popup.h"
#include "rules.h"
#include <map>
#include <set>
#include <string>
#include <vector>

struct LiveFollow {   // one per game session
    bool checked = false;                 // the first read happened
    std::map<std::string, double> last;   // save-rule values at the previous read
};

// `unlockedBefore`: ids of this game in the unlock store before this read (unlockMet adds the new ones).
std::vector<PopupRequest> livePopups(LiveFollow& st, const Game& g, const std::set<std::string>& unlockedBefore,
                                     const PopupOptions& opts, unsigned long long readMs);
