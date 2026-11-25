// Host test of the live play-log achievements (rules.h: playStatsMet, parseUniversal) and the overlay's live
// memory test file (overlay_data.h: memTestJson / parseMemTest / valueText).
//   g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_playstats.cpp source/overlay_data.cpp source/rules.cpp \
//       source/memrules.cpp source/fileutil.cpp source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c
#include "overlay_data.h"
#include "rules.h"
#include <cstdio>
#include <set>
#include <string>
#include <vector>

static int fails = 0;
static void ok(bool c, const char* m) {
    if (!c) fails++;
    printf("%s %s\n", c ? "ok  " : "FAIL", m);
}

int main() {
    // --- universal.json from text, as the module embeds it
    const char* uni = R"({ "per_game": [
        { "id": "u_first", "name": "Press Start", "points": 5, "rule": { "type": "launches", "target": 1 } },
        { "id": "u_25", "name": "Regular", "rule": { "type": "launches", "target": 25 } },
        { "id": "u_h10", "name": "Getting Into It", "rule": { "type": "playtime_hours", "target": 10 } },
        { "id": "u_days", "name": "Week of Play", "rule": { "type": "days_played", "target": 7 } } ],
      "global": [ { "id": "g5", "name": "Collector", "rule": { "type": "games_played", "target": 5 } } ] })";
    std::vector<Achievement> per, global;
    std::string err;
    ok(parseUniversal(uni, per, global, err) && per.size() == 4 && global.size() == 1, "universal.json parsed from text");
    ok(!parseUniversal("not json", per, global, err) && !parseUniversal(nullptr, per, global, err), "bad text: false");
    parseUniversal(uni, per, global, err);

    // --- which play-log achievements are met
    auto ids = [](const std::vector<const Achievement*>& v) {
        std::string s;
        for (const Achievement* a : v) s += a->id + ",";
        return s;
    };
    ok(ids(playStatsMet(per, 0.1, 1, {})) == "u_first,", "first launch: Press Start only");
    ok(ids(playStatsMet(per, 10.0, 25, {})) == "u_first,u_25,u_h10,", "exact targets count (10 h, 25 launches)");
    ok(ids(playStatsMet(per, 9.99, 24, {})) == "u_first,", "just below: not yet");
    ok(ids(playStatsMet(per, 50, 99, {"u_first", "u_h10"})) == "u_25,", "already unlocked ones skipped");
    ok(ids(playStatsMet(per, 1000, 1000, {})).find("u_days") == std::string::npos, "other rule types (days played) left to the app");
    ok(isPlayStatsRule(RuleType::PlaytimeHours) && isPlayStatsRule(RuleType::Launches) && !isPlayStatsRule(RuleType::Flag), "rule kinds");

    // --- live memory test file (module -> overlay)
    ov::MemTest t;
    t.titleId = 0x0100000000010000ULL;
    t.build = "3CA12DFAAF9C82DA";
    t.watched = 2;
    t.total = 3;
    t.reading = true;
    ov::MemTestItem a;
    a.id = "rich";
    a.target = 10000;
    a.value = 1234;
    a.read = true;
    ov::MemTestItem b;
    b.id = "boss";
    b.cmp = "becomes";
    b.target = 1;
    b.holds = true;
    b.read = true;
    b.value = 1;
    b.done = true;
    ov::MemTestItem c;
    c.id = "other";
    c.target = 5;
    t.items = {a, b, c};
    ov::MemTest r;
    ok(ov::parseMemTest(ov::memTestJson(t).c_str(), r), "memory test round trip parses");
    ok(r.titleId == t.titleId && r.build == t.build && r.watched == 2 && r.total == 3 && r.reading && r.items.size() == 3, "header kept");
    ok(r.items[0].id == "rich" && r.items[0].read && r.items[0].value == 1234 && r.items[0].cmp == ">=" && !r.items[0].holds,
       "value, default comparison");
    ok(r.items[1].cmp == "becomes" && r.items[1].holds && r.items[1].done, "change rule, held, unlocked");
    ok(!r.items[2].read && r.items[2].value == 0, "no value for this version");
    ov::MemTest n;
    n.titleId = 1;
    n.reading = false;
    n.note = "no addresses for this version";
    ok(ov::parseMemTest(ov::memTestJson(n).c_str(), r) && !r.reading && r.note == n.note && r.items.empty(), "not reading: note kept");
    ok(!ov::parseMemTest("{}", r) && !ov::parseMemTest("oops", r), "no title: false");
    ok(ov::memTestPath("sdmc:/switch/TrophyNX", 0x0100000000010000ULL) == "sdmc:/switch/TrophyNX/overlay/0100000000010000.memtest.json",
       "memory test path");
    ok(ov::valueText(1234567) == "1,234,567" && ov::valueText(-1000) == "-1,000" && ov::valueText(12) == "12" &&
           ov::valueText(3.5) == "3.50" && ov::valueText(0) == "0",
       "value text");

    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
