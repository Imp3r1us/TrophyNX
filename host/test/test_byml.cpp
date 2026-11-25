// g++ -std=gnu++17 -Iinclude host/test/test_byml.cpp source/byml.cpp -o /tmp/test_byml && /tmp/test_byml <File0.bin>...
#include "byml.h"
#include <cstdio>
#include <cstring>
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
        // Same file read in pages (live module), with the smallest cache: every path must give the same leaves
        std::ifstream in2(argv[f], std::ios::binary);
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in2)), {});
        BymlDoc s;
        uint64_t calls = 0;
        bool sok = s.parseStream([&](uint64_t off, void* b, size_t len) {
            calls++;
            if (off > raw.size() || len > raw.size() - off) return false;
            memcpy(b, raw.data() + off, len); return true; }, raw.size(), 80 * 1024);
        if (!sok) { printf("  streamed: %s\n", s.error.c_str()); fails++; }
        for (const char* p : paths) {
            if (!sok) break;
            std::vector<BymlValue> a, b;
            d.query(p, a); s.query(p, b); s.query(p, b);   // second query: from the path cache
            bool same = b.size() == 2 * a.size();
            for (size_t i = 0; same && i < a.size(); i++)
                for (size_t j : {i, i + a.size()}) same = same && a[i].kind == b[j].kind && a[i].num == b[j].num && a[i].str == b[j].str;
            if (!same) { printf("  FAIL streamed %s: %zu vs %zu leaves\n", p, a.size(), b.size() / 2); fails++; }
        }
        if (sok) printf("  streamed = whole file (%llu page reads, %zu bytes)\n", (unsigned long long)s.pageReads(), raw.size());
        // too little memory and read errors fail cleanly
        BymlDoc t;
        if (t.parseStream([&](uint64_t, void*, size_t) { return true; }, raw.size(), 1024)) { printf("  FAIL: 1 KB budget accepted\n"); fails++; }
        BymlDoc u;
        if (u.parseStream([&](uint64_t, void*, size_t) { return false; }, raw.size(), 1 << 20) || u.error != "read error") { printf("  FAIL: read error not reported (%s)\n", u.error.c_str()); fails++; }
        (void)calls;
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
    for (size_t cut : {0x20, 0x400, 0x3000, 0x8000}) {
        BymlDoc d;
        if (d.parseStream([&](uint64_t off, void* b, size_t len) { if (off + len > cut) return false; memcpy(b, trunc.data() + off, len); return true; }, cut, 1 << 20)) {
            std::vector<BymlValue> v; d.query("ShineData[*]/IsGet", v); d.query("ItemCap[*]", v);
        }
    }
    printf("fuzz ok\n");
    return fails;
}
