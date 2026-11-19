#!/usr/bin/env python3
"""Generates themed play-log achievement sets (romfs/games/<TITLEID>.json) for games whose
save format isn't supported yet. Each set: one launch goal + three play-time goals, scaled
to the game's typical length.  Usage: make_playtime_sets.py romfs/games"""
import json, os, sys

# length class -> (launch target, hour targets)
CLASSES = {
    "short":  (5,  [1, 4, 8]),       # ~5-10 h games
    "medium": (10, [3, 15, 40]),    # ~15-30 h games
    "long":   (20, [5, 30, 75]),    # long RPGs / open worlds
    "party":  (15, [2, 15, 30]),    # replayable / multiplayer / roguelites
}

# title id, name, class, launch-title, [three hour-titles]
GAMES = [
    ("0100000000010000", "Super Mario Odyssey", "medium", "Cappy's Companion", ["Bonjour, Bonneton", "Kingdom Hopper", "Moon Collector"]),
    ("0100152000022000", "Mario Kart 8 Deluxe", "party", "Grand Prix Regular", ["Rookie Racer", "Drift Master", "Rainbow Road Veteran"]),
    ("010019A01E2F2000", "Metroid Prime 4: Beyond", "medium", "Bounty Hunter", ["Visor Online", "Psychic Explorer", "Beyond Viewros"]),
    ("01001F5010DFA000", "Pokémon Legends: Arceus", "long", "Survey Corps Member", ["Hisui Newcomer", "Pokédex Researcher", "Galaxy Expedition Legend"]),
    ("010026300BA4A000", "Slay the Spire", "party", "One More Run", ["Deckbuilder", "Spire Climber", "Heart Breaker"]),
    ("01002E700C366000", "COCOON", "short", "Worlds Within Worlds", ["Hatchling", "Orb Carrier", "World Weaver"]),
    ("0100307018934000", "SIGNALIS", "short", "Replika Reactivated", ["Lost Signal", "Sierpinski Survivor", "Promise Kept"]),
    ("010040502453E000", "Vampire Crawlers", "party", "Wildcard Dealer", ["First Crawl", "Dungeon Gambler", "Turbo Crawler"]),
    ("010048701995E000", "Luigi's Mansion 2 HD", "medium", "Ghost Hunter on Call", ["Poltergust Trainee", "Gloomy Manor Regular", "E. Gadd's Best Assistant"]),
    ("01004D300C5AE000", "Kirby and the Forgotten Land", "medium", "Waddle Dee Rescuer", ["Mouthful Mode", "Forgotten Explorer", "Star-Crossed Hero"]),
    ("01004D501F078000", "The Lord of the Rings: War in the North", "medium", "Fellowship of the North", ["Ranger of the North", "Orc Slayer", "Hero of the North"]),
    ("01005CA01580E000", "Persona 5 Royal", "long", "Back to Shujin", ["Phantom Thief Recruit", "Mementos Regular", "Take Your Time"]),
    ("0100633007D48000", "Hollow Knight", "medium", "Rest at the Bench", ["Hallownest Wanderer", "Path of Pain Seeker", "Void Embraced"]),
    ("010065A014024000", "GTA: San Andreas – The Definitive Edition", "long", "Grove Street, Home", ["Ah Shit, Here We Go Again", "Los Santos Local", "San Andreas Legend"]),
    ("0100698009C6E000", "Blasphemous", "medium", "Penitent Returns", ["Guilt Bearer", "Pilgrim of Cvstodia", "Mea Culpa"]),
    ("01006A800016E000", "Super Smash Bros. Ultimate", "party", "Challenger Approaching", ["Fighter", "World of Light", "Smash Legend"]),
    ("01006BB00C6F0000", "The Legend of Zelda: Link's Awakening", "short", "Wake Up, Link", ["Mabe Village Guest", "Instrument Collector", "Wind Fish Dreamer"]),
    ("01006BB0203CA000", "Wizard of Legend 2", "party", "Arcana Apprentice", ["Spell Slinger", "Chaos Trials Regular", "Legendary Wizard"]),
    ("01006FD0080B2000", "Overcooked! 2", "party", "Kitchen Reopened", ["Line Cook", "Sous Chef", "Onion King's Favourite"]),
    ("01006FE013472000", "Mario Party Superstars", "party", "Party Starter", ["Minigame Rookie", "Star Hunter", "Superstar"]),
    ("010086A022444000", "BALL x PIT", "party", "Back in the Pit", ["Ball Breaker", "Pit Builder", "Bottom of the Pit"]),
    ("010089A0197E4000", "Vampire Survivors", "party", "Night Shift", ["Survivor", "Whip It Good", "Thirty Minutes, Every Time"]),
    ("01008CF01BAAC000", "The Legend of Zelda: Echoes of Wisdom", "medium", "Tri Rod Ready", ["Echo Maker", "Rift Closer", "Wisdom Restored"]),
    ("010092A0172E4000", "It Takes Two", "medium", "Better Together", ["Cody & May", "Book of Love", "It Took Two"]),
    ("010093801237C000", "Metroid Dread", "short", "Wake Up, Samus", ["E.M.M.I. Escapee", "ZDR Explorer", "Speedrun Material"]),
    ("0100965017338000", "Super Mario Party Jamboree", "party", "Jamboree Guest", ["Board Hopper", "Party Planner", "Jamboree Legend"]),
    ("010097F018538000", "DAVE THE DIVER", "medium", "Dive Shift", ["Blue Hole Diver", "Sushi Bar Regular", "Master of the Deep"]),
    ("010099C022B96000", "Super Mario Galaxy", "medium", "Launch Star", ["Comet Observatory", "Star Bit Collector", "Galactic Hero"]),
    ("0100A1C00359C000", "TowerFall", "party", "Arrows Ready", ["Archer", "Sacred Ground Regular", "Tower Champion"]),
    ("0100A3900C3E2000", "Paper Mario: The Origami King", "medium", "Unfolded", ["Streamer Chaser", "Confetti Thrower", "Origami Undone"]),
    ("0100AD10102B2000", "BioShock: The Collection", "long", "Would You Kindly", ["Rapture Arrival", "Columbia Tourist", "A Man Chooses"]),
    ("0100C9A00ECE6000", "Nintendo 64 – Nintendo Classics", "party", "Blow on the Cartridge", ["Rumble Pak Ready", "Retro Session", "N64 Archivist"]),
    ("0100D7C01F254000", "Dragon Quest I & II HD-2D Remake", "long", "Descendant of Erdrick", ["Slime Slayer", "Hero of Alefgard", "Legend Reborn"]),
    ("0100D810170D0000", "Nine Sols", "medium", "Wake of Yi", ["Taopunk Rookie", "Parry Master", "Solarian Hunter"]),
    ("0100D870045B6000", "NES – Nintendo Switch Online", "party", "Press Start, 1985", ["8-Bit Session", "Retro Regular", "NES Archivist"]),
    ("0100DCA0064A6000", "Luigi's Mansion 3", "medium", "Last Resort Check-In", ["Bellhop", "Floor Clearer", "Hotel Manager"]),
    ("0100F43008C44000", "Pokémon Legends: Z-A", "long", "Lumiose Arrival", ["Wild Zone Rookie", "Mega Evolution", "Lumiose Legend"]),
    ("0100FCF002A58000", "Ultimate Chicken Horse", "party", "Party Box Opened", ["Platform Builder", "Trap Master", "Ultimate Chicken"]),
]


def build(tid, name, cls, launch_title, hour_titles):
    launches, hours = CLASSES[cls]
    achs = [{
        "id": "t_launch", "name": launch_title, "desc": f"Launch {name} {launches} times", "points": 10,
        "rule": {"type": "launches", "target": launches},
    }]
    for i, (h, title) in enumerate(zip(hours, hour_titles)):
        achs.append({
            "id": f"t_hours_{h}", "name": title,
            "desc": f"Play {name} for {h} hour{'s' if h > 1 else ''}",
            "points": [10, 25, 50][i],
            "rule": {"type": "playtime_hours", "target": h},
        })
    return {"title_id": tid, "name": name, "reader": "", "save_files": [], "achievements": achs}


if __name__ == "__main__":
    out = sys.argv[1]
    for tid, name, cls, lt, ht in GAMES:
        path = os.path.join(out, tid + ".json")
        if os.path.exists(path) and json.load(open(path)).get("reader"):
            print("skip (has a save-based set):", name)
            continue
        json.dump(build(tid, name, cls, lt, ht), open(path, "w"), indent=1, ensure_ascii=False)
    print(len(GAMES), "games")
