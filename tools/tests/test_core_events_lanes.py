"""docs/core-events-lanes.md lists every plugin/core-events source exactly once, and CMake builds the same files (#943)."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "plugin" / "core-events"


def allowlisted():
    seen = []
    for line in (ROOT / "docs" / "core-events-lanes.md").read_text(encoding="utf-8").splitlines():
        m = re.match(r"\| ([^|]+) \| ([ABCDS]) \|", line)
        if m:
            seen += [f.strip() for f in m.group(1).split(",")]
    return seen


class CoreEventsLanes(unittest.TestCase):
    def test_every_file_in_exactly_one_lane(self):
        listed = allowlisted()
        self.assertEqual(len(listed), len(set(listed)), "a file is listed twice")
        # A lane-<x>/ folder is one row: everything in it is that lane's. Everything else is listed by file name.
        flat = sorted(p.name for p in SRC.iterdir() if p.is_file())
        folders = sorted(p.name + "/" for p in SRC.iterdir() if p.is_dir())
        self.assertEqual(sorted(listed), sorted(flat + folders))
        self.assertTrue(all(re.fullmatch(r"lane-[abcd]/", f) for f in folders), folders)

    def test_cmake_builds_every_cpp(self):
        cmake = (ROOT / "plugin" / "CMakeLists.txt").read_text(encoding="utf-8")
        built = set(re.findall(r"core-events/(\w+\.cpp)", cmake))
        self.assertEqual(built, {p.name for p in SRC.glob("*.cpp")})
        self.assertIn("core-events/lane-*/*.cpp", cmake)  # the lane folders are globbed

    def test_part_and_counter_orders_are_unique(self):
        text = {p: p.read_text(encoding="utf-8") for p in list(SRC.glob("*.cpp")) + list(SRC.glob("lane-*/*.cpp"))}
        for kind in ("PartReg", "CountersReg"):
            orders = [int(o) for t in text.values() for o in re.findall(kind + r" \w+\((\d+),", t)]
            self.assertTrue(orders, kind)
            self.assertEqual(len(orders), len(set(orders)), kind + " order used twice")


if __name__ == "__main__":
    unittest.main()
