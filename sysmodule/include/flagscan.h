// Live game-flag reader for Breath of the Wild (RetroAchievements-style memory watching).
//
// BotW keeps every game-data flag as an object on the heap (zeldaret/botw, gdtFlag.h, NX 1.5.0):
//   FlagBool (0x18): +0x00 vtable, +0x08 u8 type (0=Bool), +0x09 props, +0x0A u8 initial,
//                    +0x0B u8 value, +0x0C u32 hash, +0x10 debug ptr
//   FlagS32/F32 (0x28): +0x00 vtable, +0x08 u8 type (1=S32, 2=F32), +0x0C init, +0x10 min,
//                    +0x14 max, +0x18 value, +0x1C u32 hash, +0x20 debug ptr
// The hash is CRC32(flag name), the same key used in game_data.sav. Nothing here depends on the
// game version: we scan the heap once for the hashes we care about, keep the objects whose
// layout checks out (vtable inside the main executable, type byte, value range), then re-read
// only those values every few seconds.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct MemRegion { uint64_t base, size; };

class MemReader {
public:
    virtual ~MemReader() {}
    virtual bool read(uint64_t addr, void* buf, size_t size) = 0;
};

enum FlagKind : uint8_t { FK_BOOL = 0, FK_S32 = 1, FK_F32 = 2 };

struct FlagHit {
    uint32_t hash;
    uint64_t objAddr;   // start of the Flag object
    FlagKind kind;
};

struct ScanStats {
    uint64_t bytesScanned = 0, readErrors = 0, candidates = 0, rejected = 0;
    uint32_t hashesFound = 0, hashesWanted = 0;
};

class FlagScanner {
public:
    void setWanted(const std::vector<uint32_t>& hashes);
    void setMainRange(uint64_t base, uint64_t size) { m_mainBase = base; m_mainSize = size; }
    // Scans all regions; replaces the current hit list. `yield` is called between chunks.
    ScanStats scan(MemReader& mem, const std::vector<MemRegion>& regions, void (*yield)() = nullptr);
    // Reads the current value of a flag (max over all copies found). Returns false if not found
    // or if every copy failed validation (objects moved -> caller should rescan).
    bool value(MemReader& mem, uint32_t hash, double& out, bool& stale);
    const std::vector<FlagHit>& hits() const { return m_hits; }
    size_t foundCount() const;
    bool has(uint32_t hash) const { return m_byHash.count(hash) != 0; }

private:
    bool checkObject(const uint8_t* obj, FlagKind& kind) const;
    std::unordered_set<uint32_t> m_wanted;
    std::vector<uint8_t> m_bloom = std::vector<uint8_t>(8192, 0);  // 64K-bit prefilter on (hash & 0xFFFF)
    uint64_t m_mainBase = 0, m_mainSize = 0;
    std::vector<FlagHit> m_hits;
    std::unordered_map<uint32_t, std::vector<size_t>> m_byHash;
};
