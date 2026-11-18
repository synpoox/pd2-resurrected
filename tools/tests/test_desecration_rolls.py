"""#1355: Desecration put "Monster Physical Damage as Fire" on every amulet because properties.txt *Id 443 (desecrate) was
shared with a Cartographer row; the engine finds a property by *Id, so the later row won. *Ids must be unique, and the
851-999 roll over the amulet corruption rows must reach PD2's eight outcomes with PD2's ranges."""
import collections
import random
import unittest

import data_tables

# PD2 rows 682-689 (docs/pd2/uber-bosses-dll.md section 8): mod code -> (min, max)
OUTCOMES = {"move1": (10, 10), "dmg%": (30, 40), "allskills": (1, 1), "curse-effectiveness": (10, 10),
            "res-all-max": (2, 2), "cast1": (10, 10), "res-all": (7, 10), "nofreeze": (1, 1)}


class DesecrationRolls(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_property_ids_are_unique(self):
        ids = collections.Counter(r["*Id"] for r in data_tables.rows("properties") if r["*Id"])
        assert [i for i, n in ids.items() if n > 1] == []

    def test_desecrate_ids_belong_to_desecrate(self):
        ids = {r["*Id"]: r["code"] for r in data_tables.rows("properties")}
        assert ids["443"] == "desecrate" and ids["444"] == "desecratenum"

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
