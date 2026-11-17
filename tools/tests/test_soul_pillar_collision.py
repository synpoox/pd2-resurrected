"""#1267: Dclone's soul pillars are selectable, attackable and killable, so missiles hit them."""
import unittest

import data_tables


class SoulPillarTests(unittest.TestCase):
    def test_pillars_are_targetable(self):
        data_tables.need(self)
        stats = {r["Id"]: r for r in data_tables.rows("monstats")}
        for n in range(1, 5):
            assert stats[f"ubertrappedsoul{n}"]["killable"] == "1"
        ex = {r["Id"]: r for r in data_tables.rows("monstats2")}["UberTrappedSoul"]
        assert ex["isSel"] == "1" and ex["isAtt"] == "1"


if __name__ == "__main__":
    unittest.main()
