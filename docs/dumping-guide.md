# Getting game files from your Switch (beginner guide)

A few games in TrophyNX need a file **from the game itself** before all their achievements work:

| Game | What you need | Why |
|---|---|---|
| THEATRHYTHM FINAL BAR LINE | `global-metadata.dat` (one file, ~12 MB) | its save is encrypted; the password is written inside this file |
| Heavenly Bodies | `resources.assets` (one file) | same: encrypted save (Unity “Easy Save 3”), password inside this file |
| DELTARUNE | the four `game.win` files (chapters 1–4) | a small patch makes the game record its trophies in the save |

TrophyNX never ships these files: they come from **your own copy** of the game, and you copy them yourself. This guide
does it on the console with **nxdumptool** (no PC needed for the encrypted saves). It takes about 10 minutes the first time.

> Every other game in TrophyNX works without any of this.

---

## Before you start

You need:

- a Switch running **Atmosphère** (custom firmware) that can open the **Homebrew Menu** — if TrophyNX itself runs, you have that;
- the game **installed** (or its cartridge inserted) and **updated** like you play it;
- a way to copy files to/from the SD card (SD card reader on a PC, or an FTP/MTP app) — only needed for DELTARUNE.

A few words you will see:

| Word | Meaning |
|---|---|
| **SD root** / `sdmc:/` | the top folder of your SD card |
| **RomFS** | the folder of game data inside a game (images, music, scripts…) |
| **Program NCA** | the package that holds the game's code and its RomFS |
| **Base application** / **Update** | the game as first released / the downloaded update on top of it |
| **LayeredFS** | Atmosphère's mod folder (`sdmc:/atmosphere/contents/<title id>/`): files there replace the game's files |

---

## Step 1 — one-time setup: nxdumptool and your console keys

nxdumptool reads the game's packages, so it needs your console's own decryption keys.

1. **Keys.** Check if the file `sdmc:/switch/prod.keys` exists on your SD card.
   - It is there → go to point 2.
   - It isn't → you make it once with **Lockpick_RCM**, a payload you boot from **Hekate** (Hekate → *Payloads* →
     `Lockpick_RCM.bin` → *Dump from SysNAND*). It writes `sdmc:/switch/prod.keys`. These keys are your console's own:
     never share this file.
2. **nxdumptool.** Open <https://github.com/DarkMatterCore/nxdumptool/releases>, find
   **“nxdumptool-rewrite (latest commit build)”** and download **`nxdt_rw_poc.nro`** (ignore the other files).
   Copy it into the `switch` folder of your SD card: `sdmc:/switch/nxdt_rw_poc.nro`.
3. Put the SD card back and turn the console on (into Atmosphère, as usual).

**Open it with full memory:** hold **R** while you start any game from the HOME menu → the Homebrew Menu opens instead
of the game → choose **nxdumptool**. (Opening it from the Album also works but gives it less memory.)

**Controls in nxdumptool** (it is a text menu): **D-pad / left stick** move, **A** choose, **B** go back,
**+** quit. It prints the buttons for each screen at the top.

---

## Encrypted saves in short (THEATRHYTHM, Heavenly Bodies)

TrophyNX needs the game's save password **once**, as a one-line text file in `sdmc:/switch/TrophyNX/keys/`
(`theatrhythm.txt`, `heavenlybodies.txt`). The game page tells you when it is missing (*Encrypted save: needs its
password once*). Pick **one** way:

| Way | You need | What you do |
|---|---|---|
| **A. Switch only** (easiest) | nxdumptool | dump the one game file (Step 2 or 2b below), open TrophyNX: it finds the password and writes the key file itself |
| **B. Phone or PC** | the game file + one save | open <https://imp3r1us.github.io/TrophyNX/#key> (Save Lab, 🔑 tab), pick the game, give both files, click **Find the password**: it downloads the key file. Copy it to `sdmc:/switch/TrophyNX/keys/` |
| **C. You already know it** | — | same page, *I already know the password*, or make the text file yourself (just the password, one line) |

Then open TrophyNX once: the save is read, and from then on the achievements **unlock live** while you play (the live
module uses the same key file). Nothing is sent anywhere: the Save Lab runs in your browser.

