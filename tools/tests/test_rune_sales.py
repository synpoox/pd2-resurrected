"""Runes can't be sold (#1777): core-rules refuses any item of the itemtypes `rune` type, so every rune must carry it."""
import csv
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXCEL = ROOT / "files" / "data" / "global" / "excel"


def rows(name: str) -> list[dict]:
    with open(EXCEL / name, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


class RuneSales(unittest.TestCase):
    def test_one_rune_type(self):
        self.assertEqual([r["Code"] for r in rows("itemtypes.txt") if r["Code"] == "rune"], ["rune"])

    def test_every_rune_has_the_rune_type(self):
        runes = [r for r in rows("misc.txt") if re.fullmatch(r"r\d\d", r["code"] or "")]
        self.assertEqual(len(runes), 33)
        self.assertEqual([r["code"] for r in runes if r["type"] != "rune"], [])

    def test_the_hook_looks_the_type_up_by_code(self):
        src = (ROOT / "plugin" / "core-rules" / "item_prices.cpp").read_text(encoding="utf-8")
        self.assertIn("'r' | 'u' << 8 | 'n' << 16 | 'e' << 24", src)
        self.assertIn("TableId::ItemTypes", src)


if __name__ == "__main__":
    unittest.main()
