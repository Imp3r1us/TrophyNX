# unityhook, step 2: design of the injected hook (not built)

Status: built (step 3) for Hollow Knight, **not yet run on a console**. Step 1 results:
[README.md](README.md#results-switch-dumps-2026-11-26). Usage: [README.md](README.md#the-hook-step-3-experimental).

## In one paragraph

A small exlaunch module (`subsdk9`), the same binary for every game, hooks the game's own achievement manager at
the offsets the PC tool found. Each call puts the achievement id in a ring buffer inside the module's memory. Nothing
else happens in the game: no file, no IPC, no allocation. The live module already attaches to games through
`dmnt:cht` (read-only) for memory rules. It finds that buffer by its magic and reads new ids every second. It then
unlocks through the usual path (`<data>/unlocks/<user>.json`, key `<TITLEID>/<id>`, `popupEnqueue`). The per-game
part is a config blob that the PC tool writes into a copy of the module: offsets, a signature of the hooked bytes
per build, the argument to read, and the id table. A game update the config doesn't know leaves the game unhooked,
never broken.

```
 game process                                              sys-trophynx (live module, unchanged rules: read-only)
 ┌────────────────────────────────────────────┐
 │ AchMgr::Earn(this, ach=5)                   │
 │   └─ trampoline ─> tnx_hook(): ring.push("5")│   dmnt:cht read, every 1 s     ┌──────────────────────────┐
 │                     then original Earn      │ ─────────────────────────────> │ ring: seq 41 "5"         │
 │ subsdk9 .bss: "TNXRING1" header + 64 slots  │                                │ ids["5"] = complete_...  │
 └────────────────────────────────────────────┘                                │ store + popup (existing) │
                                                                                └──────────────────────────┘
```

## 1. Pieces

| Piece | Where | What |
|---|---|---|
| Hook module | new `hook/` (exlaunch, C++) | one generic `subsdk9`: reads its embedded config, checks signatures, installs trampolines, fills the ring |
| Config writer | `tools/unityhook/unityhook.py --make-hook` | from the dump + a reviewed `data/unityhook/<TID>.json`, writes the config blob into a copy of the module, plus the npdm (see 5) |
| Per-game data | `data/unityhook/<TID>.json` | builds (build ID → hook offsets + 16-byte signatures), argument kind, id → Steam name table |
| Live module | `sysmodule/source/hookring.cpp` (~150 lines) | finds and reads the ring through the existing dmnt attach, turns ids into unlocks |
| Definitions | `romfs/games/<TID>.json` | a `"steam"` name on achievements, and a new rule `{"type": "hook", "key": "<Steam name>"}` |
| App | game page | "Official achievements (experimental)" toggle: copies or removes `atmosphere/contents/<TID>/exefs/subsdk9` (+ `main.npdm` only if 5. requires it) |

The live module still never writes game memory, saves or code. The only thing that writes into the game is the hook
module, a separate opt-in download per game, as you asked.

## 2. How the hook talks to the live module: neither file nor IPC, a ring read through dmnt

| | File (game writes `sdmc:/…/hook.log`) | IPC (live module serves `tnx:h`) | **Ring buffer read through dmnt:cht** |
|---|---|---|---|
| Work inside the game | fs open/append/flush **on the game's thread** at the moment of the unlock (SD stalls of 10–100 ms are common; that is a frame hitch or worse) | IPC call on the game's thread; blocks if the live module is busy (popup drawing, close scan) | two stores to memory |
| Permissions | npdm needs SD access (`filesystem_access`) | npdm service list must include the new service | none beyond what hooking needs |
| Live module changes | poll a file (easy) | a server thread + a service registration: a new long-running component in a module that crashes the console when it goes wrong | reuse the existing dmnt attach (memory rules already do it every second) |
| If the live module is off or crashed | still recorded (good) | the game's call fails or blocks (bad) | events wait in the ring until the game closes (64 slots) |
| If dmnt is unavailable | works | works | no events (same limit as memory rules; the module already has an "own debugger" fallback) |

Ring layout (in the module's `.bss`, 16-byte aligned, ~3.2 KiB):

```c
struct TnxRing {                    // all little-endian
    char     magic[8];              // "TNXRING1"
    uint32_t version;               // 1
    uint32_t state;                 // 1 = hooks installed, 2 = no build matched (unknown version), 3 = config error
    uint64_t title_id;              // from the config
    uint8_t  build_id[16];          // the build whose signatures matched (zeros if none)
    uint32_t hooks_installed;
    uint32_t write_seq;             // atomic, incremented by each event (slot = seq % 64)
    struct { uint32_t seq; uint32_t hook; uint64_t tick; char id[48]; } ev[64];   // seq written last
};
```

Writer (any game thread): `s = atomic_fetch_add(&write_seq, 1)`, fill `ev[s % 64]` and then store `ev.seq = s + 1` with
release order. Reader (live module): read the header, then each slot from its last seen seq up to `write_seq`. A slot
whose `seq` doesn't match yet is read again next second. More than 64 events in a second gets logged as an overflow.
That can't happen in practice: Hollow Knight fires every earned achievement at save load, and that is 63 events at most.

Finding the ring: after the dmnt attach, walk the game's mappings (`dmntchtQueryCheatProcessMemory`) and keep only
the small read-write code-data regions after `main` (the modules' `.data`/`.bss`, a few at most). Search them for
the magic once and cache the address for the session. The overlay "memory test" page shows `state` (hooked /
unknown build) the same way it shows memory-rule coverage today.

## 3. Ids → Steam names → TrophyNX achievements

The hook sends the game's raw id as text. Turning it into an achievement happens on the PC, once per game, and is
reviewed:

| Game | Raw id | Table in `data/unityhook/<TID>.json` |
|---|---|---|
| Hollow Knight | the `string key` argument (`x1`: a `System.String*`; length `int32` at +0x10, UTF-16 at +0x14) | none: the key **is** the Steam API name (`CHARMED`…) |
| BALL x PIT | `AchType ach` (`w1`: int32, 1–62) | `"ids": {"1": "complete_graveyards", …}` |

Building the enum table (BALL x PIT and similar games). The literals in a v27+ metadata file are sorted, so
their order means nothing. Two sources, used together:
1. **The game's own table.** `AchMgr..cctor` (7,760 bytes) almost certainly builds the AchType → platform-name
   table. The tool disassembles it, pairing each enum constant (`mov wN, #k`) with the next string-literal load. In
   v27+ that load is `adrp`+`ldr` of a metadata-usage slot whose initial value encodes the literal index. This is an
   assumption until I look at the cctor.
2. **A name match** as a cross-check: `kCompleteGraveyard` ~ `complete_graveyards`, `k100000Kills` ~ `100000_kills`
   (lowercase, `_` removed, trailing `s` ignored).
   If the two disagree, the entry is left out and listed in the tool's report for a human to fill.

Definitions: each achievement gets an optional `"steam": "<API name>"`. For a game with a hook config, the tool
adds every Steam achievement (from `data/pc2switch/steam/<appid>.json`: name, description, hidden) that the
definition doesn't already cover, with id `steam_<name>` and rule `{"type": "hook", "key": "<name>"}`. An
achievement that already exists (Hollow Knight's save-based `charm_first`…) keeps its id and rule and only gains
`"steam"`. So the hook unlocks it early, the save scan still unlocks it without the hook, and nobody's existing
unlocks change key. `hook` rules never evaluate from saves, so without the module they just stay locked. The app shows
them in an "Official" filter with the module's status.

The live module looks up `id → Steam name → definition achievement`, skips what the store already has (it re-reads
the store first, exactly like the memory path), writes `<TITLEID>/<id> = now` and enqueues the popup.

## 4. Versions and build IDs

- `data/unityhook/<TID>.json` lists builds by **main NSO build ID** (the 20 bytes the tool prints; the same key that
  memory rules use under `"builds"`). Each hook gives `offset`, the **first 16 bytes at that offset** (signature),
  `arg` (`x0`/`x1`/`w1`…) and `type` (`string` / `int32`).
- Atmosphère loads `exefs/` overrides for **every** version of the title. So at start the module compares the
  signature bytes of each build in its config against the running `main` (base from `nn::ro`/rtld module info) and
  installs only a build that matches on every hook. No match → `state = 2`, nothing patched, the game runs untouched,
  and the live module logs `hook: no build for <build id>, run unityhook on this version` (like memory rules do).
- Several builds in one config: a game update needs a new dump, one tool run and one data PR. The module binary
  stays the same.
- The tool refuses a hook whose first 4 instructions are PC-relative (`adr`, `adrp`, `b`, `bl`, `cbz`, `ldr` literal):
  a trampoline would have to relocate them. IL2CPP methods start with `stp x29, x30` or `sub sp`, so this shouldn't
  happen, but it gets checked.

## 5. The npdm question (most important unknown)

exlaunch deploys `subsdk9` plus an npdm generated from a JSON. Its application sample has full filesystem access
(`0x4000000000000000`), `allow_debug` and extra memory syscalls (`svcSetMemoryPermission`, `svcMapMemory`,
`svcQueryMemory`, …). A replaced `main.npdm` is the riskiest file here: it is version-specific (services, fs
permissions, thread priorities). After a game update, a stale npdm can stop the game from booting until the player
removes it.

**Checked on both dumps (ACI0 of the stock `exefs/main.npdm`).** Hollow Knight (49 syscalls) and BALL x PIT (50)
already allow every syscall in exlaunch's sample npdm: `SetMemoryPermission`, `MapMemory`, `UnmapMemory`,
`QueryMemory`, `GetInfo`, `MapPhysicalMemory`, `FlushProcessDataCache`. Their fs permissions are the same value
(`0x4000000000000000`), and their debug-flags descriptor sets allow-debug. The exlaunch sample looks like a copy of a
stock game npdm. Neither game has `SetProcessMemoryPermission` / `MapProcessCodeMemory`.

**Checked in exlaunch's source (commit `f9f4b0d`, 2026-08-26): an npdm change is unavoidable.** To patch `.text`,
exlaunch maps a read-write view of the code with `svcMapProcessMemory` / `svcUnmapProcessMemory`
(`lib/util/sys/rw_pages.cpp`), using its own process handle from Mesosphère's `svcGetInfo`. Neither game allows
0x74 / 0x75. (`svcCreateSession` / `svcReplyAndReceive`, also missing, are only an old-kernel fallback that
Atmosphère never takes.) Its repo `config.json` simply allows every syscall. The exlaunch sample npdm I compared
against first is not the one it builds with: that earlier conclusion was wrong.

So the choice is between:
- **A. Patched npdm** (what Skyline and exlaunch mods ship): the tool writes **the dumped npdm + 0x74 and 0x75
  only** (same services, fs permissions, priorities), records its hash and the game version it came from, and the app
  warns when the installed version differs. Risk: a game update that changes its npdm (rare: services and
  permissions usually stay the same) would boot with the stale one until the player removes it or re-dumps.
- **B. No injected code at all:** watch the game's own achievement state with a `memory` rule (pointer chains), which
  the live module already supports. No hook, no npdm, read-only, but it is the existing mechanism, not the "official
  call" this experiment is about.
- **C. Atmosphère `exefs_patches` (IPS) only:** branch from the hooked function into a code cave in `main` (for
  example the dead `PlatformAchievementManager::UnlockAchievement` body) that writes to a free `.bss` slot. No npdm,
  no subsdk, but hand-written per build and fragile. Not recommended.

## 6. Crash risks and what each costs

| Risk | Effect | Mitigation |
|---|---|---|
| Wrong offset (update, wrong dump) | jump into the middle of code: crash on the first unlock | signature check per build, all-or-nothing install |
| Trampoline over PC-relative code | wrong branch target: crash | tool refuses such hooks (4.) |
| Bad `System.String*` | read fault inside the game | null check, length 1–47, copy ASCII only, and read through the managed object only (the pointer comes from the game's own call). Anything odd → id `"?"` + log |
| Hook changes the call | game logic differs | the callback **records, then tail-calls the original** with all of `x0`–`x7` untouched (IL2CPP's hidden `MethodInfo*` is the last argument). No return value of its own, no float registers touched |
| Managed exception thrown through the hook frame | IL2CPP exceptions are C++ exceptions; without unwind info the process aborts | record **before** calling the original; build the module with unwind tables so an exception passes through cleanly |
| Two threads unlocking at once | torn ring slot | atomic `fetch_add` + per-slot `seq` written last (2.) |
| Another exefs mod for the same game (`subsdk9`, other npdm) | one of them doesn't load or the npdm conflicts | the app refuses to install over an `exefs/` folder it didn't create, and says so |
| Memory | the game's own heap is tight on some ports | no heap allocation at all; module ~100 KiB, ring 3 KiB |
| Game closes within 1 s of an unlock | event lost | Hollow Knight re-fires earned achievements at load (`CheckAllAchievements`). BALL x PIT's `IsEarned` check means it may not, so the save/close scan would need an `AchMgr` source later. Accepted for the experiment |
| Online / bans | modified code in a game process | both games are offline. Same advice as any exefs mod (emuMMC); the toggle is off by default and says so |
| Live module side | new reader in a module that must not crash | only `dmntchtReadCheatProcessMemory` of a fixed-size header + slots, bounds-checked, skipped in safe mode like memory watches |

## 7. What would be built (step 3, after your go-ahead)

1. Read exlaunch's hook code and `npdm.mk` to confirm 5.1. **Stop and report** if a replaced npdm turns out to be
   unavoidable.
2. `hook/`: the exlaunch module (config parse, signature check, trampolines, ring), GPL-2.0-or-later, built by the
   existing Makefile flow.
3. `unityhook.py --make-hook <TID>`: config blob + Hollow Knight table. Hollow Knight comes first because its ids
   need no table.
4. `sysmodule/source/hookring.cpp` + the `hook` rule type in `shared_rules`, then the app toggle.
5. Test on Hollow Knight (fresh save → first charm → popup, then reload → nothing duplicated), then BALL x PIT with
   the cctor table.

## Credits

Goldberg Steam Emulator (hooking the unlock call), exlaunch by shadowninja108 (GPL-2.0, injection and trampolines),
Skyline (the `subsdk` + npdm deployment pattern used by Smash mods), Il2CppDumper by Perfare (metadata/binary
layouts). If exlaunch is GPL-2.0-only, the built hook module ships under GPLv2, as `sysmodule/` already does for
`dmntcht`.
