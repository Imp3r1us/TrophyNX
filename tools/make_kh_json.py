#!/usr/bin/env python3
"""Builds romfs/games/010090C022978000.json: KINGDOM HEARTS HD 1.5+2.5 ReMIX (native Switch version).

The save keeps every slot of a game in one PNG ("KHFM.png", "KHIIFM.png"...): a 0x70-byte header, a table of
0x158-byte entries, then one fixed-size slot per entry from base = 0x70 + count * 0x158 (same as the PC version,
see kikeprime/KH-Save-Editor remix_png.js). The slots are the PS2-era saves, unencrypted, so the binary_le reader
reads them with "slots" (offsets relative to the slot) and "file" (each rule reads its own game's PNG).

Offsets come from kikeprime/KH-Save-Editor (kh1.js, kh2.js and their dicts). Every rule was checked on a real
Switch backup with 40 KH1 and 59 KH2 saves from one playthrough: the values grow in order from slot to slot.

Achievement names and descriptions are the Steam ones (app 2552430). Only the ones the save can prove are listed:
"without a continue", "within 15 hours", gummi scores, Re:Chain of Memories and Birth by Sleep (no save layout
yet) are left out.

    python3 tools/make_kh_json.py
"""
import json
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
DIR = 'KINGDOM HEARTS HD 1.5+2.5 ReMIX/'
KH1, KH2 = DIR + 'KHFM.png', DIR + 'KHIIFM.png'


def h(v):
    return '0x%X' % v


ACHS = []


def add(steam, name, desc, rule, hidden=False, points=10):
    a = {'id': steam.lower(), 'name': name, 'desc': desc, 'points': points, 'steam': steam, 'rule': rule}
    if hidden:
        a['hidden'] = True
    ACHS.append(a)


# ------------------------------------------------------------------ KINGDOM HEARTS FINAL MIX
# World story progress: one byte per world at 0x1500 + index (kh1 dicts/worlds.js world_progress_dict).
KH1_PROG = 0x1500
W1 = {'TT': 0x04, 'DJ': 0x05, 'OC': 0x06, 'WL': 0x07, 'AG': 0x08, 'MO': 0x09, 'AT': 0x0A, 'HT': 0x0C,
      'NL': 0x0D, 'HB': 0x0E, 'EW': 0x0F, 'TT2': 0x12}


def kh1_story(world, value):
    return {'type': 'flag', 'file': KH1, 'offset': h(KH1_PROG + W1[world]), 'size': 1, 'target': value,
            'show_progress': False}


def kh1_value(off, size, target, maxv=0, show=True):
    r = {'type': 'flag', 'file': KH1, 'offset': h(off), 'size': size, 'target': target}
    if maxv:
        r['max'] = maxv
    if not show:
        r['show_progress'] = False
    return r


def kh1_bits(off, n, target):
    return {'type': 'sum', 'file': KH1, 'offsets': [h(off + i) for i in range(n)], 'size': 1, 'bits': True,
            'target': target}


K1 = '[KH FM] '
add('ACH_007', 'He Who Doesn\'t Exist', K1 + 'Defeat the Mysterious Man in Hollow Bastion.',
    kh1_value(0x1118, 1, 1, show=False), hidden=True)
add('ACH_008', 'The Cloaked Shadow', K1 + 'Defeat the Phantom at the Clock Tower.', kh1_story('NL', 0x96), hidden=True)
add('ACH_009', 'The Sandy Blade', K1 + 'Defeat Kurt Zisa in Agrabah.', kh1_story('AG', 0xA0), hidden=True)
for steam, name, off in [('ACH_010', 'Novice Hero', 0x0F36), ('ACH_011', 'Artisan Hero', 0x0F37),
                         ('ACH_012', 'Hero of the Coliseum', 0x0F38), ('ACH_013', 'Coliseum Champion', 0x0F39)]:
    cup = {0x0F36: 'Phil', 0x0F37: 'Pegasus', 0x0F38: 'Hercules', 0x0F39: 'Hades'}[off]
    add(steam, name, K1 + 'Win the %s Cup.' % cup, kh1_value(off, 1, 1, maxv=16, show=False), hidden=True)
add('ACH_014', 'The Frosty Giant', K1 + 'Defeat the Ice Titan in the Gold Match at Olympus Coliseum.',
    kh1_value(0x0F69, 1, 1, maxv=16, show=False), hidden=True)
add('ACH_015', 'One-Winged Angel', K1 + 'Defeat Sephiroth in the Platinum Match at Olympus Coliseum.',
    kh1_value(0x0F6A, 1, 1, maxv=16, show=False), hidden=True)
add('ACH_018', 'Level Master', K1 + 'Get Sora to Level 100.', kh1_value(0x04, 1, 100, maxv=100), hidden=True)
add('ACH_019', 'Treasure Hunter', K1 + 'Open 100 treasure chests.', kh1_bits(0x05CC, 0x1FD, 100), hidden=True)
add('ACH_020', 'From Rags to Riches', K1 + 'Obtain over 10,000 Munny.',
    kh1_value(0x1641C, 4, 10000, maxv=999999), hidden=True)
