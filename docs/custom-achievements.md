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

The [`drafts/`](../drafts) folder has lists generated automatically from Steam achievements (see
[`drafts/README.md`](../drafts/README.md) for the full table of games and verdicts).

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
| Nintendo BYML (`YB` / `BY` at the start or at 0x10) | `byml` |
| anything else with values at fixed positions | `binary_le` (see the main README) |

If the save is JSON scrambled with a repeating key (SIGNALIS does this), the Save Lab recovers the key: add it as
`"xor_key": "..."`.

### Point at a value (paths)

The save is read as a tree; a path walks it with `/`:

| Path | Selects |
|---|---|
| `playerData/geo` | key `geo` inside `playerData` |
| `WorldInfo[3]/ScenarioNo` | 4th element of a list (counting from 0) |
| `ShineData[*]/IsGet` | `IsGet` of every element of the list |
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

Inside a rule, `"show_progress": false` hides the "37 / 100" counter (shown by default when `target` is above 1),
and `"divisor": 4` divides the shown value (e.g. quarter hearts shown as hearts).

**One rule = one condition.** There is no AND yet ("beat the game AND all charms"), and nothing that the save
doesn't remember (no-damage runs, speedruns) can be checked.

### Find the right field: the Save Lab

[`tools/savelab/index.html`](../tools/savelab) is a single web page that runs offline in any browser, phones
included. Download the file and open it.

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

Got a list working? Open a pull request adding it to `romfs/games/` (or attach it to an
[Achievement idea issue](https://github.com/Imp3r1us/TrophyNX/issues/new?template=achievement-idea.yml)) so
everyone gets it. Stuck on a game? Open a
[Save batch issue](https://github.com/Imp3r1us/TrophyNX/issues/new?template=save-batch.yml) with a few labelled
JKSV backups from **the same game version**, and say what you did between them.
