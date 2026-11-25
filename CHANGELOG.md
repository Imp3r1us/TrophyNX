# Changelog

Each version has its own section. The release workflow copies it into the GitHub release, and the
in-app updater (ZL) shows its first line and its bullets before you install.

How to write a section (see `tools/release/changelog.py`):
- `## X.Y.Z` heading (the APP_VERSION in `include/version.h`), optional date after it.
- First line: one sentence for players, shown as the update's title in the app (max ~100 characters).
- Then `### New`, `### Fixed`, `### Changed`, `### Live module`, `### Known issues`... with `- ` bullets
  written for players: what changed for them, and what they need to do, if anything.
- A release without a section for its version fails in CI.

## 0.17.0 (2026-11-25)

Achievements now play a sound when they unlock, play time achievements pop up while you play, and the overlay shows live progress.

### New
- Unlock sound: a short chime plays with each achievement popup: a rising whoosh, a soft thump and a two-note bell, with extra sparkle for silver, gold and platinum. Popup settings: Unlock sound (Chime, Fanfare, My sound, Off) and Sound volume.
- Play time and launch achievements ("Play 10 hours", "Launch 25 times") now unlock while you play, with a popup, instead of waiting for the next time you open the app.
- Tesla overlay: refreshes by itself while open, and shows live progress for play time and memory achievements ("12,500 / 50,000").
- Tesla overlay: a Live memory test section for games with memory achievements: the game version (build ID), how many achievements it can watch on that version, and each value it reads. Handy to check a draft from drafts/memory before trusting it.
- Compact popup: two new positions, Top center and Bottom center. The card drops down from the top edge (or rises from the bottom).
- My sound: put your own sound in `switch/TrophyNX/sounds/unlock.wav` (uncompressed PCM .wav, 3 seconds at most) and pick My sound.

### Changed
- Progress popups ("Pet Cerberus 5 / 10") are off by default: a popup shows only when an achievement unlocks. Turn them back on in Popup settings (Progress popups: Every progress). Settings saved with an older version also switch to this default once.

