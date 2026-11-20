# PC → Switch

Reuses PC (Steam) achievement lists for Switch games whose saves can be read. Everything is deterministic:
name matching and save comparisons, no AI and no paid service.

```
Switch app (ZR on Overview) ──> scan_bundle.json ──> samples/ in the repo ──┐
titledb (all Switch games) ──┐                                              ├─> draft.js ──> drafts/<TITLEID>.json + .md
Steam (app list, achievements) ┴─> catalog.js ──> data/pc2switch/ ─────────┘        (pull request, you review)
                                                     └──> Save Lab, tab 6 (same matcher, on the phone)
```

## One-time setup

1. *(Optional)* A Steam Web API key (<https://steamcommunity.com/dev/apikey>) as the repository secret `STEAM_API_KEY`.
   **Not needed**: without it, achievement lists come from Steam's public `IPlayerService/GetGameAchievements`
   endpoint (same idea as the Achievements app / Achievement Watcher) and the Steam app list from the public
   `ISteamApps/GetAppList`, or the matches already in `catalog.json`. The key only speeds up the popularity step of `harvest.js`.
2. *(skip)*
3. Settings → Actions → General → tick **Allow GitHub Actions to create and approve pull requests**.
4. Actions → **PC to Switch** → Run workflow. The first run fetches the Steam app list and up to 3,000
   achievement lists (raise `max_schemas` or let the weekly runs fill the rest; everything is cached).

## Using it

1. On the Switch, open the app, stay on **Overview**, press **ZR**. It mounts every save read-only and writes
   `switch/TrophyNX/scan_bundle.json` (verdicts + copies of readable saves, up to 4 MB each and 17 MB in all) and
   `scan_report.txt`.
2. Either
   - upload `scan_bundle.json` to the repo's `samples/` folder (GitHub → Add file → Upload). The workflow runs
     and opens a pull request with `drafts/README.md` (compatibility of every game) and a draft per readable
     game; or
   - open Save Lab, tab **6 PC → Switch**, import the bundle, *Charger comme capture*, pick the Steam list,
     *Générer les brouillons*, then *Ajouter les sûrs* and export from tab 5.
3. Review the draft: keep `sure` rules, check `maybe` ones against their candidates, drop the rest.
   Remove the `_draft`/`_api` keys if you like (the app ignores them) and move the file to `romfs/games/`
   or the SD card's `games/` folder.

Raw saves work too: put a game's files in `samples/<TITLEID>/` (e.g. from a JKSV backup). With two saves of
different progress, the less advanced one is used as a "new game" hint.

## Every game at once (`bulk.js`)

`node tools/pc2switch/bulk.js [--engines] [--install] [--min-sure 1] [--with-maybe] [--allow-pc-only]` goes through the whole
catalogue (every Switch game also on Steam with achievements) and drafts a definition for each game whose save is known:

- **Switch saves**: same inputs as `draft.js` (`samples/scan_bundle*.json`, `samples/<TITLEID>/`).
- **PC saves**: copy the game's save folder from a PC into `samples/pc/<Steam app id>/` (or `samples/pc/<TITLEID>/`).
  When a game has both, their structures are compared: `compatible` (≥ 60 % of the fields in common), `partial`, `different`.
  With a PC save only, the rules are written on the PC layout (`pc-only`); on the console they simply stay locked if the layout
  differs, and a ZR scan confirms it.
- `--engines` asks PCGamingWiki for each game's engine (cached in `.pc2switch-cache/pcgw_engines.json`) and lists the games
  most worth a save: engines that usually keep the same save files on PC and Switch (Unity, GameMaker, MonoGame/FNA, Godot…).
- `--install` copies into `romfs/games/` the drafts confirmed on a Switch save with at least `--min-sure` sure rules
  (sure rules only unless `--with-maybe`; PC-only drafts only with `--allow-pc-only`). Hand-made definitions are never replaced.

Outputs: `drafts/all/<TITLEID>.json` + `.md`, `drafts/all/coverage.md` (summary) and `drafts/all/coverage.csv` (one line per game).
The workflow runs it on every pass; tick **install** when starting it by hand to put the confirmed sets in the pull request.

## PC saves of the most played games (`harvest.js`)

`node tools/pc2switch/harvest.js [--top 100] [--per-game 4] [--refresh] [--dry-run]` (needs `GITHUB_TOKEN`):

1. ranks the catalogue by number of Steam reviews (cached 60 days) and takes the `--top` most played games not tried in the
   last 30 days, so each weekly run goes further down the list;
2. reads where each one keeps its saves in the Ludusavi manifest (save locations of ~10,000 Steam games, built from PCGamingWiki,
   which itself blocks GitHub's servers);
3. searches public GitHub repositories for those folders (`path:"StardewValley/Saves"`) with the official code search API
   (10 searches a minute);
4. downloads the data files of the best folders (never executables, archives or files over 2 MB), parses them like the
   console and scores them against the Steam achievements;
5. writes the winner to `samples/pc/<Steam app id>/`, with its origin in `samples/pc/SOURCES.md` and `sources.json`.

`bulk.js` then drafts those games. Games with a Switch save or a hand-made set are skipped; games with nothing usable are
retried after 30 days. The workflow runs it every Monday and on manual runs (**harvest** box). If GitHub refuses the code
search with the workflow token, add a `SEARCH_TOKEN` secret (fine-grained token, public repositories, read-only).

Expect saves for part of the list only: XML (Stardew Valley, Celeste), encrypted or binary saves are not readable by the
app yet, and many games have no save on GitHub at all.

## Verdicts

| verdict | meaning |
|---|---|
| `supported` | a save-based set already exists |
| `draftable` | JSON or BYML save: drafts are generated |
| `text` | XML/INI/other text: a reader is needed before drafting |
| `binary` | fixed-layout binary: use Save Lab captures and diffs |
| `encrypted` | compressed or encrypted: not readable as is |
| `no-save` | nothing to read for this user |

## How matching works (`match.js`)

- **Titles**: normalised names (case, accents, ™, edition words such as *Deluxe* / *Definitive Edition*).
  `how` = `exact`, `normalized`, `main-title` or `exact-short` (one short word: check the publisher).
  Different subtitles never match (*Vice City* ≠ *San Andreas*).
- **Achievements → save fields**: words from the Steam API name, description and title are compared with the
  field names (`killedDungDefender` ↔ *Defeat the Dung Defender*), with synonyms (defeat/kill/slay…),
  rarer words weighing more, dialogue/UI flags weighing less. Numbers in descriptions become targets;
  "half"/"all" use the value in the reference save; a family of flags (`mapCrossroads`, `mapGreenpath`…)
  becomes a `count` rule. Time limits, "without…", difficulty modes and online play are skipped.
- **Confidence**: `sure` needs a strong, unambiguous match that the reference save actually meets.
  Measured on Hollow Knight (63 achievements, a hard case: its saves use internal code names):
  5 sure, all correct; the right field is among the 3 listed candidates for 25 more.

## Tests

`node tools/pc2switch/test/test_pc2switch.js` (matcher quality, title matching, catalogue and drafts end to end,
offline). The console side is covered by `host/test/test_json.cpp` and the PC build:
`ACH_HOST_ROOT=<dir with saves/<TITLEID>/…> ACH_SCRIPT="B,ZR,PLUS" ./host/achievements_host`.
