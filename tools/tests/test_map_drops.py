"""Map drops (#1307): PD2's Map Tier / T1-T3 Map Drop treasure classes over m01-m25; maps stack to 50 (a core-rules hook merges only normal maps with equal stats, #1326)."""
import unittest

import data_tables

TIERS = {"t1m": "Map Tier 1", "t2m": "Map Tier 2", "t3m": "Map Tier 3"}
DROPS = ("T1 Map Drop", "T2 Map Drop", "T3 Map Drop")


class MapDrops(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)
        self.tc = {r["Treasure Class"]: r for r in data_tables.rows("treasureclassex")}

    def items(self, name):
        return [(r, r[f"Prob{n}"]) for r in [self.tc[name]] for n in range(1, 11) if r[f"Item{n}"]]

    def test_map_tier_classes_list_each_map_of_the_tier_once(self):
        for typ, name in TIERS.items():
            want = sorted(r["code"] for r in data_tables.rows("misc") if r["type"] == typ)
            got = sorted(r[f"Item{n}"] for r in [self.tc[name]] for n in range(1, 11) if r[f"Item{n}"])
            assert want and got == want, name

    def test_drop_classes_use_pd2_weights(self):
        def w(name):
            r = self.tc[name]
            return [(r[f"Item{n}"], int(r[f"Prob{n}"])) for n in range(1, 11) if r[f"Item{n}"]]
        assert w("T1 Map Drop") == [("Map Tier 1", 66), ("Map Tier 2", 33)]
        assert w("T2 Map Drop") == [("Map Tier 1", 39), ("Map Tier 2", 40), ("Map Tier 3", 20)]
        assert w("T3 Map Drop") == [("Map Tier 1", 32), ("Map Tier 2", 33), ("Map Tier 3", 33)]

    def test_in_map_classes_call_the_tier_drop(self):
        for t, d in enumerate(DROPS, 1):
            r = self.tc[f"Map Good t{t}"]
            assert d in [r[f"Item{n}"] for n in range(1, 11)], r["Treasure Class"]
        for name, d in (("Map Boss T1", DROPS[0]), ("Map Boss T2", DROPS[1]), ("Map Boss T3", DROPS[2]), ("Map Boss T4", DROPS[2])):
            assert d in [self.tc[name][f"Item{n}"] for n in range(1, 11)], name

    def test_maps_stack_to_50(self):
        maps = [r for r in data_tables.rows("misc") if r["type"] in TIERS]
        assert len(maps) == 25
        for r in maps:
            assert (r["stackable"], r["minstack"], r["maxstack"], r["spawnstack"]) == ("1", "1", "50", "1"), r["code"]


if __name__ == "__main__":
    unittest.main()
