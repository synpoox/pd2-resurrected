"""PD2's sheet target rules (#2048): a mirror of ProjectDiablo.dll 0x10304f40 pins sheet_to_hit.cpp's TargetDefense and
TargetAr (stats 115, 116, 123, 124 against monstats boss 0x40, demon 0x2000 and undead 0x1800)."""
import re
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[2] / "plugin" / "core-rules" / "skill" / "sheet_to_hit.cpp"
FLAGS = {"FlagBoss": 0x40, "FlagDemon": 0x2000, "FlagUndead": 0x1800}


def flag(text: str) -> int:
    return FLAGS[text] if text in FLAGS else int(text, 0)


def target_defense(defense: int, ignore: int, fraction: int, flags: int) -> int:
    """0x10304f40 on defense, read with capstone (the superunique / unique hover globals left at 0)."""
    boss = flags & 0x40
    if ignore and not boss:
        defense = 0
    if fraction > 0:
        v = int(fraction / 2) if boss else fraction
        v = 0 if v < 1 else (100 if v > 99 else v)
        defense -= int(defense * v / 100)
    return defense


def target_ar(ar: int, demon: int, undead: int, flags: int) -> int:
    if demon and flags & 0x2000:
        ar += demon
    if undead and flags & 0x1800:
        ar += undead
    return ar


class SheetTargetRules(unittest.TestCase):
    def test_cpp_pins_match_the_mirror(self):
        text = SOURCE.read_text(encoding="utf-8")
        defense = re.findall(r"static_assert\(TargetDefense\((-?\d+), (-?\d+), (-?\d+), (\w+)\) == (-?\d+)\)", text)
        ar = re.findall(r"static_assert\(TargetAr\((-?\d+), (-?\d+), (-?\d+), (\w+)\) == (-?\d+)\)", text)
        self.assertGreaterEqual(len(defense), 6)
        self.assertGreaterEqual(len(ar), 4)
        for *args, flags, want in defense:
            self.assertEqual(target_defense(*map(int, args), flag(flags)), int(want), args)
        for *args, flags, want in ar:
            self.assertEqual(target_ar(*map(int, args), flag(flags)), int(want), args)


if __name__ == "__main__":
    unittest.main()
