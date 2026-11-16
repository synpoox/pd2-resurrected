"""Frozen Orb casts with the Sorceress's plain cast motion (#690): PD2's SQ sequence 12 plays no cast animation in D2R."""
import unittest

import data_tables as d


class FrozenOrbCast(unittest.TestCase):
    def test_player_row_uses_cast_mode(self):
        d.need(self)
        row = next(r for r in d.rows("skills") if r["skill"] == "Frozen Orb")
        self.assertEqual((row["anim"], row["seqtrans"], row["monanim"]), ("SC", "SC", "xx"))
        self.assertIn(row["seqnum"], ("", "0"))


if __name__ == "__main__":
    unittest.main()
