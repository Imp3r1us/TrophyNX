# DELTARUNE trophies on Switch

DELTARUNE has 30 trophies on PS4/PS5. The Switch version contains the same trophy code (`obj_event_manager`) but only
turns it on for PlayStation, and an unlocked trophy only ever goes to PSN, never into the save. So TrophyNX can't see them.

This folder has a small patch that you apply **to your own dump** of the game. It changes two things in chapters 1 to 4:

1. the trophy code is turned on on Switch too (`obj_initializer2`: `enable_trophies()` no longer limited to PS4/PS5);
2. each trophy, when the game unlocks it, also sets `global.flag[2400 + trophy number]` to 1 (`queue_trophy()` and
   `unlock_trophy()` in `obj_event_manager`).

`global.flag` is part of every save, so the unlock ends up in `deltarune.sav` at the next save, and TrophyNX reads it
(reader `deltarune`, `romfs/games/0100A0D022A68000.json`). Nothing else in the game changes, and saves stay compatible
with the unpatched game (an unused flag is just left set).

Flags 2400–2429 are unused by every chapter (1–5) of the game: the patch writes nothing that the game reads.
Chapter 5 has no trophy code (the PS5 version only keeps activity cards), so it is left as is: its 30 trophies are
those of chapters 1–4.

No game file is included here: the script edits your copy, and you need your own dump.

## What you need

New to dumping? Follow [the beginner guide](../../docs/dumping-guide.md) first (Step 3).

- Your DELTARUNE dump (title `0100A0D022A68000`, the `romfs` folder with `chapter1_switch` … `chapter4_switch`), e.g. dumped
  with nxdumptool from your own cartridge or installed game. Use the **same game version** as the one on your console
  (base game + update): after a game update, dump and patch again.
- [UndertaleModTool](https://github.com/UnderminersTeam/UndertaleModTool/releases) **CLI** build (`UTMT_CLI_…zip`, tested with
  0.9.2.0 on Windows). The GUI works too (Scripts → Run other script, then File → Save as), one chapter at a time.
- Atmosphère (LayeredFS) on the console.

## Patch

Windows (PowerShell):

```powershell
.\tools\deltarune\patch.ps1 -Romfs D:\dump\romfs -Cli C:\Tools\UTMT_CLI\UndertaleModCli.exe -Out D:\patched
```

Linux / macOS:

```sh
sh tools/deltarune/patch.sh ~/dump/romfs ~/UTMT_CLI/UndertaleModCli ~/patched
```

Each chapter takes a few seconds. The script stops with an error (and writes nothing for that chapter) if the code doesn't
look like the version it was made for. Running it on an already patched file does nothing.

By hand, per chapter:

```sh
UndertaleModCli load <romfs>/chapter1_switch/game.win -s tools/deltarune/TrophyNX_trophies.csx -o out/chapter1_switch/game.win
```

(Close its standard input, e.g. `< /dev/null`: the CLI otherwise waits for input when it's done.)

## Install

Copy the result to the SD card so that you get:

```
sdmc:/atmosphere/contents/0100A0D022A68000/romfs/chapter1_switch/game.win
sdmc:/atmosphere/contents/0100A0D022A68000/romfs/chapter2_switch/game.win
sdmc:/atmosphere/contents/0100A0D022A68000/romfs/chapter3_switch/game.win
sdmc:/atmosphere/contents/0100A0D022A68000/romfs/chapter4_switch/game.win
```

Only these four files: don't copy the rest of the romfs. To uninstall, delete the
`atmosphere/contents/0100A0D022A68000/romfs` folder.

Then play: a trophy is written to the save the next time the game saves (save point, or the end of a chapter). Close the
game, open TrophyNX and press Y. The X screen of the game shows, for each trophy, the value read from the save.

## What works without the patch

The definition reads both the patch flags and what an unpatched save shows anyway, so without the patch you still get:

| Trophy | Without the patch |
|---|---|
| CHAPTER 1–4 END | the chapter's completion save exists |
| CONSUMER (Chapter 1) | the moss was eaten |
| ARMOR (Chapter 1) | someone wears armor in a Chapter 1 save |
| CLOSENESS | the Chapter 2 photo choice |
| BEAST | Susiezilla won |
| RING | a ThornRing or FreezeRing is owned |
| CONNECTION | all 29 others, however they were unlocked |

The others need the patch, and only count from the moment it is installed (the PlayStation counters, like the 110
treasure chests, start at 0 on Switch).

## Tree read by TrophyNX

`deltarune.sav` is a JSON object `{file name: file text}`. The reader turns it into (see `include/save_formats.h`):

- `files[*]`: one entry per chapter file `filechN_S` (`chapter`, `slot`, `completion`, `plot`, `gold`, `lv`, `weapon`,
  `chararmor1`, `flags/<n>`…);
- `ini/dr.ini/<section>/<key>` (and the other `.ini` files of the save);
- `completed[1..5]`, `patch[0..29]` (patch flags only), `trophy[0..29]` (patch or fallback).

So a custom list can use e.g. `files[*]/flags/1220` or `files[chapter=2]/plot`.

## Tests

`make_test_save.py` writes made-up saves in the game's format; `host/test/test_deltarune.cpp` (app) and
`tools/savelab/test/test_formats.js` (Save Lab) check the reader on them and that both build the same tree.
