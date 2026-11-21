"""Map immunities (#1552): the committed map rows and strings are what tools/map_immunities.py builds."""
import unittest

import data_tables
import map_immunities


class MapImmunities(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_committed_data_is_up_to_date(self):
        for path, new in map_immunities.build().items():
            self.assertEqual(path.read_bytes(), new, f"{path.name}: run python tools/map_immunities.py")

    def test_line_keeps_100_and_up_in_c_f_l_p_order_with_the_highest_value(self):
        mon = {"a": {"ResCo(H)": "130", "ResFi(H)": "75", "ResLi(H)": "100", "ResPo(H)": "99"},
               "b": {"ResCo(H)": "50", "ResFi(H)": "125", "ResLi(H)": "0", "ResPo(H)": ""}}
        self.assertEqual(map_immunities.line("a,b", mon),
                         "Immunities: \xffc3C130\xffc0, \xffc1F125\xffc0, \xffc9L100\xffc0")
        self.assertEqual(map_immunities.line("b", {"b": {**mon["b"], "ResFi(H)": "99"}}), "")

    def test_maps_with_an_immune_monster_name_it_and_the_others_keep_the_shared_tier(self):
        misc = {r["code"]: r for r in data_tables.rows("misc")}
        want = map_immunities.expected()
        self.assertIn("m02", want)  # Necropolis: Hell fire and poison 120
        self.assertEqual(misc["m02"]["spelldescstr"], "MapTierm02")
        self.assertEqual(want["m02"][1], "Tier: 2\nImmunities: \xffc1F120\xffc0, \xffc2P120\xffc0")
        self.assertNotIn("m01", want)  # Tunneling Depths: nothing reaches 100
        self.assertEqual(misc["m01"]["spelldescstr"], "MapTier1")
