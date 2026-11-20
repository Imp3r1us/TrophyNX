# Save Lab

**Open it online: https://imp3r1us.github.io/TrophyNX/** (English / French, switch with the button at the top right).

A single web page (works on a phone, no PC needed) to **understand a Switch save and turn it into achievements**
in the app's format (`games/<TITLEID>.json`). Everything runs in your browser: your save files are never uploaded.
It also works offline: download `index.html` from this folder and open it.

Step-by-step guide for players: [Making your own achievement list](../../docs/custom-achievements.md).

1. **Captures**: import several saves of the same game (files or JKSV .zip), in order, named after what you did
   in the game. A “control” capture (nothing done since the previous one) removes noise.
2. **Structure**: recognised format (BotW/TotK/MK8D/Smash, BYML trees, JSON text saves of Unity games such as
   Hollow Knight, Unreal Engine “GVAS” saves, .NET BinaryFormatter “NRBF” data of Unity games, repeating-key XOR),
   header, entropy (encrypted/compressed?), hash→value table, name dictionary (CRC32/Murmur3/FNV-1a), texts.
3. **Differences**: every value that changes is classified (0→1 flag, counter, bit field, float, noise).
   Multiple selection → “count” / “sum” achievements; “Extend the series” finds the neighbouring flags of a collection.
   **⚡ Generate achievements**: from a new game + an advanced/100% save (e.g. downloaded from GBAtemp), groups the
   flags into collections (first / half / complete tiers) and the counters into tiers.
4. **Known value**: Cheat Engine-style search for a number seen in the game.
5. **Achievements**: editing, test on every capture with the same logic as the console, validation, JSON export
   (copy or download).
6. **PC → Switch**: import the app's `scan_bundle.json` (ZR) and draft rules from a game's Steam achievement list.

Files: `engine.js` (engine, tested by `node test/test_engine.js`), `formats.js` (GVAS and NRBF format modules,
tested by `node test/test_formats.js` on `test/fixtures/`), `app.html` (interface),
`build.py` → `index.html` (standalone page that also embeds the definitions of `romfs/games`).
The website is rebuilt and published by `.github/workflows/pages.yml` on every change to these files.

### Languages

Interface texts are written as `T('English', 'French')` (global `T` defined at the top of `app.html`; `engine.js` and
`formats.js` fall back to English under Node). Static HTML uses `<span class="l-en">…</span><span class="l-fr">…</span>`
pairs, and `data-fr` / `data-fr-ph` for `<option>` texts and placeholders. The language comes from the saved choice,
else the browser's language.

### Format modules

A module turns a binary save into a tree (objects, lists, numbers, texts): every path-based tool (differences,
⚡ Generate, rules) then works unchanged. Each module has its C++ twin in the app (`source/save_formats.cpp`), which
must produce **exactly** the same tree (checked by `test_formats.js` with `CPP_DUMP`).
To add one: a `parseXxx(buf)` function in `formats.js` that returns `null` when the data isn't its format,
an entry in `TREE_FORMATS` (`engine.js`), the matching C++ reader and a line in `evaluateSave`.
