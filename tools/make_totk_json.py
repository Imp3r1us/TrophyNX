#!/usr/bin/env python3
"""Generates romfs/games/0100F2C0115B6000.json (TotK) from the editor's completion lists."""
import json, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from totk_extract import extract

L = extract(sys.argv[1])
H = lambda k: ['0x%08x' % h for h in L[k]]
SH = {"type": "count", "pattern": "DungeonState.Dungeon%03d", "from": 0, "to": 151, "equals": "Clear"}
def a(id, name, desc, pts, rule, **kw):
    d = {"id": id, "name": name, "desc": desc, "points": pts, "rule": rule}; d.update(kw); return d
def cnt(key, target, **kw):
    r = {"type": "count", "names": H(key), "target": target}; r.update(kw); return r

achs = [
    a("shrine_1", "Light of Blessing", "Complete your first shrine", 10, dict(SH, target=1)),
    a("shrine_50", "Shrine Seeker", "Complete 50 shrines", 25, dict(SH, target=50)),
    a("shrine_100", "Shrine Devotee", "Complete 100 shrines", 50, dict(SH, target=100)),
    a("shrine_152", "Rauru's Blessing", "Complete all 152 shrines", 100, dict(SH, target=152)),
    a("tower_1", "Skyview", "Activate a Skyview Tower", 10, cnt("TOWERS_ACTIVATED", 1)),
    a("tower_15", "Cartographer", "Activate all 15 Skyview Towers", 25, cnt("TOWERS_ACTIVATED", 15)),
    a("lightroot_1", "Into the Depths", "Light up your first Lightroot", 10, cnt("LIGHTROOTS_STATUS", 1, equals="Open")),
    a("lightroot_60", "Depths Explorer", "Light up 60 Lightroots", 25, cnt("LIGHTROOTS_STATUS", 60, equals="Open")),
    a("lightroot_120", "Light in the Dark", "Light up all 120 Lightroots", 100, cnt("LIGHTROOTS_STATUS", 120, equals="Open")),
    a("korok_hidden_100", "Ya-ha-ha!", "Find 100 hidden Koroks", 25, cnt("KOROKS_HIDDEN", 100)),
    a("korok_hidden_800", "Every Last Korok", "Find all 800 hidden Koroks", 100, cnt("KOROKS_HIDDEN", 800)),
    a("korok_carry_10", "Korok Taxi", "Reunite 10 lost Koroks with their friends", 10, cnt("KOROKS_CARRY", 10, equals="Clear")),
    a("korok_carry_100", "Friends Reunited", "Reunite all 100 lost Koroks", 50, cnt("KOROKS_CARRY", 100, equals="Clear")),
    a("cave_25", "Spelunker", "Visit 25 caves", 10, cnt("LOCATION_CAVES_VISITED", 25)),
    a("cave_147", "Underground Expert", "Visit all 147 caves", 50, cnt("LOCATION_CAVES_VISITED", 147)),
    a("well_58", "Well Wisher", "Visit all 58 wells", 25, cnt("LOCATION_WELLS_VISITED", 58)),
    a("bubbul_1", "Bubbul Buster", "Defeat a Bubbulfrog", 10, cnt("BUBBULS_DEFEATED", 1)),
    a("bubbul_147", "Gem Hoarder", "Defeat all 147 Bubbulfrogs", 50, cnt("BUBBULS_DEFEATED", 147)),
    a("hinox_1", "Giant Problem", "Defeat a Hinox", 10, cnt("BOSSES_HINOXES_DEFEATED", 1)),
    a("talus_1", "Stone Cold", "Defeat a Stone Talus", 10, cnt("BOSSES_TALUSES_DEFEATED", 1)),
    a("molduga_4", "Sand Shark Hunter", "Defeat all 4 Moldugas", 25, cnt("BOSSES_MOLDUGAS_DEFEATED", 4)),
    a("flux_1", "Construct Crusher", "Defeat a Flux Construct", 10, cnt("BOSSES_FLUX_CONSTRUCT_DEFEATED", 1)),
    a("frox_1", "Frox Hunter", "Defeat a Frox", 10, cnt("BOSSES_FROXS_DEFEATED", 1)),
    a("gleeok_1", "Dragon Slayer", "Defeat a Gleeok", 25, cnt("BOSSES_GLEEOKS_DEFEATED", 1)),
    a("gleeok_all", "Gleeok Nemesis", "Defeat all 14 Gleeoks", 100, cnt("BOSSES_GLEEOKS_DEFEATED", 14)),
    a("schematic_12", "Ancient Engineer", "Find all 12 Schematic Stones", 25, cnt("SCHEMATICS_STONE_FOUND", 12)),
    a("location_100", "Sightseer", "Discover 100 surface locations", 10, cnt("LOCATIONS_VISITED", 100)),
    a("location_374", "Hyrule Cartographer", "Discover all 374 surface locations", 100, cnt("LOCATIONS_VISITED", 374)),
    a("chasm_10", "Into the Abyss", "Visit 10 chasms", 10, cnt("LOCATION_CHASMS_VISITED", 10)),
    a("yiga_34", "Yiga Blueprint Thief", "Find all 34 Yiga schematics", 50, cnt("SCHEMATICS_YIGA_FOUND", 34)),
    a("treasure_map_31", "Treasure Hunter", "Find all 31 old maps", 50, cnt("TREASURE_MAPS_FOUND", 31)),
    a("rematch_12", "Rematch Champion", "Win all 12 boss rematches", 100, cnt("BOSSES_REMATCH_DEFEATED", 12)),
    a("hinox_all", "Hinox Hunter", "Defeat every Hinox", 50, cnt("BOSSES_HINOXES_DEFEATED", len(L["BOSSES_HINOXES_DEFEATED"]))),
    a("talus_all", "Talus Breaker", "Defeat every Stone Talus", 50, cnt("BOSSES_TALUSES_DEFEATED", len(L["BOSSES_TALUSES_DEFEATED"]))),
    a("hearts_20", "Big Hearted", "Reach 20 heart containers", 25, {"type": "flag", "hash": "0xfbe01da1", "target": 80, "divisor": 4}),
    a("stamina_3", "Endless Endurance", "Fill all three stamina wheels", 25, {"type": "flag", "hash": "0xf9212c74", "float": True, "target": 3000, "divisor": 1000}),
    a("rupees_10000", "Hyrule Tycoon", "Hold 10,000 rupees", 25, {"type": "flag", "hash": "0xa77921d7", "target": 10000}),
]
doc = {
    "title_id": "0100F2C0115B6000",
    "name": "The Legend of Zelda: Tears of the Kingdom",
    "reader": "totk_progress",
    "save_files": ["slot_0%d/progress.sav" % i for i in range(6)],
    "achievements": achs,
}
json.dump(doc, open(sys.argv[2], 'w'), indent=1)
print(len(achs), 'achievements')
