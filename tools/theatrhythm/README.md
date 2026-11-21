# THEATRHYTHM FINAL BAR LINE (010081B01777C000)

The 42 PS4 trophies, ported from the Switch save: **32 tracked** + the usual 4 play-time / launch achievements
(`romfs/games/010081B01777C000.json`, written by `make_definition.py`).

## Save format (reader `theatrhythm`)

Three files in the save: `backup.inf`, `musicBackup.inf`, `proficaBackup.inf`, each a MessagePack map
(MessagePack-CSharp), same layout as on PS4 ([theatrhythm-fbl-save-converter](https://github.com/rcostanza/theatrhythm-fbl-save-converter)):

```
[u32 LE length]          PS4 only (the Switch files start directly with the header)
q0 q1 q2 q3              32-byte header, 4 × 8 bytes
AES-128-CBC ciphertext   salt = q0‖q3, IV = q2‖q1, key = PBKDF2-HMAC-SHA1(password, salt, 1000 iterations, 16 bytes)
  └ PKCS#7 (1–16 bytes) of raw DEFLATE (no zlib header) of MessagePack
```

What the Switch saves (game v327680, Unity 2021.2.19f1, IL2CPP metadata v29) actually contain:

- class fields are **string keys** (`[MessagePackObject(true)]`), `Dictionary<SomeID, T>` keys are **integers**
  (ids like `160000005`, `4294967295` = none); a few classes use `[Key(n)]` → **arrays**: `MusicInfo.PlayStyleRecord`,
  `ProficaInfo.MyProficaData`, `ProficaInfo.RivalProficaData`. The reader names their fields (labels from `dump.cs`).
- **ext type −1** (MessagePack timestamps, `DateTime`): 3 in backup.inf, 4650 in musicBackup.inf → seconds since 1970
  (`DateTime.MinValue` = −62135596800). No LZ4 blocks (ext 98/99), no other ext types.
- sizes: backup.inf 19 KB → 268 KB of MessagePack; musicBackup.inf 9.7 KB → 726 KB (453 songs × difficulties × styles);
  proficaBackup.inf 176 B. Every object has a `"Version": "1.00"` (save schema, not the game version).

Useful paths (trees are identical in the app and the Save Lab):

| Path | Meaning |
|---|---|
| `MusicInfo/MusicRecordDictionary/<music>/DifficultyRecordDictionary/<0..3>/PlayStyleRecordDictionary/<0..2>/…` | per song × difficulty × style (0 Standard, 1 Simple, 2 Pair): `Highscore`, `BestPlayRank` (9 = none), `MaxChainCount`, `BestClearMark` (3 = none, 0 = best mark), `CriticalBarZones[20]`, `TotalPlayCount`, `TotalClearCount`, `TotalFullChainClearCount`, `MaxDamage` |
| `MusicStageInfo/TotalScore` (long), `TotalAbilityCount`, `TotalSummonCount` | lifetime totals |
| `MusicStageInfo/{Standard,Simple,Pair}PlayInfo/TotalMonsterKillCount`, `TotalItemGetCount`, `TotalDamage`… | per play style |
| `SeriesQuestInfo/WorldDictionary/<500000xx>/Opened, Cleared, Completed, StageDictionary/<id>/…` | 30 worlds (50000029 has no stage: Endless World) |
| `SeriesQuestInfo/EndlessWorldUniqueInfo/BestStageClearCount`, `SeriesQuestInfo/PlayedStaffCredit` | Endless World best, ending seen |
| `CharacterInfo/Characters/<id>/Level, StarLevel, …GrownAmount, UsedCount` | 111 characters |
| `CollecaInfo/Collecas/<id>/ObtainedFlag` (1225), `ItemInfo/Items/<id>/TotalUseCount` (23) | CollectaCards, stage items |
| `RhythpoInfo/TotalRhythpoValue`, `SummonStoneInfo/TotalGetCount`, `MusicSelectInfo/PickupFullChainCount` | Rhythmia, Summonstones, Top Hits ("Pickup") |
| `AchievementInfo/AchievementRecordDictionary/<390000xxx>/ViewHint` | in-game achievement *hints* only: `Achieved` is `[IgnoreMember]`, recomputed at load |

## The password (never in this repo)

It is a string literal of the game's `global-metadata.dat` (romfs `Data/Managed/Metadata/`). The converter's index
(#13610) is for v1.00; in the current Switch build it is **#12425** (62 bytes of UTF-8, not plain ASCII). The index moves with updates, so:

```
python3 tools/theatrhythm/find_password.py <romfs>/Data/Managed/Metadata/global-metadata.dat <save>/backup.inf local/theatrhythm.key
```

tries every literal against your save and writes the one that decrypts it (prints only index and length).
Easier, no PC tools: dump `global-metadata.dat` with nxdumptool on the console; TrophyNX finds the password itself
when it sees that file in `sdmc:/switch/TrophyNX/keys/` or under `sdmc:/nxdt_rd/`, or use the Save Lab button
(*Captures → Encrypted saves → Find it in global-metadata.dat*).
`local/` is git-ignored. Then:

- **console**: copy it to `sdmc:/switch/TrophyNX/keys/theatrhythm.txt` (one line). The definition says
  `"key_file": "theatrhythm.txt"`; without the file the scan log (X) says *password file … is missing* and only the
  play-time achievements count.
- **Save Lab**: *Captures → Encrypted saves*, paste it (kept in the browser's localStorage).
- **decrypt to JSON**: `python3 tools/theatrhythm/decrypt_saves.py --key-file local/theatrhythm.key OUT captures/`

Reading it on the console from the installed game instead: not practical. The app would have to open another
title's romfs (NCA parsing + title keys, or a filesystem service the homebrew isn't granted), and the literal index
changes per version, so it would still have to try ~12,600 PBKDF2 candidates (seconds on PC with C-speed PBKDF2,
much longer on the console). One file on the SD card is simpler and keeps the secret out of the build.

## Trophy code in the Switch build (from Il2CppDumper's dump.cs)

- `Gummi.Device.TrophyAdapter` survives with `Register`, `GetUnlockedTrophies`, `SetProgressPercent(int trophyID, double)`,
  `Unlock(int trophyID, OnUnlocked)`: on Switch they are empty virtual methods (no platform subclass is compiled in),
  so like DELTARUNE the calls exist but do nothing.
- `Table.AchievementTable.Data` (game data, MessagePack) has `Threshold`, `SortOrder`, `EnableAnnouncements[]` and
  **`TrophyID`**: the 100 in-game achievements (ids 390000000+) carry the PS trophy they unlock. That table lives in
  the Addressables bundles (not extracted here); it would give the exact thresholds/conditions per trophy.
- In-game achievement state is not saved (`AchievementRecord.Achieved` is `[IgnoreMember]`): the game recomputes it
  from the counters above at load — which is what the definition does too.

## Coverage: 32 / 42

Tracked: every trophy whose condition is a saved counter or flag (see `_source` of each entry).
To verify on the console: `all_critical` (assumes `BestClearMark` 0 = All Critical: unplayed = 3), `quest_complete_all`
(29 worlds), `encore` (per difficulty+style, so it may need more plays than on PS4), `top_hits_5` (the counter is per
day: it must be scanned the same day), `summonstones_30` (may include Summonstones from rival ProfiCards).

Not tracked (10):

| Trophy | Why |
|---|---|
| THEATRHYTHM Maestro (platinum) | no rule type for "all other achievements of this game" |
| A Musical Journey Begins | needs to know which world/songs are the THEATRHYTHM FINAL FANTASY series (game table) |
| Overflowing Gold | "Critical Chart filled" is not stored; `CriticalBarZones` would need a 20-value test per record |
| Show of Finesse, Unstoppable Force, Summon Tamer | per-song values (abilities, kills, summons in one song) are not saved |
| Summon's True Power | `MaxDamage` is per song, not per summon attack |
| 999er | current stats are not saved (only `…GrownAmount`), the cap depends on the character table |
| Grand Rhythmist, Ultimate Rhythmist | need the list of base-game songs (vs DX / DLC) and a per-song test across difficulties |

## Files

- `decrypt_saves.py`: saves → JSON (pure Python, used to confirm the format on real captures)
- `find_password.py`: finds the password in your `global-metadata.dat`
- `make_definition.py`: writes the definition
- Save Lab module: `parseTheatrhythm` / `parseMsgpack` in `tools/savelab/formats.js`; app: `theatrhythmToJson` /
  `msgpackToJson` in `source/save_formats.cpp`. Tests: `tools/savelab/test/test_formats.js`, `host/test/test_theatrhythm.cpp`
  on synthetic saves made with a dummy password (`tools/savelab/test/make_theatrhythm_fixtures.js`).
