#pragma once
// Binary save formats decoded into a JSON tree, then queried with the JSON reader's path language
// (see json_doc.h / byml.h). The trees are identical to the Save Lab's (tools/savelab/formats.js):
//   "gvas" reader: Unreal Engine 4 / 5 (< 5.4) SaveGame files (.sav starting with "GVAS")
//   "nrbf" reader: .NET BinaryFormatter streams (Unity games)
//   "sgb" reader: Supergiant Games saves (Hades: "SGB1" header + LZ4 block + luabins Lua state)
//   "deltarune" reader: DELTARUNE deltarune.sav (JSON of GameMaker text files; see deltaruneToJson below)
// Mapping, in short: properties / class members -> object keys; arrays, sets, List<T>, HashSet<T> -> lists;
// maps and Dictionary<K,V> -> lists of {"Key","Value"}; enums -> number (NRBF) or name (GVAS);
// GUIDs -> 32 hex digits; dates -> ticks; values that cannot be decoded -> null.
#include "json_doc.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct cJSON;

struct SaveFormatInfo {
    std::string summary;   // e.g. "UE 4.27.2, class /Script/Game.MySave" / "root class SaveData, 37 objects"
    int skipped = 0;       // GVAS: values that could not be decoded (null in the tree)
};

// Repeating XOR keys of known games (same list as KNOWN_XOR_KEYS in tools/savelab/formats.js).
const std::vector<std::string>& knownXorKeys();

bool isGvas(const uint8_t* data, size_t size);
bool isNrbf(const uint8_t* data, size_t size);
// Return a new tree (caller frees with cJSON_Delete) or nullptr with `error` set.
cJSON* gvasToJson(const uint8_t* data, size_t size, std::string& error, SaveFormatInfo* info = nullptr);
cJSON* nrbfToJson(const uint8_t* data, size_t size, std::string& error, SaveFormatInfo* info = nullptr);
// Hades (Supergiant) "SGB1" saves, version 15/16. Tree = the saved Lua globals (GameState, CurrentRun,
// TextLinesRecord, CodexStatus...), every Lua table as an object (number keys written "1", "2"...),
// plus "_header": {version, location, runs, meta_points, shrine_points, god_mode, hell_mode, map}.
bool isSgb(const uint8_t* data, size_t size);
cJSON* sgbToJson(const uint8_t* data, size_t size, std::string& error, SaveFormatInfo* info = nullptr);
// DELTARUNE (Switch) "deltarune.sav": a JSON object {file name: file text} written by GameMaker. Tree:
//   files[*]: one object per "filechN_S" text (chapter N, slot S; S >= 3 = the completion save of slot S - 3):
//     name, chapter, slot, completion (0/1), route (chapter 5 "_b" files), truename, char[3], gold, xp, lv, darkzone,
//     hp, maxhp, at, df, mag, guts, charweapon, chararmor1, chararmor2, weaponstyle (party lists, index = character),
//     item, keyitem, weapon, armor, pocketitem (ch2+), lweapon, larmor, lxp, llv, lgold, litem, phone,
//     flags: {"<index>": value} (global.flag, non-zero values only), plot, room, time.
//     Console layout of scr_saveprocess: arrays are GameMaker ds_list_write hex strings.
//   ini: {"dr.ini": {"<section>": {"<key>": value}}, ...} (every *.ini text of the save; numbers as numbers)
//   completed[0..5]: 1 when a completion file of that chapter exists (index = chapter)
//   patch[0..29]: global.flag[2400 + trophy id] in any file (set by tools/deltarune/TrophyNX_trophies.csx)
//   trophy[0..29]: patch[i], or for a few trophies a condition the save shows without the patch (chapter ends:
//     completed[N]; 9 moss eaten (flag 106); 14 armor equipped in a chapter 1 file; 18 flag 325 = 1; 22 flag 1220;
//     27 ThornRing/FreezeRing owned); trophy[0] = all of trophy[1..29].
bool isDeltarune(const uint8_t* data, size_t size);
cJSON* deltaruneToJson(const uint8_t* data, size_t size, std::string& error, SaveFormatInfo* info = nullptr);
// LZ4 block format (no frame). false if the input is malformed or the output would exceed maxOut.
bool lz4BlockDecompress(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t maxOut);

class GvasDoc : public JsonDoc {
public:
    bool parse(std::vector<uint8_t>&& data);
    SaveFormatInfo info;
};

class NrbfDoc : public JsonDoc {
public:
    bool parse(std::vector<uint8_t>&& data);
    SaveFormatInfo info;
};

class SgbDoc : public JsonDoc {
public:
    bool parse(std::vector<uint8_t>&& data);
    SaveFormatInfo info;
};

class DeltaruneDoc : public JsonDoc {
public:
    bool parse(std::vector<uint8_t>&& data);
    SaveFormatInfo info;
};
