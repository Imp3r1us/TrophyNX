// SPDX-License-Identifier: GPL-2.0-or-later
// See include/hookwatch.h. Read-only: dmntchtGetCheatProcessMappings / dmntchtReadCheatProcessMemory only.
#include "hookwatch.h"
#include "dmntcht.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

static uint8_t g_buf[hookring::kRingSize];   // static: the module's heap is small

bool HookWatch::wanted(uint64_t titleId, const GameDef& def) {
    bool any = false;
    for (const Achievement& a : def.achs) if (!a.steam.empty()) { any = true; break; }
    if (!any) return false;
    char path[96];
    snprintf(path, sizeof(path), "sdmc:/atmosphere/contents/%016llX/exefs/subsdk9", (unsigned long long)titleId);
    struct stat st;
    return stat(path, &st) == 0;
}

void HookWatch::reset() {
    addr_ = 0;
    tries_ = 0;
    reported_ = false;
    lastSeq_ = 0xFFFFFFFFu;
    hdr_ = hookring::Header();
    cur_ = hookring::Cursor();
}

bool HookWatch::locate(uint64_t mainEnd, void (*log)(const char*, ...)) {
    if (addr_) return true;
    if (++tries_ > 10) return false;   // logged once below
    u64 count = 0;
    if (R_FAILED(dmntchtGetCheatProcessMappingCount(&count))) return false;
    MemoryInfo mi[32];
    // The hook module is loaded after main (rtld, main, subsdk*, sdk): its .data/.bss is a small read-write
    // code mapping there. The ring is 64-byte aligned and starts with its magic.
    for (u64 off = 0; off < count; off += 32) {
        u64 got = 0;
        if (R_FAILED(dmntchtGetCheatProcessMappings(mi, 32, off, &got)) || !got) break;
        for (u64 i = 0; i < got; i++) {
            const MemoryInfo& m = mi[i];
            if (m.addr < mainEnd || m.type != MemType_CodeMutable || (m.perm & Perm_Rw) != Perm_Rw) continue;
            if (m.size > 8u * 1024 * 1024) continue;
            for (u64 at = 0; at < m.size; at += sizeof(g_buf) - 64) {
                size_t n = (size_t)std::min<u64>(sizeof(g_buf), m.size - at);
                if (R_FAILED(dmntchtReadCheatProcessMemory(m.addr + at, g_buf, n))) break;
                for (size_t k = 0; k + 8 <= n; k += 64) {
                    if (memcmp(g_buf + k, hookring::kMagic, 8) != 0) continue;
                    addr_ = m.addr + at + k;
                    log("hook: ring found at 0x%llX (try %d)", (unsigned long long)addr_, tries_);
                    return true;
                }
            }
        }
    }
    if (tries_ == 10) log("hook: subsdk9 is installed but its ring was not found (not the TrophyNX hook, or it failed to load)");
    return false;
}

std::vector<std::string> HookWatch::poll(void (*log)(const char*, ...)) {
    std::vector<std::string> out;
    if (!addr_) return out;
    uint8_t head[hookring::kHeaderSize];
    if (R_FAILED(dmntchtReadCheatProcessMemory(addr_, head, sizeof(head))) ||
        !hookring::parseHeader(head, sizeof(head), hdr_)) return out;
    if (!reported_) {
        reported_ = true;
        log("hook: %s, build %s, %u hook(s) installed, %u event(s) so far", hookring::stateName(hdr_.state),
            hookring::buildHex(hdr_.buildId).c_str(), hdr_.hooksInstalled, hdr_.writeSeq);
        if (hdr_.state == hookring::NoBuild)
            log("hook: add this version with tools/unityhook (hook-add, then hook-make)");
    }
    if (hdr_.state != hookring::Hooked || hdr_.writeSeq == lastSeq_) return out;
    if (R_FAILED(dmntchtReadCheatProcessMemory(addr_, g_buf, sizeof(g_buf)))) return out;
    hookring::Poll p = hookring::readEvents(g_buf, sizeof(g_buf), cur_);
    if (p.lost) log("hook: %u event(s) lost (more than 64 at once)", p.lost);
    if (!p.pending) lastSeq_ = hdr_.writeSeq;
    return p.ids;
}
