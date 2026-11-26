// SPDX-License-Identifier: GPL-2.0-or-later
// The ring buffer the optional injected hook (hook/, tools/unityhook/DESIGN.md) fills inside the game: each call
// of the game's own "unlock achievement" function adds one event with the achievement's raw id. The live module
// reads it read-only through dmnt:cht, once a second, and unlocks what it reports. This part has no libnx: the
// decoding is shared with the host test (host/test/test_hookring.cpp). Layout = hook/source/program/main.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct GameDef;
struct Achievement;

namespace hookring {

constexpr size_t kHeaderSize = 64;
constexpr size_t kSlots = 64;
constexpr size_t kEventSize = 64;
constexpr size_t kIdLen = 48;
constexpr size_t kRingSize = kHeaderSize + kSlots * kEventSize;   // 4160
constexpr char kMagic[9] = "TNXRING1";

enum State : uint32_t { Starting = 0, Hooked = 1, NoBuild = 2, BadConfig = 3 };

struct Header {
    bool valid = false;          // magic + version 1
    uint32_t state = 0;
    uint64_t titleId = 0;
    uint8_t buildId[16] = {};
    uint32_t hooksInstalled = 0;
    uint32_t writeSeq = 0;       // events written so far
};

bool parseHeader(const uint8_t* buf, size_t size, Header& h);
std::string buildHex(const uint8_t* id, size_t bytes = 8);   // "FB968CFB649A5842", like memory rules' build keys
const char* stateName(uint32_t state);

// Reader position. Starts at 0, so the events written before the live module attached (the game re-awarding the
// achievements of the loaded save, for instance) are read too, as long as they are still in the ring.
struct Cursor {
    uint32_t next = 0;           // next event sequence number to read
};

struct Poll {
    std::vector<std::string> ids;   // raw ids, in order
    uint32_t lost = 0;              // events overwritten before they were read (more than 64 at once)
    bool pending = false;           // an event is being written: read again next time
};

// Decodes the events [cur.next, header.writeSeq) from a full ring image (kRingSize bytes) and advances cur.
Poll readEvents(const uint8_t* ring, size_t size, Cursor& cur);

// The achievement a raw id stands for: hook_ids maps it to a Steam name when the game passes a number, otherwise
// the raw id is the Steam name; then the definition achievement with that "steam" name. nullptr if none.
const Achievement* achievementFor(const GameDef& def, const std::string& rawId);

}  // namespace hookring
