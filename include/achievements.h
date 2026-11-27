#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include "platform.h"
#include "memrules.h"

enum class RuleType {
    // Save-based (reader "gamedata_hash": BotW-style u32 hash / u32 value pairs)
    Flag,          // one flag/value >= target
    Count,         // number of listed flags that are non-zero
    Sum,           // sum of listed values
    // Play-statistics based, available for every game
    PlaytimeHours,
    Launches,
    // Play history (system play log) and album, available for every game
    DaysPlayed,    // distinct days the game was played
    StreakDays,    // most days in a row
    ComebackDays,  // most days away before playing again
    Screenshots,   // album screenshots of the game
    Videos,        // album video clips of the game
    // Global (across all games)
    GamesPlayed,
    TotalHours,
    UnlockedTotal,
    DaysTotal,     // distinct days with any game played
    StreakTotal,   // most days in a row with any game played
    CapturesTotal, // album screenshots + videos, all games
    // Live: a value in the running game's memory (memrules.h), checked by the background module only
    Memory,
};

struct Rule {
    RuleType type = RuleType::Flag;
    std::vector<uint32_t> hashes;
    bool isFloat = false;
    bool hasEquals = false;   // count/flag: value must equal this (e.g. murmur3("Clear") in TotK)
    uint32_t equals = 0;
    double target = 1;
    double divisor = 1;   // progress shown as value/divisor (e.g. quarter hearts -> hearts)
    bool showProgress = true;
    // "binary_le" reader: little-endian integers at fixed file offsets
    std::vector<uint32_t> offsets;
    int size = 4;          // bytes per value (1, 2 or 4)
    double maxSane = 0;    // > 0: values above this are treated as garbage (offset moved in a game update)
    uint32_t mask = 0;     // != 0: each value becomes 1 when any of these bits is set, else 0 (bit flags)
    // "needs": the rule only counts when the value at needsOffset is >= needsMin (e.g. a table long enough
    // to hold this entry: DLC achievements in Binding of Isaac)
    bool hasNeeds = false;
    uint32_t needsOffset = 0;
    int needsSize = 4;
    double needsMin = 1;
    // "byml" / "json" readers: paths into the document tree (see byml.h, json_doc.h), e.g. "ShineData[*]/IsGet"
    std::vector<std::string> paths;
    bool hasEqualsStr = false;  // byml/json: matched values must equal this (string, number, true/false)
    std::string equalsStr;
    // "memory": per-build addresses, value type and comparison (target/max are copied into it)
    mem::MemRule mem;
};

struct Achievement {
    std::string id, name, desc;
    int points = 10;
    bool hidden = false;   // description hidden until unlocked
    std::string tier;      // optional popup medal ("bronze"...); default from points
    Rule rule;
    // runtime
    double value = 0;
    bool unlocked = false;
    bool isNew = false;
    uint64_t unlockTime = 0;
};

enum class SaveStatus { None, Ok, NoSave, Unreadable, NeedsKey };   // NeedsKey: encrypted save, its password file is missing

struct Game {
    uint64_t id = 0;
    std::string name;
    std::vector<uint8_t> iconJpeg;
    PlayStats stats;
    std::string reader;                 // "" when the game only has universal achievements
    std::vector<std::string> saveFiles; // relative paths inside the save
    uint64_t fileSize = 0;              // binary_le: required file size (0 = any)
    std::string magic;                  // binary_le: required bytes at offset 0
    std::string xorKey;                 // tree readers: repeating-key XOR undone before parsing ("xor_key")
    std::string keyFile;                // encrypted saves: password file in <data folder>/keys ("key_file")
    std::vector<Achievement> achs;
    SaveStatus saveStatus = SaveStatus::None;
    bool hasDefinition = false;
    int unlocked = 0, points = 0, maxPoints = 0;
    std::vector<std::string> debug;     // what happened during the last scan (shown with X)
};

struct Library {
    std::vector<Game> games;        // games[0] is the global "Overview"
    int totalUnlocked = 0, totalCount = 0, totalPoints = 0;
    std::vector<std::pair<int, int>> newlyUnlocked; // (game index, ach index)
    std::string loadErrors;
    std::vector<std::string> defsLoaded; // "romfs:/games/X.json (33 achievements)"
    std::string liveStatus;
};

uint32_t crc32str(const std::string& s);

// Loads definitions, scans the console, evaluates everything and persists unlocks.
void buildLibrary(Library& lib, plat::ProgressFn progress, void* ctx);
// Re-reads saves and stats for already listed games (Y = rescan).
void rescanLibrary(Library& lib, plat::ProgressFn progress, void* ctx);
// Re-locks achievements (removes them from the unlock file).
// gameIndex = -1 -> everything for this user; achIndex = -1 -> the whole game.
// Returns how many unlocks were removed.
int relockAchievements(Library& lib, int gameIndex, int achIndex);

uint32_t murmur3str(const std::string& s);

// Reader for saves made of u32 hash / u32 value pairs:
//  "gamedata_hash": BotW game_data.sav (crc32 names, pairs from 0x0C, LE on Switch / BE on Wii U)
//  "totk_progress": TotK progress.sav (murmur3 names, magic 01020304, pairs from 0x28 until MetaData.SaveTypeHash)
class GamedataSave {
public:
    bool parse(std::vector<uint8_t>&& data, const std::string& format);
    // Low-memory variant (background module): reads the file in chunks through `read(offset, buf, len)`
    // and keeps only the values of `wanted` hashes. Same results as parse() for those hashes.
    bool parseStream(const std::function<bool(uint64_t, uint8_t*, size_t)>& read, uint64_t size,
                     const std::string& format, const std::unordered_set<uint32_t>& wanted);
    bool get(uint32_t hash, uint32_t& out) const;
    bool littleEndian() const { return m_le; }
    size_t entryCount() const { return m_filtered ? m_pairs : m_index.size(); }
private:
    std::vector<uint8_t> m_data;
    std::unordered_map<uint32_t, uint32_t> m_index; // hash -> offset of first value (parseStream: hash -> value)
    bool m_filtered = false;
    size_t m_pairs = 0;     // parseStream: distinct hashes seen (estimate shown in the log)
    bool m_le = true;
    uint32_t rd(size_t off) const;
};
