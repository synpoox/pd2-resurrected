"""Map boss loot follows the map tier (#1423): each map boss rolls the Map Boss TC of the map's tier (T1/T2/T3).

A boss's superuniques.txt row names one TC per difficulty, but the same boss opens maps of every tier, and Cartographer takes
at most 16 bosses.txt entries, so the plugin (core-events map_boss_tc.cpp) swaps the TC at the drop. This pins the data it
swaps over, and the boss x tier -> TC table that results.
"""
import re
import unittest
from pathlib import Path

import data_tables

CARTO = "global/excel/d2rloader/cartographer/"
HEADER = Path(__file__).resolve().parents[2] / "plugin/common/map_boss_tier.h"
TIER_OF_TYPE = {"t1m": 1, "t2m": 2, "t3m": 3}


def key(name):
    """The superuniques.txt key Cartographer writes for a bosses.txt Name (README: `cartoboss:` + the text key)."""
    return "cartoboss:" + "".join(c if c.isalnum() and c.isascii() else f"_{ord(c):02X}" for c in name)


def table(rel):
    lines = [ln.rstrip("\r").split("\t") for ln in data_tables.text(rel).split("\n") if ln.strip()]
    return [dict(zip(lines[0], ln)) for ln in lines[1:]]


class MapBossTier(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)
        self.tc = {r["Treasure Class"]: r for r in data_tables.rows("treasureclassex")}
        self.sup = {r["Superunique"]: r for r in data_tables.rows("superuniques")}
        self.bosses = table(CARTO + "bosses.txt")
        self.maps = table(CARTO + "maps.txt")
        self.type = {r["code"]: r["type"] for r in data_tables.rows("misc")}
        names = re.findall(r'"(Map Boss T\d(?: Blood)?)"', HEADER.read_text(encoding="utf-8"))
        self.plain, self.blood = names[:3], names[3:]

    def expected(self, boss, tier):
        """The TC each difficulty of `boss` rolls in a map of `tier`: the plugin keeps the row's family (plain or Blood)."""
        row = self.sup[key(boss)]
        return {col: (self.blood if row[col] in self.blood else self.plain)[tier - 1] for col in ("TC", "TC(N)", "TC(H)", "TC(H) Desecrated")}

    def test_the_plugin_names_the_three_tiers_in_both_families(self):
        self.assertEqual(self.plain, ["Map Boss T1", "Map Boss T2", "Map Boss T3"])
        self.assertEqual(self.blood, ["Map Boss T1 Blood", "Map Boss T2 Blood", "Map Boss T3 Blood"])

    def test_each_tier_class_drops_its_own_tier_of_maps(self):
        for t in (1, 2, 3):
            drops = [self.tc[f"Map Boss T{t}"][f"Item{n}"] for n in range(1, 11)]
            self.assertIn(f"T{t} Map Drop", drops)
            self.assertEqual([d for d in drops if d.endswith("Map Drop")], [f"T{t} Map Drop"])
            self.assertEqual(self.tc[f"Map Boss T{t} Blood"]["Item1"], f"Map Boss T{t}")

    def test_every_map_boss_row_names_a_class_the_plugin_swaps(self):
        for b in self.bosses:
            row = self.sup[key(b["Name"])]
            for col in ("TC", "TC(N)", "TC(H)", "TC(H) Desecrated"):
                self.assertIn(row[col], self.plain + self.blood, f"{b['Name']} {col}")

    def test_map_tier_is_its_tier_type_and_its_area_level(self):
        for m in self.maps:
            tier = TIER_OF_TYPE[self.type[m["Item"]]]
            self.assertEqual(int(m["Level"]) - 86, tier, m["Name"])

    def test_boss_by_tier_rolls_the_tier_class(self):
        pairs = set()
        for m in self.maps:
            tier = TIER_OF_TYPE[self.type[m["Item"]]]
            pairs.add((m["Boss"], tier))
            got = self.expected(m["Boss"], tier)
            self.assertEqual(got["TC"], f"Map Boss T{tier}", m["Name"])
            self.assertEqual(got["TC(N)"], f"Map Boss T{tier}", m["Name"])
            self.assertEqual(got["TC(H)"], f"Map Boss T{tier} Blood", m["Name"])
            self.assertEqual(got["TC(H) Desecrated"], f"Map Boss T{tier}", m["Name"])
        # a boss opens maps of several tiers, so one row per boss cannot be right for all of them
        self.assertGreater(len(pairs), len(self.bosses))


if __name__ == "__main__":
    unittest.main()