**Never post the password or the key file publicly** (it is the game's own key). Share this guide instead.

---

## Step 2 — THEATRHYTHM FINAL BAR LINE: dump `global-metadata.dat`

In nxdumptool:

1. **`user titles menu`** → choose **THEATRHYTHM FINAL BAR LINE**.
2. **`nca / nca fs dump options`** → **`dump base application`**.
3. Choose the line that starts with **`Program #0`**.
4. You now see the sections of that package. Choose the one that says **`RomFS`**
   (for example `FS section #2: RomFS (…)`).
5. Check that the option **`use layeredfs dir`** is set to **`no`** (it is by default).
6. Choose **`browse nca fs section`**. A file browser opens on the game's RomFS.
7. Go into **`Data`** → **`Managed`** → **`Metadata`** (A to enter a folder).
8. Select **`global-metadata.dat`** and press **A**. It is copied (a few seconds).
9. Press **+** to quit nxdumptool.

The file is now somewhere under `sdmc:/nxdumptool/NCA FS/User/Extracted/` (in a folder named after the game). **You don't
need to move it.**

### Let TrophyNX find the password

1. Open **TrophyNX**. When it reads THEATRHYTHM's save and has no password yet, it looks for `global-metadata.dat` in
   `sdmc:/nxdumptool/` and in `sdmc:/switch/TrophyNX/keys/`, finds the password in it (instant) and saves it as
   `sdmc:/switch/TrophyNX/keys/theatrhythm.txt`.
2. Check it: select THEATRHYTHM in TrophyNX and press **X**. You should see
   `Password found in …global-metadata.dat`, then the three save files with **OK**.
3. Done. From now on TrophyNX uses `keys/theatrhythm.txt`; you can delete the `sdmc:/nxdumptool/` folder if you want.

> Why the base application and not the update? The password is the same in every version, so the base game is enough
> (and it is the simplest path). If the base doesn't work for you, try step 2 with **`dump update`** and the
> **`Patch RomFS`** section (see Step 3, point 4).

### Without the console (phone or PC)

If you already have the file on a phone or PC (for example from a dump made with another tool):

- open <https://imp3r1us.github.io/TrophyNX/#key> (the Save Lab's **🔑 Save password** tab), pick THEATRHYTHM, give
  `global-metadata.dat` and one save file (`backup.inf`, or the whole JKSV `.zip`), click **Find the password**. It
  downloads **`theatrhythm.txt`**: copy that file to `sdmc:/switch/TrophyNX/keys/` on the SD card.
- or, with Python: `python tools/theatrhythm/find_password.py global-metadata.dat backup.inf theatrhythm.txt`.

---

## Step 2b — Heavenly Bodies: dump `resources.assets`

TrophyNX first tries to read this file from the installed game by itself; some firmwares refuse it (the X page then
says *the system didn't open the game's files*). In that case, in nxdumptool:

1. **`user titles menu`** → choose **Heavenly Bodies**.
2. **`nca / nca fs dump options`** → **`dump base application`**.
3. Choose **`Program #0`**, then the section that says **`RomFS`**.
4. Keep **`use layeredfs dir`** on **`no`**, then **`browse nca fs section`**.
5. Go into **`Data`**, select **`resources.assets`** and press **A**. Then **+** to quit.

Open **TrophyNX**: it finds `resources.assets` under `sdmc:/nxdumptool/`, takes the password from it and saves
`sdmc:/switch/TrophyNX/keys/heavenlybodies.txt`. Check it with **X** on the game: `Password found in …resources.assets`,
then `Save password: …heavenlybodies.txt`. You can delete the dumped file afterwards.

With a phone or PC instead: <https://imp3r1us.github.io/TrophyNX/#key>, pick Heavenly Bodies, give `resources.assets`
and one save file (`slot_0`, or the JKSV `.zip`), click **Find the password**, copy `heavenlybodies.txt` to
`sdmc:/switch/TrophyNX/keys/`.

---

## Step 3 — DELTARUNE: dump the four `game.win` files

This one needs a PC for the patch itself. Dump the files from **the version you play** (base game + update): after the
game updates, do it again.

In nxdumptool:

1. **`user titles menu`** → **DELTARUNE**.
2. **`nca / nca fs dump options`**.
3. If your game has an update installed: choose **`dump update`**. Otherwise: **`dump base application`**.
4. Choose **`Program #0`**, then the section:
   - with an update: **`Patch RomFS`**, then set **`use base/patch title`** to the base game (DELTARUNE's base
     application) — nxdumptool needs both to show the up-to-date files;
   - without an update: **`RomFS`**.
5. Keep **`use layeredfs dir`** on **`no`**, then **`browse nca fs section`**.
6. For each chapter, go into the folder and dump its `game.win` (select it, press **A**), then **B** to go back:
   - `chapter1_switch` → `game.win`
   - `chapter2_switch` → `game.win`
   - `chapter3_switch` → `game.win`
   - `chapter4_switch` → `game.win`
7. Press **+** to quit.

The four files are under `sdmc:/nxdumptool/NCA FS/User/Extracted/<DELTARUNE folder>/Program #0/<section>/`, each in its
`chapterN_switch` folder (nxdumptool keeps the folders).

### Patch them on the PC

1. Copy that `<section>` folder (the one that contains `chapter1_switch` … `chapter4_switch`) from the SD card to your PC,
   for example to `D:\deltarune\romfs`.
2. Download the **UndertaleModTool CLI** (file `UTMT_CLI_….zip` from
   <https://github.com/UnderminersTeam/UndertaleModTool/releases>) and unzip it, e.g. to `C:\Tools\UTMT_CLI`.
3. Download TrophyNX's `tools/deltarune` folder (or the whole repository: green **Code** button → *Download ZIP*).
4. In that folder, right-click → *Open in Terminal* (PowerShell) and run:

   ```powershell
   .\patch.ps1 -Romfs D:\deltarune\romfs -Cli C:\Tools\UTMT_CLI\UndertaleModCli.exe -Out D:\deltarune\patched
   ```

   Each chapter takes a few seconds. If it says the code doesn't look like the expected version, your dump is from
   another game version: dump again from the version you play.

### Install the patch

The script puts the result in `D:\deltarune\patched\atmosphere\…`: copy that **`atmosphere`** folder to the root of the
SD card (say yes if asked to merge folders). You then have exactly these four files:

```
sdmc:/atmosphere/contents/0100A0D022A68000/romfs/chapter1_switch/game.win
sdmc:/atmosphere/contents/0100A0D022A68000/romfs/chapter2_switch/game.win
sdmc:/atmosphere/contents/0100A0D022A68000/romfs/chapter3_switch/game.win
sdmc:/atmosphere/contents/0100A0D022A68000/romfs/chapter4_switch/game.win
```

Play: a trophy is written to the save the next time the game saves. To remove the patch, delete the folder
`sdmc:/atmosphere/contents/0100A0D022A68000/romfs`. More details: [tools/deltarune/README.md](../tools/deltarune/README.md).

---

## If something goes wrong

| Problem | What to do |
|---|---|
| nxdumptool says keys are missing / can't decrypt | `sdmc:/switch/prod.keys` is missing or old: run Lockpick_RCM again (after a system update too) |
| nxdumptool doesn't start or crashes | open it with **R held on a game** (full memory). Its last build is from 2023 and the project is archived: if it doesn't run on your firmware at all, dump the RomFS on a PC with your usual tool and use the PC steps above |
| The game isn't in `user titles menu` | it must be installed on the console or SD card (for a cartridge, use nxdumptool's `gamecard menu` instead) |
| TrophyNX's X screen says `password file … is missing` | `global-metadata.dat` wasn't found: check it is under `sdmc:/nxdumptool/` or copy it to `sdmc:/switch/TrophyNX/keys/` |
| X says `no string decrypts this save` | that `global-metadata.dat` is from another game; dump the right one |
| You set `use layeredfs dir` to **yes** by mistake | the file went to `sdmc:/atmosphere/contents/<title id>/romfs/…`, where the game would load it as a mod: move it out (or delete that `romfs` folder) |

Menu names in this guide are those of nxdumptool's rewrite build (`nxdt_rw_poc`); a later build may word them slightly
differently.
