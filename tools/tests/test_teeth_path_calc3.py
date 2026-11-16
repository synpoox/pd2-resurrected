"""#464: srvdofunc 8 spawns calc3 damaging missiles unclamped; calc3 100 made 100 server missiles for calc1 on screen."""
import unittest

import data_tables


RESET = (
    "Magic Arrow",
    "Fire Arrow",
    "Cold Arrow",
    "Teeth",
    "Bone Spear",
    "Holy Bolt",
    "PrimePoisonball",
    "Shock Wave",
    "Catapult Charged Ball",
    "Ice Barrage",
    "UnravelerBossMultiShot",
    "TraitorBossConeJab",
    "TraitorBossStunSpear",
    "RathmaBoneSpear",
    "Shock Wave Boss",
    "Teeth Rathma",
    "BruteShockWave",
    "UberMadawcOWAxe",
    "Bone Spear Serpent",
    "Fire Ball Ashen Boss",
    "ZharBarrage",
    "Merc Magic Arrow",
    "Bone Spear Putrid Defiler",
    "DjinnBoss Poison Bolt",
    "ArcherBossCageShot",
)


class TeethPathCalc3(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)
        self.skills = {r["skill"]: r for r in data_tables.rows("skills")}

    def test_bone_spear_and_teeth_damage_only_their_own_missiles(self):
        for name in ("Bone Spear", "Teeth"):
            self.assertEqual((self.skills[name]["srvdofunc"], self.skills[name]["calc3"]), ("8", "0"))  # 0 = the count (calc1)

    def test_every_listed_skill_has_calc3_reset(self):
        for name in RESET:
            self.assertEqual(self.skills[name]["calc3"], "0", name)

    def test_cage_shot_calc3_2_against_calc1_1_is_reset(self):
        self.assertEqual(self.skills["ArcherBossCageShot"]["calc3"], "0")


if __name__ == "__main__":
    unittest.main()
