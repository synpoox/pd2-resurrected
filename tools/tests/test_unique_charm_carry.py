"""#358: PD2's one-unique-charm rule is data: every unique charm row has its own carry1 group (a second copy is refused)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import data_tables as D  # noqa: E402

CHARMS = {"cm1", "cm2", "cm3"}
PRECRAFTED = "PreCrafted"  # the crafting templates, never carried


class UniqueCharmCarry(unittest.TestCase):
    def setUp(self):
        D.need(self)
        self.charms = [r for r in D.rows("uniqueitems") if r.get("code") in CHARMS and not r["index"].startswith(PRECRAFTED)]

    def test_every_unique_charm_has_a_group(self):
        self.assertTrue(self.charms)
        self.assertEqual([r["index"] for r in self.charms if not r["carry1"].strip()], [])

    def test_groups_are_one_per_row(self):
        groups = [r["carry1"] for r in self.charms]
        self.assertEqual(len(groups), len(set(groups)))

    def test_d2r_groups_kept(self):
        by = {r["index"]: r["carry1"] for r in self.charms}
        self.assertEqual((by["Gheed's Fortune"], by["Annihilus"], by["Hellfire Torch"]), ("1001", "1002", "1003"))


if __name__ == "__main__":
    unittest.main()