### Live module 0.13.0
- Plays the unlock sound on the console's audio output while the popup is on screen. It is skipped, never the popup, when memory is short.
- Reads the play log once a minute while a game runs and unlocks play time / launch achievements (the game's own and universal.json).
- Writes the live memory test for the overlay while it watches a game's memory.

## 0.16.3 (2026-11-25)

Live popups no longer depend on how big a game's save is: every supported game is read while you play, not only after you open TrophyNX.

### Fixed
- Saves too big for the live module's memory were skipped (achievements only unlocked when you opened the app). This now applies to every save format, not game by game: Hollow Knight and other JSON saves are read in pieces like Odyssey and Hades, and any other format gets more memory for the time of the read.

### Live module 0.12.0
- JSON saves (Hollow Knight, SIGNALIS, Paper Mario: The Origami King, DAVE THE DIVER, Vampire Survivors...) are read 16 KB at a time, and only the values the achievements use are kept, whatever the file size.
- Every other format (Unreal GVAS, .NET NRBF, RPG Maker, GameMaker, DELTARUNE, THEATRHYTHM...): when a file doesn't fit, the module grows its memory for the read (up to 16 MB, only if the console keeps 6 MB free for itself), then gives it back. Same when a reader finds it needs more than expected.
- The logs say when this happens (`memory grown from ... to ... KB`); a file is only skipped if the console really has no room, and the log says so.

## 0.16.2 (2026-11-25)

Super Mario Odyssey achievements now unlock with a popup while you play, not only when you open TrophyNX.

### Fixed
- Super Mario Odyssey: the live module skipped its 2 MB save files because they did not fit in its memory, so nothing unlocked (no popup, no progress in the overlay) until you opened the app. It now reads them in small pieces.

### Live module 0.11.2
- BYML saves (Super Mario Odyssey) are read in 16 KB pages as the achievement paths are walked, with at most 512 KB in memory. Popups come a few seconds after the game saves, and when you quit the game.
- `live_log.txt` / `close_log.txt` show `BYML v3 LE (read in pages, N page reads)` for each file read.


## 0.16.1 (2026-11-25)

The compact popup is bigger, and its size is now a setting.

### Changed
- Compact popup: new Size setting (Small, Medium, Large, Extra large). Large, 1.5x the old card, is the new default. The card and its text are drawn at that size, so they stay sharp.
- The popup settings preview shows the card at its real size on screen.

### Live module 0.11.1
- Draws the compact card at the chosen size.

## 0.16.0 (2026-11-25)

Popup settings in the app: choose the card, its colours and position, and what is shown, with a live preview.

### New
- Popup settings: press the right stick in the app. Card (classic or compact), where popups show (on screen, Ultrahand, off), duration, live save check and progress popups on or off.
- Compact card, made yours: position (any corner), theme (Dark, Light, Midnight, OLED black, Warm), accent colour (by tier, or Cyan, Gold, Violet, Green, Pink, Orange, Red, White), background opacity, border, icon and label line.
- Classic card: dark or light, icon on the right or the left.
- A live preview of the real popup: a small TV shows where it appears, with the selected game's icon and achievements. Y switches it between an unlock, a progress, a live save check and an already unlocked popup.
- Everything is saved in popup.json and used from the next popup, without restarting.

### Changed
- The look switch is now on the left stick only; the right stick opens the popup settings.

### Live module 0.11.0
- Reads the new popup settings (themes, accents, opacity, border, icon, label line).

## 0.15.2 (2026-11-25)

Popups while you play now really work, a new compact popup style, and popups that show TrophyNX is following your game.

### New
- Live save check: the first time live save reads your game's save in a session, a "Live save" popup says what it read (e.g. 3/41 met), then shows up to 3 achievements you already have that the save meets. Turn it off with "live_check": false in popup.json.
- Progress popups: while you play, a popup when a locked achievement's counter goes up (Pet Cerberus 4/10, then 5/10...). At most 3 per save, hidden achievements never. Turn them off with "progress": false.
- Compact popup: "card": "compact" in popup.json for a small dark card with the icon, a coloured label, the title and a short line. Choose the corner with "position" (top-left, top-right, bottom-left, bottom-right).
- Several popups at once now each stay on screen a shorter time.

### Fixed
- Live save never finished a single read, for any game: after each read it lost track of which save it had read, took that for "the game saved meanwhile" and started over, forever. It now keeps that information, so achievements pop up a few seconds after the game saves instead of only when you quit.

### Live module 0.10.2
- The fix above (the module logged "save image not readable: unknown error" three times, then stopped watching until you quit).
- Live check, already unlocked, progress and compact popups.

## 0.15.1 (2026-11-24)

Hades achievements now unlock when you quit the game, not only after a scan in the app.

### Fixed
- Hades: the live module skipped the game's saves (too big for its memory), so achievements only unlocked when you opened the app and rescanned, and the in-game menu kept them locked. It now reads them in small pieces and keeps only the values the achievements need, so they unlock when you quit (and while you play once live save works on your console: see below).
- Already played? Open the app once and rescan (Y): anything you earned meanwhile unlocks then.

### Live module 0.10.1
- Hades saves are read 16 KB at a time, with LZ4 decoded through its 64 KB window: the memory needed no longer grows with the save.
- Live save now says in `live_log.txt` why it could not read a save image (it used to retry silently at every save, so popups during play never came and nothing said why). If you see `save image not readable: ...` there, please share the line.

## 0.15.0 (2026-11-24)

See your achievements without leaving the game: TrophyNX now has a Tesla / Ultrahand menu.

### New
- In-game menu: pick TrophyNX in the Tesla or Ultrahand menu to see the running game's achievements, your points and medals, what is in progress (closest to done first), what is locked and what you unlocked (most recent first). A on one shows its medal, description and progress. Needs a Tesla menu and nx-ovlloader, as for any overlay. Open the app once after updating: it prepares every game's list for the menu when it scans.
- Memory achievements can now watch events, not just values: several conditions at once ("win a race without being hit"), a condition that only counts once it changes during play (nothing already true when the game opens unlocks it), and "gain N in one sitting". See docs/custom-achievements.md.

### Live module 0.10.0
- Runs the new memory conditions (all / becomes / increase).
- Writes your progress for the in-game menu while you play: values read on a live save or on close, and memory values (at most every 5 s).

## 0.14.0 (2026-11-24)

Two new save readers that work for whole game engines: RPG Maker MV / MZ and GameMaker.

### New
- RPG Maker MV / MZ games: the app reads their saves (switches, variables, gold, items, party levels, battles...). One achievement list per game is still needed, but no new code: the Save Lab finds the values.
- GameMaker games: the app reads their INI saves (also when the INI is base64-encoded) and their line-by-line text saves, including the lists and maps GameMaker writes as long hex strings.
- The compatibility scan (ZR on the Overview) now recognises both formats and marks those games "draftable", so they show up as candidates for new achievement lists.
- The Save Lab detects and decodes both formats too, with the same result as the app.

## 0.13.0 (2026-11-24)

Every game gets 7 new achievements from your play days and your album, no save needed.

### New
- New achievements for every game: play on 7 and 30 different days, play 3 and 7 days in a row, come back after 90 days away, take 10 screenshots, record a video clip.
- New global achievements: play on 100 and 365 different days, play something 30 days in a row, keep 100 screenshots or videos in the album.
- Days are read from the console's play log for the selected user, and captures from the album (console memory and SD card). Older play-log entries may already be gone, so long histories can count a little less than the real total.
- The game's debug view (X) now shows its play days, longest streak, longest break and album counts. The Overview shows what the play log and album returned.
- Custom definitions can use the new rule types: days_played, streak_days, comeback_days, screenshots, videos (and days_total, streak_total, captures_total in universal.json). See docs/custom-achievements.md.

## 0.12.0 (2026-11-23)

A new look for the app, and achievement popups while you play, right after the game saves.

### New
- New library: your points, medals by tier and latest unlocks at the top, then every game as a card with its progress. L / R sorts the games (Recent, Progress, Playtime, A-Z...).
- New game page: big icon, play stats, medals by tier and a completion ring. L / R shows All, Unlocked (most recent first) or Locked achievements.
- Prefer the old layout? Press either stick to switch to the classic look and back. The app remembers your choice.
- Memory achievements: definitions can now watch a value in the game's memory (money, level...) per game version, and unlock it the moment it's reached. See docs/custom-achievements.md.

### Live module 0.9.0
- Live save: for every game with a save reader, the module reads the game's latest save while it runs (read-only, without touching the game's own save files) and shows popups a few seconds after the game saves. Turn it off for a game with "live_save": false.
- Runs the new memory achievements while you play.

