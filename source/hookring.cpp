// SPDX-License-Identifier: GPL-2.0-or-later
// Hook ring decoding: see include/hookring.h
#include "hookring.h"
#include "rules.h"
#include <cstdio>
#include <cstring>

namespace hookring {

static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }

bool parseHeader(const uint8_t* buf, size_t size, Header& h) {
    h = Header();
    if (!buf || size < kHeaderSize || memcmp(buf, kMagic, 8) != 0 || rd32(buf + 8) != 1) return false;
    h.valid = true;
    h.state = rd32(buf + 12);
    h.titleId = rd64(buf + 16);
    memcpy(h.buildId, buf + 24, 16);
    h.hooksInstalled = rd32(buf + 40);
    h.writeSeq = rd32(buf + 44);
    return true;
}

std::string buildHex(const uint8_t* id, size_t bytes) {
    std::string s;
    char b[3];
    for (size_t i = 0; i < bytes; i++) { snprintf(b, sizeof(b), "%02X", id[i]); s += b; }
    return s;
}

const char* stateName(uint32_t state) {
    switch (state) {
        case Starting: return "starting";
        case Hooked: return "hooked";
        case NoBuild: return "this game version is not in the hook's table";
        case BadConfig: return "the hook's table is invalid";
        default: return "unknown state";
    }
}

Poll readEvents(const uint8_t* ring, size_t size, Cursor& cur) {
    Poll p;
    Header h;
    if (size < kRingSize || !parseHeader(ring, size, h)) return p;
    uint32_t end = h.writeSeq;
    if (end - cur.next > 0x80000000u) cur.next = end;   // the game restarted its counter (new process): resync
    if (end - cur.next > kSlots) {                       // more than the ring holds since the last read
        p.lost = end - cur.next - (uint32_t)kSlots;
        cur.next = end - (uint32_t)kSlots;
    }
    while (cur.next != end) {
        const uint8_t* e = ring + kHeaderSize + (cur.next % kSlots) * kEventSize;
        uint32_t seq = rd32(e);
        if (seq == cur.next + 1) {
            const char* id = (const char*)e + 16;
            size_t n = strnlen(id, kIdLen);
            if (n) p.ids.emplace_back(id, n);
            cur.next++;
        } else if (seq == 0 || seq - (cur.next + 1) > 0x80000000u) {
            p.pending = true;                            // still being written (or not yet): next poll
            break;
        } else {
            p.lost++;                                    // overwritten by a newer event
            cur.next++;
        }
    }
    return p;
}

const Achievement* achievementFor(const GameDef& def, const std::string& rawId) {
    auto it = def.hookIds.find(rawId);
    const std::string& steam = it != def.hookIds.end() ? it->second : rawId;
    if (steam.empty()) return nullptr;
    for (const Achievement& a : def.achs)
        if (a.steam == steam) return &a;
    return nullptr;
}

}  // namespace hookring
