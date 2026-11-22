"""Every Materials/Gems/Runes tab cell is a stackable misc.txt item; Worldstone Shard, Tainted Worldstone Shard and the
craft infusions have one (#1725)."""
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import data_tables as D  # noqa: E402

LAYOUTS = Path(__file__).resolve().parents[2] / "files" / "data" / "global" / "ui" / "layouts"
NEW = ["wss", "cwss", "crfb", "crfc", "crfs", "crfh", "crfv", "crfu", "crfp"]


def cell_codes(path: Path) -> list[str]:
    return re.findall(r'"itemCode":\s*"(\w+)"', path.read_text(encoding="utf-8"))


class MaterialsTab(unittest.TestCase):
    def setUp(self):
        D.need(self)
        self.misc = {r["code"]: r for r in D.rows("misc") if r.get("code")}

    def test_every_itemcode_in_the_file_is_a_stackable_item_once(self):
        for name in ("bankexpansionlayouthd.json", "controller/bankexpansionlayouthd.json"):
            codes = cell_codes(LAYOUTS / name)
            self.assertEqual(len(codes), len(set(codes)), name)
            for code in codes:
                self.assertEqual(self.misc[code]["AdvancedStashStackable"], "1", f"{name} {code}")

    def test_shards_and_infusions_have_a_cell(self):
        for name in ("bankexpansionlayouthd.json", "controller/bankexpansionlayouthd.json"):
            self.assertTrue(set(NEW) <= set(cell_codes(LAYOUTS / name)), name)


if __name__ == "__main__":
    unittest.main()
