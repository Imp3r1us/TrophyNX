// SPDX-License-Identifier: GPL-2.0-or-later
// Official achievements from the optional injected hook (hook/, tools/unityhook/DESIGN.md): finds the hook's ring
// in the running game and reads its events, read-only, through dmnt:cht (the attach the memory rules already use).
#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include "hookring.h"
#include "rules.h"

class HookWatch {
public:
    // Is the hook installed for this game (atmosphere/contents/<TID>/exefs/subsdk9) and does its definition name
    // official achievements ("steam")? Cheap: a stat and a loop over the definition.
    static bool wanted(uint64_t titleId, const GameDef& def);

    void reset();
    // Once attached: find the ring (after main, in the modules' read-write data), retried every call until found.
    // mainEnd = end of the main NSO in the game's address space.
    bool locate(uint64_t mainEnd, void (*log)(const char*, ...));
    // Every second: new raw ids since the last call (empty when nothing happened or the ring is not found yet).
    std::vector<std::string> poll(void (*log)(const char*, ...));

    bool found() const { return addr_ != 0; }
    const hookring::Header& header() const { return hdr_; }

private:
    uint64_t addr_ = 0;
    int tries_ = 0;
    bool reported_ = false;
    uint32_t lastSeq_ = 0xFFFFFFFFu;
    hookring::Header hdr_;
    hookring::Cursor cur_;
};
