"""Healers cure poison only while the poison state is curable in states.txt (#1642): a PD2 merge that brings PD2's blank cell back breaks every healer."""
import unittest

import data_tables


class HealerPoisonCure(unittest.TestCase):
    def test_poison_state_is_curable(self):
        data_tables.need(self)
        poison = [r for r in data_tables.rows("states") if r["state"] == "poison"]
        self.assertEqual(len(poison), 1)
        self.assertEqual(poison[0]["curable"], "1", "poison.curable reset: healers stop curing poison (#1642)")


if __name__ == "__main__":
    unittest.main()
