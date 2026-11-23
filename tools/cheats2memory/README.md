# Cheat codes → live memory achievements

`import.js` drafts `memory` achievements ([docs/custom-achievements.md, section 5b](../../docs/custom-achievements.md#5b-live-achievements-from-memory-any-game))
from the codes in [switch-cheats-db](https://github.com/HamletDuFromage/switch-cheats-db): a code that sets a game's
money or level already says where that value lives, for each game version.

```
git clone --depth 1 --filter=blob:none --sparse https://github.com/HamletDuFromage/switch-cheats-db.git /tmp/cheatsdb
git -C /tmp/cheatsdb sparse-checkout set --no-cone '/cheats/*'
node tools/cheats2memory/import.js /tmp/cheatsdb            # -> drafts/memory/<TITLEID>.json + README.md
node tools/cheats2memory/import.js /tmp/cheatsdb --only 0100698009C6E000 --out /tmp/x
```

The *PC to Switch* workflow runs it every week.

## What it keeps

- **Codes:** only plain stores: a single `0` line writing to `heap`/`alias`, or a pointer chain (`5` loads, `7`
  adds/subs, one final `6` store). Code patches (writes to `main`), button or conditional codes, several stores and
  the `aslr` region are skipped.
- **Names:** money-like balances ("Max Gold", "Inf. Rupees", "99999 Money") and levels ("Max Level"). Multipliers,
  prices, enemy values, HP and the like are skipped.
- **Value type:** the width the code writes, and `f32`/`f64` when the value it writes is a float. Reading a float as
  an integer gives a huge number, so it would unlock at once.
- **Versions and slots:** every build ID gets its own address. When several codes for one build set the same
  balance (save slots, characters), they're all listed and **any** of them unlocks it.
- **Tiers:** three per balance. The top one is a tenth of what the code writes, at most 1,000,000; the other two
  are a tenth and a hundredth of that. Levels get a quarter, half and all of the code's level. These are guesses:
  tune them after testing. `max` is ten times the code's value, so garbage reads are ignored.
- A game that already has a definition in `romfs/games/` gets its whole definition plus the memory achievements, since
  a file on the SD card replaces the built-in one.

Every draft says `"_draft": "unverified"`, lists the codes it came from in `_cheats`, and credits the code authors
(`_source`). **None has been checked on a console.** Test one (docs, section 5b) before adding it to `romfs/games/`.

The idea comes from [HomeNX's achievement packs](https://github.com/ad3333m/homenx/blob/master/docs/achievement-packs.md).
This is an independent implementation. Unlike HomeNX's packs, floats are detected and existing definitions are kept.

Tests: `node tools/cheats2memory/test/test_import.js`.
