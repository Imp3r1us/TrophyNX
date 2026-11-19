#pragma once
// Read-only BYML ("YB" little-endian / "BY" big-endian, versions 1-7) reader with a small path
// language, used by the "byml" save reader (Super Mario Odyssey File*.bin and similar saves).
//
// Path syntax (segments separated by '/'):
//   CoinNum                              dictionary key
//   GameProgressData/UnlockWorldNum      nested keys
//   WorldInfo[3]/ScenarioNo              list index
//   ShineData[*]/IsGet                   every list element
//   ShineData[UniqueId=205]/IsGet        list elements (dictionaries) whose field equals a value
//   ItemCap[*]                           every element of a list of strings
// A path matches 0..n leaf values.
#include <cstdint>
#include <string>
#include <vector>

struct BymlValue {
    enum Kind { Null, Bool, Int, Float, String, Container } kind = Null;
    double num = 0;      // Bool -> 0/1, numbers, Container -> number of children
    std::string str;     // String
    // Truthiness used by "count": non-zero number, true, non-empty string, non-empty container.
    bool truthy() const { return kind == String ? !str.empty() : num != 0; }
};

// Path helpers shared by every path-addressed reader (BYML, JSON).
// Splits "A/B[3]/C[Id=5]" into atoms {"A","B","[3]","C","[Id=5]"}; false if malformed or empty.
bool treeSplitPath(const std::string& path, std::vector<std::string>& atoms);
// "[Field=want]" / "[=want]" comparison: strings compare as text, numbers numerically, true/false as 1/0.
bool treeValueEquals(const BymlValue& v, const std::string& want);

class BymlDoc {
public:
    // Finds the BYML document at offset 0 or 0x10 (SMO saves have a 16-byte header).
    bool parse(std::vector<uint8_t>&& data);
    // Collects every leaf matched by `path`. Returns false if the path is malformed.
    bool query(const std::string& path, std::vector<BymlValue>& out) const;
    size_t base() const { return m_base; }
    bool bigEndian() const { return m_be; }
    int version() const { return m_version; }
    std::string error;
private:
    struct Node { uint8_t type; uint32_t raw; };
    std::vector<uint8_t> m_data;
    size_t m_base = 0;
    bool m_be = false;
    int m_version = 0;
    std::vector<std::string> m_keys, m_strings;
    Node m_root{0, 0};

    uint32_t rd32(size_t off) const;
    uint32_t rd24(size_t off) const;
    bool readStringTable(uint32_t off, std::vector<std::string>& out);
    bool inRange(size_t off, size_t len) const { return off <= m_data.size() && len <= m_data.size() - off; }
    BymlValue leaf(const Node& n) const;
    bool dictGet(uint32_t off, const std::string& key, Node& out) const;
    size_t listSize(uint32_t off) const;
    bool listItem(uint32_t off, size_t i, Node& out) const;
    void walk(const Node& n, const std::vector<std::string>& segs, size_t i, std::vector<BymlValue>& out, int depth) const;
};
