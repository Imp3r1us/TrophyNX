#!/usr/bin/env python3
"""Writes romfs/games/010081B01777C000.json (THEATRHYTHM FINAL BAR LINE): the PS4 trophies the save can prove.
Usage: python3 tools/theatrhythm/make_definition.py [out.json]
Paths: see the tree described in tools/theatrhythm/README.md (reader "theatrhythm").
"""
import json
import os
import sys

PS = 'MusicInfo/MusicRecordDictionary[*]/DifficultyRecordDictionary[*]/PlayStyleRecordDictionary[*]/'
WORLD = 'SeriesQuestInfo/WorldDictionary[*]/'
KILLS = ['MusicStageInfo/%sPlayInfo/TotalMonsterKillCount' % m for m in ('Standard', 'Simple', 'Pair')]
DROPS = ['MusicStageInfo/%sPlayInfo/TotalItemGetCount' % m for m in ('Standard', 'Simple', 'Pair')]
POINTS = {'platinum': 100, 'gold': 50, 'silver': 25, 'bronze': 10}


def ach(i, grade, name, desc, rule, src):
    return {'id': i, 'name': name, 'desc': desc, 'points': POINTS[grade], 'tier': grade, 'rule': rule, '_source': src}


def flag(path, target=1, **k):
    return dict({'type': 'flag', 'path': path, 'target': target}, **k)


def total(paths, target):
    return {'type': 'sum', 'paths': paths if isinstance(paths, list) else [paths], 'target': target}


def count(path, target, **k):
    return dict({'type': 'count', 'path': path, 'target': target}, **k)


