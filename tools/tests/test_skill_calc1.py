"""Skills whose D2R function reads calc1 where PD2's engine read a Param or none keep PD2's value in calc1 (#1165)."""
import unittest

import data_tables as d

CALC1 = {
    "MonHydra": "ln12", "Hydra Throne Boss": "ln12", "Tentacle": "ln12", "ExtraDrop": "ln12",  # summon lifetime
    "Telekinesis": "par2",  # % chance to knockback/stun
    "Confuse": "100",  # chance to confuse
    "Blessed Hammer": "par1",  # Concentration share in 8ths
}


class SkillCalc1(unittest.TestCase):
    def test_calc1(self):
        d.need(self)
        sk = {r["skill"]: r for r in d.rows("skills")}
        for name, calc in CALC1.items():
            self.assertEqual(sk[name]["calc1"], calc, name)
        for name in ("MonHydra", "Hydra Throne Boss", "Tentacle", "ExtraDrop"):
            self.assertTrue(sk[name]["Param1"], name)
        self.assertEqual(sk["Telekinesis"]["Param2"], "35")
        self.assertEqual(sk["Blessed Hammer"]["Param1"], "4")


if __name__ == "__main__":
    unittest.main()
