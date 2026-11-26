// SPDX-License-Identifier: GPL-2.0-or-later
// TrophyNX hook: records the game's own "unlock achievement" calls for the live module (sys-trophynx).
// See tools/unityhook/DESIGN.md. One binary for every game: the per-game part is g_Config, a table that
// tools/unityhook/unityhook.py --make-hook writes into a copy of this module (it finds it by its magic).
//
// What runs in the game, per hooked call: copy the achievement id (an int32 or an IL2CPP System.String) into
// g_Ring, then tail-call the original function with every argument register untouched. No file, no IPC, no heap.
// The live module finds g_Ring by its magic through dmnt:cht (read-only) and turns the ids into unlocks.
//
// Built with exlaunch (shadowninja108, GPL-2.0): the module as a whole is distributed under GPLv2.

#include "lib.hpp"

#include <atomic>
#include <cstring>

namespace {

constexpr int kMaxBuilds = 8;
constexpr int kMaxHooks = 4;
constexpr int kRingSlots = 64;
constexpr int kIdLen = 48;

enum ArgType : uint8_t { ArgInt32 = 1, ArgString = 2 };

struct HookSlot {               // 24 bytes
    uint32_t offset;            // entry point, offset from the start of main
    uint8_t reg;                // argument register x0..x7 holding the id
    uint8_t type;               // ArgType
    uint8_t pad[2];
    uint8_t sig[16];            // first 16 bytes at main+offset in this build
};

struct BuildCfg {               // 120 bytes
    uint8_t build_id[16];       // main NSO build ID (first 16 bytes), reported in the ring
    uint32_t hook_count;
    uint32_t pad;
    HookSlot hooks[kMaxHooks];
};

struct Config {                 // 8 + 4 + 4 + 8 + 8 * 120 = 984 bytes
    char magic[8];              // "TNXHCFG1": the PC tool looks for it in the NSO
    uint32_t version;
    uint32_t build_count;
    uint64_t title_id;
    BuildCfg builds[kMaxBuilds];
};
static_assert(sizeof(HookSlot) == 24 && sizeof(BuildCfg) == 120 && sizeof(Config) == 984, "config layout");

// Written by the PC tool after the build. `volatile` so the compiler can't fold the empty defaults.
__attribute__((used, section(".rodata.tnxcfg"), aligned(16)))
const volatile Config g_Config = {{'T', 'N', 'X', 'H', 'C', 'F', 'G', '1'}, 1, 0, 0, {}};

enum RingState : uint32_t { StateStarting = 0, StateHooked = 1, StateNoBuild = 2, StateBadConfig = 3 };

struct RingEvent {              // 64 bytes
    uint32_t seq;               // write_seq + 1 of this event, stored last (release)
    uint32_t hook;
    uint64_t tick;
    char id[kIdLen];
};

struct Ring {                   // 64 + 64 * 64 bytes
    char magic[8];              // "TNXRING1", set at start (it must only exist in this RW memory)
    uint32_t version;
    uint32_t state;
    uint64_t title_id;
    uint8_t build_id[16];
    uint32_t hooks_installed;
    uint32_t write_seq;
    uint32_t pad[4];
    RingEvent ev[kRingSlots];
};
static_assert(sizeof(Ring) == 64 + 64 * 64, "ring layout");

__attribute__((aligned(64))) Ring g_Ring;

using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
Fn g_Orig[kMaxHooks];
HookSlot g_Active[kMaxHooks];

// Everything on the call path keeps to general registers and calls no library function (no memcpy): the hooked
// function's float arguments in v0-v7 must reach it untouched. svcQueryMemory is a bare `svc`.
#define TNX_HOT __attribute__((target("general-regs-only"), noinline, optimize("no-tree-loop-distribute-patterns")))

// Is [p, p + n) mapped and readable? A bad pointer must never fault inside the game.
TNX_HOT bool readable(uintptr_t p, size_t n) {
    MemoryInfo mi;
    u32 pi;
    if (R_FAILED(svcQueryMemory(&mi, &pi, p))) return false;
    if (!(mi.perm & Perm_R) || mi.type == MemType_Unmapped) return false;
    return p + n <= mi.addr + mi.size;
}

TNX_HOT void record(int slot, const uint64_t* args) {
    const HookSlot& h = g_Active[slot];
    char id[kIdLen];
    for (int i = 0; i < kIdLen; i++) ((volatile char*)id)[i] = 0;
    uint64_t v = args[h.reg & 7];
    if (h.type == ArgInt32) {
        // decimal, the key the id table uses ("5"); negative values kept as is
        int32_t n = (int32_t)(uint32_t)v;
        char tmp[12];
        int len = 0;
        uint32_t u = n < 0 ? (uint32_t)(-(int64_t)n) : (uint32_t)n;
        do { tmp[len++] = (char)('0' + u % 10); u /= 10; } while (u && len < 11);
        int o = 0;
        if (n < 0) id[o++] = '-';
        while (len) id[o++] = tmp[--len];
    } else if (h.type == ArgString) {
        // IL2CPP System.String: klass, monitor, int32 length at 0x10, UTF-16 at 0x14
        if (!v || (v & 7)) return;
        if (!readable(v, 0x14)) return;
        int32_t len = *(const int32_t*)(v + 0x10);
        if (len <= 0 || len >= kIdLen || !readable(v + 0x14, (size_t)len * 2)) {
            id[0] = '?';
        } else {
            const uint16_t* s = (const uint16_t*)(v + 0x14);
            for (int i = 0; i < len; i++) id[i] = (s[i] >= 0x20 && s[i] < 0x7F) ? (char)s[i] : '?';
        }
    } else {
        return;
    }
    uint32_t seq = __atomic_fetch_add(&g_Ring.write_seq, 1, __ATOMIC_ACQ_REL);
    RingEvent& e = g_Ring.ev[seq % kRingSlots];
    __atomic_store_n(&e.seq, 0u, __ATOMIC_RELAXED);   // slot being rewritten: readers skip it
    e.hook = (uint32_t)slot;
    e.tick = svcGetSystemTick();
    for (int i = 0; i < kIdLen; i++) ((volatile char*)e.id)[i] = id[i];
    __atomic_store_n(&e.seq, seq + 1, __ATOMIC_RELEASE);
}

// One callback per slot. Records, then *tail-calls* the original: no frame of ours stays on the stack, so a managed
// exception thrown by the original unwinds exactly as without the hook. general-regs-only (target attribute)
// so float argument registers v0-v7 reach the original untouched.
#define TNX_CALLBACK(N)                                                                                     \
    uint64_t cb##N(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, \
                   uint64_t a7) {                                                                            \
        {                                                                                                    \
            const uint64_t args[8] = {a0, a1, a2, a3, a4, a5, a6, a7};                                       \
            record(N, args);                                                                                 \
        }                                                                                                    \
        __attribute__((musttail)) return g_Orig[N](a0, a1, a2, a3, a4, a5, a6, a7);                         \
    }
