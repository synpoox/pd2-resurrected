"""docs/core-maps-lanes.md lists every plugin/core-maps source exactly once, and CMake builds the same files (#943)."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "plugin" / "core-maps"


def allowlisted():
    seen = []
    for line in (ROOT / "docs" / "core-maps-lanes.md").read_text(encoding="utf-8").splitlines():
        m = re.match(r"\| ([^|]+) \| ([ABCDS]) \|", line)
        if m:
            seen += [f.strip() for f in m.group(1).split(",")]
    return seen


class CoreMapsLanes(unittest.TestCase):
    def test_every_file_in_exactly_one_lane(self):
        listed = allowlisted()
        self.assertEqual(len(listed), len(set(listed)), "a file is listed twice")
        self.assertEqual(sorted(listed), sorted(p.name for p in SRC.iterdir() if p.is_file()))

    def test_cmake_builds_every_cpp(self):
        cmake = (ROOT / "plugin" / "CMakeLists.txt").read_text(encoding="utf-8")
        built = set(re.findall(r"core-maps/(\w+\.cpp)", cmake))
        self.assertEqual(built, {p.name for p in SRC.glob("*.cpp")})


if __name__ == "__main__":
    unittest.main()
