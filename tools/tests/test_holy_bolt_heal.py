"""Holy Bolt, Fist of the Heavens' bolts and Holy Nova heal through shit 70, PD2's srvhit 7 (#2227).

D2R's srvhit 7 heals from calc1..calc2, and PD2's calc1 is the bolt count, so the three rows must not point at 7.
"""
import re
import unittest
from pathlib import Path

import data_tables

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "plugin" / "core-rules" / "missile" / "holy_bolt.cpp"
ROWS = {"holybolt", "fistoftheheavensbolt", "holynova"}


class HolyBoltHeal(unittest.TestCase):
    def test_rows_use_shit_70_and_none_keeps_7(self):
        data_tables.need(self)
        missiles = data_tables.rows("missiles")
        self.assertEqual({r["Missile"] for r in missiles if r["pSrvHitFunc"] == "70"}, ROWS)
        self.assertEqual([r["Missile"] for r in missiles if r["pSrvHitFunc"] == "7"], [])
        for row in missiles:
            if row["Missile"] in ROWS:
                self.assertEqual(row["sHitPar1"], "1", row["Missile"])  # the heal gate
                self.assertIn(row["sHitPar2"], ("", "0"), row["Missile"])  # D2R's damage results match PD2's
                self.assertIn(row["sHitPar3"], ("", "0"), row["Missile"])

    def test_heal_calcs_are_cltcalc2_and_cltcalc3(self):
        data_tables.need(self)
        skills = {r["skill"]: r for r in data_tables.rows("skills")}
        self.assertEqual(skills["Holy Bolt"]["cltcalc2"], "edln")
        self.assertEqual(skills["Holy Bolt"]["cltcalc3"], "1 + edln")
        self.assertNotEqual(skills["Holy Bolt"]["calc1"], "")  # the bolt count, which D2R's srvhit 7 would heal by

    def test_plugin_registers_slot_70_and_reads_cltcalc(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertRegex(text, r"PD2R_SLOT\(\d+, MissileSrvHit, 70, ")
        self.assertIn("SkCltCalc2", text)
        self.assertIn("SkCltCalc3", text)
        self.assertNotRegex(text, r"\bSkCalc[12]\b")
        self.assertEqual(int(re.search(r"HealResult = (\d+);", text).group(1)), 5)  # PD2's result

    def test_port_maps_pd2_srvhit_7(self):
        text = (ROOT / "tools" / "pd2_port.py").read_text(encoding="utf-8")
        self.assertRegex(text, r'"pSrvHitFunc": \{"7": "70"')


if __name__ == "__main__":
    unittest.main()
