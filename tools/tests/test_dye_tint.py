"""The red sprite tint for a dyed item (#1529): core-rules' StatDye is itemstatcost's transform_dye, sent to the client."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXCEL = ROOT / "files/data/global/excel"


def rows(name):
    lines = (EXCEL / name).read_text("utf-8-sig").splitlines()
    head = lines[0].split("\t")
    return [dict(zip(head, ln.split("\t"))) for ln in lines[1:]]


class DyeTint(unittest.TestCase):
    def test_stat_id_matches_hook(self):
        src = (ROOT / "plugin/core-rules/item_rules.cpp").read_text("utf-8")
        hook = int(re.search(r"StatDye = (\d+);", src).group(1))
        row = next(r for r in rows("itemstatcost.txt") if r["Stat"] == "transform_dye")
        self.assertEqual(int(row["*ID"]), hook)
        self.assertTrue(row["Send Bits"], "the client never sees a stat with no send bits")

    def test_annihilus_corruptions_dye(self):
        n = sum(1 for r in rows("cubemain.txt") if "Annihilus" in r.get("input 1", "") and "dye" in r.values())
        self.assertGreater(n, 0)

    def test_hd_tint_table_follows_colors_txt(self):
        src = (ROOT / "plugin/core-rules/item_rules.cpp").read_text("utf-8")
        table = re.search(r"DyeRgb\[21\]\[3\] = \{(.*?)\n\};", src, re.S).group(1)
        codes = re.findall(r"// (\w{4})", table)
        self.assertEqual(codes, [r["Code"] for r in rows("colors.txt")])


if __name__ == "__main__":
    unittest.main()
