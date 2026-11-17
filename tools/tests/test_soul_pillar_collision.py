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

    def test_nova_collides_with_friends(self):
        data_tables.need(self)
        nova = {r["Missile"]: r for r in data_tables.rows("missiles")}["ubernova"]
        assert nova["CollideKill"] == "1" and nova["CollideFriend"] == "1"  # the pillars are Dclone's own side


if __name__ == "__main__":
    unittest.main()