TNX_CALLBACK(0)
TNX_CALLBACK(1)
TNX_CALLBACK(2)
TNX_CALLBACK(3)
const Fn kCallbacks[kMaxHooks] = {cb0, cb1, cb2, cb3};

void readConfig(Config& c) {   // volatile -> plain copy
    const volatile uint8_t* src = (const volatile uint8_t*)&g_Config;
    uint8_t* dst = (uint8_t*)&c;
    for (size_t i = 0; i < sizeof(Config); i++) dst[i] = src[i];
}

}  // namespace

extern "C" void exl_main(void* x0, void* x1) {
    (void)x0;
    (void)x1;
    static const char ringMagic[8] = {'T', 'N', 'X', 'R', 'I', 'N', 'G', '1'};
    g_Ring.version = 1;
    g_Ring.state = StateStarting;

    static Config c;
    readConfig(c);
    g_Ring.title_id = c.title_id;
    const auto& text = exl::util::GetMainModuleInfo().m_Text;
    const uintptr_t base = exl::util::modules::GetTargetStart();

    int match = -1;
    bool sane = memcmp(c.magic, "TNXHCFG1", 8) == 0 && c.version == 1 && c.build_count <= kMaxBuilds;
    for (uint32_t b = 0; sane && b < c.build_count && match < 0; b++) {
        const BuildCfg& bc = c.builds[b];
        if (bc.hook_count == 0 || bc.hook_count > kMaxHooks) { sane = false; break; }
        bool all = true;
        for (uint32_t i = 0; i < bc.hook_count && all; i++) {
            const HookSlot& h = bc.hooks[i];
            uintptr_t at = base + h.offset;
            if (h.offset & 3 || h.reg > 7 || (h.type != ArgInt32 && h.type != ArgString) || !text.InRange(at) ||
                !text.InRange(at + 15)) { sane = false; all = false; break; }
            all = memcmp((const void*)at, h.sig, 16) == 0;
        }
        if (all && sane) match = (int)b;
    }

    if (!sane) {
        g_Ring.state = StateBadConfig;
    } else if (match < 0) {
        g_Ring.state = StateNoBuild;   // unknown version: the game runs untouched
    } else {
        const BuildCfg& bc = c.builds[match];
        memcpy(g_Ring.build_id, bc.build_id, 16);
        exl::hook::Initialize();
        for (uint32_t i = 0; i < bc.hook_count; i++) {
            g_Active[i] = bc.hooks[i];
            g_Orig[i] = exl::hook::Hook(base + bc.hooks[i].offset, kCallbacks[i], true);
            g_Ring.hooks_installed++;
        }
        g_Ring.state = StateHooked;
    }
    // magic last: the live module only trusts a ring whose header is complete
    __atomic_thread_fence(__ATOMIC_RELEASE);
    memcpy(g_Ring.magic, ringMagic, 8);
}

extern "C" NORETURN void exl_exception_entry() {
    /* Note: this is only applicable in the context of applets/sysmodules. */
    EXL_ABORT("Default exception handler called!");
}
