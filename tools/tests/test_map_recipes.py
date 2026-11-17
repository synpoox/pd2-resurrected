"""#1306: every new map recipe uses valid codes and types, and none outputs a magic map."""
import re
import unittest

import data_tables

MAPS = ("t1m", "t2m", "t3m")


class MapRecipeTests(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)
        rows = data_tables.rows("cubemain")
        i = next(k for k, r in enumerate(rows) if r["description"].startswith("native map + iwss"))
        self.rows = [r for r in rows[i + 2:] if r["output"] and r["input 1"]]
        misc = {r["code"] for r in data_tables.rows("misc")}
        types = {r["Code"] for r in data_tables.rows("itemtypes")}
        self.known = misc | types
        self.props = {r["code"] for r in data_tables.rows("properties")}

    def test_there_are_rows(self):
        assert len(self.rows) >= 50

    def test_inputs_are_valid_codes(self):
        for r in self.rows:
            for k in range(1, 8):
                c = r.get(f"input {k}", "").strip('"')
                if c:
                    code = c.split(",")[0]
                    assert code in self.known or code == "cmap", (r["description"], code)

    def test_outputs_are_valid_and_never_magic(self):
        for r in self.rows:
            parts = r["output"].split(",")
            assert parts[0] in self.known | {"useitem", "usetype"}, r["description"]
            assert "mag" not in parts[1:], r["description"]
            if parts[0] == "usetype":
                assert parts[1] in ("nor", "rar"), r["description"]
            for k in range(1, 6):
                m = r.get(f"mod {k}", "")
                assert not m or m in self.props, (r["description"], m)

    def test_no_map_input_asks_for_magic(self):
        for r in self.rows:
            for k in range(1, 8):
                c = r.get(f"input {k}", "").strip('"')
                if c.split(",")[0] in MAPS + ("cmap",):
                    assert "mag" not in c.split(",")[1:], r["description"]

    def test_new_orbs_exist(self):
        codes = {r["code"] for r in data_tables.rows("misc")}
        assert {"upmp", "fort"} <= codes
