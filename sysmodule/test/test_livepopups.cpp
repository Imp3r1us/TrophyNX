// Host test of the live save "following" popups (include/livepopups.h): live check on the first read,
// already-unlocked achievements met by the save, progress when a counter goes up, caps, switches.
//   g++ -std=gnu++17 -fno-exceptions -Isysmodule/include -Iinclude sysmodule/test/test_livepopups.cpp sysmodule/source/livepopups.cpp \
//       sysmodule/source/popup_draw.cpp source/overlay_data.cpp source/rules.cpp source/memrules.cpp source/fileutil.cpp \
//       source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/test_livepopups
#include "livepopups.h"
#include <cstdio>

static int fails = 0;
static void ok(bool c, const char* m) {
    if (!c) fails++;
    printf("%s %s\n", c ? "ok  " : "FAIL", m);
}

static Achievement ach(const char* id, const char* name, RuleType t, double target, int points, bool hidden = false) {
    Achievement a;
    a.id = id;
    a.name = name;
    a.points = points;
    a.hidden = hidden;
    a.rule.type = t;
    a.rule.target = target;
    a.rule.showProgress = target > 1;
    return a;
}

static Achievement* find(Game& g, const char* id) {
    for (Achievement& a : g.achs) if (a.id == id) return &a;
    return nullptr;
}

int main() {
    Game g;
    g.id = 0x0100535012974000ULL;
    g.name = "Hades";
    g.achs.push_back(ach("escaped_tartarus", "Escaped Tartarus", RuleType::Flag, 1, 10));
    g.achs.push_back(ach("no_escape", "Is There No Escape?", RuleType::Flag, 1, 50));
    g.achs.push_back(ach("cerberus_pets", "Pet Cerberus", RuleType::Count, 10, 10));
    g.achs.push_back(ach("family_secret", "Family Secret", RuleType::Count, 5, 25, true));
    g.achs.push_back(ach("broker", "Broker", RuleType::Count, 20, 10));
    g.achs.push_back(ach("home_makeover", "Home Makeover", RuleType::Count, 50, 25));
    g.achs.push_back(ach("skelly_slayer", "Skelly Slayer", RuleType::Count, 15, 10));
    g.achs.push_back(ach("t_hours_2", "Escape Attempt", RuleType::PlaytimeHours, 2, 10));   // not a save rule
    find(g, "escaped_tartarus")->value = 1;
    find(g, "no_escape")->value = 1;            // met in the save, but not unlocked yet: unlockMet announces it
    find(g, "cerberus_pets")->value = 4;
    find(g, "t_hours_2")->value = 5;
    std::set<std::string> before = {"escaped_tartarus", "t_hours_2"};
    PopupOptions on;
    LiveFollow st;

    // --- first read: the live check card, then the already-unlocked ones the save meets
    std::vector<PopupRequest> p = livePopups(st, g, before, on, 80);
    ok(p.size() == 2, "first read: live check + 1 already unlocked");
    ok(p.size() >= 1 && p[0].kind == POPUP_LIVE_CHECK && p[0].game == "Hades" && p[0].line == "2/7 met \xC2\xB7 read in 80 ms",
       "live check line: met / save achievements, read time (play time not counted)");
    ok(p.size() >= 2 && p[1].kind == POPUP_KNOWN && p[1].achId == "escaped_tartarus" && p[1].line == "met in your save",
       "already unlocked + met: shown; met but new (no_escape) left to the unlock popup");
    ok(livePopups(st, g, before, on, 80).empty(), "same values again: nothing");

    // --- Pet Cerberus 4 -> 5: a progress popup; a hidden counter going up stays quiet
    find(g, "cerberus_pets")->value = 5;
    find(g, "family_secret")->value = 2;
    p = livePopups(st, g, before, on, 75);
    ok(p.size() == 1 && p[0].kind == POPUP_PROGRESS && p[0].title == "Pet Cerberus" && p[0].line == "5 / 10" && p[0].points == 10,
       "Pet Cerberus 5 / 10");
    find(g, "cerberus_pets")->value = 10;   // reached: the unlock popup announces it, not a progress one
    ok(livePopups(st, g, before, on, 75).empty(), "counter reaching the target: no progress popup");

    // --- a burst: 4 counters up at once -> 3 cards (closest to done first) + "+1 more"
    find(g, "broker")->value = 18;           // 90 %
    find(g, "home_makeover")->value = 5;     // 10 %
    find(g, "skelly_slayer")->value = 9;     // 60 %
    find(g, "family_secret")->value = 3;     // hidden: never
    Achievement extra = ach("well_versed", "Well Versed", RuleType::Count, 9, 10);
    g.achs.push_back(extra);
    st.last["well_versed"] = 0;
    find(g, "well_versed")->value = 4;       // 44 %
    p = livePopups(st, g, before, on, 75);
    ok(p.size() == 4 && p[0].title == "Broker" && p[1].title == "Skelly Slayer" && p[2].title == "Well Versed",
       "burst: 3 progress cards, closest to done first");
    ok(p.size() == 4 && p[3].title == "+1 more achievements" && p[3].kind == POPUP_PROGRESS, "burst: then one '+N more' card");

    // --- switches in popup.json
    PopupOptions off;
    off.liveCheck = false;
    off.progress = false;
    LiveFollow s2;
    ok(livePopups(s2, g, before, off, 80).empty(), "\"live_check\": false: no first-read cards");
    find(g, "broker")->value = 19;
    ok(livePopups(s2, g, before, off, 80).empty(), "\"progress\": false: no progress cards");
    PopupOptions onlyProgress;
    onlyProgress.liveCheck = false;
    LiveFollow s3;
    ok(livePopups(s3, g, before, onlyProgress, 80).empty(), "first read without live check: silent, values remembered");
    find(g, "broker")->value = 20;
    find(g, "skelly_slayer")->value = 10;
    p = livePopups(s3, g, before, onlyProgress, 80);
    ok(p.size() == 1 && p[0].title == "Skelly Slayer" && p[0].line == "10 / 15", "progress still works without the live check");

    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
