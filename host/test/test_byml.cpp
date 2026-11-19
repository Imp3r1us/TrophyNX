// g++ -std=gnu++17 -Iinclude host/test/test_byml.cpp source/byml.cpp -o /tmp/test_byml && /tmp/test_byml <File0.bin>...
#include "byml.h"
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int argc, char** argv) {
    const char* paths[] = {"CoinNum", "TotalCoinNum", "GameProgressData/UnlockWorldNum", "GameProgressData/WaterfallWorldProgress",
        "ShineData[*]/IsGet", "ShineData[UniqueId=205]/IsGet", "ShineData[31]/UniqueId", "HackDictionary[*]", "HackDictionary[=Wanwan]",
        "ItemCap[*]", "ItemCloth[=Mario]", "AchievementSaveData[Name=Other_Jump]/IsGet", "PlayerJumpCount", "WorldInfo[1]/ScenarioNo",
        "GetCoinCollectData[*]/IsGet", "Checkpoint[*]/ObjId", "CollectBgmList[*]/IsCollected", "Missing/Path", "ShineData"};
    int fails = 0;
    for (int f = 1; f < argc; f++) {
        std::ifstream in(argv[f], std::ios::binary);
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), {});
        BymlDoc d;
        if (!d.parse(std::move(buf))) { printf("%s: %s\n", argv[f], d.error.c_str()); fails++; continue; }
        printf("== %s (v%d, base 0x%zX)\n", argv[f], d.version(), d.base());
        for (const char* p : paths) {
            std::vector<BymlValue> v;
            d.query(p, v);
            double sum = 0; int truthy = 0;
            for (auto& x : v) { truthy += x.truthy(); sum += x.kind == BymlValue::String ? 0 : x.num; }
            printf("  %-45s matches %4zu  truthy %4d  sum %.0f%s%s\n", p, v.size(), truthy, sum,
                   v.size() == 1 && v[0].kind == BymlValue::String ? "  str=" : "", v.size() == 1 && v[0].kind == BymlValue::String ? v[0].str.c_str() : "");
        }
    }
    // malformed input must not crash
    for (size_t n : {0, 3, 16, 40}) { std::vector<uint8_t> junk(n, 0x59); BymlDoc d; d.parse(std::move(junk)); std::vector<BymlValue> v; d.query("A[*]/B", v); }
    std::vector<uint8_t> trunc;
    if (argc > 1) { std::ifstream in(argv[1], std::ios::binary); trunc.assign(std::istreambuf_iterator<char>(in), {}); }
    if (trunc.size() < 0x8000) trunc.resize(0x8000, 0);
    for (size_t cut : {0x20, 0x400, 0x3000, 0x8000}) { std::vector<uint8_t> t(trunc.begin(), trunc.begin() + cut); BymlDoc d; if (d.parse(std::move(t))) { std::vector<BymlValue> v; d.query("ShineData[*]/IsGet", v); d.query("ItemCap[*]", v); } }
    printf("fuzz ok\n");
    return fails;
}
