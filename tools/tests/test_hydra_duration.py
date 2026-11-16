"""Hydra and Lesser Hydra take their lifetime from calc1 (ln12 = Param1 + Param2*(lvl-1)); an empty calc1 made them vanish after ~1.5 s (#728)."""
import unittest

import data_tables as d


class HydraDuration(unittest.TestCase):
    def test_calc1_is_duration(self):
        d.need(self)
        sk = {r["skill"]: r for r in d.rows("skills")}
        for name in ("Hydra", "Lesser Hydra"):
            self.assertEqual(sk[name]["calc1"], "ln12", name)
            self.assertTrue(sk[name]["Param1"], name)


if __name__ == "__main__":
    unittest.main()
