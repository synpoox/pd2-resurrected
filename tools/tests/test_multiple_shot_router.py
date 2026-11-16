"""#1056: srvdofunc 190 (the router) takes Multiple Shot, Multiple Shot Proc and TraitorBossStunSpear, by skill id."""
import unittest

import data_tables

ROUTED = {"Multiple Shot", "Multiple Shot Proc", "TraitorBossStunSpear"}


class MultipleShotRouter(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_each_skill_gets_the_router_slot(self):
        routed = {r["skill"]: r["srvdofunc"] for r in data_tables.rows("skills")}
        for skill in ROUTED:
            self.assertEqual(routed[skill], "190", skill)


if __name__ == "__main__":
    unittest.main()
