// "sgb" reader (Hades SGB1 saves): header, LZ4 block, luabins, paths. Synthetic save built with python-lz4
// (same bytes as the Save Lab's test in tools/savelab/test/test_formats.js).
//   g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_sgb.cpp source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/test_sgb
#include "save_formats.h"
#include <cstdio>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const char* HEX = "5347423100000000100000000000000000000000110000004c6f636174696f6e5f54617274617275732a000000070000000300000000010000000004000000526f6f6d000000003f010000f10901540000000003000000530900000047616d6553746174651700f10504000000530b00000051756573745374617475731900f10602000000530c000000576561706f6e436c656172734100914361736865644f75741f00f50c5061637455706772616465735308000000556e6c6f636b6564530d3d00021300025800140171005253776f72645d00325402000100124e070023f03f0900231440090014000900f701f03f530a000000456e656d794b696c6c5500f3020d000000547261696e696e674d656c65653c00b230405305000000466c61672e00010b01f12808000000486172644d6f64653153030000004f66663053070000004e6f7468696e672d530f000000546578744c696e65735265636f72641c0101c400110887007164696e673031313800df50616464696e6753c8000000780100af507878787878";

static double one(const SgbDoc& d, const char* path, size_t* n = nullptr) {
    std::vector<BymlValue> v;
    d.query(path, v);
    if (n) *n = v.size();
    double s = 0;
    for (auto& x : v) s += x.kind == BymlValue::String ? (x.str.empty() ? 0 : 1) : x.num;
    return s;
}

int main() {
    std::vector<uint8_t> b;
    for (size_t i = 0; HEX[i] && HEX[i + 1]; i += 2) b.push_back((uint8_t)std::stoi(std::string(HEX + i, 2), nullptr, 16));
    CHECK(isSgb(b.data(), b.size()));
    SgbDoc d;
    std::vector<uint8_t> copy(b);
    CHECK(d.parse(std::move(copy)));
    printf("%s\n", d.info.summary.c_str());
    CHECK(one(d, "_header/runs") == 42);
    CHECK(one(d, "_header/meta_points") == 7);
    CHECK(one(d, "_header/hell_mode") == 1);
    CHECK(one(d, "GameState/EnemyKills/TrainingMelee") == 16);
    CHECK(one(d, "GameState/WeaponUnlocks/SwordWeapon/1") == 5);   // number keys -> "1", "2"
    size_t n = 0;
    CHECK(one(d, "GameState/WeaponUnlocks[*][*]", &n) == 6 && n == 2);   // [*] over objects
    CHECK(one(d, "GameState/Flags/HardMode") == 1 && one(d, "GameState/Flags/Off") == 0);
    std::vector<BymlValue> q;
    d.query("GameState/QuestStatus/WeaponClears", q);
    CHECK(q.size() == 1 && q[0].kind == BymlValue::String && q[0].str == "CashedOut");
    q.clear();
    d.query("GameState/QuestStatus[*]", q);
    CHECK(q.size() == 2);
    CHECK(one(d, "TextLinesRecord/Ending01") == 1);
    CHECK(one(d, "Padding") == 1);
    // damaged files fail cleanly
    for (size_t cut : {(size_t)20, (size_t)60, b.size() - 30, b.size() - 1}) {
        std::vector<uint8_t> t(b.begin(), b.begin() + cut);
        SgbDoc x;
        CHECK(!x.parse(std::move(t)));
    }
    std::vector<uint8_t> bad(b);
    bad[bad.size() - 40] ^= 0x5A;
    SgbDoc y;
    y.parse(std::move(bad));   // must not crash (may or may not parse)
    std::vector<uint8_t> out;
    const uint8_t lit[] = {0x40, 'a', 'b', 'c', 'd'};
    CHECK(lz4BlockDecompress(lit, sizeof(lit), out, 100) && out.size() == 4);
    CHECK(!lz4BlockDecompress(lit, sizeof(lit), out, 3));   // output limit
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