TROPHIES = [
    ach('first_series_clear', 'bronze', 'In Pursuit of Harmony', 'Cleared first series title',
        count(WORLD + 'Cleared', 1, show_progress=False), 'Series Quest worlds with Cleared = true'),
    ach('first_perfect_chain', 'bronze', 'Unbroken Symphony', 'Achieved first Perfect Chain',
        total(PS + 'TotalFullChainClearCount', 1), 'song records: TotalFullChainClearCount ([Key(8)] of MusicInfo.PlayStyleRecord; Perfect Chain = "FullChain" in the code)'),
    ach('all_critical', 'silver', 'Perfectionist', 'Achieved first All Critical',
        count(PS + 'BestClearMark', 1, equals=0, show_progress=False),
        'a song record whose BestClearMark is ClearMark01 (0); 3 = never cleared. 0 = All Critical is inferred from the enum order: check on console'),
    ach('ending', 'gold', 'Outro', 'Beat the game and watched the ending',
        flag('SeriesQuestInfo/PlayedStaffCredit'), 'SeriesQuestInfo/PlayedStaffCredit (staff roll)'),
    ach('played_100', 'bronze', 'Battling Musicians', 'Played 100 Music Stages',
        total(PS + 'TotalPlayCount', 100), 'sum of TotalPlayCount of every song record'),
    ach('played_300', 'silver', 'Veteran Orchestra', 'Played 300 Music Stages',
        total(PS + 'TotalPlayCount', 300), 'same counter'),
    ach('encore', 'bronze', 'Encore!', 'Played one song 10 times',
        flag(PS + 'TotalPlayCount', 10),
        'largest TotalPlayCount of one song at one difficulty and style (if the trophy counts every difficulty of a song, it can unlock later than on PS4)'),
    ach('chains_50', 'bronze', 'Uniting Melody', 'Achieved 50 Perfect Chains',
        total(PS + 'TotalFullChainClearCount', 50), 'sum of TotalFullChainClearCount'),
    ach('chains_100', 'silver', 'Eternal Harmony', 'Achieved 100 Perfect Chains',
        total(PS + 'TotalFullChainClearCount', 100), 'same counter'),
    ach('points_500m', 'bronze', 'Winding Rhapsody', 'Received five hundred million points',
        flag('MusicStageInfo/TotalScore', 500000000), 'MusicStageInfo/TotalScore (long)'),
    ach('points_5b', 'silver', 'Remnant Träumerei', 'Received five billion points',
        flag('MusicStageInfo/TotalScore', 5000000000), 'MusicStageInfo/TotalScore (long)'),
    ach('items_100', 'bronze', 'Item Enthusiast', 'Used 100 stage items',
        total('ItemInfo/Items[*]/TotalUseCount', 100), 'sum of ItemInfo/Items/<id>/TotalUseCount'),
    ach('abilities_500', 'bronze', 'Skilled Combatant', 'Activated abilities 500 times',
        flag('MusicStageInfo/TotalAbilityCount', 500), 'MusicStageInfo/TotalAbilityCount'),
    ach('abilities_1000', 'silver', 'Combat Artist', 'Activated abilities 1,000 times',
        flag('MusicStageInfo/TotalAbilityCount', 1000), 'MusicStageInfo/TotalAbilityCount'),
    ach('enemies_500', 'bronze', 'Seasoned Hunter', 'Defeated 500 enemies',
        total(KILLS, 500), 'TotalMonsterKillCount of the Standard, Simple and Pair play records'),
    ach('enemies_5000', 'silver', 'Unparalleled Hunter', 'Defeated 5,000 enemies',
        total(KILLS, 5000), 'same counters'),
    ach('lv99_x4', 'bronze', 'Indomitable Band', 'Four characters reached Lv99',
        count('CharacterInfo/Characters[*]/Level', 4, equals=99), 'characters whose Level is 99'),
    ach('star9', 'silver', 'Beyond the Stars', 'One character reached Star Level 9',
        flag('CharacterInfo/Characters[*]/StarLevel', 9), 'largest CharacterInfo/Characters/<id>/StarLevel'),
    ach('rhythmia_10k', 'bronze', 'Making Sound Waves', 'Obtained 10,000 Rhythmia',
        flag('RhythpoInfo/TotalRhythpoValue', 10000), 'RhythpoInfo/TotalRhythpoValue (Rhythmia = "Rhythpo" in the code)'),
    ach('rhythmia_100k', 'silver', 'Riding the Symphonic Tide', 'Obtained 100,000 Rhythmia',
        flag('RhythpoInfo/TotalRhythpoValue', 100000), 'RhythpoInfo/TotalRhythpoValue'),
    ach('cards_300', 'bronze', 'CollectaCard Buff', 'Got 300 different CollectaCards',
        count('CollecaInfo/Collecas[*]/ObtainedFlag', 300), 'CollectaCards ("Colleca") with ObtainedFlag'),
    ach('cards_1000', 'silver', 'CollectaCard Master', 'Got 1,000 different CollectaCards',
        count('CollecaInfo/Collecas[*]/ObtainedFlag', 1000), 'same flags'),
    ach('drops_1000', 'bronze', 'Renowned Treasure Hunter', 'Got 1,000 dropped items',
        total(DROPS, 1000), 'TotalItemGetCount of the Standard, Simple and Pair play records'),
    ach('summonstones_30', 'silver', 'An Excess of Summonstones', 'Got 30 dropped Summonstones',
        flag('SummonStoneInfo/TotalGetCount', 30),
        'SummonStoneInfo/TotalGetCount (may include the ones received from rival ProfiCards: GetCountFromRivalProfica)'),
    ach('quests_10', 'bronze', 'Path of the Harmonist', 'Cleared 10 Series Quest titles',
        count(WORLD + 'Cleared', 10), 'Series Quest worlds with Cleared = true'),
    ach('quest_complete_10', 'bronze', 'A Deeper Harmony', 'Completed quests from 10 Series Quest titles',
        count(WORLD + 'Completed', 10), 'Series Quest worlds with Completed = true'),
    ach('quest_complete_20', 'silver', 'Harmonic Enlightenment', 'Completed quests from 20 Series Quest titles',
        count(WORLD + 'Completed', 20), 'same flags'),
    ach('quest_complete_all', 'gold', 'Complete Harmony', 'Completed all Series Quests',
        count(WORLD + 'Completed', 29),
        '29 worlds with Completed = true: the save has 30 worlds and 50000029 has no stage (Endless World). Check on console'),
    ach('endless_5', 'bronze', 'Endless Challenger', 'Finished at least 5 stages in an Endless World run',
        flag('SeriesQuestInfo/EndlessWorldUniqueInfo/BestStageClearCount', 5), 'SeriesQuestInfo/EndlessWorldUniqueInfo/BestStageClearCount'),
    ach('endless_10', 'silver', 'Endless Walker', 'Finished at least 10 stages in an Endless World run',
        flag('SeriesQuestInfo/EndlessWorldUniqueInfo/BestStageClearCount', 10), 'same value'),
    ach('endless_20', 'gold', 'Endless Ruler', 'Finished at least 20 stages in an Endless World run',
        flag('SeriesQuestInfo/EndlessWorldUniqueInfo/BestStageClearCount', 20), 'same value'),
    ach('top_hits_5', 'bronze', 'Master of the Moment', 'Perfect Chained 5 Top Hits in one day',
        flag('MusicSelectInfo/PickupFullChainCount', 5),
        'MusicSelectInfo/PickupFullChainCount (Top Hits = "Pickup"; reset each day): unlocks if a scan sees 5 before the day changes'),
]

