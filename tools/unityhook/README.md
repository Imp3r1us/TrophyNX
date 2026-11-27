# unityhook (experiment, step 1): find a Unity game's own "unlock achievement" call

Goal: for Switch ports of Unity games, hook the game's own achievement call (the way
[Goldberg Steam Emulator](https://gitlab.com/Mr_Goldberg/goldberg_emulator) answers `ISteamUserStats::SetAchievement`
on PC) so TrophyNX gets the official achievements, exactly when the game awards them.

This folder is the **PC-side survey tool** only. Nothing here runs on the Switch, and the live module
(`sysmodule/`) stays read-only: any hook would be a separate, optional per-game module (see step 2, not built).

## What it does

```
global-metadata.dat (RomFS) ──> class / method names, method tokens ──┐
main (ExeFS NSO) ──> LZ4 segments, MOD0 relocations ──> Il2CppCodeRegistration ──> methodPointers[] ──┴─> list
```

For every method whose type or name matches `Achiev|Troph|Unlock|ReportProgress|SetAchievement|Stats`, it prints:

| Column | Meaning |
|---|---|
| verdict | `HOOKABLE` own address and called directly · `VIRTUAL` virtual method, called through a vtable / interface (its entry is still hookable) · `NO-CALLER?` not virtual and never called directly (inlined, delegate / UnityEvent / reflection, or dead) · `HOOKABLE-SHARED` / `STUB-SHARED` the compiler merged it with other identical functions (a hook there fires for all of them) · `NO-CODE` abstract, interface or stripped |
| `main+0x…` | entry point as an offset from the start of the game's main module (what exlaunch / Skyline hook) |
| kind | `static`, `instance`, `virtual` or `abstract` (decides which register holds the first argument: x0, or x1 after `this`) |
| size | bytes to the next method (4 = a lone `ret`: a stub, still hookable if it is called) |
| calls | direct `BL` / `B` (tail call) sites to that address |
| shared | other methods at the same address |
| `*` | looks like an unlock call (`Unlock…Achievement`, `SetAchievement`, `ReportProgress`, `Award`…, not UI classes) |

The header line gives the metadata version, how the addresses were found and the **build ID** (first 20 bytes of the
NSO build ID, the folder name Atmosphère uses for `exefs_patches` and cheats): addresses are only valid for that build.

Supported: metadata v24.0 / 24.1 (Unity 2017–2018: one global `methodPointers` array), v24.2–v31 (Unity 2019–2023:
`codeGenModules`), v38+ (Unity 6: new header, variable-width indexes). Compressed or plain NSOs, `DT_RELA` and
`DT_RELR` relocations. A PC `GameAssembly.dll` is accepted too (used to check the tool).

## Use

Dump two files of the game with nxdumptool ([dumping guide](../../docs/dumping-guide.md), same menus):

