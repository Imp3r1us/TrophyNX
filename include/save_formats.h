#pragma once
// Binary save formats decoded into a JSON tree, then queried with the JSON reader's path language
// (see json_doc.h / byml.h). The trees are identical to the Save Lab's (tools/savelab/formats.js):
//   "gvas" reader: Unreal Engine 4 / 5 (< 5.4) SaveGame files (.sav starting with "GVAS")
//   "nrbf" reader: .NET BinaryFormatter streams (Unity games)
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