add('ACH_021', 'Heartless Hunter', K1 + 'Defeat over 2,000 Heartless.', kh1_value(0x0856, 2, 2000), hidden=True)
add('ACH_022', 'Where the Bells Toll', K1 + 'Seal the keyhole in Traverse Town.', kh1_story('TT2', 0x1E))
add('ACH_023', 'The Rabbit Hole', K1 + 'Seal the keyhole in Wonderland.', kh1_story('WL', 0x30))
add('ACH_024', 'Junior Hero', K1 + 'Seal the keyhole in Olympus Coliseum.', kh1_story('OC', 0x3C))
add('ACH_025', 'Member of the Tribe', K1 + 'Seal the keyhole in Deep Jungle.', kh1_story('DJ', 0x5C))
add('ACH_026', 'Magic Lamp', K1 + 'Seal the keyhole in Agrabah.', kh1_story('AG', 0x5A))
add('ACH_027', 'Honest Soul', K1 + 'Escape from Monstro.', kh1_story('MO', 0x46))
add('ACH_028', 'Master of the Seas', K1 + 'Seal the keyhole in Atlantica.', kh1_story('AT', 0x64))
add('ACH_029', 'Pumpkin Prince', K1 + 'Seal the keyhole in Halloween Town.', kh1_story('HT', 0x6A))
add('ACH_030', 'Pixie Dust', K1 + 'Seal the keyhole in Never Land.', kh1_story('NL', 0x64))
add('ACH_031', 'End of the World', K1 + 'Seal the keyhole in Hollow Bastion.', kh1_story('HB', 0xC3), hidden=True)
add('ACH_035', 'Searcher', K1 + 'Collect all Ansem Reports in Jiminy\'s Journal.', kh1_bits(0x19C0, 2, 13))
add('ACH_041', 'First Synthesis', K1 + 'Synthesize an item for the first time.', kh1_bits(0x19C8, 5, 1))
add('ACH_042', 'Synthesis Novice', K1 + 'Synthesize 3 types of items.', kh1_bits(0x19C8, 5, 3))
add('ACH_043', 'Synthesis Amateur', K1 + 'Synthesize 15 types of items.', kh1_bits(0x19C8, 5, 15))
add('ACH_044', 'Synthesis Vet', K1 + 'Synthesize 30 types of items.', kh1_bits(0x19C8, 5, 30))
add('ACH_040', 'Synthesis Master', K1 + 'Synthesize all items.', kh1_bits(0x19C8, 5, 33))
add('ACH_052', 'Oathkeeper', K1 + 'Obtain the Oathkeeper Keyblade.', kh1_story('TT', 0x8C), hidden=True)

# ------------------------------------------------------------------ KINGDOM HEARTS II FINAL MIX
# Story flags: 0x20 bytes per world at 0x1C90 + world * 0x20; flag value v = byte (v >> 4), bit (v & 7)
# (kh2 dicts/worlds.js progress_dict). Difficulty at 0x2498 (0 Beginner, 1 Standard, 2 Proud, 3 Critical).
KH2_PROG, KH2_DIFF = 0x1C90, 0x2498
W2 = {'TT': 0x02, 'HB': 0x04, 'BB': 0x05, 'HE': 0x06, 'AL': 0x07, 'MU': 0x08, 'PO': 0x09, 'LK': 0x0A,
      'LM': 0x0B, 'DC': 0x0C, 'WI': 0x0D, 'NM': 0x0E, 'CA': 0x10, 'TR': 0x11, 'EH': 0x12}


def kh2_off(world, v):
    return KH2_PROG + W2[world] * 0x20 + (v >> 4), 1 << (v & 7)


def kh2_flag(world, v, min_difficulty=None):
    off, mask = kh2_off(world, v)
    r = {'type': 'flag', 'file': KH2, 'offset': h(off), 'size': 1, 'mask': h(mask), 'show_progress': False}
    if min_difficulty:
        r['needs'] = {'offset': h(KH2_DIFF), 'size': 1, 'min': min_difficulty}
    return r


GAME_COMPLETE = ('EH', 0x090)
K2 = '[KH II FM] '
add('ACH_103', 'Critical Competitor', K2 + 'Clear game on Critical.', kh2_flag(*GAME_COMPLETE, min_difficulty=3), hidden=True)
add('ACH_104', 'Proud Player', K2 + 'Clear game on Proud Mode or a higher difficulty level.',
    kh2_flag(*GAME_COMPLETE, min_difficulty=2), hidden=True)
add('ACH_105', 'Ambitious Adventurer', K2 + 'Clear the game and watch the ending sequence.', kh2_flag(*GAME_COMPLETE), hidden=True)
add('ACH_106', 'Level Master', K2 + 'Get Sora to Level 99.',
    {'type': 'flag', 'file': KH2, 'offset': h(0x24F0 + 0x0F), 'size': 1, 'target': 99, 'max': 99})
