"""PD2-only monsters with no D2R legacy sprite are hovered by a fixed hit box (#1092)."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOX = {"rathma": "1\t-140\t-40\t80\t140", "zhar": "1\t-140\t-40\t80\t140",
       "demonpillar": "1\t-60\t-20\t40\t70", "Lucion": "1\t-140\t-40\t80\t140"}


class HoverPlateTests(unittest.TestCase):
    def test_hit_box_columns(self):
        rows = (ROOT / "files/data/global/excel/monstats2.txt").read_bytes().decode("latin-1").split("\r\n")
        head = rows[0].split("\t")
        i = head.index("noGfxHitTest")
        self.assertEqual(head[i:i + 5], ["noGfxHitTest", "htTop", "htLeft", "htWidth", "htHeight"])
        found = {f[0]: "\t".join(f[i:i + 5]) for f in (r.split("\t") for r in rows) if f[0] in BOX}
        self.assertEqual(found, BOX)


if __name__ == "__main__":
    unittest.main()
