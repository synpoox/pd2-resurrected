"""#1355: Desecration put "Monster Physical Damage as Fire" on every amulet. D2R numbers properties by row (past the
Expansion row), but corruption_roll.cpp used the *Id column, so corruptnum's 1001 landed on row 443, a Cartographer stat.
The plugins resolve property numbers by code, and the 851-999 roll over the amulet corruption rows must reach PD2's
eight outcomes with PD2's ranges."""
import collections
import random
import re
import unittest

import data_tables

# PD2 rows 682-689 (docs/pd2/uber-bosses-dll.md section 8): mod code -> (min, max)
OUTCOMES = {"move1": (10, 10), "dmg%": (30, 40), "allskills": (1, 1), "curse-effectiveness": (10, 10),
            "res-all-max": (2, 2), "cast1": (10, 10), "res-all": (7, 10), "nofreeze": (1, 1)}


class DesecrationRolls(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_plugin_property_codes_exist(self):
        # #1371: the plugins resolve these codes by name at load (plugin/common/property_ids.h); each must be a properties.txt row
        codes = {r["code"] for r in data_tables.rows("properties")}
        for path in ("plugin/core-events/corruption_roll.cpp", "plugin/core-rules/cube_rules.cpp",
                     "plugin/core-rules/cube_stacks.cpp"):
            src = (data_tables.ROOT / path).read_text()
            asked = re.findall(r'propids::Resolve\("([^"]+)"\)', src)
            assert asked, path
            for code in asked:
                assert code in codes, (path, code)

    def test_row_numbers_skip_expansion(self):
        # the numbers the plugins resolve (the game log must show them): rows past the Expansion row, not *Id
        # the pins are intentional: a properties.txt edit that moves these rows fails here so a person reviews it
        codes = [r["code"] for r in data_tables.rows("properties") if r["code"] != "Expansion"]
        for code, row in (("corrupt", 287), ("corruptnum", 288), ("desecrate", 349), ("desecratenum", 350),
                          ("map-level", 374), ("map-force-event-l", 383)):
            assert codes.index(code) == row, code

    def test_many_rolls_reach_all_eight_pd2_outcomes(self):
        # corruption_roll.cpp: roll 851-999, the first row (file order) whose value beats the roll
        rows = [r for r in data_tables.rows("cubemain") if r["op"] in ("16", "18") and r["param"] in ("corruptor", "414")
                and r["value"] not in ("", "0") and "amul" in r["input 1"] and r["output"] == "useitem"]
        rng, seen = random.Random(1), collections.Counter()
        for _ in range(8000):
            roll = rng.randrange(851, 1000)
            row = next(r for r in rows if int(r["value"]) > roll)
            lo, hi = OUTCOMES[row["mod 3"]]
            assert lo <= int(row["mod 3 min"]) and int(row["mod 3 max"]) <= hi, row["mod 3"]
            seen[row["mod 3"]] += 1
        assert set(seen) == set(OUTCOMES), seen
        assert all(n > 8000 / 8 / 2 for n in seen.values()), seen  # about 1 in 8 each


if __name__ == "__main__":
    unittest.main()
