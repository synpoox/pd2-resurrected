"""Quest reward items stay usable while misc pSpell is set (#1643): a PD2 merge that brings PD2's blank cells back makes them unusable."""
import unittest

import data_tables

# code: (item, pSpell, spelldescstr), D2R's values
ITEMS = {
    "xyz": ("Potion of Life, Act 3 quest 1", "11", "UsePotionOfLife"),
    "tr2": ("Scroll of Resistance, Act 5 quest 3", "12", "UseScrollOfResistance"),
    "ass": ("Book of Skill, Act 2 quest 1", "10", "UseBookOfSkill"),
}


class QuestRewardItemSpells(unittest.TestCase):
    def test_item_spell_cells(self):
        data_tables.need(self)
        rows = {r["code"]: r for r in data_tables.rows("misc")}
        for code, (what, spell, desc) in ITEMS.items():
            with self.subTest(code=code):
                self.assertEqual(rows[code]["pSpell"], spell, f"{what}: pSpell reset, item unusable (#1643)")
                self.assertEqual(rows[code]["spelldesc"], "1", f"{what}: spelldesc reset (#1643)")
                self.assertEqual(rows[code]["spelldescstr"], desc, f"{what}: spelldescstr reset (#1643)")


if __name__ == "__main__":
    unittest.main()