- RomFS → `Data/Managed/Metadata/global-metadata.dat`
- ExeFS → `main` (in the same *Program #0* NCA, the section that says **ExeFS**). Use the **update** if the game has one:
  `dump update` instead of `dump base application`, so the code matches the version you play.

```
python tools/unityhook/unityhook.py global-metadata.dat main
python tools/unityhook/unityhook.py --scan "<SD>/nxdumptool/NCA FS" --json survey.json   # every game dumped
```

`--scan` pairs each `global-metadata.dat` with the nearest `main` (NSO) found under the same game folder and prints
one summary per game: verdict counts, how many unlock-like methods (`strong`) and how many of those have an address
of their own (`usable`). Python 3, no dependency (uses the `lz4` module if installed, a pure-Python decoder otherwise).

## The hook (step 3, experimental)

Design and risks: [DESIGN.md](DESIGN.md). Pieces: the generic module [`hook/`](../../hook) (exlaunch), per-game
data in [`data/unityhook/`](../../data/unityhook), the reader in the live module (`sysmodule/source/hookwatch.cpp`,
ring format in `include/hookring.h`), and `"steam"` names / `hook` rules in the game's definition
([docs](../../docs/custom-achievements.md#5c-official-achievements-through-the-hook-experimental-unity-games)).

```
# 1. once per game version: find the method in your dump, record its offset + first 16 bytes for that build
python tools/unityhook/unityhook.py hook-add 0100633007D48000 "<dump>/HOLLOW KNIGHT" \
       --method AchievementHandler::AwardAchievementToPlayer --arg x1 --type string
# 2. build the generic module (Docker image devkitpro/devkita64, or the CI artifact "TrophyNX-hook-module")
cd hook && make
# 3. make the SD files: the module with this game's table + YOUR dump's npdm with 2 syscalls added
python tools/unityhook/unityhook.py hook-make 0100633007D48000 "<dump>/HOLLOW KNIGHT"
#    -> out/hook/atmosphere/contents/<TITLEID>/exefs/{subsdk9,main.npdm}: copy atmosphere/ to the SD card
```

- `--arg`: the register that holds the id. Instance methods: `x0` is `this`, the first argument is `x1`.
- `--type`: `string` (IL2CPP `System.String`) or `int32` (an enum: map the numbers in the definition's
  `hook_ids`).
- `hook-add` refuses methods whose code is shared with others, that start with PC-relative instructions, or that
  are overloaded.
- `out/` is git-ignored: the npdm in it comes from your dump and must not be shared.
- To remove the hook: delete `atmosphere/contents/<TITLEID>/exefs/subsdk9` and `main.npdm`.
- The live module log (`sdmc:/switch/TrophyNX/live_log.txt`) says `hook: ring found`,
  `hook: hooked, build …` and `UNLOCKED … [hook: NAME]`. For an unknown game version it says so and the game runs
  unhooked.

## Results (Switch dumps, 2026-11-26)

| Game | Unity / metadata | Addresses resolved | Hook point | Calls | Argument | Steam mapping |
|---|---|---|---|---|---|---|
| Hollow Knight (`0100633007D48000`, build `FB968CFB649A5842…`) | 2017.x / v24.0 (global `methodPointers`) | 35,951 / 37,567 | `AchievementHandler::AwardAchievementToPlayer(string key)` `main+0x226E20` | 47 | `x1` = `System.String` key (instance method, `x0` = this) | key **is** the Steam API name: 46 of 63 literals in code, the other 17 (endings, Steel Soul, Zote…) in scene / PlayMaker data, same function |
| BALL x PIT (`010086A022444000` v393216, build `ED9090F18AE37A5A…`) | 2022.x / v31 (`codeGenModules`) | 115,304 / 128,047 | `AchMgr::Earn(AchType ach)` `main+0x31D4630` | 33 | `w1` = enum constant 0–62 (0 passed as `wzr`) | `AchType` enum: values read from the metadata's default-value table (`kCompleteGraveyard` = 0 … `kCompleteGame` = 62); value N = the Nth entry of the Steam schema, cross-checked by name similarity (62 of 63 agree; `kStatScalingBuildingFullUpgrade` → `stat_scaling_full_upgrade` by elimination). Table: `hook_ids` in `romfs/games/010086A022444000.json`. The 19 other Steam achievements are later content not in this build |

Both games keep a working, game-side achievement manager on Switch, called from dozens of places with the id as a
plain argument. The Switch platform layer is what is gutted (Hollow Knight: `SwitchPlatform::PushAchievementUnlock`
virtual, no direct caller; BALL x PIT: `Toybox.Port.PlatformAchievementManager::UnlockAchievement` never called),
so the hook goes on the game's manager, one level above the platform call Goldberg would catch on PC.
Every resolved address lies in `.text`, and 88–94 % follow a `ret` / branch / padding word (function boundaries).

## Credits

- Locating `Il2CppCodeRegistration` from the `"mscorlib.dll"` string, and the metadata layouts per version, follow
  [Il2CppDumper](https://github.com/Perfare/Il2CppDumper) by Perfare (MIT). Reimplemented here, no code copied.
- The hook-the-unlock-call idea comes from Goldberg Steam Emulator by Mr_Goldberg.
- Hooking on Switch (step 2): [exlaunch](https://github.com/shadowninja108/exlaunch) by shadowninja108 and
  [Skyline](https://github.com/skyline-dev/skyline).

License: GPL-2.0-or-later, like the rest of TrophyNX.
