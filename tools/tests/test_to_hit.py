"""PD2's to-hit chance (#2010): a mirror of ProjectDiablo.dll 0x10304e70 pins core-rules' Pd2Chance (to_hit.cpp)."""
import re
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[2] / "plugin" / "core-rules" / "skill" / "to_hit.cpp"


def div(a: int, b: int) -> int:
    """idiv: truncates toward zero."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def pd2_chance(defense: int, ar: int, dlvl: int, alvl: int) -> int:
    """0x10304e70 (fastcall ecx defense, edx AR; stack defender level, attacker level), read with capstone."""
    if defense < 0:
        ar, defense = ar - defense, 0
    if ar < 0:
        defense, ar = defense - ar, 0
    total = ar + defense
    ratio = div(111 * ar, total) if total else 100
    levels = dlvl + alvl
    if levels == 0:
        return 5
    chance = div(ratio * alvl * 2, levels)
    if chance < 5:
        return 5
    if chance <= 95 or alvl == 0:
        return min(chance, 100)
    over = ratio - div(95 * levels, 2 * alvl)
    if total:
        over = div(div(total * over, 111) * 90, total)
    return min(div(over * alvl * 2, levels) + 95, 100)


def d2r_chance(defense: int, ar: int, dlvl: int, alvl: int) -> int:
    """D2R 93847 0x44bd31-0x44bd64, the block the port replaces."""
    total = ar + defense
    ratio = div(100 * ar, total) if total else 100
    return max(5, min(95, div(ratio * alvl * 2, dlvl + alvl)))


class ToHitChance(unittest.TestCase):
    def test_cpp_pins_match_the_mirror(self):
        pins = re.findall(r"static_assert\(Pd2Chance\((-?\d+), (-?\d+), (-?\d+), (-?\d+)\) == (\d+)\)",
                          SOURCE.read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(pins), 6)
        for *args, want in pins:
            self.assertEqual(pd2_chance(*map(int, args)), int(want), args)

    def test_pd2_differs_from_d2r(self):
        self.assertEqual((pd2_chance(1000, 1000, 50, 50), d2r_chance(1000, 1000, 50, 50)), (55, 50))
        self.assertEqual((pd2_chance(500, 5000, 50, 50), d2r_chance(500, 5000, 50, 50)), (99, 90))
        self.assertEqual((pd2_chance(1000, 10, 80, 10), d2r_chance(1000, 10, 80, 10)), (5, 5))

    def test_bounds(self):
        for defense in (0, 1, 50, 500, 5000, 50000):
            for ar in (0, 1, 100, 1000, 10000, 100000):
                for dlvl, alvl in ((1, 1), (99, 1), (1, 99), (85, 99), (99, 99)):
                    self.assertTrue(5 <= pd2_chance(defense, ar, dlvl, alvl) <= 100)


if __name__ == "__main__":
    unittest.main()
