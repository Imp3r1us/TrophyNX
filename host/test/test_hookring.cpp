// Host test of the hook ring (include/hookring.h): header, events in order, events being written, more than the ring
// holds, counter restart, and raw id -> achievement through "steam" names and "hook_ids".
//   g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_hookring.cpp source/hookring.cpp source/rules.cpp source/memrules.cpp source/fileutil.cpp source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/test_hookring
#include "hookring.h"
#include "rules.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int fails = 0;
static void ok(bool c, const char* m) {
    if (!c) fails++;
    printf("%s %s\n", c ? "ok  " : "FAIL", m);
}

using namespace hookring;

// What the hook module writes (hook/source/program/main.cpp), in the same layout.
struct FakeRing {
    std::vector<uint8_t> b = std::vector<uint8_t>(kRingSize, 0);
    uint32_t seq = 0;
    FakeRing() {
        memcpy(b.data(), "TNXRING1", 8);
        put32(8, 1);
        put32(12, Hooked);
        uint64_t tid = 0x0100633007D48000ULL;
        memcpy(&b[16], &tid, 8);
        const uint8_t bid[16] = {0xFB, 0x96, 0x8C, 0xFB, 0x64, 0x9A, 0x58, 0x42};
        memcpy(&b[24], bid, 16);
        put32(40, 1);
    }
    void put32(size_t o, uint32_t v) { memcpy(&b[o], &v, 4); }
    uint8_t* slot(uint32_t s) { return &b[kHeaderSize + (s % kSlots) * kEventSize]; }
    void push(const char* id, bool finish = true) {   // finish=false: stopped between fetch_add and the seq store
        uint32_t s = seq++;
        put32(44, seq);
        uint8_t* e = slot(s);
        memset(e, 0, kEventSize);
        memcpy(e + 16, id, strlen(id));
        if (finish) { uint32_t v = s + 1; memcpy(e, &v, 4); }
    }
    void finish(uint32_t s) { uint32_t v = s + 1; memcpy(slot(s), &v, 4); }
};

int main() {
    FakeRing r;
    Header h;
    ok(parseHeader(r.b.data(), r.b.size(), h) && h.valid && h.state == Hooked && h.titleId == 0x0100633007D48000ULL &&
           h.hooksInstalled == 1 && h.writeSeq == 0, "header");
    ok(buildHex(h.buildId) == "FB968CFB649A5842", "build id as memory rules write it");
    std::vector<uint8_t> bad = r.b;
    bad[0] = 'X';
    ok(!parseHeader(bad.data(), bad.size(), h), "wrong magic refused");
    ok(!parseHeader(r.b.data(), 32, h), "short read refused");

    Cursor c;
    Poll p = readEvents(r.b.data(), r.b.size(), c);
    ok(p.ids.empty() && !p.lost && !p.pending && c.next == 0, "empty ring");

    r.push("CHARMED");
    r.push("PROTECTED");
    p = readEvents(r.b.data(), r.b.size(), c);
    ok(p.ids.size() == 2 && p.ids[0] == "CHARMED" && p.ids[1] == "PROTECTED" && c.next == 2, "two events in order");
    p = readEvents(r.b.data(), r.b.size(), c);
    ok(p.ids.empty(), "nothing twice");

    r.push("SOULFUL", false);   // being written
    r.push("MASKED");
    p = readEvents(r.b.data(), r.b.size(), c);
    ok(p.ids.empty() && p.pending && c.next == 2, "event being written: stop before it, keep the order");
    r.finish(2);
    p = readEvents(r.b.data(), r.b.size(), c);
    ok(p.ids.size() == 2 && p.ids[0] == "SOULFUL" && p.ids[1] == "MASKED" && c.next == 4, "then both");

    for (int i = 0; i < 70; i++) { char id[16]; snprintf(id, sizeof(id), "E%d", i); r.push(id); }
    p = readEvents(r.b.data(), r.b.size(), c);
    ok(p.lost == 6 && p.ids.size() == 64 && p.ids.front() == "E6" && p.ids.back() == "E69", "70 at once: the last 64, 6 lost");

    Cursor late;   // attached after the events: still read what the ring holds
    FakeRing r2;
    r2.push("ENDING_A");
    p = readEvents(r2.b.data(), r2.b.size(), late);
    ok(p.ids.size() == 1 && p.ids[0] == "ENDING_A", "events written before the attach are read");

    Cursor stale;
    stale.next = 500;   // reader from a previous run of the game
    p = readEvents(r2.b.data(), r2.b.size(), stale);
    ok(stale.next == 1 && p.ids.empty(), "counter went back (game restarted): resync without replaying");

    // id -> achievement
    GameDef d;
    std::string err;
    const char* defText = R"({"title_id": "0100633007D48000", "name": "Hollow Knight",
        "hook_ids": {"5": "complete_graveyards"},
        "achievements": [
          {"id": "charm_first", "name": "Lucky Trinket", "desc": "x", "steam": "CHARMED",
           "rule": {"type": "flag", "path": "playerData/hasCharm", "target": 1}},
          {"id": "steam_STEELSOUL", "name": "Steel Soul", "desc": "x", "steam": "STEELSOUL", "rule": {"type": "hook"}},
          {"id": "graveyard", "name": "BONExYARD", "desc": "x", "steam": "complete_graveyards", "rule": {"type": "hook"}},
          {"id": "nohook", "name": "x", "desc": "x", "rule": {"type": "hook"}}
        ]})";
    ok(parseGameDef(defText, d, err) && d.achs.size() == 3, "definition with hook rules (one without a steam name refused)");
    ok(err.find("nohook") != std::string::npos, "the refused one is reported");
    const Achievement* a = achievementFor(d, "CHARMED");
    ok(a && a->id == "charm_first" && a->rule.type == RuleType::Flag, "Steam name -> existing save achievement");
    a = achievementFor(d, "STEELSOUL");
    ok(a && a->id == "steam_STEELSOUL" && a->rule.type == RuleType::Hook && a->rule.target == 1, "Steam name -> hook-only achievement");
    a = achievementFor(d, "5");
    ok(a && a->id == "graveyard", "number -> hook_ids -> Steam name");
    ok(!achievementFor(d, "UNKNOWN") && !achievementFor(d, ""), "unknown id");

    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails ? 1 : 0;
}
