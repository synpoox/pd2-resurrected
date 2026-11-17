"""#1288: a second unique charm of the same name may be picked up, so no unique charm row has a carry1 group (as in PD2).
The rows 5-8 rule (two same-name copies both off, one moved out restores the other, different names both on) is core-rules' charm hook, checked in game."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import data_tables as D  # noqa: E402

CHARMS = {"cm1", "cm2", "cm3"}
PRECRAFTED = "PreCrafted"  # the crafting templates, never carried
# The crafted sunder charms (cs2) keep carry1 1004 on purpose: PD2's own uniqueitems has the same 1004, so they stay one-per-character as in PD2.


class UniqueCharmCarry(unittest.TestCase):
    def setUp(self):
        D.need(self)
        self.charms = [r for r in D.rows("uniqueitems") if r.get("code") in CHARMS and not r["index"].startswith(PRECRAFTED)]

    def test_no_unique_charm_has_a_carry_group(self):
        self.assertTrue(self.charms)
        self.assertEqual([r["index"] for r in self.charms if r["carry1"].strip()], [])

    def test_crafted_sunder_charms_keep_pd2_group_1004(self):
        crafted = [r for r in D.rows("uniqueitems") if r.get("code") == "cs2"]
        self.assertTrue(crafted)
        self.assertEqual({r["carry1"].strip() for r in crafted}, {"1004"})


if __name__ == "__main__":
    unittest.main()