## 0.11.4 (2026-11-23)

Fixes the console shutting down (error 2001-0132) when a game or sphaira starts.

### Fixed
- On consoles with several background modules, the console could shut down with error 2001-0132 right after booting, or when starting a game, sphaira or TrophyNX. All background modules share one block of system memory, and TrophyNX kept 4 MB of it reserved the whole time. It now keeps 2 MB, and only takes more for a moment when there is room to spare.

### Live module 0.8.6
- The health check now really reads the shared system resources (it always said "resource limit unreadable"), and logs them at every game start and close in `live_log.txt`.
- Heap sized on demand: 2 MB while idle, 4 MB only while a popup is shown or a save is read, and only if at least 6 MB stay free for the other modules. Without room, popups and save checks run in the 2 MB heap.
- Safe mode no longer turns on for threads (the module never creates any after boot), and the scan on close checks resources again right before it runs.
- Tip: if the console still crashes, turn off background modules you don't use (Ultrahand), e.g. emuiibo or sys-clk.

## 0.11.3

Turning the live module off now really keeps it off, even when you open TrophyNX.

### Fixed
- The app started the live module every time it opened, even after you turned it off (Ultrahand, or by renaming/removing `flags/boot2.flag`). Now, without `boot2.flag`, the app never starts it, and an update installs the new module without starting it.

