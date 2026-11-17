"""#985: dcso and dcbl drop again through treasure-class data, at the owner's odds."""
import unittest

import data_tables as dt


def tcs():
    return {r["Treasure Class"]: r for r in dt.rows("treasureclassex")}


def items(tc):
    return {tc[f"Item{i}"]: int(tc[f"Prob{i}"]) for i in range(1, 11) if tc.get(f"Item{i}")}


def chance(tc, code):
    """Odds the TC drops `code` at least once, in one kill (picks > 0, NoDrop counted)."""
    it = items(tc)
    total = sum(it.values()) + int(tc["NoDrop"] or 0)
    return 1 - (1 - it[code] / total) ** int(tc["Picks"])


class UberDrops(unittest.TestCase):
    def setUp(self):
        dt.need(self)
        self.t = tcs()

    def test_dcso_on_hell_primes(self):
        for name, odds in (("Mephisto", 21), ("Diablo", 21), ("Baal", 13)):
            for row in (f"{name} (H)", f"{name}q (H)"):
                self.assertAlmostEqual(1 / chance(self.t[row], "dcso"), odds, delta=0.7, msg=row)
            self.assertNotIn("dcso", items(self.t[name]))
            self.assertNotIn("dcso", items(self.t[f"{name} (N)"]))

    def test_dcso_on_hell_desecrated(self):
        for name, odds in (("Mephisto", 26), ("Diablo", 26), ("Baal", 13)):
            rows = [r for k, r in self.t.items() if k.startswith(f"{name} Item (H) Desecrated")]
            self.assertTrue(rows)
            for row in rows:
                self.assertAlmostEqual(1 / chance(row, "dcso"), odds, delta=0.7, msg=row["Treasure Class"])

    def test_dcbl_on_map_bosses(self):
        self.assertAlmostEqual(1 / chance(self.t["Map Blood Drop"], "dcbl"), 9)
        bosses = [r for r in dt.rows("superuniques") if r["Superunique"].startswith("cartoboss:")]
        self.assertEqual(len(bosses), 16)
        for b in bosses:
            wrap = self.t[b["TC(H)"]]
            base = b["TC"]
            self.assertEqual(items(wrap), {base: 1, "Map Blood Drop": 1})
            self.assertEqual(wrap["Picks"], "-2")

    def test_defined_above_use(self):
        order = list(self.t)
        for name in ("Map Boss T1 Blood", "Map Boss T2 Blood", "Map Boss T3 Blood"):
            for it in items(self.t[name]):
                self.assertLess(order.index(it), order.index(name))


if __name__ == "__main__":
    unittest.main()
