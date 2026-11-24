// Host test of "memory" rules (include/memrules.h): address syntax, pointer chains on a fake memory,
// value types, comparisons, build matching, the 3-checks-in-a-row watcher and definition parsing.
//   g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_memrules.cpp source/memrules.cpp source/rules.cpp \
//       source/fileutil.cpp source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/test_memrules
#include "memrules.h"
#include "rules.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

static int fails = 0;
static void ok(bool c, const char* m) {
    if (!c) fails++;
    printf("%s %s\n", c ? "ok  " : "FAIL", m);
}

// Sparse little-endian memory.
struct FakeMem {
    std::map<uint64_t, uint8_t> bytes;
    void put(uint64_t a, const void* v, size_t n) { for (size_t i = 0; i < n; i++) bytes[a + i] = ((const uint8_t*)v)[i]; }
    void put64(uint64_t a, uint64_t v) { put(a, &v, 8); }
    void put32(uint64_t a, uint32_t v) { put(a, &v, 4); }
    mem::Reader reader() {
        return [this](uint64_t a, void* out, size_t n) {
            for (size_t i = 0; i < n; i++) {
                auto it = bytes.find(a + i);
                if (it == bytes.end()) return false;
                ((uint8_t*)out)[i] = it->second;
            }
            return true;
        };
    }
};

