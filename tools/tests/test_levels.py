"""pd2_levels on synthetic tables and a synthetic DS1 (no PD2 or D2R data)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_ids as I  # noqa: E402
import pd2_levels as L  # noqa: E402
from tests.test_ids import table  # noqa: E402


class MonPresetTests(unittest.TestCase):
    def test_seed_check_and_merge(self):
        vanilla = dict(L.MONPRESET_VANILLA)
        L.MONPRESET_VANILLA.update({"1": 1, "5": 1})
        try:
            pd2 = {"1": ["gheed", "New1"], "5": ["larzuk", "Boss", "Boss"]}
            d2r = {"1": ["gheed"], "5": ["larzuk", "place_nothing"]}
            lines = L.seed_monpreset(pd2, d2r, [])
            self.assertEqual(lines, [("1/gheed", 0, "gheed"), ("1/New1", 1, "New1"), ("5/larzuk", 0, "larzuk"),
                                     ("5/Boss", 2, "Boss"), ("5/Boss#2", 3, "Boss")])
            L.check_monpreset(lines, pd2, d2r)
            self.assertEqual(L.seed_monpreset(pd2, d2r, lines), [])
            with self.assertRaisesRegex(I.IdError, "not a PD2 row"):  # monpreset rows can't be left out
                L.check_monpreset(lines[:-1] + [("5/Boss#2", I.LEFT_OUT, "Boss")], pd2, d2r)
            with self.assertRaisesRegex(I.IdError, "no index"):
                L.check_monpreset(lines[:-1], pd2, d2r)
            merged = L.merge_monpreset(table("Act|Place\n1|gheed\n5|larzuk\n5|place_nothing\n"), lines)
            self.assertEqual(merged.rows, [["1", "gheed"], ["1", "New1"], ["5", "larzuk"], ["5", "place_nothing"],
                                           ["5", "Boss"], ["5", "Boss"]])
        finally:
            L.MONPRESET_VANILLA.clear()
            L.MONPRESET_VANILLA.update(vanilla)
