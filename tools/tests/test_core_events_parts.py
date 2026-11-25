"""core-events: part and counter orders are unique, and CMake builds every source (replaces the #943 lane test)."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "plugin" / "core-events"
CMAKE = (ROOT / "plugin" / "CMakeLists.txt").read_text(encoding="utf-8")


class CoreEventsParts(unittest.TestCase):
    def test_part_and_counter_orders_are_unique(self):
        text = [p.read_text(encoding="utf-8") for p in SRC.rglob("*.cpp")]
        for kind in ("PartReg", "CountersReg"):
            orders = [int(o) for t in text for o in re.findall(kind + r" \w+\((\d+),", t)]
            self.assertTrue(orders, kind)
            self.assertEqual(len(orders), len(set(orders)), kind + " order used twice")

    def test_cmake_builds_every_cpp(self):
        for p in SRC.rglob("*.cpp"):
            rel = p.relative_to(SRC)
            if len(rel.parts) == 1:
                self.assertIn("core-events/" + rel.name, CMAKE, f"{rel} is not built")
            else:
                self.assertIn(f"core-events/{rel.parts[0]}/*.cpp", CMAKE, f"{rel} is not globbed")


if __name__ == "__main__":
    unittest.main()
