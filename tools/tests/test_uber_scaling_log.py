"""uber_scaling splits the log on the real uber-open line, so opens on several tiers are separate entries (#1250)."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

OPEN = "uber: level 145 (dclone, source 1) open at tier {t}, 1 players in ({n} opened)\n"
SCALED = ("uber scaling: monster 806 (unit 5) in level 145, tier {t}, entries {e} (boss): maxhp 1000 -> {hp}, "
          "life 1000 -> {hp}, skills +{s}, absorbs {a}/{a}, dr 0, plr 0\n")
LOG = OPEN.format(t=0, n=1) + SCALED.format(t=0, e=1, hp=1500, s=0, a=2) + \
    OPEN.format(t=1, n=2) + SCALED.format(t=1, e=1, hp=4800, s=4, a=20)


class Ctx:
    def skip(self, msg):
        raise unittest.SkipTest(msg)


class UberScalingLog(unittest.TestCase):
    def test_two_tiers_are_two_entries(self):
        try:
            sys.path.insert(0, str(__import__("pd2_table").toolkit(ROOT) / "testing"))
            sys.path.insert(0, str(ROOT / "tests" / "game"))
            import core_events_checks as c
        except (ValueError, ImportError, OSError) as e:  # no toolkit: nothing to import the check from
            raise unittest.SkipTest(f"no toolkit: {e}")
        c._log = lambda ctx: LOG
        self.assertIn("2 monsters scaled", c.uber_scaling(Ctx()))


if __name__ == "__main__":
    unittest.main()
