"""Map monsters (#1322): every maps.txt Monsters entry is a spawning monstats row with map treasure classes."""
import unittest

import data_tables


class MapMonsters(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_monsters_exist_spawn_and_drop_map_tcs(self):
        mon = {r["Id"]: r for r in data_tables.rows("monstats")}
        tcs = {r["Treasure Class"] for r in data_tables.rows("treasureclassex")}
        text = data_tables.text("global/excel/d2rloader/cartographer/maps.txt")
        lines = [ln.rstrip("\r").split("\t") for ln in text.split("\n") if ln.strip()]
        head = lines[0]
        for row in lines[1:]:
            self.assertEqual(len(row), len(head), row[0])
            m = dict(zip(head, row))
            ids = m["Monsters"].split(",")
            self.assertTrue(1 <= len(ids) <= 13, m["Name"])
            self.assertEqual(len(set(ids)), len(ids), m["Name"])
            for i in ids:
                r = mon.get(i)
                self.assertIsNotNone(r, f"{m['Name']}: {i} is not a monstats Id")
                self.assertEqual((r["enabled"], r["isSpawn"]), ("1", "1"), i)
                self.assertNotEqual(r["Rarity"], "0", i)
                for col in ("TreasureClass(H)", "TreasureClassChamp(H)", "TreasureClassUnique(H)"):
                    self.assertIn(r[col], tcs, f"{i} {col}")
                    self.assertTrue(r[col].startswith("Map "), f"{i} {col}")

    def test_minions_share_the_map_tier(self):
        mon = {r["Id"]: r for r in data_tables.rows("monstats")}
        for i, r in mon.items():
            if not i.endswith(("map", "map2")):
                continue
            for col in ("minion1", "minion2"):
                m = r[col]
                if m.endswith(("map", "map2")):
                    self.assertIn(m, mon, f"{i} {col} {m}")
                    self.assertEqual(m.endswith("map2"), i.endswith("map2"), f"{i} {col} {m}")
