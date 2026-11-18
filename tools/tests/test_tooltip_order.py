"""#1345 #1346: Corrupted first, then Area Level, density, magic find, experience, then the rest; Corrupted's string resets the colour."""
import json
import unittest

import data_tables

ORDER = ["corrupted", "cartographer_map_level", "cartographer_map_density", "item_magicbonus", "item_addexperience"]


class TooltipOrder(unittest.TestCase):
    def test_priorities_descend(self):
        data_tables.need(self)
        pri = {r["Stat"]: int(r["descpriority"] or 0) for r in data_tables.rows("itemstatcost")}
        got = [pri[s] for s in ORDER]
        self.assertEqual(got, sorted(set(got), reverse=True), got)
        # every other Cartographer stat sits below experience
        for s, p in pri.items():
            if s.startswith("cartographer_") and s not in ORDER:
                self.assertLess(p, pri["item_addexperience"], s)

    def test_nothing_ties_or_beats_corrupted(self):
        data_tables.need(self)
        rows = data_tables.rows("itemstatcost")  # a stat with no descstrpos shows nothing (item_shiny_appearance)
        pri = {r["Stat"]: int(r["descpriority"] or 0) for r in rows if r["descstrpos"]}
        self.assertEqual(pri["corrupted"], 255)
        self.assertEqual([s for s, p in pri.items() if p >= 255 and s != "corrupted"], [])

    def test_corrupted_resets_colour(self):
        data_tables.need(self)
        for d in ("strings", "strings-legacy"):
            rows = json.loads(data_tables.text(f"local/lng/{d}/item-modifiers.json").encode("latin-1").decode("utf-8-sig"))
            for r in rows:
                if r["Key"] == "StrCorrupted":
                    self.assertTrue(r["enUS"].startswith("\u00ffc1Corrupted") and r["enUS"].endswith("\u00ffc3"), (d, r["enUS"]))


if __name__ == "__main__":
    unittest.main()
