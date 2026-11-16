"""monprop hit/death/att-skill par1 ids name the intended skill row (#1134): PD2's row numbers moved here."""
import unittest

import data_tables as d

# monprop Id -> skill name every *-skill par of the row resolves to (the PD2 skill, by name)
EXPECT = {
    "fenris": "Skeleton Splash", "necroskeleton": "Skeleton Splash",
    "firegolem": "proc_SplashDamage", "irongolem": "proc_SplashDamage", "claygolem": "proc_SplashDamage",
    "bloodgolemow": "proc_SplashDamage", "druidbear": "proc_SplashDamage",
    "druidhawk": "Raven Splash", "butchermapprop": "Golem Splash",
    "stygian": "DollMeteor", "rathma": "RathmaDeath", "nihlathakmap": "NihlathakMapDeath",
}


class MonpropSkillIds(unittest.TestCase):
    def test_par_names(self):
        d.need(self)
        byid = {r["*Id"]: r["skill"] for r in d.rows("skills")}
        for r in d.rows("monprop"):
            want = EXPECT.get(r["Id"])
            for k, v in r.items():
                if k.startswith("prop") and v.endswith("-skill"):
                    par = r["par" + k[4:]]
                    self.assertIn(par, byid, f"{r['Id']} {k}")
                    if want and r["Id"] not in ("rathma", "nihlathakmap"):
                        self.assertEqual(byid[par], want, f"{r['Id']} {k}")
        for row, name in (("rathma", "RathmaDeath"), ("nihlathakmap", "NihlathakMapDeath")):
            r = next(x for x in d.rows("monprop") if x["Id"] == row)
            self.assertIn(name, [byid[r["par" + k[4:]]] for k, v in r.items() if k.startswith("prop") and v == "death-skill"])


if __name__ == "__main__":
    unittest.main()