add('ACH_108', 'Lingering Will', K2 + 'Defeat the Lingering Will.', kh2_flag('DC', 0x042), hidden=True)
for steam, name, cup, v in [('ACH_112', 'Rookie', 'Pain and Panic', 0x076), ('ACH_113', 'Novice Hero', 'Cerberus', 0x080),
                            ('ACH_114', 'Artisan Hero', 'Titan', 0x081), ('ACH_115', 'True Hero', 'Goddess of Fate', 0x084)]:
    add(steam, name, K2 + 'Win the %s Cup.' % cup, kh2_flag('HE', v))
add('ACH_120', 'Summer\'s End', K2 + 'Complete Roxas\'s episode.', kh2_flag('TT', 0x1A0), hidden=True)
for steam, name, what, world, v in [
        ('ACH_121', 'A Timeless World', 'the Timeless River episode', 'WI', 0x047),
        ('ACH_122', 'Above Honor', 'the Land of Dragons episodes', 'MU', 0x017),
        ('ACH_123', 'A Budding Romance', 'the Beast\'s Castle episodes', 'BB', 0x022),
        ('ACH_124', 'Hail the Hero', 'the Olympus Coliseum episodes', 'HE', 0x046),
        ('ACH_125', 'Lifting the Curse', 'the Port Royal episodes', 'CA', 0x0A0),
        ('ACH_126', 'What Friends Are For', 'the Agrabah episodes', 'AL', 0x040),
        ('ACH_127', 'The Gift of Love', 'the Halloween Town episodes', 'NM', 0x067),
        ('ACH_128', 'Return of the King', 'the Pride Lands episodes', 'LK', 0x060),
        ('ACH_129', 'Electric Spark', 'the Space Paranoids episodes', 'TR', 0x063),
        ('ACH_130', 'Always Together', 'the 100 Acre Wood episode', 'PO', 0x092),
        ('ACH_131', 'Kindred Spirits', 'the Atlantica episodes', 'LM', 0x075),
        ('ACH_132', 'A Taste of the Past', 'the Twilight Town episodes', 'TT', 0x193)]:
    add(steam, name, K2 + 'Complete %s.' % what, kh2_flag(world, v))
# Data Organization XIII: one *_RE_CLEAR flag per member, each in its own byte and bit ("masks").
DATA_ORG = [('TT', 0x1B4), ('HB', 0x160), ('HB', 0x161), ('HB', 0x162), ('HB', 0x165), ('HB', 0x167), ('HB', 0x170),
            ('BB', 0x047), ('EH', 0x096), ('EH', 0x097), ('EH', 0x0A0), ('EH', 0x0A1), ('EH', 0x0A2)]
offs = [kh2_off(w, v) for w, v in DATA_ORG]
add('ACH_110', 'To Rule Them All', K2 + 'Defeat all Organization XIII Replica Data.',
    {'type': 'count', 'file': KH2, 'offsets': [h(o) for o, m in offs], 'masks': [h(m) for o, m in offs], 'size': 1,
     'target': 13}, hidden=True)


# ------------------------------------------------------------------ play time (every version, no save needed)
for i, (t, target, name, desc, pts) in enumerate([
        ('launches', 3, 'Dive to the Heart', 'Launch the collection 3 times', 10),
        ('playtime_hours', 10, 'Keyblade Wielder', 'Play for 10 hours', 10),
        ('playtime_hours', 50, 'Hero of Light', 'Play for 50 hours', 25),
        ('playtime_hours', 150, 'Kingdom Hearts', 'Play for 150 hours', 50),
        ('days_played', 7, 'Simple and Clean', 'Play on 7 different days', 10)]):
    ACHS.append({'id': 't_%s_%d' % (t, target), 'name': name, 'desc': desc, 'points': pts,
                 'rule': {'type': t, 'target': target}})


# ------------------------------------------------------------------ output
def main():
    doc = {
        'title_id': '010090C022978000',
        'name': 'KINGDOM HEARTS - HD 1.5+2.5 ReMIX -',
        'reader': 'binary_le',
        'save_files': [KH1, KH2],
        'slots': {
            KH1: {'base': h(0x70 + 200 * 0x158), 'stride': h(0x16C40), 'count': 200,
                  'check': {'offset': h(KH1_PROG), 'size': 1, 'min': 1}},
            KH2: {'base': h(0x70 + 100 * 0x158), 'stride': h(0x10FC0), 'count': 100, 'magic': 'KH2J'},
        },
        '_note': __doc__.strip().split('\n\n')[1].replace('\n', ' '),
        'achievements': ACHS,
    }
    out = os.path.join(ROOT, 'romfs', 'games', '010090C022978000.json')
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
        f.write('\n')
    print(out, len(doc['achievements']), 'achievements')


if __name__ == '__main__':
    main()
