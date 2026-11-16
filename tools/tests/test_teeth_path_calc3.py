"""#464: srvdofunc 8 spawns calc3 damaging missiles unclamped; calc3 100 made 100 server missiles for calc1 on screen."""
import json
import unittest
from pathlib import Path

OPS = json.loads((Path(__file__).resolve().parents[2] / "patches/075-teeth-path-calc3.json").read_text())["ops"]


class TeethPathCalc3(unittest.TestCase):
    def test_bone_spear_and_teeth_damage_only_their_own_missiles(self):
        keys = {o["key"]: o["set"] for o in OPS}
        self.assertEqual(keys["Bone Spear"], {"calc3": "0"})  # 0 = the count (calc1)
        self.assertEqual(keys["Teeth"], {"calc3": "0"})

    def test_cage_shot_calc3_2_against_calc1_1_is_reset(self):
        self.assertIn("ArcherBossCageShot", {o["key"] for o in OPS})

    def test_every_op_is_a_calc3_reset_on_skills(self):
        self.assertTrue(OPS)
        for o in OPS:
            self.assertEqual((o["table"], o["set"]), ("skills", {"calc3": "0"}))


if __name__ == "__main__":
    unittest.main()
