#!/usr/bin/env python3
"""Generates save-based sets for games whose save is a fixed-layout little-endian file.
Usage: make_binary_sets.py <Ultimate Smasher.py> romfs/games

Sources:
- Smash Ultimate: offsets from CapitanRetraso/Ultimate-Smasher (stats + fighter flags) and
  Marc Robledo's editor (gold). Verified on a real system_data.bin of 5,982,968 bytes.
- Mario Kart 8 Deluxe: offsets from WerWolv/EdiZon_CheatsConfigsAndScripts (Ac_K, mk8.py).
  Not verified on a current save, hence the "max" sanity caps.
"""
import json, os, re, sys

def ach(id, name, desc, pts, rule, **kw):
    d = {"id": id, "name": name, "desc": desc, "points": pts, "rule": rule}
    d.update(kw)
    return d

def val(off, target, size=4, mx=None, **kw):
    r = {"type": "flag", "offset": off, "size": size, "target": target}
    if mx: r["max"] = mx
    r.update(kw)
    return r


def smash(smasher_py):
    src = open(smasher_py, encoding="utf-8").read()
    fighters = [o for _, o in re.findall(r'char_button\("([^"]+)","[^"]+",(0x[0-9A-Fa-f]+)', src)]
    assert len(fighters) == 71, len(fighters)
    F = lambda t: {"type": "count", "offsets": fighters, "size": 1, "max": 1, "target": t}
    CAP = 100000000
    achs = [
        ach("fighters_20", "New Challengers", "Unlock 20 fighters (base roster)", 10, F(20)),
        ach("fighters_45", "Growing Roster", "Unlock 45 fighters (base roster)", 25, F(45)),
        ach("fighters_71", "Everyone Is Here!", "Unlock every base-roster fighter, Mario to Incineroar", 100, F(71)),
        ach("ko_100", "KO Artist", "Score 100 KOs", 10, val("0x540288", 100, mx=CAP)),
        ach("ko_1000", "Smash Master", "Score 1,000 KOs", 25, val("0x540288", 1000, mx=CAP)),
        ach("ko_10000", "Ten Thousand Launches", "Score 10,000 KOs", 50, val("0x540288", 10000, mx=CAP)),
        ach("damage_100k", "Percent Machine", "Deal 100,000% damage in total", 25, val("0x540284", 100000, mx=4000000000)),
        ach("stock_100", "Stock Trader", "Fight 100 stock battles", 10, val("0x54026C", 100, mx=CAP)),
        ach("stock_1000", "Battle Hardened", "Fight 1,000 stock battles", 25, val("0x54026C", 1000, mx=CAP)),
        ach("eight_player", "Eight-Player Smash", "Fight a battle with 8 players", 10, val("0x540278", 1, mx=CAP)),
        ach("classic_1", "Classic Clear", "Clear Classic Mode", 10, val("0x5402E8", 1, mx=CAP)),
        ach("classic_10", "Classic Collector", "Clear Classic Mode 10 times", 25, val("0x5402E8", 10, mx=CAP)),
        ach("classic_50", "Classic Legend", "Clear Classic Mode 50 times", 50, val("0x5402E8", 50, mx=CAP)),
        ach("wol_wins_100", "Spirit Liberator", "Win 100 battles in World of Light", 25, val("0x5402D4", 100, mx=CAP)),
        ach("wol_clear", "World of Light", "Complete World of Light", 100, val("0x5402D8", 1, mx=CAP), hidden=True),
        ach("spirit_summon", "Summoner", "Summon a spirit", 10, val("0x5402C0", 1, mx=CAP)),
        ach("spirit_lv99", "Maxed Out", "Raise a spirit to level 99", 25, val("0x5402CC", 1, mx=CAP)),
        ach("spirit_board_50", "Spirit Board Regular", "Win 50 Spirit Board battles", 25, val("0x5402D0", 50, mx=CAP)),
        ach("online_1", "Going Online", "Fight a solo Quickplay battle online", 10, val("0x540324", 1, mx=CAP)),
        ach("online_wins_100", "Quickplay Champion", "Win 100 solo Quickplay battles", 50, val("0x540328", 100, mx=CAP)),
        ach("elite", "Elite Smash", "Fight an Elite Smash battle", 50, val("0x54033C", 1, mx=CAP)),
        ach("mii", "Mii Maker", "Create a Mii Fighter", 5, val("0x5402FC", 1, mx=CAP)),
        ach("gold_10000", "Gold Hoarder", "Hold 10,000 gold", 10, val("0x5506DC", 10000, mx=CAP)),
    ]
    return {
        "title_id": "01006A800016E000", "name": "Super Smash Bros. Ultimate", "reader": "binary_le",
        "file_size": 5982968, "save_files": ["save_data/system_data.bin", "system_data.bin"], "achievements": achs,
    }