UNIVERSAL = [
    {'id': 't_launch', 'name': 'Curtain Call', 'desc': 'Launch THEATRHYTHM FINAL BAR LINE 10 times', 'points': 10, 'rule': {'type': 'launches', 'target': 10}},
    {'id': 't_hours_3', 'name': 'Opening Number', 'desc': 'Play THEATRHYTHM FINAL BAR LINE for 3 hours', 'points': 10, 'rule': {'type': 'playtime_hours', 'target': 3}},
    {'id': 't_hours_15', 'name': 'Season Ticket', 'desc': 'Play THEATRHYTHM FINAL BAR LINE for 15 hours', 'points': 25, 'rule': {'type': 'playtime_hours', 'target': 15}},
    {'id': 't_hours_40', 'name': 'Grand Finale', 'desc': 'Play THEATRHYTHM FINAL BAR LINE for 40 hours', 'points': 50, 'rule': {'type': 'playtime_hours', 'target': 40}},
]

NOTE = ('The 42 PS4 trophies, read from the encrypted save (reader "theatrhythm": AES-128-CBC + raw DEFLATE + MessagePack). '
        'The save password is not shipped: put it in sdmc:/switch/TrophyNX/keys/theatrhythm.txt (see tools/theatrhythm/README.md). '
        'close_scan false: the decoded musicBackup.inf (~750 KB of MessagePack) does not fit in the live module heap; the app reads it. '
        'Not tracked: THEATRHYTHM Maestro (platinum: no rule type for "every other"), A Musical Journey Begins (which world is the '
        'THEATRHYTHM series is unknown), Overflowing Gold (a filled Critical Chart is not stored as such), Show of Finesse, Unstoppable Force, '
        "Summon Tamer and Summon's True Power (per-song / per-attack values, not saved), 999er (current stats are not saved, only growth), "
        'Grand Rhythmist and Ultimate Rhythmist (need the list of base-game songs and a per-song test across difficulties).')

DEF = {
    'title_id': '010081B01777C000', 'name': 'THEATRHYTHM FINAL BAR LINE', 'reader': 'theatrhythm', 'key_file': 'theatrhythm.txt',
    'close_scan': False, 'save_files': ['backup.inf', 'musicBackup.inf'], '_note': NOTE, 'achievements': TROPHIES + UNIVERSAL,
}

if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', '..', 'romfs', 'games', '010081B01777C000.json')
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(DEF, ensure_ascii=False, indent=1) + '\n')
    print(out, len(TROPHIES), 'trophies +', len(UNIVERSAL), 'universal')
