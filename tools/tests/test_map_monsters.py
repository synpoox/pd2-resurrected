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
            self.assertTrue(1 <= len(ids) <= 10, m["Name"])
            self.assertEqual(len(set(ids)), len(ids), m["Name"])
            for i in ids:
                r = mon.get(i)
                self.assertIsNotNone(r, f"{m['Name']}: {i} is not a monstats Id")
                self.assertEqual((r["enabled"], r["isSpawn"]), ("1", "1"), i)
                self.assertNotEqual(r["Rarity"], "0", i)
                self.assertTrue(i.lower().endswith(("map", "map2")), i)
                for col in ("TreasureClass(H)", "TreasureClassChamp(H)", "TreasureClassUnique(H)"):
                    self.assertIn(r[col], tcs, f"{i} {col}")
                    self.assertTrue(r[col].startswith("Map "), f"{i} {col}")
