"""presets/passes/floor_ground.py: per-tile biome overrides for the floor (#740)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets.passes import floor_ground as F  # noqa: E402
from presets_fixtures import A, FLOOR, TERRAIN, stock  # noqa: E402

COURT = "data/hd/env/biome/court.json"
MAP = "data/hd/env/biome/x.json"
GRASS = ("act1/a/grass.dt1", 0, 0, 2)
ROCK = ("act1/a/rock.dt1", 0, 0, 3)
FL = ("f", (FLOOR,))
GR = ("f", (GRASS,))


def stock_with(keys: dict, over: dict, biome: str = MAP):
    lay = P.Layout(4, 1, keys)
    preset = stock()
    preset["biomeFilename"] = biome
    preset["perTileBiomeOverrides"] = [{"type": "TileBiomeOverride", "name": "n", "tileX": x, "tileY": y,
                                        "biomeFilename": b} for (x, y), b in over.items()]
    return lay, preset


class FloorGroundTests(unittest.TestCase):
    def test_a_floor_key_stock_draws_in_another_biome_gets_an_override(self):
        s = stock_with({(0, 0): FL, (1, 0): FL, (2, 0): GR, (3, 0): GR}, {(0, 0): COURT, (1, 0): COURT})
        lay = P.Layout(3, 1, {(0, 0): GR, (1, 0): FL, (2, 0): FL})
        out = F.overrides(lay, [s], [], set(), MAP, "m.ds1")
        self.assertEqual([(o["tileX"], o["tileY"], o["biomeFilename"]) for o in out], [(1, 0, COURT), (2, 0, COURT)])
        self.assertTrue(all(o["type"] == "TileBiomeOverride" and o["name"].startswith("biomeEntry_") for o in out))
        self.assertEqual(out, F.overrides(lay, [s], [], set(), MAP, "m.ds1"))  # reproducible

    def test_a_key_without_a_majority_or_a_wall_tile_stays_in_the_maps_biome(self):
        s = stock_with({(0, 0): FL, (1, 0): FL, (2, 0): ("w", (A,))}, {(0, 0): COURT})
        lay = P.Layout(3, 1, {(0, 0): FL, (1, 0): ("w", (A,)), (2, 0): ("f", (ROCK,))})
        self.assertEqual(F.overrides(lay, [s], [], set(), MAP, "m"), [])  # a tie, a wall, a DT1 stock lacks

    def test_a_dt1_the_key_shares_votes_for_a_key_stock_lacks(self):
        other = ("act1/a/floor.dt1", 0, 9, 9)
        s = stock_with({(0, 0): FL, (1, 0): FL}, {(0, 0): COURT, (1, 0): COURT})
        out = F.overrides(P.Layout(1, 1, {(0, 0): ("f", (other,))}), [s], [], set(), MAP, "m")
        self.assertEqual([o["biomeFilename"] for o in out], [COURT])

    def test_a_pasted_room_keeps_its_stock_overrides_shifted_and_its_tiles_are_not_voted(self):
        s = stock_with({(0, 0): FL}, {(0, 0): COURT, (3, 0): "data/hd/env/biome/far.json"})
        lay = P.Layout(6, 1, {(2, 0): FL, (3, 0): FL, (5, 0): FL})
        out = F.overrides(lay, [s], [(0, 2, 0)], {(2, 0)}, MAP, "m")
        self.assertEqual([(o["tileX"], o["biomeFilename"]) for o in out], [(2, COURT), (3, COURT), (5, COURT)])

    def test_the_pass_sets_the_presets_overrides_and_depends_on_their_biomes(self):
        s = stock_with({(0, 0): FL}, {(0, 0): COURT})
        lay = P.Layout(1, 1, {(0, 0): FL})
        preset, _ = P.build(lay, P.Kit(), "m", "m.ds1", MAP, TERRAIN, [s])
        self.assertEqual([(o["tileX"], o["tileY"], o["biomeFilename"]) for o in preset["perTileBiomeOverrides"]],
                         [(0, 0, COURT)])
        self.assertIn(COURT, [d["path"] for d in preset["dependencies"]["json"]])


if __name__ == "__main__":
    unittest.main()
