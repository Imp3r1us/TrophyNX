#pragma once
// Read-only JSON save reader ("json" reader): plain-text JSON saves, as written by many Unity games
// on Switch (Hollow Knight user1.dat...). Uses the same path language as BymlDoc (see byml.h):
//   playerData/grubsCollected               nested keys
//   playerData/bossDoorStateTier1/completed nested object field
//   sceneData/persistentBoolItems[*]/activated
//   sceneData/persistentBoolItems[id=Battle Scene]/activated
// Leaves are reported as BymlValue: true/false -> Bool 1/0, numbers -> Int/Float, strings -> String,
// objects/arrays -> Container (num = number of children), null -> Null.
#include "byml.h"
#include <cstdint>
#include <string>
#include <vector>

struct cJSON;

class JsonDoc {
public:
    JsonDoc() = default;
    ~JsonDoc();
    JsonDoc(const JsonDoc&) = delete;
    JsonDoc& operator=(const JsonDoc&) = delete;
    JsonDoc(JsonDoc&& o) noexcept : error(std::move(o.error)), m_root(o.m_root), m_textValues(o.m_textValues) { o.m_root = nullptr; }
    JsonDoc& operator=(JsonDoc&& o) noexcept;

    // Accepts an optional UTF-8 BOM and trailing NULs/whitespace; the document must start with '{' or '['.
    bool parse(std::vector<uint8_t>&& data);
    bool query(const std::string& path, std::vector<BymlValue>& out) const;
    size_t topLevelCount() const;
    std::string error;

protected:
    // Takes ownership of a tree built by another reader (GVAS, NRBF). textValues = false: strings stay
    // strings (only JSON text saves store numbers and booleans as text).
    void adopt(cJSON* root, bool textValues);

private:
    cJSON* m_root = nullptr;
    bool m_textValues = true;
    void walk(const cJSON* n, const std::vector<std::string>& atoms, size_t i, std::vector<BymlValue>& out, int depth) const;
};