def mk8d():
    C = 10000000   # anything above this means the offset moved
    achs = [
        ach("coins_1000", "Coin Collector", "Collect 1,000 coins", 10, val("0x195C", 1000, mx=C)),
        ach("coins_10000", "Coin Hoarder", "Collect 10,000 coins", 25, val("0x195C", 10000, mx=C)),
        ach("coins_50000", "Golden Kart", "Collect 50,000 coins", 50, val("0x195C", 50000, mx=C)),
        ach("drift_1000", "Drift King", "Drift 1,000 times", 10, val("0x1968", 1000, mx=C)),
        ach("drift_25000", "Sideways Forever", "Drift 25,000 times", 25, val("0x1968", 25000, mx=C)),
        ach("mt_1000", "Mini-Turbo", "Perform 1,000 Mini-Turbos", 10, val("0x1970", 1000, mx=C)),
        ach("smt_500", "Super Mini-Turbo", "Perform 500 Super Mini-Turbos", 25, val("0x1974", 500, mx=C)),
        ach("umt_100", "Ultra Mini-Turbo", "Perform 100 Ultra Mini-Turbos", 25, val("0x1978", 100, mx=C)),
        ach("jump_500", "Trick Shot", "Perform 500 jump boosts", 10, val("0x1964", 500, mx=C)),
        ach("online_race_1", "Worldwide", "Finish an online race", 10, val("0x1EA8", 1, mx=C)),
        ach("online_race_100", "Online Regular", "Finish 100 online races", 25, val("0x1EA8", 100, mx=C)),
        ach("online_gold_1", "Online Winner", "Win an online race", 25, val("0x1EAC", 1, mx=C)),
        ach("online_gold_50", "Online Ace", "Win 50 online races", 50, val("0x1EAC", 50, mx=C)),
        ach("vr_5000", "5,000 VR", "Reach a race rating of 5,000", 25, val("0x1E94", 5000, mx=99999)),
        ach("vr_10000", "10,000 VR", "Reach a race rating of 10,000", 50, val("0x1E94", 10000, mx=99999)),
        ach("balloon_win", "Pop Star", "Win an online Balloon Battle", 10, val("0x1EBC", 1, mx=C)),
        ach("local_race_10", "Couch Racer", "Finish 10 local wireless races", 10, val("0x1F08", 10, mx=C)),
    ]
    return {
        "title_id": "0100152000022000", "name": "Mario Kart 8 Deluxe", "reader": "binary_le",
        "magic": "SUTC", "save_files": ["userdata.dat"], "achievements": achs,
    }


if __name__ == "__main__":
    out = sys.argv[2]
    for doc in (smash(sys.argv[1]), mk8d()):
        path = os.path.join(out, doc["title_id"] + ".json")
        if os.path.exists(path):   # keep the themed play-time goals that were there
            old = json.load(open(path))
            ids = {a["id"] for a in doc["achievements"]}
            doc["achievements"] += [a for a in old["achievements"] if a["id"].startswith("t_") and a["id"] not in ids]
        json.dump(doc, open(path, "w"), indent=1, ensure_ascii=False)
        print(doc["name"], len(doc["achievements"]), "achievements ->", path)