## 0.11.2

Safer live module, a beginner guide for game files, and THEATRHYTHM's password found again.

### New
- Beginner guide for the two games that need a file from your own copy (THEATRHYTHM, DELTARUNE): `docs/dumping-guide.md`.
- The update screen (ZL) now lists what each update changes, like these notes.

### Fixed
- THEATRHYTHM: the app now finds `global-metadata.dat` where nxdumptool really saves it (`sdmc:/nxdumptool/...`, up to 16 folders deep).

### Live module 0.8.2
- Ignores homebrew started like a game (Homebrew Menu, forwarders, launchers such as homeNX, title takeover with R): no live mode, capture or scan on close for them.
- The raw NAND save probes are off. Create an empty `sdmc:/switch/TrophyNX/probe.flag` to turn them back on.
- Fixed a display layer that could stay open if a popup failed to start.
- New health check: `health:` lines in `live_log.txt` show how much of the shared system resources background modules use. Near a limit, the module switches to safe mode (toasts instead of popups, no save reading) until it recovers.

## 0.11.1 (2026-11-21)

THEATRHYTHM FINAL BAR LINE: 19 of the game's own feats added as achievements.

### New
- THEATRHYTHM: 19 feats (first summon, ProfiCard edit, 24/100 hours, 200/500 stages, a song 100 times, 300 Perfect Chains, 32 characters Lv99, 4 characters Star Level 9, 5/20 Series Quests, 1,500 chain in Endless World, every airship and moogle style, 9,800,000+ scores).

### Live module 0.8.1
- Also reads THEATRHYTHM high scores on close, within its memory limit (a save too big is skipped, never a crash).

## 0.11.0 (2026-11-21)

THEATRHYTHM popups when you close the game, and its save password found from the game's own file.

### New
- THEATRHYTHM: popups about 3 seconds after closing the game, like the other save-based games.
- No `keys/theatrhythm.txt`? The app finds the password in the game's `global-metadata.dat` (in `keys/` or nxdumptool's folder) and writes the key file. The Save Lab can do it too.
- THEATRHYTHM FINAL BAR LINE: save reader and 32 of the 42 PS4 trophies (needs the save password, see above).

### Live module 0.8.0
- Reads only the parts of THEATRHYTHM's save that the achievements need (~2.3 MB instead of ~11 MB).

## 0.10.2 (2026-11-21)

DELTARUNE: the 30 PS4/PS5 trophies.

### New
- DELTARUNE: 30 trophies read from `deltarune.sav`. Some unlock on an unpatched save; all of them need the trophy patch in `tools/deltarune/` (you patch your own dump on a PC).

## 0.10.1 (2026-11-20)

The in-app update now really replaces the app and restarts the live module.

### Fixed
- Updating with ZL downloaded everything but left the old app and module running. The app now restarts itself after an update, and the module is restarted once its old process is gone.
- New `update_log.txt`: what happened to each file and to the module.

### Live module 0.7.1
- At boot, installs an app update an older version left behind as `TrophyNX.nro.new`. Coming from 0.10.0: update, then reboot once.

## 0.10.0 (2026-11-20)

Save captures shared over Wi-Fi, Hades achievements and save-based achievements for 7 more games.

### New
- Save captures: ZR on a game, A to record. Each time the game closes, its save is copied; Y shares the captures to a phone over Wi-Fi (Save Lab at `http://<console IP>:8080`).
- Hades: 41 achievements from the save (38 Steam + 3 escape milestones).
- Save-based achievements: DAVE THE DIVER (56), Blasphemous (45), Slay the Spire (46), Vampire Survivors (243), Vampire Crawlers (161), OCTOPATH TRAVELER (22), Paper Mario: The Origami King (22).
- Play-time achievements for Hades and DELTARUNE.

### Live module 0.7.0
- Copies the save of recorded games when they close (keeps the last 20 per game).

## Older versions

See the [GitHub releases](https://github.com/Imp3r1us/TrophyNX/releases) (0.9.1 and before have no detailed notes).
