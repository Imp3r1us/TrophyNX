// Host test of the Tesla overlay's data (include/overlay_data.h): snapshot round trip, live progress merge,
// unlock store lookup for one game, ordering, progress texts and word wrap.
//   g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_overlay_data.cpp source/overlay_data.cpp -x c source/cJSON.c -o /tmp/test_overlay_data
#include "overlay_data.h"
#include <cstdio>
#include <cstring>

static int fails = 0;
static void ok(bool c, const char* m) {
    if (!c) fails++;
    printf("%s %s\n", c ? "ok  " : "FAIL", m);
}

int main() {
    using namespace ov;
    // --- tiers
    ok(tierFor("", 50) == TIER_GOLD && tierFor("", 25) == TIER_SILVER && tierFor("", 10) == TIER_BRONZE && tierFor("platinum", 1) == TIER_PLATINUM &&
           tierFor("bronze", 90) == TIER_BRONZE, "tier names and point defaults (as the popups)");
    ok(titleHex(0x01007EF00011E000ULL) == "01007EF00011E000", "title id hex");
    ok(snapshotPath("sdmc:/switch/TrophyNX", 0x01007EF00011E000ULL) == "sdmc:/switch/TrophyNX/overlay/01007EF00011E000.json" &&
           livePath("d", 1) == "d/overlay/0000000000000001.live.json", "paths");

    // --- snapshot round trip
    GameData g;
    g.titleId = 0x01007EF00011E000ULL;
    g.name = "Zelda \"BotW\" \xC3\xA9";
    Ach a;
    a.id = "shrines"; a.name = "Shrine Seeker"; a.desc = "Clear 120 shrines"; a.points = 50; a.tier = TIER_GOLD;
    a.showProgress = true; a.target = 120; a.value = 37;
    g.achs.push_back(a);
    a = Ach();
    a.id = "hearts"; a.name = "Hearty"; a.desc = "secret"; a.points = 25; a.tier = TIER_SILVER; a.hidden = true;
    a.showProgress = true; a.target = 120; a.divisor = 4; a.value = 40;
    g.achs.push_back(a);
    a = Ach();
    a.id = "first"; a.name = "First steps"; a.desc = "Leave the cave"; a.points = 10;
    g.achs.push_back(a);
    a = Ach();
    a.id = "koroks"; a.name = "Koroks"; a.desc = "Find 900"; a.points = 10; a.showProgress = true; a.target = 900; a.value = 899;
    g.achs.push_back(a);
    std::string js = snapshotJson(g);
    GameData r;
    ok(parseSnapshot(js.c_str(), r), "snapshot parses");
    ok(r.titleId == g.titleId && r.name == g.name && r.achs.size() == 4, "title, escaped UTF-8 name, count");
    ok(r.achs[0].id == "shrines" && r.achs[0].points == 50 && r.achs[0].tier == TIER_GOLD && r.achs[0].showProgress &&
           r.achs[0].target == 120 && r.achs[0].value == 37 && !r.achs[0].hidden, "fields kept");
    ok(r.achs[1].hidden && r.achs[1].divisor == 4 && r.achs[2].value == 0 && !r.achs[2].showProgress && r.achs[2].target == 1, "hidden, divisor, defaults");
    GameData bad;
    ok(!parseSnapshot("{broken", bad) && !parseSnapshot(nullptr, bad) && !parseSnapshot("{\"achievements\":[]}", bad), "broken / empty snapshots rejected");

    // --- live progress
    std::string live = mergeLive("", {{"shrines", 40}});
    live = mergeLive(live, {{"koroks", 12}});
    live = mergeLive(live, {{"shrines", 41}});
    std::map<std::string, double> lv = parseLive(live.c_str());
    ok(lv.size() == 2 && lv["shrines"] == 41 && lv["koroks"] == 12, "merge keeps other ids, updates values");
    ok(parseLive("not json").empty() && mergeLive("garbage", {{"x", 1}}).find("\"x\":1") != std::string::npos, "broken live file replaced");

    // --- unlock store (the app's / module's format) for one game
    const char* store =
        "{\n"
        "  \"0100000000000001/first\": 1700000000,\n"
        "  \"01007EF00011E000/first\": 1763900000,\n"
        "  \"01007EF00011E000/koroks\": 1763990000,\n"
        "  \"global/games_5\": 1763000000,\n"
        "  \"01007EF00011E000/odd\" : 5\n"
        "}\n";
    std::map<std::string, uint64_t> st = storeEntriesFor(store, 0x01007EF00011E000ULL);
    ok(st.size() == 3 && st["first"] == 1763900000ULL && st["koroks"] == 1763990000ULL && st["odd"] == 5, "only this game's keys, with times");
    ok(storeEntriesFor("", 1).empty() && storeEntriesFor("{\"0000000000000001/x\": \"str\"}", 1).empty(), "empty store, non-number values");

    // --- merge and sections
    merge(r, st, lv);
    ok(r.unlocked == 2 && r.points == 20 && r.maxPoints == 95, "unlocked count and points");
    ok(r.achs[0].value == 41 && r.achs[3].value == 12 && r.achs[1].value == 40, "live values override the snapshot");
    Sections s = sections(r);
    ok(s.inProgress.size() == 1 && s.inProgress[0] == 0, "in progress: started, shown, not hidden (hearts is hidden)");
    ok(s.locked.size() == 1 && s.locked[0] == 1, "locked: the hidden one");
    ok(s.unlocked.size() == 2 && s.unlocked[0] == 3 && s.unlocked[1] == 2, "unlocked: most recent first");

    // --- texts
    Ach p;
    p.target = 120; p.value = 37;
    ok(progressText(p) == "37 / 120", "progress text");
    p.divisor = 4; p.value = 41;
    ok(progressText(p) == "10.2 / 30", "progress with divisor (quarter hearts)");
    p.divisor = 1; p.target = 100000; p.value = 250000;
    ok(progressText(p) == "100,000 / 100,000" && progressRatio(p) == 1, "capped at target, thousands separators");
    ok(dateText(1763900000ULL) == "23 Nov 2025" && dateText(0).empty(), "date like the app");
    Ach h;
    h.hidden = true; h.desc = "secret";
    ok(descText(h) == "Hidden achievement - keep playing!", "hidden description");
    h.unlocked = true;
    ok(descText(h) == "secret", "shown once unlocked");

    // --- wrap (1 unit per byte)
    auto len = [](const std::string& x) { return (int)x.size(); };
    auto w = wrap("the quick brown fox jumps", 10, len);
    ok(w.size() == 3 && w[0] == "the quick" && w[1] == "brown fox" && w[2] == "jumps", "word wrap");
    w = wrap("abcdefghijklmnopqrstuvwxy", 10, len);
    ok(w.size() == 3 && w[0] == "abcdefghij" && w[2] == "uvwxy", "long word cut");
    w = wrap("\xC3\xA9\xC3\xA9\xC3\xA9", 3, len);   // "ééé", 2 bytes each
    ok(w.size() == 3 && w[0] == "\xC3\xA9" && w[2] == "\xC3\xA9", "cuts on UTF-8 boundaries");
    w = wrap("one two three four five six", 9, len, 2);
    ok(w.size() == 2 && w[1].size() >= 3 && w[1].compare(w[1].size() - 3, 3, "...") == 0, "too many lines: last one ends with ...");
    w = wrap("line one\nline two", 40, len);
    ok(w.size() == 2 && w[0] == "line one" && w[1] == "line two", "newlines kept");
    ok(wrap("", 10, len).empty(), "empty text");

    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
