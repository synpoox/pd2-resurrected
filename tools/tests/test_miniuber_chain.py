"""#955: the mini-uber key chain stays on D2R's vanilla levels 133-135 and its organs still drop (source-level guard)."""
import unittest
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class MiniuberChainTests(unittest.TestCase):
    def test_miniubers_keep_vanilla_levels_and_are_not_uber_fights(self):
        src = (ROOT / "plugin/placements/miniubers.cpp").read_text(encoding="utf-8")
        rows = re.findall(r'\{"(miniuber\d)", (\d+), (\d+), Source::(\w+), (true|false), (true|false)\}', src)
        assert [(n, int(lv), int(t), s, u) for n, lv, t, s, u, _ in rows] == [
            ("miniuber1", 133, 133, "Vanilla", "false"),
            ("miniuber2", 134, 134, "Vanilla", "false"),
            ("miniuber3", 135, 135, "Vanilla", "false"),
        ]


    def test_organs_drop_from_the_three_mini_uber_bosses(self):
        src = (ROOT / "plugin/core-events/uber_drops.cpp").read_text(encoding="utf-8")
        got = re.findall(r'\{(70[678]), \{D2RL::Items::MakeItemCode\("(\w+)"\)', src)
        assert got == [("706", "mbr"), ("707", "dhn"), ("708", "bey")]
