# Save Lab

**Open it online: https://imp3r1us.github.io/TrophyNX/** (English / French, switch with the button at the top right).

A single web page (works on a phone, no PC needed) to **understand a Switch save and turn it into achievements**
in the app's format (`games/<TITLEID>.json`). Everything runs in your browser: your save files are never uploaded.
It also works offline: download `index.html` from this folder and open it.

Step-by-step guide for players: [Making your own achievement list](../../docs/custom-achievements.md).

0. **⚡ Auto**: the whole chain in one button (no AI): game + an early and an advanced save → format, differences,
   collections/counters and milestone flags that look like progress (name heuristics), Steam list match (built-in or
   `steam/<appid>.json` from the website), readable names, play-time set, validation. Then Review / Download / Share.
1. **Captures**: import several saves of the same game (files or JKSV .zip), in order, named after what you did
   in the game. A “control” capture (nothing done since the previous one) removes noise.
2. **Structure**: recognised format (BotW/TotK/MK8D/Smash, BYML trees, JSON text saves of Unity games such as
   Hollow Knight, Unreal Engine “GVAS” saves, .NET BinaryFormatter “NRBF” data of Unity games, repeating-key XOR,
   THEATRHYTHM FINAL BAR LINE encrypted MessagePack saves once the password is entered),
   header, entropy (encrypted/compressed?), hash→value table, name dictionary (CRC32/Murmur3/FNV-1a), texts.
3. **Differences**: every value that changes is classified (0→1 flag, counter, bit field, float, noise).
   Multiple selection → “count” / “sum” achievements; “Extend the series” finds the neighbouring flags of a collection.
   **⚡ Generate achievements**: from a new game + an advanced/100% save (e.g. downloaded from GBAtemp), groups the
   flags into collections (first / half / complete tiers) and the counters into tiers.
4. **Known value**: Cheat Engine-style search for a number seen in the game.
5. **Achievements**: editing, test on every capture with the same logic as the console, validation, JSON export
   (copy or download), and **Share with everyone**: a pre-filled GitHub issue; `.github/workflows/share-list.yml`
   checks it with `tools/community/import_issue.js` and opens a pull request adding `drafts/community/<TITLEID>.json`.
6. **PC → Switch**: import the app's `scan_bundle.json` (ZR) and draft rules from a game's Steam achievement list.
7. **🔑 Save password** (`#key` opens it directly): for every built-in game with a `key_file`, finds the save password
   in the game file (`resources.assets` / `global-metadata.dat`) checked against one save, and downloads the key file
   for `sdmc:/switch/TrophyNX/keys/`. Players' page; the app's log links to it when a key file is missing.

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

`parseTree` is synchronous everywhere (differences, rules, ⚡ Auto), so a module cannot await WebCrypto or
`DecompressionStream`: `formats.js` has small synchronous SHA-1 / PBKDF2 / AES-128-CBC / raw-inflate helpers
(mirrored in `source/save_formats.cpp`) and a generic **MessagePack** decoder, `parseMsgpack(buf)` (C++:
`msgpackToJson`), reusable by any game. Its tree: maps → objects (integer keys in decimal, listed first in
ascending order exactly like a JavaScript object — the C++ side reorders the same way), arrays → lists,
bin → hex, float32 → 6 decimals, ext −1 (timestamp) → seconds since 1970, other ext → `ext:<type>:<HEX>`.

**theatrhythm** (THEATRHYTHM FINAL BAR LINE `backup.inf`, `musicBackup.inf`, `proficaBackup.inf`): `[u32 length, PS4]` +
32-byte header q0‖q1‖q2‖q3 + AES-128-CBC (salt q0‖q3, IV q2‖q1, key PBKDF2-HMAC-SHA1(password, salt, 1000, 16)),
plaintext = raw DEFLATE of a MessagePack map. The password is typed in the page (*Captures → Encrypted saves*,
localStorage `savelab-key-theatrhythm`), never bundled; without it the files show as “encrypted”. *Find it in
global-metadata.dat* recovers it from the game's own file (`theatrhythmFindPassword`, same search as the app) and
offers `theatrhythm.txt` for the SD card. The game's
`[Key(n)]` records are labelled (`…/PlayStyleRecordDictionary/<style>/TotalPlayCount`, `ProficaInfo/MyProfica/…`).
Tests: synthetic saves with a dummy password (`test/make_theatrhythm_fixtures.js` → `test/fixtures/theatrhythm/`).
Same tree as the app on real saves: `test_formats --dump OUT --key local/theatrhythm.key <files>` then
`CPP_DUMP=OUT THEATRHYTHM_KEY=local/theatrhythm.key node test/test_formats.js <files>`.
