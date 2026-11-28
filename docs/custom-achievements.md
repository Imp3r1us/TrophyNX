# Making your own achievement list

This guide shows how to add achievements for a game TrophyNX doesn't support yet, or change the ones it has.
You don't need to compile anything: an achievement list is a JSON text file on the SD card.

- [1. How it works in 30 seconds](#1-how-it-works-in-30-seconds)
- [2. Use a ready-made draft from GitHub](#2-use-a-ready-made-draft-from-github)
- [3. Find your game's title ID and save files](#3-find-your-games-title-id-and-save-files)
- [4. Easiest list: play time and launches (works for every game)](#4-easiest-list-play-time-and-launches-works-for-every-game)
- [5. Save-based achievements](#5-save-based-achievements)
- [6. Test it](#6-test-it)
- [7. Troubleshooting](#7-troubleshooting)
- [8. Share it](#8-share-it)

## 1. How it works in 30 seconds

- TrophyNX loads its built-in lists, then every `.json` file in `sdmc:/switch/TrophyNX/games/`.
- One file = one game. The game is chosen by the `"title_id"` **inside** the file (naming the file
  `<TITLEID>.json` just keeps things tidy).
- If your file has the same title ID as a built-in list, **your file replaces it completely**
  (it doesn't merge). Delete your file to get the built-in list back.
- Each achievement has a **rule**: either play statistics (hours, launches; work for any game) or a value read
  from the game's save (needs a save *reader* that understands that game's format).
- TrophyNX only ever **reads** saves. Your progress is never at risk.

## 2. Use a ready-made draft from GitHub

Two places to look:

- [`drafts/community/`](../drafts/community): lists shared by players (see section 8), with the game version they were
  made on and whether they were tested on a console.
- [`drafts/`](../drafts): lists generated automatically from Steam achievements (see
  [`drafts/README.md`](../drafts/README.md) for the full table of games and verdicts).
- [`drafts/memory/`](../drafts/memory/README.md): live money / level achievements made from cheat codes for about
  300 games (section 5b). **Not checked on a console**: test before you trust them.

1. Open the game's `drafts/<TITLEID>.json` on GitHub, press **Raw**, save the file.
2. Copy it to `sdmc:/switch/TrophyNX/games/` (create the `games` folder if it isn't there).
3. Close the game completely (HOME → X → Close), open TrophyNX, press **Y**.

The matching `drafts/<TITLEID>.md` says how sure each rule is: **sure** rules were checked on a real save,
**maybe** rules point at the right place but nobody has unlocked them yet to confirm. Fields starting with `_`
(`_draft`, `_api`, `_generated`…) are notes for humans and are ignored by the app.

## 3. Find your game's title ID and save files

Press **ZR** on the Overview screen (compatibility scan). It writes `sdmc:/switch/TrophyNX/scan_report.txt`, one
block per game:

```
010026300BA4A000  Slay the Spire  ->  draftable (json)
    preferences/STSAchievements  1532 bytes  json
    preferences/STSPlayer  2210 bytes  json
```

- The 16-character code is the **title ID**.
- The indented lines are the **save files**, with their path inside the save. That path is exactly what goes in
  `"save_files"`.
- The verdict tells you what is possible today:

| Verdict | Meaning | What you can do |
|---|---|---|
| `supported` | a list with a save reader already exists | tweak it (section 5) |
| `draftable` | the save is JSON, BYML, Unreal (GVAS) or .NET (NRBF): TrophyNX can read it | save-based achievements (section 5) |
| `text` | XML, INI or another text format with no reader yet | play time only (section 4) for now |
| `binary` | a custom binary format | possible with fixed offsets (`binary_le`), but it takes detective work in the Save Lab |
| `encrypted` / `compressed` | not readable as-is | play time only (section 4) for now |
| `no-save` | no save data for this user | — |

You can also look at the files yourself: back up the save with [JKSV](https://github.com/J-D-K/JKSV)
(it lands in `sdmc:/JKSV/<game>/<backup name>/`) and copy that folder to your PC or phone (SD reader, ftpd, or
JKSV's WebDAV/Drive upload). The inside of a JKSV backup folder is the save root, so
`sdmc:/JKSV/Slay the Spire/backup1/preferences/STSAchievements` is `preferences/STSAchievements`.

## 4. Easiest list: play time and launches (works for every game)

No save reading needed, so this works even for games with encrypted or unknown saves. Save as
`sdmc:/switch/TrophyNX/games/<TITLEID>.json`:

```json
{
  "title_id": "0100535012974000",
  "name": "Hades",
  "achievements": [
    { "id": "first_escape_try", "name": "Down Below", "desc": "Launch Hades 10 times", "points": 10,
      "rule": { "type": "launches", "target": 10 } },
    { "id": "h10", "name": "Regular of the House", "desc": "Play for 10 hours", "points": 10,
      "rule": { "type": "playtime_hours", "target": 10 } },
    { "id": "h50", "name": "Many Escapes", "desc": "Play for 50 hours", "points": 25,
      "rule": { "type": "playtime_hours", "target": 50 } },
    { "id": "h100", "name": "Prince of the Underworld", "desc": "Play for 100 hours", "points": 50,
      "rule": { "type": "playtime_hours", "target": 100 } }
  ]
}
```

Replace the title ID with the one from your `scan_report.txt` (section 3), and the names with your own.

Fields of an achievement:

| Field | Required | Notes |
|---|---|---|
| `id` | yes | unique within the game, letters/digits/underscores. Unlocks are stored under it: **don't rename it later** or the achievement re-locks |
| `name` | yes | shown in the list and the popup |
| `desc` | no | description |
| `points` | no | ≤10 bronze, ≤25 silver, ≤50 gold, above that platinum (or force it with `"tier": "gold"`) |
| `hidden` | no | `true` hides the description until unlocked (endings, spoilers) |
| `rule` | yes | see below |

You can mix play-time rules and save rules in the same file.

## 5. Save-based achievements

Add a `reader` and `save_files` to the file, then rules that point at values in the save.

```json
{
  "title_id": "0100633007D48000",
  "name": "Hollow Knight",
  "reader": "json",
  "save_files": ["user1.dat", "user2.dat", "user3.dat", "user4.dat"],
  "achievements": [
    { "id": "hornet", "name": "Protector", "desc": "Defeat Hornet in Greenpath", "points": 10,
      "rule": { "type": "flag", "path": "playerData/hornet1Defeated" } },
    { "id": "grubs_23", "name": "Grubfriend", "desc": "Rescue 23 grubs", "points": 25,
      "rule": { "type": "flag", "path": "playerData/grubsCollected", "target": 23 } }
  ]
}
```

When several save files are listed (slots), **the best one counts**.

### Pick the reader

| Save looks like | `reader` |
|---|---|
| readable text starting with `{` or `[` | `json` |
| starts with the bytes `GVAS` (Unreal Engine 4 / 5 up to 5.3) | `gvas` |
| .NET BinaryFormatter (many Unity games; the Save Lab detects it) | `nrbf` |
| starts with `SGB1` (Supergiant Games: Hades) | `sgb` |
| DELTARUNE `deltarune.sav` (JSON of the game's text files) | `deltarune` |
| RPG Maker MV / MZ `fileN.rpgsave` / `fileN.rmmzsave` (LZString base64 or zlib) | `rpgmaker` |
| GameMaker text save: INI (`[Section]` / `key="value"`, also base64), or one value per line | `gamemaker` |
| THEATRHYTHM FINAL BAR LINE `backup.inf` / `musicBackup.inf` (encrypted MessagePack) | `theatrhythm` + `"key_file": "theatrhythm.txt"` |
| Nintendo BYML (`YB` / `BY` at the start or at 0x10) | `byml` |
| anything else with values at fixed positions | `binary_le` (see the main README) |

**RPG Maker MV / MZ paths**: `switches/_data[12]` (switch 12 on), `variables/_data[5]` (variable 5),
`selfSwitches/_data/1,4,A` (map 1, event 4, self switch A), `party/_gold`, `party/_items/7` (how many of item 7),
`party/_weapons/2`, `actors/_data[1]/_level`, `system/_battleCount`, `system/_winCount`, `system/_saveCount`.
The engine's class markers are removed, so the paths follow the game's own field names.

**GameMaker paths**: an INI file reads as `Section/key` (`Progress/Level`); quoted numbers such as `"12.000000"` are
numbers. Other text files read line by line: `lines[0]`, `lines[1]`... Values saved with `ds_list_write` /
`ds_map_write` (long hex strings starting with `2F010000` / `91010000`) are decoded: `Progress/Flags/boss_dead`,
`lines[2][4]`.

If the save is JSON scrambled with a repeating key (SIGNALIS does this), the Save Lab recovers the key: add it as
`"xor_key": "..."`.

Encrypted saves whose password is a text of the game itself (THEATRHYTHM FINAL BAR LINE) never ship it: the
definition names a file with `"key_file"`, and the app reads the password from `sdmc:/switch/TrophyNX/keys/<that file>`
(one line). Without it the game shows "password file ... is missing" in its log (X) and only the play-time
achievements count. In the Save Lab, paste the password in *Captures → Encrypted saves*; it stays in your browser.
Step by step for beginners: [Getting game files from your Switch](dumping-guide.md). In short, without a PC: dump the game's `global-metadata.dat` (romfs `Data/Managed/Metadata/`) with nxdumptool and
leave it where nxdumptool saves it (`sdmc:/nxdumptool/...`), or copy it to `sdmc:/switch/TrophyNX/keys/`: the app finds the password in it and
writes the key file itself. The Save Lab can do the same (*Find it in global-metadata.dat*). Details:
[tools/theatrhythm/README.md](../tools/theatrhythm/README.md).

### Point at a value (paths)

The save is read as a tree; a path walks it with `/`:

| Path | Selects |
|---|---|
| `playerData/geo` | key `geo` inside `playerData` |
| `WorldInfo[3]/ScenarioNo` | 4th element of a list (counting from 0) |
| `ShineData[*]/IsGet` | `IsGet` of every element of the list |
| `GameState/QuestStatus[*]` | every value of an object (dictionaries, Lua tables) |
| `ShineData[UniqueId=205]/IsGet` | the element whose `UniqueId` is 205 |
| `Unlocks[Key=Ending_A]/Value` | a dictionary entry (Unity `Dictionary` / Unreal `Map` become `{Key, Value}` lists) |
| `HackDictionary[=Wanwan]` | 1 if the list contains `Wanwan` |

### Rule types

| `type` | Unlocks when | Example |
|---|---|---|
| `flag` | the value is ≥ `target` (default 1). `true` counts as 1 | `{ "type": "flag", "path": "playerData/killedMantisLord" }` |
| `flag` + `equals` | the value equals a text, number or boolean | `{ "type": "flag", "path": "state", "equals": "Clear" }` |
| `count` | at least `target` of the matched values are true / non-zero / non-empty | `{ "type": "count", "path": "ShineData[*]/IsGet", "target": 100 }` |
| `sum` | the matched values add up to `target` | `{ "type": "sum", "paths": ["coins/a", "coins/b"], "target": 500 }` |
| `playtime_hours` / `launches` | play statistics | section 4 |
| `days_played` / `streak_days` / `comeback_days` | days played, most days in a row, longest break before coming back (system play log) | `{ "type": "streak_days", "target": 7 }` |
| `screenshots` / `videos` | captures of the game in the album | `{ "type": "screenshots", "target": 10 }` |
| `memory` | a value in the game's memory, while you play | section 5b |

Inside a rule, `"show_progress": false` hides the "37 / 100" counter (shown by default when `target` is above 1),
and `"divisor": 4` divides the shown value (e.g. quarter hearts shown as hearts).

**Extra conditions: `requires`** (path readers). The achievement only counts in a save file where every condition
also holds. Each condition reads the largest value at its `path` (a missing value counts as 0) and checks
`"min"` (value ≥ min) and/or `"below"` (value < below):

```json
{ "type": "flag", "path": "Checkpoint", "target": 36,
  "requires": [ { "path": "HasDied", "below": 1 }, { "path": "SpeedrunTime", "below": 2700 } ] }
```

This reads as "reached the last checkpoint, never died, in under 45 minutes". The conditions are checked file by file,
so one slot that finished and another that never died don't add up. Things the save doesn't remember (a no-damage
boss fight the game never writes down) still can't be checked.

### Getting saves without JKSV: save captures

TrophyNX can record the saves for you: **ZR** on the game in the app, **A** to turn recording on, then play and close
the game (HOME, X) after each step worth comparing. Each close adds a capture. **ZR → Y** shares them over Wi-Fi:
open the address shown on the screen on your phone, the Save Lab opens with a **From your Switch** panel. See
[Save captures](../README.md#save-captures-record-a-game-share-over-wi-fi).

### The quick way: ⚡ Auto in the Save Lab

Open the [Save Lab](https://imp3r1us.github.io/TrophyNX/), tab **⚡ Auto**: pick the game, give an **early save**
(right after the intro) and your **most advanced save** (JKSV backup files or .zip), press **⚡ Make my achievement list**.
It detects the save format, finds what changed, keeps the changes that look like progress (bosses, items, collections,
counters), takes the game's official Steam achievements when the save matches them, adds play-time achievements and
checks everything like the app does. No AI and nothing uploaded: it all runs in your browser.
Then rename what needs it in tab 5, and **Download** or **🌐 Share**.

If the save is encrypted or in a format TrophyNX can't read yet, it says so and you get a play-time list.

### Find the right field by hand: the Save Lab

The **Save Lab** is a web page that runs in any browser, phones included: open
**https://imp3r1us.github.io/TrophyNX/** (English / French). Your saves stay on your device: nothing is uploaded.
It also works offline: download [`tools/savelab/index.html`](../tools/savelab/index.html) and open it.

1. Back up your save with JKSV at a few moments, naming each backup after what you just did:
   `fresh`, `beat first boss`, `got double jump`… Close the game between captures.
2. Load them in order in the Save Lab (the JKSV `.zip` or the files).
3. **Differences** shows what changed between two captures: the field that flipped right after you beat the boss
   is the one you want. Tick **control** on a capture where you did nothing: whatever still changes is noise
   (timers, RNG).
4. **⚡ Generate achievements** turns the changes into rules, and the **Achievements** tab tests them against your
   captures and exports the JSON.
5. **Known value** finds a number you can see on screen (coins, deaths, percentage) in the save.

For JSON saves, a text editor works too: search for a value you know (e.g. your gold) and copy the path.

## 5b. Live achievements from memory (any game)

Some games can't be read from their save (encrypted, custom binary) but cheat codes already know where their
values live **in memory**. A `memory` rule watches such a value while you play: the background module
(`sys-trophynx`) reads it once a second through Atmosphère's cheat service (read-only, like EdiZon) and unlocks
the achievement with a popup the moment it holds. No save reader is needed.

```json
{ "id": "rich", "name": "Rolling In It", "desc": "Have 100,000 coins", "points": 25,
  "rule": { "type": "memory", "value_type": "u32", "cmp": ">=", "target": 100000, "max": 99999999,
            "builds": { "3CA12DFAAF9C82DA": "[[main+2D21A90]+20]+1C0",
                        "0123456789ABCDEF": ["heap+61C77360", "heap+61C77400"] } } }
```

| Field | Notes |
|---|---|
| `builds` | one entry per **game version**: the start of its build ID (the name of its cheat file, `atmosphere/contents/<title id>/cheats/<build id>.txt`) → an address, or a list of addresses (save slots, characters: **any** of them unlocks it). A version that isn't listed stays locked |
| address | `main`, `heap` or `alias` + a hex offset. `[...]` reads the 64-bit pointer stored there: `[[main+2D21A90]+20]+1C0` follows two pointers, then adds 0x1C0. Same syntax as HomeNX packs |
| `value_type` | `u8` `u16` `u32` `u64` (default `u32`), `s8`…`s64` (signed), `f32` `f64` (decimals). Use the width the cheat code writes, and `f32` when its value is a float (`47C34FF0` = 99999.875): an integer read of a float is huge and would unlock at once |
| `cmp` | `>=` (default), `>`, `<=`, `<`, `==`, `!=`, or `&` (any of the bits of `target`, e.g. `"target": "0x4"`) |
| `target` | the number to compare with |
| `max` | optional: values above it are ignored. A pointer read during a loading screen can give nonsense |

The condition has to hold on **3 checks in a row** before it unlocks, for the same reason.

**Several conditions at once.** Put them in an `all` list: each one has its own `value_type`, `cmp`, `target`,
`max` and `builds`, and **all** of them must hold on the same check (still 3 checks in a row). A version unlocks
only when every condition has an address for it.

**Events, not states.** Add `"change"` to a condition:

| `change` | The condition holds when… | Use it for |
|---|---|---|
| (none) | the value compares as it is now | "have 100,000 coins", "reach level 50" |
| `"becomes"` | the comparison is true **and** it was seen false (3 checks in a row) since the game started | "finish a race in 1st", "beat a boss": a flag or counter that turns on during play. Already true when the game opens (a results screen, a finished save) → nothing |
| `"increase"` | `cmp` / `target` apply to how much the value **rose** since the lowest value seen this session | "earn 500 coins in one sitting". Loading a save with less restarts the count from there |

```json
{ "id": "flawless", "name": "Untouchable", "desc": "Win a race without being hit", "points": 25,
  "rule": { "type": "memory", "all": [
    { "value_type": "u8",  "cmp": "==", "target": 1, "change": "becomes", "builds": { "3CA12DFAAF9C82DA": "[main+3A0B10]+58" } },
    { "value_type": "u8",  "cmp": "==", "target": 1,                      "builds": { "3CA12DFAAF9C82DA": "[main+3A0B10]+5C" } },
    { "value_type": "u32", "cmp": "==", "target": 0, "max": 999,          "builds": { "3CA12DFAAF9C82DA": "[main+3A0B10]+60" } } ] } }
```

(race finished, place, hits taken). Put the `becomes` condition on what marks the moment (the race ends) and plain
conditions on what must be true at that moment. Addresses above are made up: find real ones with a memory search
tool (EdiZon, Breeze) by watching the value change while you play.

**From a cheat code.** `580F0000 XXXXXXXX` loads the pointer at `main+XXXXXXXX`, each `580F1000 YYYYYYYY` reads the
next pointer at `+YYYYYYYY`, `780F0000 ZZZZZZZZ` adds `ZZZZZZZZ`, and the final `6T0F0000 …` writes `T` bytes there:
that's `[[main+XXXXXXXX]+YYYYYYYY]+ZZZZZZZZ` with `value_type` `uT`. A single `04100000 AAAAAAAA …` line writes to
`heap+AAAAAAAA`. [`tools/cheats2memory`](../tools/cheats2memory/README.md) does this for every money / level code
of switch-cheats-db: its drafts are in [`drafts/memory/`](../drafts/memory/README.md).

**Testing one.** Copy the file to `sdmc:/switch/TrophyNX/games/`, start the game and play for a minute.
`sdmc:/switch/TrophyNX/live_log.txt` then shows the game's **build ID** (`memory: <game> build 3CA12D…`), how many
achievements have addresses for that version, and the first value read for each (`  rich = 2500`). Change the value
in game and check that the achievement unlocks at the right moment. Nothing happens for a version that isn't in
`builds`: the log says which build ID to add.

Limits: Atmosphère's cheat service (`dmnt:cht`) must be on, the game is watched only while it runs (progress made
earlier counts as soon as the value is read, except for `becomes` / `increase` conditions), and if you use cheats in the same game the module shares the cheat
service with them (it never turns them off). Addresses can change with a game update: that's why they're per build.

## 6. Test it

1. Copy the file to `sdmc:/switch/TrophyNX/games/`.
2. **Close the game** (a suspended game keeps its save locked).
3. Open TrophyNX and press **Y** (rescan).
4. Select the game and press **X**: the debug view shows, for each achievement, the value read from the save
   (`grubs_23: 12 / 23 [not yet]`), and any file that couldn't be opened or read.

The background module picks up your file too: from then on, achievements also unlock with a popup a few seconds
after you close the game.

To test an achievement again, select it, press **−** to re-lock it, then **Y**.

## 7. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `definition_errors.txt` appears in `sdmc:/switch/TrophyNX/` | your JSON has a mistake (missing comma or quote, wrong field). The file says which one. A JSON validator such as jsonlint.com helps |
| the game still shows the old list | the `"title_id"` inside the file doesn't match (16 hex characters, check `scan_report.txt`) |
| X shows `not present` for the save files | the path in `save_files` is wrong: copy it from `scan_report.txt`, case matters |
| X shows `NOT RECOGNISED` or a parse error | wrong `reader` for this format, or the save is compressed / encrypted |
| a value is always 0 | wrong path: check spelling and case, or the field only appears later in the game |
| everything unlocked at once | the rule is too loose (e.g. `count` on a list that is always filled): use `equals`, or a higher `target` |

## 8. Share it

Got a list working? Share it so everyone gets it:

- **From the Save Lab:** tab 5 → **🌐 Share with everyone**. It opens a pre-filled page on GitHub (free account needed).
- **By hand:** open a [Share an achievement list](https://github.com/Imp3r1us/TrophyNX/issues/new?template=achievement-list.yml)
  issue and paste your JSON.

A bot checks the list within a minute (same checks as the app) and replies on the issue: what to fix, or a link to the
pull request that adds it to [`drafts/community/`](../drafts/community). Edit the issue to fix it, the bot checks again.
Once the maintainer merges it, anyone can install it. Only the list is public: **your save files are never sent**.

Stuck on a game? Open a
[Save batch issue](https://github.com/Imp3r1us/TrophyNX/issues/new?template=save-batch.yml) with a few labelled
JKSV backups from **the same game version**, and say what you did between them.
