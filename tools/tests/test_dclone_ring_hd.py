"""#1261: every missile Dclone's fire ring and nova (skills UberDiabFire, UberDiabSuperFire, UberDiabWall) spawns has an HD model."""
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# JSON keys are case-sensitive; the value is the D2R model the key must keep
RING = {"uberdiabfire": "ground_fire_big", "ubernova": "expansion_firenova",
        "uberwallmaker": "ground_fire_medium", "uberwall": "ground_fire_medium"}


class DcloneRingHdTests(unittest.TestCase):
    def test_ring_missiles_have_hd_models(self):
        hd = json.loads((ROOT / "files/data/hd/missiles/missiles.json").read_text(encoding="utf-8"))
        for key, model in RING.items():
            self.assertEqual(hd.get(key), model, key)


if __name__ == "__main__":
    unittest.main()
