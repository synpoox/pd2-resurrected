"""#1056: srvdofunc 190 (the router) takes Multiple Shot, Multiple Shot Proc and TraitorBossStunSpear, by skill id."""
import json
import unittest
from pathlib import Path

OPS = json.loads((Path(__file__).resolve().parents[2] / "patches/020-skill-function-slots.json").read_text())["ops"]
ROUTED = {"Multiple Shot", "Multiple Shot Proc", "TraitorBossStunSpear"}


class MultipleShotRouter(unittest.TestCase):
    def test_each_skill_gets_the_router_slot(self):
        routed = {o["key"]: o["set"] for o in OPS if o["set"].get("srvdofunc") == "190"}
        for skill in ROUTED:
            self.assertIn(skill, routed)
            self.assertEqual(routed[skill], {"srvdofunc": "190"})

    def test_the_ops_are_skills_rows_and_only_set_srvdofunc(self):
        for o in OPS:
            if o["key"] in ROUTED:
                self.assertEqual((o["op"], o["table"], o["bank"]), ("set", "skills", "excel"))


if __name__ == "__main__":
    unittest.main()
