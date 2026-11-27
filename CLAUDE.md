# TrophyNX: notes for coding agents

TrophyNX is a Nintendo Switch homebrew achievements app (Atmosphère). It reads play statistics and game saves and
turns them into achievements, with popups while you play. Player docs: `README.md`, `docs/`. This file covers what you
need to change the code safely.

## Layout

| Path | What |
|---|---|
| `source/`, `include/` | the app (`TrophyNX.nro`). `rules.cpp` evaluates achievements, `save_formats.cpp` holds the save readers, `achievements.cpp` builds the library, `main.cpp` is the UI, `platform_switch.cpp` / `platform_host.cpp` are the platform layers |
| `sysmodule/` | live module **sys-trophynx** (`atmosphere/contents/420000000000AC01/exefs.nsp`). It reuses the app's code through `source/shared_*.cpp` wrappers (`shared_rules.cpp` → `../../source/rules.cpp`), so a change in `rules.cpp` changes both. Save-based achievements unlock live from here, not only when the app is opened |
| `overlay/` | Tesla overlay (`TrophyNX.ovl`) |
| `romfs/games/<TITLEID>.json` | built-in achievement lists (one per game). `romfs/universal.json` holds the play-time sets for every game |
| `tools/savelab/` | **Save Lab** web tool (https://imp3r1us.github.io/TrophyNX/): `engine.js`, `formats.js` (JS twins of the C++ readers), `app.html` → `python3 build.py` → `index.html` (**commit the rebuilt index.html**). Tests: `node test/test_engine.js`, `node test/test_formats.js` |
| `host/test/*.cpp` | host unit tests (plain g++). The commands are in `.github/workflows/build.yml` |
| `drafts/` | candidate lists (`drafts/community/` comes from the issue-sharing bot) |
| `tools/release/changelog.py` | checks `CHANGELOG.md` against the version |

## Achievement definitions

A game JSON has `title_id`, `name`, `reader`, `save_files` and `achievements[]`, each with a `rule`. Readers (see
`README.md`'s reader table and `docs/custom-achievements.md`) include `json`, `gamemaker` (lines), `binary_le` (fixed
offsets: `mask` for bit flags, `needs` to check the table size, best value across all `save_files` = save slots),
`es3` (Easy Save 3, encrypted), `theatrhythm`, `byml`, `gamedata_hash` / `totk_progress` (Zelda), `gvas`, `nrbf`, `rpgmaker`, `sgb`, `deltarune`. Tree readers use
`path` (`a/b[0]`, `list[=value]`). Lists generated from Steam carry each achievement's `"steam"` name.

To add a reader: a C++ reader in `save_formats.cpp` plus a line in `evaluateSave`, its JS twin in `formats.js`
(`TREE_FORMATS` in `engine.js`), and tests. Both must produce the same tree.

## Encrypted saves: never ship passwords

Games such as THEATRHYTHM and Heavenly Bodies use `"key_file": "<name>.txt"`. The password lives only on the player's SD
card, in `sdmc:/switch/TrophyNX/keys/`. It never goes in the repo, a commit, an issue or a PR. The app finds it, in this
order:
1. the key file;
2. the installed game (`plat::readGameFile`: `fsOpenFileSystemWithPatch`, then the base program NCA through ncm, both
   refused on some firmwares);
3. a dumped game file in `keys/` or `sdmc:/nxdumptool/`.

Players can also make the key file with the Save Lab's 🔑 tab (`…/TrophyNX/#key`). The live module reads the same
folder. When no password is found, the game status is `SaveStatus::NeedsKey`.

## Versions and releases

- App: `APP_VERSION` in **both** `include/version.h` and `Makefile`. Live module: `MODULE_VERSION` in
  `sysmodule/source/main.cpp`.
- Every version needs a `## X.Y.Z (date)` section in `CHANGELOG.md`, written for players. Its first line is the
  update's title. The format is at the top of the file.
- Push to main with a new version → CI makes the GitHub release that the in-app updater installs. Any push to main →
  test build on the `builds` branch (the updater's "builds" channel: **don't delete that branch**).
- A version that was already released never changes: bump the patch version for follow-ups.

## Building and testing (Windows dev machine, no local g++)

Use Docker with `devkitpro/devkita64`, and build libnx from source (the image's copy can be older than what the code needs):

```sh
# inside the container, on a copy of the repo with CRLF stripped from sources/Makefiles
g++ -std=gnu++17 -fno-exceptions -Iinclude host/test/test_theatrhythm.cpp source/rules.cpp source/memrules.cpp \
    source/fileutil.cpp source/save_formats.cpp source/json_doc.cpp source/byml.cpp -x c source/cJSON.c -o /tmp/t && /tmp/t
git clone --depth 1 https://github.com/switchbrew/libnx /libnx && make -C /libnx install
make -j3                                    # TrophyNX.nro
sh sysmodule/make_data.sh && make -C sysmodule -j3   # make_data.sh first, or games_bin.h is missing
```

- The app is built with **`-fno-exceptions`**: no `try`/`catch` in app code.
- Limit the jobs (`-j3`, `docker run -m 6g`): WSL easily eats 15 GB of RAM.
- Many files are CRLF. Keep each file's line endings when editing.

## Trying it on the console

- **Install:** DBI's MTP mode over USB. From Windows, use the Shell COM API (`Shell.Application`, `NameSpace(17)` → the
  "Switch" device → `1: SD Card`). `CopyHere(path, 4+16+1024)` replaces files; check the size afterwards, because the
  first copy sometimes silently doesn't replace.
  - App: `switch/TrophyNX.nro`.
  - Live module: `atmosphere/contents/420000000000AC01/exefs.nsp`, then restart the console.
- **Logs:** the game page's **X** shows its log. **Y** (Send logs, `hook-experiment` branch) posts them to the PC
  address in `switch/TrophyNX/log_server.txt`. `tools/logserver/trophynx_logserver.py` receives them and serves them at
  `http://<pc>:8421/latest/` (`log.txt`, `live_log.txt`, …).

## Git

- Never push to `main`: work on a branch and open a PR. The owner merges.
- `hook-experiment`: the experimental Unity achievement hook (`hook/`, `tools/unityhook/`), open as a draft PR.
  Don't merge it until the hook has been confirmed in a real game.
- The owner's main checkout may hold uncommitted work: never clean, reset or stash it. Use a separate worktree.
