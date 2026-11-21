"""#1419: every map's Density/Champions/Uniques/MinCharLevel, and the seven PD2-themed maps' names, monsters and bosses."""
import csv
import json
import unittest
from pathlib import Path

import data_tables

CART = Path(__file__).resolve().parents[2] / "files/data/global/excel/d2rloader/cartographer"
STRINGS = Path(__file__).resolve().parents[2] / "files/data/local/lng"

# item -> (name, monsters, boss, PD2 source row); monsters follow the PD2 area's list, substitutions in the PR
THEMED = {
    "m07": ("River of Blood", "doomknightmap,vilemothermap,zealotmap,oblivionknightmap,deathmaulermap,sk_archermap,regurgitatormap,councilmembermap", "Belial Lord of Lies"),
    "m10": ("Ashen Plains", "sandleapermap2,thornhulkmap2,pantherwomanmap2,mosquitomap2,fetishmap2,slingermap2,deathmaulermap2,cantormap2", "Borq the Consumed"),
    "m12": ("Blood Moon", "hellbovineMap", "The Cow Queen"),
    "m13": ("Ruins of Viz'jun", "cr_lancermap,goatmanmap,cr_archermap,bigheadmap,cantormap,unravelermap,arachmap,thornhulkmap,sandleapermap,mosquitomap,fetishshamanmap", "Karvarousku the Cartographer"),
    "m14": ("Leoric's Passage", "sk_archermap,zealotmap,doomknightmap,oblivionknightmap", "Rotting Horror"),
    "m16": ("Khazra Den", "cr_archermap2,cr_lancermap2,goatmanmap2,fallenmap2", "Indio The Insane"),
    "m18": ("Bastion Keep", "fallenmap2,goatmanmap2,fallenshamanmap2,siegebeastmap2", "Johnathan The Damned"),
    "m17": ("Sewers of Harrogath", "fetishmap2,minionmap2,vilemothermap,fetishshamanmap,bloodlordmap2,mosquitomap2,sandleapermap2,arachmap", "The Mad Philosopher"),
    "m21": ("Ruined Cistern", "cr_archermap2,fetishmap2,minionmap2,mosquitomap2", "Ancient Cistern Hydra"),
    "m25": ("Halls of Torture", "sandleapermap2,zealotmap2,sk_archermap2,siegebeastmap2,bloodlordmap2,councilmembermap2,regurgitatormap2", "Gimli the Summoned One"),
}
# #1599: the owner's density tuning of the 150 base, by map item
DENSITY = {"m02": "300", "m03": "120", "m04": "210", "m07": "300", "m09": "225", "m10": "225", "m11": "210", "m13": "195", "m15": "450", "m22": "300", "m25": "195"}
OLD_NAMES = ("Ruins of Corvus", "Cellar of the Damned", "Dark Temple", "Battlefields of Eternity", "Howling Plateau", "Shadows of Westmarch")


def table(name):
    with open(CART / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


class MapPd2Themes(unittest.TestCase):
    def test_columns_on_every_row(self):
        maps = table("maps.txt")
        self.assertEqual(len(maps), 25)
        for m in maps:
            self.assertEqual((m["Density"], m["Champions"], m["Uniques"], m["MinCharLevel"]), (DENSITY.get(m["Item"], "150"), "100", "100", "80"), m["Name"])

    def test_themed_maps(self):
        by = {m["Item"]: m for m in table("maps.txt")}
        for code, (name, monsters, boss) in THEMED.items():
            self.assertEqual((by[code]["Name"], by[code]["Monsters"], by[code]["Boss"]), (name, monsters, boss), code)

    def test_every_boss_is_an_entry_with_a_superunique_row(self):
        data_tables.need(self)
        bosses = {b["Name"]: b["Base"] for b in table("bosses.txt")}
        self.assertEqual(len(bosses), 21)  # past the old 16-entry cap (d2r-tools #552)
        supers = {r["Superunique"]: r for r in data_tables.rows("superuniques")}
        for m in table("maps.txt"):
            self.assertIn(m["Boss"], bosses, m["Name"])
        for name, base in bosses.items():
            key = "cartoboss:" + "".join(c if c.isalnum() and c.isascii() else "_%02X" % ord(c) for c in name)
            self.assertEqual(supers[key]["Class"], base, name)

    def test_old_names_are_gone_and_item_strings_follow(self):
        data_tables.need(self)
        misc = {r["namestr"]: r["name"] for r in data_tables.rows("misc")}
        for sub in ("strings", "strings-legacy"):
            s = {e["Key"]: e["enUS"] for e in json.loads((STRINGS / sub / "item-names.json").read_text(encoding="utf-8-sig"))}
            for code, (name, _, _) in THEMED.items():
                self.assertEqual(s[code], name + " Map", (sub, code))
                self.assertEqual(misc[code], name + " Map", code)
        names = {m["Name"] for m in table("maps.txt")}
        self.assertFalse(names & set(OLD_NAMES))


if __name__ == "__main__":
    unittest.main()
