# TrophyNX

> Formerly "Switch Achievements". Since 0.9.0 the app is `sdmc:/switch/TrophyNX.nro` and its data folder is
> `sdmc:/switch/TrophyNX/`. On first launch the app moves the old `sdmc:/switch/SwitchAchievements/` folder there
> (unlocks, settings, definitions) and deletes the old `SwitchAchievements.nro`; the live module (`sys-trophynx`,
> same program id 420000000000AC01) keeps using the old folder until then.

A homebrew app for a modded Nintendo Switch that adds achievements to your games.
It reads your play statistics and save files **read-only**, unlocks achievements,
and remembers them on the SD card. A background module shows a popup when you unlock one.

![Achievement popup in Breath of the Wild](docs/popup-botw.jpg)

### The app

| Library | Game page |
|---|---|
| ![Library: profile card, medals by tier, latest unlocks and the game grid](docs/ui-library.png) | ![Game page: completion ring, medals by tier, filters and the achievement list](docs/ui-game.png) |
| Your points, medals by tier and latest unlocks, then every game as a card. **L / R** sorts the grid. | **A** on a game: play stats, save status, completion ring, and its achievements filtered with **L / R** (All, Unlocked, Locked). |

Prefer the original layout? **Press the left stick** (the *LS* chip in the header) to switch to the classic look
and back. The app remembers your choice.

![Classic look: game list on the left, achievements on the right](docs/ui-classic.png)

*Screenshots from the PC preview build (placeholder icons and play times).*

