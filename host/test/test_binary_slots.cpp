// binary_le "slots" (many save slots in one file), per-rule "file", "bits" (set-bit count) and "masks" (one mask
// per offset), in the app's whole-file mode and the live module's streaming mode (same results).
//   g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_binary_slots.cpp source/rules.cpp source/memrules.cpp \
//       source/fileutil.cpp source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/t
#include "rules.h"
#include <cstdio>
#include <cstring>
#include <map>

struct MemSource : SaveSource {
    std::map<std::string, std::vector<uint8_t>> files;
    bool stream = false;
    size_t avail = (size_t)-1;
    int reads = 0;
    size_t memoryAvailable() override { return avail; }
    bool open(uint64_t) override { return true; }
    void close() override {}
    std::string lastError() override { return "none"; }
    bool readAll(const std::string& rel, std::vector<uint8_t>& out) override {
        auto it = files.find(rel);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
    bool streaming() const override { return stream; }
    bool fileSize(const std::string& rel, uint64_t& size) override {
        auto it = files.find(rel);
        if (it == files.end()) return false;
        size = it->second.size();
        return true;
    }
    bool readAt(const std::string& rel, uint64_t off, void* buf, size_t len) override {
        reads++;
        auto it = files.find(rel);
        if (it == files.end() || off + len > it->second.size()) return false;
        memcpy(buf, it->second.data() + off, len);
        return true;
    }
};

static int fails = 0;
static void check(bool c, const char* what) {
    printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) fails++;
}

static const char* DEF = R"({
 "title_id": "0100000000ABC000", "name": "Slots test", "reader": "binary_le",
 "save_files": ["a.png", "b.png"],
 "slots": {
  "a.png": { "base": "0x10", "stride": "0x40", "count": 4, "check": { "offset": "0x20", "size": 1, "min": 1, "max": 9 } },
  "b.png": { "base": "0x08", "stride": "0x20", "count": 8, "magic": "SLOT" }
 },
 "achievements": [
  { "id": "a_level", "name": "a", "rule": { "type": "flag", "file": "a.png", "offset": "0x04", "size": 1, "target": 50 } },
  { "id": "a_bits", "name": "a", "rule": { "type": "sum", "file": "a.png", "offsets": ["0x08", "0x09"], "size": 1, "bits": true, "target": 10 } },
  { "id": "b_flags", "name": "b", "rule": { "type": "count", "file": "b.png", "offsets": ["0x10", "0x11", "0x12"], "masks": ["0x01", "0x80", "0x04"], "size": 1, "target": 3 } },
  { "id": "b_value", "name": "b", "rule": { "type": "flag", "file": "b.png", "offset": "0x04", "size": 2, "target": 1000 } },
  { "id": "any_04", "name": "x", "rule": { "type": "flag", "offset": "0x04", "size": 1, "target": 1 } }
 ]
})";

static double value(const Game& g, const char* id) {
    for (const Achievement& a : g.achs) if (a.id == id) return a.value;
    return -1;
}

int main() {
    GameDef def;
    std::string err;
    check(parseGameDef(DEF, def, err) && err.empty(), "definition parses");
    check(def.slots.size() == 2 && def.slots["a.png"].stride == 0x40 && def.slots["b.png"].magic == "SLOT", "slots parsed");

    MemSource src;
    // a.png: 4 slots of 0x40 from 0x10. slot 0 valid (check 3), slot 1 check 0 (skipped), slot 2 check 12 (> max,
    // skipped), slot 3 valid. The skipped slots hold bigger values that must not count.
    std::vector<uint8_t> a(0x10 + 4 * 0x40, 0);
    auto A = [&](int slot, int off) -> uint8_t& { return a[0x10 + slot * 0x40 + off]; };
    A(0, 0x20) = 3;  A(0, 0x04) = 30; A(0, 0x08) = 0xFF; A(0, 0x09) = 0x01;   // 9 bits
    A(1, 0x20) = 0;  A(1, 0x04) = 99; A(1, 0x08) = 0xFF; A(1, 0x09) = 0xFF;
    A(2, 0x20) = 12; A(2, 0x04) = 99;
    A(3, 0x20) = 1;  A(3, 0x04) = 55; A(3, 0x08) = 0x03;                       // 2 bits
    src.files["a.png"] = a;
    // b.png: slots of 0x20 from 0x08; only 3 fit in the file (the 4th is cut short), "SLOT" on slots 0 and 2.
    std::vector<uint8_t> b(0x08 + 3 * 0x20 + 0x10, 0);
    auto B = [&](int slot, int off) -> uint8_t& { return b[0x08 + slot * 0x20 + off]; };
    memcpy(&B(0, 0), "SLOT", 4); memcpy(&B(2, 0), "SLOT", 4);
    B(0, 0x10) = 0x01; B(0, 0x11) = 0x7F; B(0, 0x12) = 0x04;   // 2 of 3 (0x11 lacks bit 0x80)
    B(1, 0x10) = 0x01; B(1, 0x11) = 0x80; B(1, 0x12) = 0x04;   // 3 of 3, but no magic
    B(2, 0x10) = 0x01; B(2, 0x11) = 0x80; B(2, 0x12) = 0x00;   // 2 of 3
    B(0, 0x04) = 0xE8; B(0, 0x05) = 0x03;                      // 1000
    B(2, 0x04) = 0x10;
    memcpy(&b[0x08 + 3 * 0x20], "SLOT", 4);                    // 4th slot: magic, but only 0x10 bytes
    b[0x08 + 3 * 0x20 + 0x04] = 0xFF; b[0x08 + 3 * 0x20 + 0x05] = 0xFF;
    src.files["b.png"] = b;

    // 0: whole file (app), 1: streaming with memory (each slot read in one piece), 2: streaming with almost no
    // memory (one read per value)
    for (int stream = 0; stream < 3; stream++) {
        src.stream = stream > 0;
        src.avail = stream == 2 ? 1024 : (size_t)-1;
        src.reads = 0;
        Game g;
        g.id = def.id;
        applyGameDef(g, def);
        evaluateSave(g, src);
        printf("-- %s mode (%d reads)\n", stream == 0 ? "whole-file" : stream == 1 ? "streaming" : "streaming, low memory", src.reads);
        if (stream == 1) check(src.reads <= 16, "streaming: a few reads per slot, not one per value");
        if (stream == 2) check(src.reads > 16, "low memory: one read per value instead");
        check(g.saveStatus == SaveStatus::Ok, "save readable");
        check(value(g, "a_level") == 55, "a.png: best valid slot, skipped slots ignored (55)");
        check(value(g, "a_bits") == 9, "bits: set bits summed over offsets (9)");
        check(value(g, "b_flags") == 2, "masks: one mask per offset, slot without magic ignored (2)");
        check(value(g, "b_value") == 65535, "short last slot still read within its bytes (65535)");
        check(value(g, "any_04") == 255, "no \"file\": every file's slots count (255)");
        if (fails) for (const std::string& l : g.debug) printf("   %s\n", l.c_str());
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
