// "memory" rules: achievements read from the running game's memory (read-only, through Atmosphère's
// dmnt:cht), for any game. Each version of a game keeps its values elsewhere, so a rule lists its
// addresses per build ID (the start of the main module's build ID, as in cheat file names).
//
//   "rule": { "type": "memory", "value_type": "u32", "cmp": ">=", "target": 100, "max": 999999,
//             "builds": { "3CA12DFAAB9C82DA": "[[main+2D21A90]+20]+1C0",
//                         "0123456789ABCDEF": ["heap+61C77360", "heap+61C77400"] } }
//
// Addresses: main / heap / alias + hex offset; "[...]" reads the 64-bit pointer stored there, so
// "[[main+2D21A90]+20]+1C0" follows two pointers, then adds 0x1C0. Several addresses for one build:
// any of them unlocks it (save slots). The condition must hold on 3 checks in a row (a second apart),
// so a pointer read during a loading screen can't unlock anything.
//
// The address syntax, per-build conditions and the 3-checks rule follow HomeNX's achievement packs
// (github.com/ad3333m/homenx, docs/achievement-packs.md); this is an independent implementation.
// Pure code (no Switch services): shared by the app, the background module and the host tests.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mem {

enum class ValType : uint8_t { U8, U16, U32, U64, S8, S16, S32, S64, F32, F64 };
enum class Cmp : uint8_t { Ge, Gt, Le, Lt, Eq, Ne, BitsAny };
enum class Region : uint8_t { Main, Heap, Alias };

// region + offsets[0], then for each further offset: read the pointer there and add it.
struct Address {
    Region region = Region::Main;
    std::vector<int64_t> offsets;
};

struct Bases {
    uint64_t main = 0, heap = 0, alias = 0;
};

struct BuildAddrs {
    std::string build;              // upper-case hex, a prefix of the running build ID
    std::vector<Address> addrs;
};

// Reads `size` bytes of the game's memory at `addr`; false when the read fails.
using Reader = std::function<bool(uint64_t addr, void* out, size_t size)>;

bool parseValType(const std::string& s, ValType& out);
bool parseCmp(const std::string& s, Cmp& out);
bool parseAddress(const std::string& text, Address& out, std::string& err);
std::string formatAddress(const Address& a);
const char* valTypeName(ValType t);
const char* cmpName(Cmp c);
int valSize(ValType t);
// Upper-case hex of the first 8 bytes of a build ID (what cheat files and definitions use).
std::string buildHex(const uint8_t* buildId);

// The addresses for the running build (the longest matching prefix), or nullptr.
const BuildAddrs* forBuild(const std::vector<BuildAddrs>& builds, const std::string& running);
bool resolve(const Address& a, const Bases& b, const Reader& read, uint64_t& out);
bool readValue(ValType t, uint64_t addr, const Reader& read, double& out);
bool holds(Cmp c, double value, double target, double max);

struct Condition {
    ValType type = ValType::U32;
    Cmp cmp = Cmp::Ge;
    double target = 1;
    double max = 0;                 // > 0: values above it are garbage (pointer read too soon)
    std::vector<BuildAddrs> builds;
};

// Parses the memory part of a rule object (cJSON*, kept opaque here to avoid the dependency).
// Returns false with err set when the rule can't be used.
bool parseCondition(const void* cjsonRule, Condition& out, std::string& err);

// True when one of the addresses for `build` holds. value = the last value read (for the log).
bool anyHolds(const Condition& c, const std::string& build, const Bases& b, const Reader& read, double& value, bool& readFailed);

// Per-game watcher used by the background module: call step() about once a second while the game runs.
class Watcher {
public:
    struct Item {
        std::string id;
        const Condition* cond = nullptr;
    };
    static const int kStreak = 3;
    void reset(const std::vector<Item>& items, const std::set<std::string>& alreadyUnlocked);
    void setDone(const std::string& id) { m_done.insert(id); }
    // How many items have a condition for this build.
    size_t coverage(const std::string& build) const;
    // Ids that held kStreak checks in a row this step (each is reported once).
    std::vector<std::string> step(const std::string& build, const Bases& b, const Reader& read);
    bool lastReadFailed() const { return m_readFailed; }
    size_t pending() const;
    const std::map<std::string, double>& lastValues() const { return m_values; }

private:
    std::vector<Item> m_items;
    std::set<std::string> m_done;
    std::map<std::string, int> m_streak;
    std::map<std::string, double> m_values;
    bool m_readFailed = false;
};

}  // namespace mem