**Want achievements for a game that isn't supported?** See [Making your own achievement list](docs/custom-achievements.md)
and the online **[Save Lab](https://imp3r1us.github.io/TrophyNX/)**.
**THEATRHYTHM FINAL BAR LINE or DELTARUNE?** They need a file from your own copy of the game: see the beginner guide
[Getting game files from your Switch](docs/dumping-guide.md).

**Download:** [latest release](https://github.com/Imp3r1us/TrophyNX/releases/latest) — unzip `TrophyNX-sd.zip`
at the root of the SD card. Then update from the app with ZL.

> **Help wanted!** TrophyNX is a one-person project and needs players to grow: save files, achievement
> ideas, and testing on more games. See [How you can help](#how-you-can-help).

## What you get

| Source | Works for | Examples |
|---|---|---|
| Play statistics (system play log) | **every installed game** | first launch, 10/50/100 hours, 25 launches |
| Global | your whole library | play 5/20/50 games, 100/500/1000 hours, unlock 25/100 achievements |
| Save file | **Zelda: Breath of the Wild** (33) | first minutes (Old Shirt, Sheikah Slate, leaving the cave), shrines, Divine Beasts, Master Sword, Ganon, Koroks, hearts, stamina, Lynels... |
| Play log, themed | **28 more games** (Persona 5 Royal, Metroid Dread...) | 1 launch goal + 3 play-time goals named after each game, scaled to its length |
| Save file | **Super Smash Bros. Ultimate** (23 + 4 play-time) | fighters unlocked, KOs, Classic Mode, World of Light, spirits, online, Elite Smash... |
| Save file | **Mario Kart 8 Deluxe** (17 + 4 play-time) | coins, drifts, mini-turbos, online races/wins, VR, battles (offsets not yet confirmed on a current save) |
| Save file | **Zelda: Tears of the Kingdom** (37) | shrines, Skyview Towers, Lightroots, Koroks, caves, wells, Bubbulfrogs, Gleeoks... |
| Save file | **Super Mario Odyssey** (59 + 4 play-time) | Power Moons, captures, each kingdom visited, kingdom stories cleared, regional coins, outfits/hats, souvenirs, checkpoints, music, jump/cap-throw counts, Jaxi, Toadette's achievements... |
| Save file | **Hollow Knight** (56 + 4 play-time) | charms, masks, soul vessels, every boss and dream boss, Dreamers, essence, grubs, stags, maps, Hunter's Journal, Colosseum, Grimm Troupe, pantheons, 112%... |
| Save file | **SIGNALIS** (13 + 4 play-time) | the game's own 13 achievements, with their original names (3 endings hidden) |
| Save file | **DELTARUNE** (30 + 4 play-time) | the 30 PS4/PS5 trophies: chapter ends without anything else; all of them with the [trophy patch](tools/deltarune/README.md) (UndertaleModTool script, LayeredFS) |
| Save file | **Hades** (41 + 4 play-time) | 38 of the 49 Steam achievements, from the game's own conditions: escapes, prophecies, keepsakes, companions, aspects, Mirror, Codex, Skelly, fishing... + escape-attempt milestones |
| Save file | **DAVE THE DIVER** (56 + 4 play-time) | the 56 Steam achievements, from the game's own record and counters |
| Save file | **Blasphemous** (45 + 4 play-time) | the 45 Steam achievements, from the game's own achievement tracker |
| Save file | **Slay the Spire** (46 + 4), **Vampire Survivors** (243 + 4), **Vampire Crawlers** (161 + 4) | every Steam achievement, from the game's own record |
| Save file | **OCTOPATH TRAVELER** (22) | travelers, levels, story chapters, chests, side stories, enemy weaknesses |
| Save file | **Paper Mario: The Origami King** (22 + 4 play-time) | coins, battles, enemies, Excellent hits, ring puzzles, museum... |

Unlocks are permanent: if you later delete or overwrite a save, what you unlocked stays unlocked.
When a game has several save slots, the best slot counts.

## Live achievements while playing (experimental)

Any game: `memory` rules watch a value at a known address (money, level…) for each game version, the way cheat
codes find it, and unlock the moment it's reached. See [docs/custom-achievements.md](docs/custom-achievements.md#5b-live-achievements-from-memory-any-game);
[`drafts/memory/`](drafts/memory/README.md) has unverified drafts for about 300 games made from switch-cheats-db.

### Breath of the Wild

`sys-trophynx` is a background module that does what RetroAchievements does on emulators:
it watches the game's memory while you play and unlocks Breath of the Wild achievements the moment
they happen, with an in-game popup: a card in the top-left corner with the achievement's name, its
description, the game icon and a bronze/silver/gold/platinum medal, sliding in for 4 seconds.

How it works:
- It detects that BotW is running, then attaches to it through Atmosphère's cheat service (`dmnt:cht`,
  the same thing EdiZon uses). It only **reads** memory.
- BotW stores every game flag as a small object on the heap: vtable, type, value, and the flag's CRC32
  hash (layout from the [zeldaret/botw](https://github.com/zeldaret/botw) decompilation, `gdtFlag.h`).
  The module scans the heap once (about 25 s after launch, takes a few seconds), keeps the objects
  whose layout checks out, then re-reads their values every 3 seconds.
- Because it looks for the flags' hashes instead of fixed addresses, it doesn't depend on the game
  version (cheat codes, by contrast, only exist for v1.6.0).
- Unlocks go into the same unlock file as the app, so they show up there too.

Safety (module 0.8.2+):
- Only real installed games are touched. Homebrew started like a game (hbmenu as an application,
  forwarders, launchers such as homeNX, title takeover with R) is logged as "ignored": no live mode,
  no capture, no scan on close.
- The raw NAND save probes (research for reading saves while a game runs) are off. Create an empty
  `sdmc:/switch/TrophyNX/probe.flag` to turn them on again.
- Health check: every minute the module reads how much of the shared "system" resources (sessions,
  events, threads, transfer memory) all background modules use, and logs it every 30 minutes and after
  each scan on close (`health:` lines in `live_log.txt`). Above 90 % of a limit it switches to **safe mode**:
  no display layer (popups become Ultrahand toasts), no save mounts, no probes, until usage drops again.

Install: copy `atmosphere/contents/420000000000AC01/` to the SD card. The `boot2.flag` starts it at boot;
opening the app also starts it if it isn't running (the Overview status line shows "Live module: running",
or the exact system error code if it couldn't start). It can also be started from ovl-sysmodules without rebooting.
It can be toggled with ovl-sysmodules (`toolbox.json` included).

### Updating from the app (ZL)

Press **ZL** on the main screen: the app asks GitHub for the **latest release** (one per version, `vX.Y.Z`,
created by CI when `include/version.h` changes; its notes come from `CHANGELOG.md`), compares every file with what is installed (SHA-256) and offers
to download the changed ones: the app itself and/or the live module (restarted right away). Downloads go to
`<file>.new` and are checked against the release's `version.json`; nothing is replaced unless every file arrived
intact. Then A restarts the app.
The update screen shows the new version's summary and its "What's new" list (the `CHANGELOG.md` section).

No setup is needed. Optional `sdmc:/switch/TrophyNX/update.json`:
`{ "channel": "builds" }` to get every build of `main` (test builds, from the `builds` branch) instead of
releases; `"repo"` for a fork; `"token"` (fine-grained, Contents: read-only) for a private copy. Without a token
GitHub allows 60 checks per hour.

Releases: https://github.com/Imp3r1us/TrophyNX/releases — `TrophyNX-sd.zip` (SD card layout), `TrophyNX.nro`,
and the files used by the updater (`sys-trophynx.nsp`, `version.json`...). To publish a version, bump
`APP_VERSION` in `include/version.h` and the `Makefile`, and add its section to `CHANGELOG.md`
(summary line + bullets for players; CI refuses to release a version without one).

Network calls use libcurl from devkitPro (`switch-curl`, TLS through the system). On PC (`platform_host.cpp`)
`ACH_UPDATE_DIR=<dir>` replaces GitHub (`<dir>/release/latest.json` + assets, or a copy of the builds branch), e.g. `ACH_SCRIPT="B,ZL,A"`.

### Scan on close (every game with a save reader)

When any game closes, its save is free again: about 3 seconds later the module reads it with the same rule
engine as the app (`source/rules.cpp`) and unlocks what is met right away, with the same popups (on the HOME
menu or over the next game). This covers every game whose definition has a `reader` (Smash, Odyssey, Hollow
Knight, SIGNALIS, TotK...), not only BotW. Play-time achievements still come from the app.

- Definitions: the ones built into the module (copied from `romfs/games` at build time), then
  `sdmc:/switch/TrophyNX/games/*.json`, like the app.
- The module has a 4 MB heap, so it reads saves in a low-memory mode: hash/value saves (BotW, TotK) are read in
  64 KB chunks keeping only the needed values, fixed-offset saves (Smash, MK8D) only the bytes the rules use.
  Tree saves (BYML, JSON, GVAS, NRBF) are read whole, one file at a time; a file too big for the heap is skipped
  and left to the app (`close_log.txt` says so). MessagePack saves (THEATRHYTHM FINAL BAR LINE) are built with
  only the values the rules use (~2.3 MB instead of ~11 MB for its song records), and the reader stops cleanly
  if even that would not fit. `host/test/test_stream.cpp` checks that the values are identical to the app's.
- Encrypted saves (THEATRHYTHM, Heavenly Bodies) need their password in `sdmc:/switch/TrophyNX/keys/` (never shipped):
  the app finds it by itself in the installed game or in the game file dumped with nxdumptool, or the Save Lab's
  **🔑 Save password** page (<https://imp3r1us.github.io/TrophyNX/#key>) makes the key file from that file and one save
  (see [docs/dumping-guide.md](docs/dumping-guide.md)).
- More than 4 new unlocks at once (first scan of an advanced save): 3 cards, then a "+N more achievements" card.
- `"close_scan": false` in a definition turns it off for that game (set for Mario Kart 8 Deluxe until its
  offsets are fixed).
- Report of the last scan on close: `sdmc:/switch/TrophyNX/close_log.txt`; one summary line per scan in
  `live_log.txt` (`close scan: <game> - save read in 120 ms, 12/37 met, 2 new`).

### Live save: popups while you play (every game with a save reader, experimental)

The same achievements also pop up **in the game**, a few seconds after it saves, without closing it.
A running game keeps its save mounted (locked), but each time it saves, the system writes the new state to the
save's image in NAND (`User:/save/<SaveDataId>`). The module reads that image from the partition's raw sectors
(read-only, it never opens the file, so it can't get in the game's way), parses the save container like
hactool does (`source/saveimage.cpp`, `source/rawfat.cpp`) and runs the same rules as the scan on close.

- About 20 s after the game starts, the module finds its save image (`live save: <game> - save image ...` in
  `live_log.txt`). Then it checks the image's header every 4 seconds (a 32 KB read); when the game has saved,
  it reads the save and unlocks what is met (`live save: <game> - save read in 80 ms, 12/37 met, 1 new`).
- Only values from one complete save are trusted: if the game saves again while the module reads, it reads again.
- Popups follow the game's own saves: autosave games are close to instant, games with manual saves show them
  when you save. The scan on close still runs, as before.
- `"live_save": false` in a definition turns it off for that game (it follows `close_scan` by default).
- Report of the last read: `sdmc:/switch/TrophyNX/live_save_log.txt`. If the image can't be found or read, the log
  says why and that game only gets popups on close.

### Save captures (record a game, share over Wi-Fi)

To make achievements for a game, the Save Lab compares saves taken at different moments. TrophyNX can take them for you:

1. In the app, select the game and press **ZR**, then **A**: recording is on for that game.
2. Play. Each time you **close** the game (HOME, then X), the live module copies its whole save to
   `sdmc:/switch/TrophyNX/captures/<TITLEID>/<date-time>/` (with the length of the session that just ended).
   A running game keeps its save locked, so closing it is the moment a copy can be taken: close it after each step
   worth comparing (a boss, a new item, a chapter). A save that didn't change since the last capture is not kept twice;
   the last 20 captures per game are kept (`"keep"` in `captures/settings.json`, up to 200); saves above 128 MB are skipped.
3. Back in the app, **ZR** on the game, then **Y** (*Share over Wi-Fi*): the screen shows an address such as
   `http://192.168.1.20:8080`. Open it on your phone or computer, on the same Wi-Fi: the Switch serves the Save Lab
   itself, with a **From your Switch** panel listing the captures. Pick some, **Load**, then ⚡ Auto or Differences.
   **Download .zip** packs the selected captures, to attach them to a [Save batch issue](https://github.com/Imp3r1us/TrophyNX/issues/new?template=save-batch.yml).

The share server is read-only, only serves the capture folders, and runs only while that screen is open
(the console doesn't go to sleep meanwhile). B stops it. In the panel, − twice deletes the game's captures.
Log lines: `capture: ...` in `live_log.txt`.

### Popups

The module draws the popup itself, on its own small display layer above the game (like Tesla/Ultrahand
overlays do; it also shows in screenshots and recordings). If that layer can't be created, it falls back
to an **Ultrahand Overlay 2.1+** toast (notifications enabled); without Ultrahand it unlocks silently.

**Popup settings in the app:** press the **right stick** (the *RS* chip in the header). Up / Down picks a
setting, Left / Right or A changes it, Y switches the preview (Unlock, Progress, Live save check, Already
unlocked), X restores the defaults, B saves, - leaves without saving. The preview is drawn by the live module's
own renderer, with the selected game's icon and achievements: a small TV shows where the popup appears, and a
close-up shows the card. The live module reads the file before every popup: no restart needed.

They are saved in `sdmc:/switch/TrophyNX/popup.json` (it can also be edited by hand; every key is optional):

```json
{ "style": "custom", "card": "compact", "duration_ms": 0, "live_check": true, "progress": true,
  "theme": "dark", "icon": "right",
  "position": "top-left", "compact_theme": "dark", "accent": "tier", "opacity": 88,
  "border": true, "show_icon": true, "show_label": true, "size": "large" }
```

- `style`: `custom` (default), `both` (custom + Ultrahand toast), `ultrahand` (toast only), `off`.
- `card`: `classic` (default: text bar + tier medal) or `compact`: a small dark card (360x70 at 1080p)
  with the icon, an uppercase label coloured by tier (bronze / silver cyan, gold or a hidden achievement
  gold, platinum violet), the title and a short line (`+25 pts`, `5 / 10`). It slides in with a fade.
- `size` (compact card): `small` (360x70 at 1080p), `medium` (1.25x), `large` (1.5x, default), `xl` (1.75x).
  The card and its text are drawn at that size (not stretched).
- `position` (compact card): `top-left` (default), `top-right`, `bottom-left`, `bottom-right`, 24 px from the edges.
- `compact_theme` (compact card): `dark` (default), `light`, `midnight`, `oled` (black), `warm`.
- `accent` (compact card): `tier` (default, as above), or one colour for every popup: `cyan`, `gold`, `violet`,
  `green`, `pink`, `orange`, `red`, `white`. On the light theme it is darkened to stay readable.
- `opacity` (compact card): background opacity, 50 to 100 % (default 88).
- `border`, `show_icon`, `show_label` (compact card): the thin border, the icon, the small uppercase line
  (without it, a small accent mark sits next to the title). All `true` by default.
- `theme`: `dark` (translucent black bar, white text) or `light` (white bar, dark text). Classic card.
- `icon`: `right` (after the text) or `left` (at the screen edge, text after it). Classic card.
- `duration_ms`: 0 = default (classic 4000; compact 220 ms in + 3.3 s + 180 ms out), or 1500 to 15000. When other
  popups are waiting, each one is shorter (compact 1.7 s, classic 2.2 s) so a burst doesn't stay on screen long.
- `live_check`: on the first live-save read of a game session, a **Live save** card (`3/41 met · read in 80 ms`),
  then up to 3 achievements you already have that the save meets (**Already unlocked**): proof that it reads your game.
- `progress`: while you play, a **Progress** card when a locked achievement's counter goes up
  (`Pet Cerberus 5 / 10`), at most 3 per save + one `+N more` card. Hidden achievements never show it.

No sound: the module never opens an audio service.

Medal tier: an achievement's optional `"tier"` (`bronze`, `silver`, `gold`, `platinum`), otherwise from its
points (under 25 bronze, under 50 silver, 50+ gold). Icon: `sdmc:/switch/TrophyNX/art/<TITLEID>/<id>.png`
(or `.jpg`) if present, otherwise the game's own icon.

Known limit: if a Tesla/Ultrahand menu is open at the same moment, the two layers compete for the top;
use `"style": "both"` if you often play with an overlay open.

Checking it: open the app, press **X** on Overview or BotW. The bottom shows the module's log
(`live_log.txt`): when it saw the game, how much memory it scanned, how many flags it found, each unlock.

| Live log line | Meaning |
|---|---|
| no log at all | module not installed / not started (check the folder and reboot) |
| `ERROR dmnt:cht init` | Atmosphère's cheat service is disabled |
| `found 0/160 flags` repeatedly | the memory layout differs from the decompilation on your version |
| `anchor flags not found yet` | game still loading; it retries every 30 s |
| `UNLOCKED ...` | worked |
| `close scan: ... mount failed` | the save was still in use (game restarted right away); retried 3 times |

Notes: it uses under 1 MB of system memory and runs on the system core. Attaching through `dmnt:cht`
takes the cheat service's single slot while BotW runs (cheats still work through it).

### In-game menu (Tesla overlay)

`sdmc:/switch/.overlays/TrophyNX.ovl` (in the SD zip) adds **TrophyNX** to the Tesla / Ultrahand menu: the
running game's achievements without leaving it. Open the menu (Tesla's default: **L + D-pad Down + Right stick**),
pick TrophyNX.

- The top shows how many are unlocked, your points and medals for this game.
- Then **In progress** (closest to done first, with "37 / 120"), **Locked** and **Unlocked** (most recent first, with the date).
- **A** on one: its medal, description, points and a progress bar. **B** goes back.

It needs a Tesla menu and its loader (nx-ovlloader) installed, as for any overlay. It reads what the app and
the live module write, and never changes anything:

- The game's list comes from the **app**, which prepares one for every game each time it scans
  (`overlay/<TITLEID>.json`). **Open the app once** after installing so your games get one.
- Unlocks come from your unlock file, so a popup you just got shows at once.
- Progress comes from the live module while you play (`overlay/<TITLEID>.live.json`): values read on a live
  save or on close, and memory values (at most every 5 s). Otherwise it is the progress at the last app scan.

## Install

Builds are made by GitHub Actions: download `TrophyNX.nro` from the latest
[release](../../releases) (or from the artifacts of the latest *Build* run).

1. Copy `TrophyNX.nro` to `sdmc:/switch/` on the SD card.
2. Launch it from the Homebrew Menu (album or title override both work).
3. Pick your user when the selector appears.

The app needs Atmosphère (or another CFW that runs `hbmenu`). It never writes to game saves:
they are mounted with the system's read-only save API.

## Controls

| Button | Action |
|---|---|
| D-pad (or stick) | Move in the game grid / the achievement list (Left / Right: page up / down in a list) |
| A | Open a game's page: completion ring, medals by tier, play stats and its achievements |
| B | Back to the library |
| L / R | Library: sort the games (Featured, Recent, Progress, Playtime, A-Z). Game page: show All / Unlocked (most recent first) / Locked |
| ZR (on a game) | Save captures: record the game's save each time it closes, share captures over Wi-Fi |
| ZR (on Overview) | Compatibility scan: checks every game's save format and writes `scan_bundle.json` (see PC → Switch) |
| Y | Rescan saves (after playing) |
| X | Debug view for the selected game (what was read, values, errors) |
| − | Re-lock: the selected achievement (on a game page), the whole game (in the library) or everything (Overview in the library), with confirmation |
| Press the left stick (LS) | Switch between the new look and the classic look (remembered in `theme.txt`) |
| Press the right stick (RS) | **Popup settings**: card, colours, position, what is shown, with a live preview |
| + | Exit |

In the classic look, Up / Down move, A or Right opens the achievement list, B or Left goes back and L / R page up / down.

**Tip:** a game that is suspended in the background keeps its save locked. Close it before scanning.

## Files on the SD card

```
sdmc:/switch/TrophyNX/
  unlocks/<user-id>.json        your unlocked achievements (with unlock dates)
  games/<TITLEID>.json          optional: your own achievement sets (override the built-in ones)
  universal.json                optional: replace the play-time achievements
  theme.txt                     the look you picked with a stick press ("new" or "classic")
  definition_errors.txt         written only if one of your JSON files has a problem
  log.txt                       full report of the last scan (same content as the X debug view)
  overlay/<TITLEID>.json        the game's achievements for the Tesla overlay (written by the app at each scan)
  overlay/<TITLEID>.live.json   progress written by the live module while you play (removed by the next app scan)
  update_log.txt                last in-app update: each file replaced (or why not) and the live module restart
  scan_bundle.json              written by ZR: save formats + copies of readable saves (upload for drafts)
  scan_report.txt               written by ZR: the same verdicts in plain text
  captures/settings.json        games recorded on close (ZR on a game, then A) and how many captures to keep
  captures/<TITLEID>/<stamp>/   one save capture (the save's files) + <stamp>.json (time, session length, CRC32s)
```

## Testing it (5 minutes with BotW)

1. Start a **new game** in BotW. In the Shrine of Resurrection, open the chests (Old Shirt, Well-Worn Trousers),
   take the Sheikah Slate from its pedestal, walk out of the cave and pick up a Tree Branch.
2. **Save**: open the menu (+) → System → Save. (Autosaves also work, but a manual save is certain.)
   The new game goes into its own slot; the app checks every slot and uses the best one.
3. **Close the game** completely: HOME → X on the game → Close. A suspended game keeps its save locked.
4. Open TrophyNX. Expected: a pop-up with **Rise and Shine**, **Sheikah Slate** and **Out of the Cave**.
5. If something is missing, select BotW and press **X**. The debug view tells you which step failed:

| Debug line | Meaning |
|---|---|
| `Save mount failed: rc=0x7D402 ... no save data` | this user has no BotW save (wrong user picked?) |
| `Save mount failed: rc=...` (other code) | the game is probably still running/suspended — close it and press Y |
| `N/game_data.sav: not present` for every slot | save folders are named differently than expected |
| `... NOT RECOGNISED` | file found but the header is unknown (game version?) |
| `old_shirt: 0 / 1 [not yet]` | save read fine, the flag isn't set yet in that save — save again in-game |
| `!! 1/1 flag(s) missing in save` | the flag name doesn't exist in this save (wrong name/version) |
| `NEW UNLOCK: ...` | unlocked during this scan |

Already-unlocked achievements never re-lock on their own. To replay a test, press **−** to re-lock
(one achievement, a game, or everything from Overview), then **Y**: anything the save still qualifies
for unlocks again with the pop-up.

## Adding a game (JSON)

> **Step-by-step guide:** [Making your own achievement list](docs/custom-achievements.md) — using a draft from
> GitHub, finding a game's title ID and save files, play-time lists for any game, save-based rules, testing.

Achievement sets are plain JSON, so you can add or tweak them without recompiling.
Drop a file in `sdmc:/switch/TrophyNX/games/` named after the title ID:

```json
{
  "title_id": "01007EF00011E000",
  "name": "The Legend of Zelda: Breath of the Wild",
  "reader": "gamedata_hash",
  "save_files": ["0/game_data.sav", "1/game_data.sav"],
  "achievements": [
    { "id": "beast_ruta", "name": "Calm Waters", "desc": "Free Divine Beast Vah Ruta", "points": 25,
      "rule": { "type": "flag", "name": "Clear_RemainsWater" } },
    { "id": "shrine_30", "name": "Pilgrim", "desc": "Complete 30 shrines", "points": 25,
      "rule": { "type": "count", "pattern": "Clear_Dungeon%03d", "from": 0, "to": 119, "target": 30 } }
  ]
}
```

### Readers

| reader | Format | Flag names hashed with |
|---|---|---|
| `gamedata_hash` | BotW `game_data.sav`: u32 hash / u32 value pairs from 0x0C | CRC32 |
| `totk_progress` | TotK `progress.sav`: magic `01020304`, pairs from 0x28 to `MetaData.SaveTypeHash` | Murmur3 |
| `binary_le` | little-endian integers at fixed offsets; optional `file_size` / `magic` checks | — (rules use `offset`/`offsets`, `size`, `max`, optional `mask` (`"0x0004"`: the value counts as 1 when any of these bits is set, else 0, for bit flags), optional `needs`: `{ "offset": "0x1C", "size": 4, "min": 405 }` keeps the rule locked unless the value there is ≥ `min`, e.g. a table long enough to hold this entry) |
| `byml` | Nintendo BYML tree (`YB`/`BY`) at offset 0 or 0x10 — Super Mario Odyssey `File*.bin` | — (rules use `path`/`paths`, optional `equals`) |
| `json` | plain-text JSON (optional BOM, trailing NULs ignored) — Unity games such as Hollow Knight `user1.dat` | — (same `path`/`paths` syntax as `byml`) |
| `gvas` | Unreal Engine 4 / 5 (up to 5.3) SaveGame `.sav` (magic `GVAS`) — property names become keys | — (same `path`/`paths` syntax) |
| `nrbf` | .NET BinaryFormatter stream (Unity games) — class fields become keys; byte arrays holding JSON text are decoded (Blasphemous) | — (same `path`/`paths` syntax) |
| `rpgmaker` | RPG Maker MV / MZ save (`.rpgsave` LZString base64, `.rmmzsave` zlib, or plain JSON) — JsonEx markers removed: `switches/_data[12]`, `variables/_data[5]`, `party/_gold`, `party/_items/7`, `actors/_data[1]/_level`, `_header/encoding` | — (same `path`/`paths` syntax) |
| `gamemaker` | GameMaker text save: INI → `Section/key` (also a base64-wrapped INI), other text → `lines[n]`; numbers parsed, `ds_list_write` / `ds_map_write` hex decoded into lists / objects | — (same `path`/`paths` syntax) |
| `es3` | Easy Save 3 (Unity asset) with encryption on: 16-byte IV + AES-128-CBC, key = PBKDF2-SHA1(password, IV, 100), JSON inside (gzip accepted) — Heavenly Bodies `slot_0`. Needs `"key_file"`: the password is the game's ES3 setting, never shipped; the app reads it from the installed game's `resources.assets` by itself (or a dump in the keys folder / nxdumptool output) | — (same `path`/`paths` syntax as `json`) |
| `sgb` | Supergiant Games save (Hades `Profile*.sav`: `SGB1` header + LZ4 + luabins) — the saved Lua globals (`GameState`, `CurrentRun`, `TextLinesRecord`, `CodexStatus`...), number keys written `1`, `2`...; `_header/runs` etc. | — (same `path`/`paths` syntax) |

**`byml` / `json` paths** select values in the tree, segments separated by `/`:
`CoinNum`, `GameProgressData/UnlockWorldNum`, `WorldInfo[3]/ScenarioNo` (index), `ShineData[*]/IsGet` (every element),
`ShineData[UniqueId=205]/IsGet` (elements whose field matches), `HackDictionary[=Wanwan]` (list contains), `ItemCap[*]`,
`GameState/QuestStatus[*]` (`[*]` on an object: every value of it).
`flag` takes the largest matched value, `count` counts matched values that are true / non-zero / non-empty
(or equal to `equals`, e.g. `"equals": false`), `sum` adds them. Strings count as 1 when non-empty.
For `json`, `true`/`false` read as 1/0, so `{ "type": "flag", "path": "playerData/killedHollowKnight" }` works as-is.
`gvas` and `nrbf` saves are decoded into the same kind of tree (`source/save_formats.cpp`, identical to the Save Lab's
`tools/savelab/formats.js`): arrays, sets, `List<T>` and `HashSet<T>` are lists; Unreal maps and `Dictionary<K,V>`
are lists of `{Key, Value}`, so `Unlocks[Key=Ending_A]/Value`; `<Coins>k__BackingField` reads as `Coins`; .NET enums
are numbers, Unreal enums names (`Books[=E_Books::Book1]`); GUIDs are 32 hex digits, dates are ticks; a value that
cannot be decoded is `null`. Unlike `json`, text stays text (`"12"` is not 12). Unreal 5.4+ saves are not supported yet.
`"xor_key": "..."` (any tree reader) undoes a repeating-key XOR before parsing — SIGNALIS stores `lstr.replika` as
JSON XORed with a fixed phrase. The Save Lab finds known keys and recovers unknown ones from the text.

```json
{ "id": "moon_100", "name": "Moon Gatherer", "points": 25,
  "rule": { "type": "count", "path": "ShineData[*]/IsGet", "target": 100 } }
```

### Rule types

| type | Meaning | Fields |
|---|---|---|
| `flag` | one value ≥ target | `name` or `hash` (`"0x1234abcd"`), `target`, optional `float`, `equals` |
| `count` | how many listed flags are set | `names` / `pattern`+`from`+`to`, `target`, optional `equals` (e.g. `"Clear"`) |
| `sum` | sum of listed values | `names`, `target` |
| `playtime_hours` | hours played (system log) | `target` |
| `launches` | number of launches | `target` |
| `days_played` | different days the game was played (system play log, this user) | `target` |
| `streak_days` | most days in a row the game was played | `target` |
| `comeback_days` | longest break (in days) before playing the game again | `target` |
| `screenshots`, `videos` | captures of the game in the album (console + SD card) | `target` |
| `games_played`, `total_hours`, `unlocked_total` | global only (`universal.json`) | `target` |
| `days_total`, `streak_total`, `captures_total` | global only: days with any game, most days in a row, all album captures | `target` |

Display options: `divisor` (e.g. `4` shows quarter-hearts as hearts), `show_progress`, and `"hidden": true` on the achievement to hide its description until it unlocks.
Points tiers: ≤10 bronze, ≤25 silver, ≤50 gold, above that platinum.

## PC → Switch (automatic drafts)

Many Switch games also exist on PC, often with the same save format. `tools/pc2switch/` (details in its README)
matches every Switch game with Steam, fetches the Steam achievement lists and drafts rules from your saves,
with a confidence for each one — no AI involved:

1. **ZR** on the Overview writes `scan_bundle.json`: each game's save format and verdict (supported, draftable,
   text, binary, encrypted) plus copies of the readable saves.
2. Upload it to `samples/` → the **PC to Switch** workflow (no Steam key needed) opens a pull request
   with drafts to review. Or do the same on the phone in Save Lab, tab 6.

## Building

The background module lives in `sysmodule/` (`sh make_data.sh` then `make` there produces `sys-trophynx.nsp` -> `exefs.nsp`;
`make_data.sh` embeds the definitions). `host/test/test_stream.cpp` (with `host/test/gen_saves.py` for synthetic saves)
checks that the module's low-memory save reading gives the app's values.
`host/test/test_byml.cpp`, `host/test/test_json.cpp` and `host/test/test_formats.cpp` test the tree readers (build lines
at the top of each file); `test_formats --dump DIR` + `CPP_DUMP=DIR node tools/savelab/test/test_formats.js` checks that
the app and the Save Lab decode GVAS / NRBF saves into exactly the same tree.
`node tools/pc2switch/test/test_pc2switch.js` tests the PC → Switch matcher and scripts.
The Tesla overlay lives in `overlay/` (`make` there produces `TrophyNX.ovl`). It needs libtesla in `overlay/libtesla`:
see the top of `overlay/Makefile` for the pinned commit. `host/test/test_overlay_data.cpp` tests what it reads
(`source/overlay_data.cpp`, shared with the app and the module).
`sysmodule/test/test_flagscan.cpp` tests the heap scanner on a synthetic heap (`g++ -Iinclude test/test_flagscan.cpp source/flagscan.cpp -lz`).


You need devkitPro with libnx:

```
make            # -> TrophyNX.nro
make -f Makefile.host && ACH_HOST_ROOT=host ACH_SCRIPT="A,DOWN" ./host/achievements_host
                # PC test build: mock console, frames saved to host/out/*.png
```

`tools/make_totk_json.py` regenerates the TotK set from the save editor's completion lists.
`tools/make_binary_sets.py "<Ultimate Smasher.py>" romfs/games` regenerates the Smash Ultimate and MK8D sets.
`tools/make_playtime_sets.py romfs/games` regenerates the themed play-log sets (edit the table to rename or retune them).

## How you can help

Every game needs someone to work out *where* its progress is stored and *which* achievements make sense.
That is where help is needed most, and you don't need to know how to code.

### 1. Share batches of saves (most useful)

Achievements are made by comparing saves of the same game at different moments. For a game you'd like supported:

1. Easiest: turn on **save captures** for the game (ZR on the game in TrophyNX, then A, see
   [Save captures](#save-captures-record-a-game-share-over-wi-fi)), play, close the game after each step, then
   share them over Wi-Fi and press **Download .zip** in the Save Lab. Or back up your save with
   [JKSV](https://github.com/J-D-K/JKSV) (or another save manager) at a few points:
   - a **fresh** save (just after the intro),
   - saves right **after specific events** (first boss beaten, an item collected, a new area reached…),
   - an **advanced or 100 %** save if you have one.
2. Write down what you did between two saves. "Beat the first boss, nothing else" is perfect.
   A "control" pair (two saves with nothing done in between) is very useful too: it shows what changes on its own.
3. Open a [Save batch issue](https://github.com/Imp3r1us/TrophyNX/issues/new?template=save-batch.yml) with the
   zip files, the game version (shown in the HOME menu under *Options*), and your notes.

All saves of one batch must come from **the same game version**: offsets and formats can move between updates.
Saves can contain your in-game name or your console nickname. Only share what you're fine with making public.

### 2. Suggest achievements

What would be fun to unlock in a game you love? Open an
[Achievement idea issue](https://github.com/Imp3r1us/TrophyNX/issues/new?template=achievement-idea.yml).
Good ideas are things the save actually remembers: items owned, bosses beaten, collectibles, counters, endings.
Things like "beat the game without taking damage" or speedruns usually can't be read from a save.

### 3. Test and report

- Games already supported but not confirmed on every version: **Mario Kart 8 Deluxe** (offsets probably wrong
  on the current version), **Smash Ultimate**, **Super Mario Odyssey**, **Tears of the Kingdom**.
- If an achievement unlocks too early, too late or never, open a
  [Bug report](https://github.com/Imp3r1us/TrophyNX/issues/new?template=bug.yml) with
  `sdmc:/switch/TrophyNX/log.txt` (press X in the app to see the values read for each game).

### 4. Know a save format? Write a definition

If you already know how a game stores its data (from an editor, a decompilation or a cheat table), you can write
its definition yourself: see [Adding a game (JSON)](#adding-a-game-json). The **Save Lab**
(**https://imp3r1us.github.io/TrophyNX/**, a single page that runs in any browser, phones included; your saves stay on your device) compares saves, finds what
changed and drafts the achievements for you. Its **🌐 Share with everyone** button (tab 5) sends your list to
[`drafts/community/`](drafts/community) through a checked pull request, so everyone can use it (your saves are never sent).

## Roadmap

- More games ported from PC: their Switch save often keeps the PC format (Hollow Knight: same JSON, unencrypted), and the PC game's own achievement checks can be reused. Then games with documented save formats: Pokémon (PKHeX), Animal Crossing (NHSE), Mario Kart 8 Deluxe, Smash Ultimate.
- Live memory watching for more games: `memory` rules (pointer chains) work for any game; TotK's flag layout next.
- Export your unlocks to a web profile page.

## Credits

- Memory rules (live achievements for any game): the idea of watching cheat-code values per build ID, the
  `[[main+X]+Y]+Z` address syntax, the 3-checks-in-a-row rule and the wait before attaching come from
  [HomeNX's achievement packs](https://github.com/ad3333m/homenx/blob/master/docs/achievement-packs.md) by ad3333m
  (reimplemented here, no HomeNX code copied). Imported addresses come from the codes in
  [switch-cheats-db](https://github.com/HamletDuFromage/switch-cheats-db), written by their respective cheat authors.
- Live mode: BotW flag layout from the zeldaret/botw decompilation; `dmntcht.c/h` client from Atmosphère/EdiZon (GPLv2); popup layer technique from Tesla (libtesla); fallback toasts via Ultrahand's `.notify` files.

- Save layouts and flag names come from [Marc Robledo's savegame editors](https://github.com/marcrobledo/savegame-editors). The BotW names were datamined by MrCheeze, and the TotK hashes were compiled by MacSpazzy, MrCheeze, savage13 and others.
- Smash Ultimate offsets: [Ultimate-Smasher](https://github.com/CapitanRetraso/Ultimate-Smasher) by CapitanRetraso; MK8D offsets: [EdiZon configs](https://github.com/WerWolv/EdiZon_CheatsConfigsAndScripts) by Ac_K.
- In-game menu built with [libtesla](https://github.com/WerWolv/libtesla) by WerWolv.
- Built with [libnx](https://github.com/switchbrew/libnx), [stb](https://github.com/nothings/stb) (truetype, image) and [cJSON](https://github.com/DaveGamble/cJSON).
- Uses the console's own system font.

## License

TrophyNX is free software under the **GNU General Public License, version 2 or (at your option) any later
version** (see [LICENSE](LICENSE)). You can use, change and share it, including in your own projects, as long as
you keep it open source under the same license and keep the credits: please mention TrophyNX and link to this
repository.

Parts written by others keep their own license: `sysmodule/source/dmntcht.c` and `sysmodule/include/dmntcht.h`
(Atmosphère-NX, GPL version 2 only, so the built module is distributed under GPLv2), cJSON (MIT), stb (public
domain / MIT), libnx (ISC) and libtesla (GPL version 2, used by the overlay; fetched at build time, not in this
repository).
