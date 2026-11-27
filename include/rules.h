// Rule engine shared by the app and the background module (sys-trophynx): definition
// parsing and save evaluation. Nothing in here touches Switch services directly; saves are read
// through a SaveSource.
#pragma once
#include "achievements.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

// Read-only access to one title's save.
// The app reads whole files (plenty of memory). The background module has a small fixed heap, so it
// sets streaming(): hash/value saves are then read in chunks keeping only the wanted values,
// fixed-offset saves read only the bytes they need, and tree saves are skipped when they would not
// fit in memoryAvailable(). Values are identical either way.
class SaveSource {
public:
    virtual ~SaveSource() {}
    virtual bool open(uint64_t titleId) = 0;
    virtual void close() = 0;
    virtual std::string lastError() = 0;
    virtual bool readAll(const std::string& rel, std::vector<uint8_t>& out) = 0;
    virtual bool streaming() const { return false; }
    // Used when streaming() is true.
    virtual bool fileSize(const std::string& /*rel*/, uint64_t& /*size*/) { return false; }
    virtual bool readAt(const std::string& /*rel*/, uint64_t /*off*/, void* /*buf*/, size_t /*len*/) { return false; }
    virtual size_t memoryAvailable() { return (size_t)-1; }
    // Streaming sources (live module): try to make memoryAvailable() >= `bytes` (grows the module heap if the
    // system has room). Lets every reader read a save file too big for the module's usual heap.
    virtual bool reserveMemory(size_t /*bytes*/) { return false; }
};

struct GameDef {
    uint64_t id = 0;
    std::string name, reader, magic, xorKey;
    std::string keyFile;     // "key_file": password of an encrypted save, read from <key dir>/<key_file> (never shipped)
    uint64_t fileSize = 0;
    bool closeScan = true;   // "close_scan": false -> the background module doesn't scan it when the game closes
    bool liveSave = true;    // "live_save": false -> the background module doesn't read the save while the game runs
    std::vector<std::string> saveFiles;
    std::vector<Achievement> achs;
    // "hook_ids": the hooked call's raw id -> Steam name (games that pass a number); empty when the call already
    // passes the Steam name. See hookring.h.
    std::map<std::string, std::string> hookIds;
};

// One definition file (romfs/games/<TITLEID>.json). Returns false if the text isn't usable JSON or
// has no title_id; achievement errors are appended to err (the definition is still returned).
bool parseGameDef(const char* text, GameDef& out, std::string& err);
// Every *.json of dir; a later definition for the same title replaces the earlier one.
void loadGameDefs(const std::string& dir, std::map<uint64_t, GameDef>& defs, std::string& errors, std::vector<std::string>& loaded);
void loadUniversal(const std::string& path, std::vector<Achievement>& perGame, std::vector<Achievement>& global, std::string& errors);
// Copies the reader settings and achievements of a definition into a game.
void applyGameDef(Game& g, const GameDef& d);

// Folder of the save passwords ("key_file" of a definition), e.g. sdmc:/switch/TrophyNX/keys. Empty = none.
void setSaveKeyDir(const std::string& dir);
// Where the app looks for a game's global-metadata.dat to find a missing password (recursively), e.g. the keys
// folder and nxdumptool's output folder (sdmc:/nxdumptool/NCA FS/User/Extracted/...).
void setSaveKeySearchDirs(const std::vector<std::string>& dirs);
// App only: reads a file of the installed game itself (romfs with its update, e.g. "Data/resources.assets"), so a
// missing password can be found with no dump at all. false (+ why) when the system refuses. Not set: dumps only.
typedef std::function<bool(uint64_t titleId, const std::string& rel, std::vector<uint8_t>& out, std::string& why)> GameFileReader;
void setSaveKeyGameReader(GameFileReader fn);

// Reads the save of g (g.reader, g.saveFiles...) and sets a.value of its save-based achievements,
// g.saveStatus and g.debug.
void evaluateSave(Game& g, SaveSource& src);
// Play-log achievements ("playtime_hours", "launches") of `achs` met by these statistics and not in `done`
// (ids), in list order. Used by the live module while a game runs; the app evaluates the same rules on its scans.
std::vector<const Achievement*> playStatsMet(const std::vector<Achievement>& achs, double hours, double launches,
                                             const std::set<std::string>& done);
// universal.json from its text (the live module embeds it). False when the text is not JSON.
bool parseUniversal(const char* text, std::vector<Achievement>& perGame, std::vector<Achievement>& global, std::string& errors);
inline bool isPlayStatsRule(RuleType t) { return t == RuleType::PlaytimeHours || t == RuleType::Launches; }
inline bool isSaveRule(RuleType t) { return t == RuleType::Flag || t == RuleType::Count || t == RuleType::Sum; }
