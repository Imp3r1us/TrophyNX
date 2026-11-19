// Host test for the heap scanner: synthetic heap with real-looking flag objects, decoys and noise.
#include "flagscan.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <zlib.h>

struct FakeMem : MemReader {
    uint64_t base; std::vector<uint8_t> d;
    bool read(uint64_t a, void* b, size_t n) override {
        if (a < base || a + n > base + d.size()) return false;
        memcpy(b, &d[a - base], n); return true;
    }
};
static uint32_t crc(const char* s) { return (uint32_t)crc32(0, (const Bytef*)s, strlen(s)); }

int main() {
    const uint64_t MAIN = 0x8504000000ULL, HEAP = 0x2400000000ULL;
    FakeMem m; m.base = HEAP; m.d.resize(64 << 20);
    std::mt19937_64 rng(42);
    for (size_t i = 0; i < m.d.size(); i += 8) { uint64_t r = rng(); memcpy(&m.d[i], &r, 8); }
    auto put64 = [&](uint64_t a, uint64_t v) { memcpy(&m.d[a - HEAP], &v, 8); };
    auto put32 = [&](uint64_t a, uint32_t v) { memcpy(&m.d[a - HEAP], &v, 4); };
    auto putBool = [&](uint64_t a, const char* name, int val) {
        put64(a, MAIN + 0x2400000); m.d[a + 8 - HEAP] = 0; m.d[a + 9 - HEAP] = 0x1; m.d[a + 0xA - HEAP] = 0;
        m.d[a + 0xB - HEAP] = val; put32(a + 0xC, crc(name)); put64(a + 0x10, 0);
    };
    auto putS32 = [&](uint64_t a, const char* name, int32_t val, int32_t mx) {
        put64(a, MAIN + 0x2400100); m.d[a + 8 - HEAP] = 1; put32(a + 0xC, 0); put32(a + 0x10, 0); put32(a + 0x14, mx);
        put32(a + 0x18, val); put32(a + 0x1C, crc(name)); put64(a + 0x20, 0);
    };
    putBool(HEAP + 0x100000, "IsGet_Obj_DRStone_Get", 1);
    putBool(HEAP + 0x2000000, "IsGet_Obj_DRStone_Get", 0);          // second copy (older param): max wins
    putBool(HEAP + 0x40000 - 0x10, "IsGet_Armor_043_Upper", 1);      // straddles a 256 KiB chunk boundary
    putS32(HEAP + 0x300008, "HiddenKorok_Number", 376, 900);
    putS32(HEAP + 0x7FFE8, "MaxHartValue", 52, 120);                 // header in one chunk, hash in the next
    // decoys: right hash, wrong shape
    put32(HEAP + 0x500004, crc("Clear_RemainsWind"));                // no vtable
    put64(HEAP + 0x600000, 0x1234567); put32(HEAP + 0x60000C, crc("Clear_RemainsWind"));
    putBool(HEAP + 0x700000, "Clear_RemainsWater", 1); m.d[0x700000 + 0xB] = 7;   // impossible bool value

    FlagScanner s;
    std::vector<uint32_t> want = {crc("IsGet_Obj_DRStone_Get"), crc("IsGet_Armor_043_Upper"), crc("HiddenKorok_Number"),
                                  crc("MaxHartValue"), crc("Clear_RemainsWind"), crc("Clear_RemainsWater"), crc("FirstTouchdown")};
    s.setWanted(want);
    s.setMainRange(MAIN, 0x4000000);
    ScanStats st = s.scan(m, {{HEAP, m.d.size()}});
    printf("scanned %llu MB, candidates %llu, rejected %llu, found %u/%u hashes, hits %zu\n",
           (unsigned long long)(st.bytesScanned >> 20), (unsigned long long)st.candidates, (unsigned long long)st.rejected,
           st.hashesFound, st.hashesWanted, s.hits().size());
    double v; bool stale;
    assert(s.value(m, crc("IsGet_Obj_DRStone_Get"), v, stale) && v == 1);
    assert(s.value(m, crc("IsGet_Armor_043_Upper"), v, stale) && v == 1);
    assert(s.value(m, crc("HiddenKorok_Number"), v, stale) && v == 376);
    assert(s.value(m, crc("MaxHartValue"), v, stale) && v == 52);
    assert(!s.value(m, crc("Clear_RemainsWind"), v, stale));
    assert(!s.value(m, crc("Clear_RemainsWater"), v, stale));
    assert(!s.value(m, crc("FirstTouchdown"), v, stale));
    // live update: value changes are seen without rescanning
    m.d[0x300008 + 0x18] = 0xB9; m.d[0x300008 + 0x19] = 0x01;   // 441
    assert(s.value(m, crc("HiddenKorok_Number"), v, stale) && v == 441);
    // object freed -> stale
    memset(&m.d[0x300008], 0, 0x28); memset(&m.d[0x100000], 0, 0x18); memset(&m.d[0x2000000], 0, 0x18);
    assert(!s.value(m, crc("HiddenKorok_Number"), v, stale) && stale);
    printf("all scanner tests passed\n");
}
