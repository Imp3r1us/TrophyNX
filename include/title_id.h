#pragma once
// Title ids of running programs.
//
// Some games are collections made of several programs in one application (KINGDOM HEARTS HD 1.5+2.5 ReMIX:
// a launcher plus one program per game). The launcher runs as the application id (…000), each game as
// application id + program index (…001, …002…). The save data, play statistics and achievement list belong
// to the application id, so every running program id is mapped back to it before anything else.
#include <cstdint>

namespace titleid {

// Program index of a running program (0 for the launcher / single-program games).
inline unsigned programIndex(uint64_t id) {
    return ((id >> 56) == 0x01 && (id & 0x1FFF) <= 0xFF) ? (unsigned)(id & 0xFF) : 0;
}

// Application id that owns a running program: clears the program index (u8, so at most 0xFF).
// Ids that aren't applications (hbmenu …3000, forwarders 0x05…) are returned unchanged.
inline uint64_t applicationId(uint64_t id) {
    return id - programIndex(id);
}

}  // namespace titleid
