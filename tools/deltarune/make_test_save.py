#!/usr/bin/env python3
"""Synthetic DELTARUNE (Switch) saves for the "deltarune" reader tests. Nothing from the game: the files only
follow the layout of scr_saveprocess (console branch) with made-up values.

  python3 tools/deltarune/make_test_save.py OUTDIR
writes OUTDIR/deltarune.sav (some trophies, see EXPECTED) and OUTDIR/deltarune_all.sav (every trophy, CONNECTION).
Used by host/test/test_deltarune.cpp, tools/savelab/test/test_formats.js and host/test/gen_saves.py.
"""
import json, os, struct, sys


def ds_list(values):
    """GameMaker ds_list_write: hex of u32 0x12F, u32 count, per entry u32 type (0 real, 1 string) + value."""
    b = struct.pack('<II', 0x12F, len(values))
    for v in values:
        if isinstance(v, str):
            e = v.encode('utf-8')
            b += struct.pack('<II', 1, len(e)) + e
        else:
            b += struct.pack('<Id', 0, float(v))
    return b.hex().upper()


def num(v):
    """GameMaker string(real): integers without decimals, else 2 decimals."""
    return str(int(v)) if float(v).is_integer() else '%.2f' % v


def chapter_file(chapter, name='Kris', gold=0, lv=1, chararmor1=None, chararmor2=None, charweapon=None,
                 weapon=None, armor=None, flags=None, plot=0, room=1, time=0, lweapon=2, larmor=1):
    one = chapter == 1
    n = 4 if one else 5                       # party list size (index = character)
    inv = 13 if one else 48                   # weapon / armor inventory
    nflags = 9999 if one else 2500
    fl = [0] * nflags
    for k, v in (flags or {}).items():
        fl[k] = v
    lines = [name, ds_list([''] * 6), num(1), num(2), num(0), num(gold), num(0), num(lv), num(0), num(12), num(1)]
    pad = lambda a: (list(a or []) + [0] * n)[:n]
    lines += [ds_list(pad([0, 90, 110, 70])), ds_list(pad([0, 90, 110, 70])), ds_list(pad([0, 10, 14, 8])),
              ds_list(pad([0, 2, 2, 2])), ds_list(pad([0, 0, 0, 7])), ds_list(pad([0, 0, 0, 0])),
              ds_list(pad(charweapon or [0, 2, 3, 4])), ds_list(pad(chararmor1)), ds_list(pad(chararmor2)),
              ds_list(pad([0, 0, 1, 2]))]
    lines += [num(0)] * ((4 * (4 * 8 + 12)) if one else (5 * (4 * 10 + 12)))   # item stats + spells
    lines += [num(3), num(1), num(1)]                                          # boltspeed, grazeamt, grazesize
    lines += [ds_list([0] * 13), ds_list([0] * 13), ds_list((list(weapon or []) + [0] * inv)[:inv]),
              ds_list((list(armor or []) + [0] * inv)[:inv])]
    if not one:
        lines.append(ds_list([0] * 72))                                        # pocketitem
    lines += [num(0), num(250), num(lweapon), num(larmor), num(0), num(1), num(2), num(20), num(20), num(10), num(10),
              num(0), num(0)]
    lines += [ds_list([0] * 8), ds_list([0] * 8), ds_list(fl), num(plot), num(room), num(time)]
    return '\r\n'.join(lines) + '\r\n'


def unknown_layout_file(flags):
    """A file from an unknown game version: only the flag list (2500) and plot/room/time are found."""
    fl = [0] * 2500
    for k, v in flags.items():
        fl[k] = v
    return '\r\n'.join(['Kris', num(5), ds_list([1, 2]), ds_list(fl), num(300), num(42), num(99.5)]) + '\r\n'


DR_INI = '[G0]\r\nName="Kris"\r\nLevel="1.000000"\r\nTime="9000.000000"\r\n[G_2_0]\r\nName="Kris"\r\n[URA]\r\n1_0="1.000000"\r\n'
BASE = 2400

# trophies of deltarune.sav: patch flags 5 (PAIN 1) and 23 (TREASUREHUNTER); fallbacks 1 (chapter 1 completed),
# 9 (moss, flag 106), 14 (armor in a chapter 1 file), 18 (flag 325 = 1), 22 (flag 1220), 27 (ThornRing, weapon 13)
EXPECTED = {
    'trophy': sorted([1, 5, 9, 14, 18, 22, 23, 27]),
    'patch': [5, 23],
    'completed': [1],
}


def save_partial():
    files = {
        'filech1_0': chapter_file(1, gold=30, chararmor1=[0, 1, 0, 0], flags={106: 1, 50: 4, BASE + 5: 1}, plot=40, room=10, time=9000),
        'filech1_3': chapter_file(1, gold=80, flags={106: 1}, plot=250, room=280, time=36000),
        'filech2_0': chapter_file(2, gold=1997, weapon=[13, 5], flags={325: 1, BASE + 23: 1}, plot=60, time=50000.5),
        'filech3_1': chapter_file(3, flags={1220: 1, 325: 2}, plot=10),
        'filech5_0_b': chapter_file(5, name='Kris', plot=5),
        'dr.ini': DR_INI,
        'config_0.ini': '[LANG]\r\nLANG="en"\r\n',
        'keyconfig_0': 'not an ini',
    }
    return files


def save_all():
    # every trophy 1..29 from the patch, spread over two files, one of them in an unknown layout
    files = {
        'filech4_0': chapter_file(4, flags={BASE + i: 1 for i in range(1, 15)}, plot=100),
        'filech4_1': unknown_layout_file({BASE + i: 1 for i in range(15, 30)}),
    }
    return files


def encode(files):
    return json.dumps(files, separators=(',', ':')).encode('utf-8') + b'\0'


def main(out):
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, 'deltarune.sav'), 'wb').write(encode(save_partial()))
    open(os.path.join(out, 'deltarune_all.sav'), 'wb').write(encode(save_all()))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