int main() {
    using namespace mem;
    // --- address syntax
    Address a;
    std::string err;
    ok(parseAddress("[[main+2D21A90]+20]+1C0", a, err) && a.region == Region::Main && a.offsets.size() == 3 &&
           a.offsets[0] == 0x2D21A90 && a.offsets[1] == 0x20 && a.offsets[2] == 0x1C0, "two pointers then an offset");
    ok(formatAddress(a) == "[[main+2D21A90]+20]+1C0", "format round trip");
    ok(parseAddress("heap+0x61C77360", a, err) && a.region == Region::Heap && a.offsets.size() == 1 && a.offsets[0] == 0x61C77360, "0x prefix, heap");
    ok(parseAddress(" [ alias - 10 ] ", a, err) && a.region == Region::Alias && a.offsets[0] == -0x10 && a.offsets[1] == 0, "spaces, minus, no trailing offset");
    ok(parseAddress("[[[[[[[main+47E81F8]+A0]]+20]+18]+78]+190]+48", a, err) && a.offsets.size() == 8 && a.offsets[2] == 0, "seven pointers, empty offset");
    ok(!parseAddress("[main+10", a, err) && !parseAddress("main+10]", a, err) && !parseAddress("code+1", a, err) &&
           !parseAddress("main+xyz", a, err) && !parseAddress("main+11111111111111111", a, err), "malformed addresses rejected");

    // --- types and comparisons
    ValType t;
    Cmp c;
    ok(parseValType("U32", t) && t == ValType::U32 && parseValType("f32", t) && t == ValType::F32 && !parseValType("u24", t), "value types");
    ok(parseCmp("&", c) && c == Cmp::BitsAny && parseCmp("", c) && c == Cmp::Ge && !parseCmp("=>", c), "comparisons");
    ok(holds(Cmp::Ge, 100, 100, 0) && !holds(Cmp::Ge, 99, 100, 0) && holds(Cmp::Eq, 1, 1, 0) && holds(Cmp::BitsAny, 6, 4, 0) &&
           !holds(Cmp::BitsAny, 3, 4, 0) && holds(Cmp::Lt, 2, 3, 0), "holds()");
    ok(!holds(Cmp::Ge, 5000000, 100, 999999), "max rejects garbage values");

    // --- pointer chains on a fake process
    FakeMem m;
    Bases b;
    b.main = 0x8000000000ULL;
    b.heap = 0x1000000000ULL;
    m.put64(b.main + 0x2D21A90, 0x2000000000ULL);        // [main+2D21A90] -> 0x2000000000
    m.put64(0x2000000000ULL + 0x20, 0x3000000000ULL);    // [... + 20] -> 0x3000000000
    m.put32(0x3000000000ULL + 0x1C0, 250);                // value
    float f = 99999.875f;
    m.put(b.heap + 0x40, &f, 4);
    int16_t neg = -5;
    m.put(b.heap + 0x50, &neg, 2);
    auto rd = m.reader();
    uint64_t addr = 0;
    parseAddress("[[main+2D21A90]+20]+1C0", a, err);
    ok(resolve(a, b, rd, addr) && addr == 0x3000000000ULL + 0x1C0, "resolve two pointers");
    double v = 0;
    ok(readValue(ValType::U32, addr, rd, v) && v == 250, "read u32 at the end of the chain");
    ok(readValue(ValType::F32, b.heap + 0x40, rd, v) && std::fabs(v - 99999.875) < 1e-3, "read f32");
    ok(readValue(ValType::S16, b.heap + 0x50, rd, v) && v == -5, "read s16 (signed)");
    parseAddress("[[main+100]+20]+1C0", a, err);
    ok(!resolve(a, b, rd, addr), "unreadable pointer -> no address");
    Bases noAlias = b;
    parseAddress("alias+10", a, err);
    ok(!resolve(a, noAlias, rd, addr), "region without a base -> no address");

    // --- build matching
    std::vector<BuildAddrs> builds(2);
    builds[0].build = "3CA12DFA";
    builds[1].build = "3CA12DFAAF9C82DA";
    ok(forBuild(builds, "3CA12DFAAF9C82DA") == &builds[1], "longest prefix wins");
    ok(forBuild(builds, "3CA12DFA00000000") == &builds[0], "short prefix matches");
    ok(forBuild(builds, "ABCDEF0123456789") == nullptr, "other version: nothing");
    uint8_t bid[32] = {0x3C, 0xA1, 0x2D, 0xFA, 0xAF, 0x9C, 0x82, 0xDA, 0x11};
    ok(buildHex(bid) == "3CA12DFAAF9C82DA", "build id hex (first 8 bytes)");

    // --- definitions (through the shared rule engine)
    const char* def = R"({ "title_id": "0100000000010000", "name": "Test",
      "achievements": [
        { "id": "rich", "name": "Rich", "desc": "Have 200 coins", "rule": { "type": "memory", "value_type": "u32", "target": 200, "max": 99999,
          "builds": { "3CA12DFAAF9C82DA": ["heap+999", "[[main+2D21A90]+20]+1C0"], "0123456789ABCDEF": "main+10" } } },
        { "id": "flt", "name": "Float", "rule": { "type": "memory", "value_type": "f32", "cmp": ">", "target": 50000,
          "builds": { "3CA12DFA": "heap+40" } } },
        { "id": "bad1", "name": "x", "rule": { "type": "memory", "builds": { "3CA1": "code+1" } } },
        { "id": "bad2", "name": "x", "rule": { "type": "memory", "value_type": "u24", "builds": { "3CA1": "main+1" } } },
        { "id": "bad3", "name": "x", "rule": { "type": "memory" } },
        { "id": "hours", "name": "h", "rule": { "type": "playtime_hours", "target": 1 } } ] })";
    GameDef gd;
    std::string perr;
    ok(parseGameDef(def, gd, perr), "definition with memory rules parses");
    ok(gd.achs.size() == 3, "3 usable achievements (memory x2 + play time)");
    ok(perr.find("bad1") != std::string::npos && perr.find("bad2") != std::string::npos && perr.find("bad3") != std::string::npos, "bad memory rules reported");
    const Achievement& rich = gd.achs[0];
    ok(rich.rule.type == RuleType::Memory && rich.rule.mem.all.size() == 1 && rich.rule.mem.all[0].builds.size() == 2 &&
           rich.rule.mem.all[0].builds[0].addrs.size() == 2 && rich.rule.mem.all[0].change == Change::None &&
           rich.rule.target == 200 && rich.rule.mem.all[0].max == 99999 && !rich.rule.showProgress && !isSaveRule(rich.rule.type), "memory rule fields");

    // --- watcher: 3 checks in a row, once
    std::vector<Watcher::Item> items = {{"rich", &gd.achs[0].rule.mem}, {"flt", &gd.achs[1].rule.mem}};
    Watcher w;
    w.reset(items, {});
    const std::string build = "3CA12DFAAF9C82DA";
    ok(w.coverage(build) == 2 && w.coverage("FFFFFFFFFFFFFFFF") == 0, "coverage per build");
    std::vector<std::string> got;
    got = w.step(build, b, rd);
    ok(got.empty() && w.lastReadFailed(), "first check: nothing yet (heap+999 unreadable, second address used)");
    got = w.step(build, b, rd);
    ok(got.empty(), "second check: nothing yet");
    got = w.step(build, b, rd);
    ok(got.size() == 2 && got[0] == "rich" && got[1] == "flt", "third check: both unlocked (250 >= 200 via the second address; 99999.875 > 50000)");
    ok(w.pending() == 0 && w.step(build, b, rd).empty(), "each unlock is reported once");
    Watcher w2;
    w2.reset(items, {"rich"});
    int unlocked = 0;
    for (int i = 0; i < 3; i++) unlocked += (int)w2.step(build, b, rd).size();
    ok(unlocked == 1 && w2.pending() == 0, "already-unlocked ids skipped; float rule unlocks");
    // a value that drops for one check resets the streak
    Watcher w3;
    std::vector<Watcher::Item> one = {{"rich", &gd.achs[0].rule.mem}};
    w3.reset(one, {});
    w3.step(build, b, rd);
    w3.step(build, b, rd);
    m.put32(0x3000000000ULL + 0x1C0, 5);       // loading screen: nonsense for one check
    w3.step(build, b, rd);
    m.put32(0x3000000000ULL + 0x1C0, 250);
    ok(w3.step(build, b, rd).empty() && w3.step(build, b, rd).empty() && w3.step(build, b, rd).size() == 1, "streak restarts after a miss");
    m.put32(0x3000000000ULL + 0x1C0, 5000000);  // above max: garbage
    Watcher w4;
    w4.reset(one, {});
    int n4 = 0;
    for (int i = 0; i < 5; i++) n4 += (int)w4.step(build, b, rd).size();
    ok(n4 == 0, "values above max never unlock");
    Watcher w5;
    w5.reset(one, {});
    int n5 = 0;
    for (int i = 0; i < 5; i++) n5 += (int)w5.step("0123456789ABCDEF", b, rd).size();
    ok(n5 == 0, "other build uses its own address (main+10 unreadable): nothing");

    // --- several conditions, "becomes" and "increase"
    // heap+100: race finished flag (u8), heap+101: place (u8), heap+104: hits taken (u32), heap+108: coins (u32)
    auto setRace = [&](uint8_t finished, uint8_t place, uint32_t hits, uint32_t coins) {
        m.put(b.heap + 0x100, &finished, 1);
        m.put(b.heap + 0x101, &place, 1);
        m.put32(b.heap + 0x104, hits);
        m.put32(b.heap + 0x108, coins);
    };
    const char* def2 = R"({ "title_id": "0100000000020000", "name": "Race",
      "achievements": [
        { "id": "flawless", "name": "Flawless", "rule": { "type": "memory", "all": [
            { "value_type": "u8", "cmp": "==", "target": 1, "change": "becomes", "builds": { "3CA12DFA": "heap+100" } },
            { "value_type": "u8", "cmp": "==", "target": 1, "builds": { "3CA12DFA": "heap+101" } },
            { "value_type": "u32", "cmp": "==", "target": 0, "max": 999, "builds": { "3CA12DFA": "heap+104" } } ] } },
        { "id": "coins", "name": "Pocket", "rule": { "type": "memory", "value_type": "u32", "change": "increase", "target": 500,
            "max": 999999, "builds": { "3CA12DFA": "heap+108" } } },
        { "id": "half", "name": "Half", "rule": { "type": "memory", "all": [
            { "value_type": "u8", "target": 1, "builds": { "3CA12DFA": "heap+100" } },
            { "value_type": "u8", "target": 1, "builds": { "0123456789ABCDEF": "heap+101" } } ] } },
        { "id": "bad4", "name": "x", "rule": { "type": "memory", "all": [] } },
        { "id": "bad5", "name": "x", "rule": { "type": "memory", "all": [ { "builds": { "3CA1": "main+1" } }, 5 ] } },
        { "id": "bad6", "name": "x", "rule": { "type": "memory", "change": "drops", "builds": { "3CA1": "main+1" } } } ] })";
    GameDef g2;
    perr.clear();
    ok(parseGameDef(def2, g2, perr) && g2.achs.size() == 3, "definition with all / change parses");
    ok(perr.find("bad4") != std::string::npos && perr.find("bad5") != std::string::npos && perr.find("bad6") != std::string::npos,
       "empty all, non-object condition, unknown change reported");
    ok(g2.achs[0].rule.mem.all.size() == 3 && g2.achs[0].rule.mem.all[0].change == Change::Becomes &&
           g2.achs[1].rule.mem.all[0].change == Change::Increase, "conditions and changes parsed");
    std::vector<Watcher::Item> race = {{"flawless", &g2.achs[0].rule.mem}, {"coins", &g2.achs[1].rule.mem}, {"half", &g2.achs[2].rule.mem}};
    Watcher wr;
    wr.reset(race, {});
    ok(wr.coverage(build) == 2, "coverage needs every condition to have the build");
    auto steps = [&](Watcher& w, int n) {
        std::vector<std::string> all;
        for (int i = 0; i < n; i++) for (const std::string& id : w.step(build, b, rd)) all.push_back(id);
        return all;
    };
    auto has = [](const std::vector<std::string>& v, const char* id) {
        for (const std::string& x : v) if (x == id) return true;
        return false;
    };
    // the game opens on a results screen already won without a hit, with 1000 coins: nothing
    setRace(1, 1, 0, 1000);
    got = steps(wr, 6);
    ok(!has(got, "flawless") && !has(got, "coins") && !has(got, "half"), "already true at start: becomes / increase / other build stay locked");
    // next race (flag 0 for 3 checks) is won, but with a hit: nothing
    setRace(0, 3, 0, 1100);
    steps(wr, 3);
    setRace(1, 1, 2, 1200);
    ok(!has(steps(wr, 5), "flawless"), "won with a hit: locked");
    // one condition drops for a check: the streak restarts
    setRace(0, 3, 0, 1300);
    steps(wr, 3);
    setRace(1, 1, 0, 1350);
    steps(wr, 2);
    setRace(1, 2, 0, 1400);
    steps(wr, 1);
    setRace(1, 1, 0, 1450);
    got = steps(wr, 2);
    ok(!has(got, "flawless"), "streak restarts when one condition drops");
    got = steps(wr, 1);
    ok(has(got, "flawless") && !has(got, "coins"), "won without a hit, 3 checks in a row: unlocked; 450 coins gained: not yet");
    setRace(0, 0, 0, 1499);
    ok(!has(steps(wr, 3), "coins"), "499 coins gained: not yet");
    setRace(0, 0, 0, 1500);
    ok(has(steps(wr, 3), "coins"), "500 coins gained since the lowest value: unlocked");
    // "increase": another save with fewer coins restarts the count from there
    Watcher wc;
    std::vector<Watcher::Item> coinOnly = {{"coins", &g2.achs[1].rule.mem}};
    wc.reset(coinOnly, {});
    setRace(0, 0, 0, 2000);
    steps(wc, 3);
    setRace(0, 0, 0, 100);
    steps(wc, 3);
    setRace(0, 0, 0, 550);
    ok(steps(wc, 5).empty(), "450 above the new low: locked (not 2000 -> 550)");
    setRace(0, 0, 0, 600);
    ok(steps(wc, 3).size() == 1, "500 above the new low: unlocked");
    // garbage values (above max) are ignored by increase
    Watcher wg;
    wg.reset(coinOnly, {});
    setRace(0, 0, 0, 10);
    steps(wg, 2);
    setRace(0, 0, 0, 5000000);
    ok(steps(wg, 5).empty(), "garbage above max neither unlocks nor moves the base");
    setRace(0, 0, 0, 4);
    steps(wg, 1);
    setRace(0, 0, 0, 504);
    ok(steps(wg, 3).size() == 1, "base follows real lows only");

    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
