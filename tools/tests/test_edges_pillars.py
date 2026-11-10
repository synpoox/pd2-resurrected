"""presets.passes.edges_pillars (#737): the stock pillar a pillar tile stands, and where."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets.passes import edges_pillars as pillars  # noqa: E402
from presets_fixtures import at, entity  # noqa: E402


def grid(*rows: str) -> bytes:
    """Subtile flags from five rows of five: # blocks, . is free."""
    return bytes(1 if ch == "#" else 0 for row in rows for ch in row)


T = "expansion/wildtemple/interior.dt1"
PD2 = ("w", (("pd2assets/x/file2.dt1", 12, 9, 1),))


def stands(flags: bytes, seed: str = "s") -> list:
    """The pieces place() stands on a pillar tile (1, 0) with these flags, next to a wall tile that holds a pillar."""
    wall = ("w", ((T, 1, 0, 0),))
    kit = P.Kit({wall: [(0, [(entity("wildtemple/pillar01", 0, 0), 5.0, 5.0)])]}, [{}])
    ents = P.place(P.Layout(2, 1, {(0, 0): wall, (1, 0): PD2}, {(1, 0): flags}), kit, seed)[0]
    return [at(e) for e in ents if at(e)[0] >= 10]


class PillarTests(unittest.TestCase):
    def test_a_pillar_tile_borrows_a_pillar_before_a_nearer_footprint(self):
        T = "expansion/wildtemple/interior.dt1"
        chair, pillar = ("w", ((T, 12, 9, 9),)), ("w", ((T, 12, 4, 5),))
        block = grid(".....", ".###.", ".###.", ".###.", ".....")
        ring = grid(".....", "..##.", "..##.", ".....", ".....")  # the tile's, and the chair's, footprint
        kit = P.Kit({chair: [(0, [(entity("chair06", 0, 0), 5.0, 5.0)])],
                     pillar: [(0, []), (0, [(entity("wildtemple/pillar01", 0, 0), 5.0, 5.0)])]},
                    [{}], flags={chair: ring, pillar: block})
        lay = P.Layout(2, 1, {(0, 0): ("w", ((T, 1, 0, 0),)), (1, 0): ("w", (("pd2assets/x/file2.dt1", 12, 9, 1),))},
                       {(1, 0): ring})
        for seed in "abcde":  # the chair's collision fits better; the empty instance is no pick
            self.assertEqual([e["name"] for e in P.place(lay, kit, seed)[0]], ["wildtemple/pillar01"])

    def test_a_pillar_tile_without_a_pillar_key_stands_the_kits_stock_pillar_centred(self):
        T = "expansion/wildtemple/interior.dt1"
        wall = ("w", ((T, 1, 0, 0),))
        kit = P.Kit({wall: [(0, [(entity("wildtemple/pillar01", 0, 0), 1.0, 9.0), (entity("wildtemple/wall01", 0, 0), 5.0, 5.0)])]},
                    [{}])
        ring = grid(".....", ".....", "..##.", "..##.", ".....")
        keys = {(0, 0): wall, (1, 0): ("w", (("pd2assets/x/file2.dt1", 12, 9, 1),))}
        ents = P.place(P.Layout(2, 1, keys, {(1, 0): ring}), kit, "s")[0]
        self.assertEqual(sorted((e["name"], at(e)) for e in ents),
                         [("wildtemple/pillar01", (1.0, 9.0)), ("wildtemple/pillar01", (16.0, 6.0)),
                          ("wildtemple/wall01", (5.0, 5.0))])  # the wall's own pieces, and one pillar on the tile
        ents = P.place(P.Layout(2, 1, keys, {}), kit, "s")[0]  # a tile that blocks nothing stands no pillar
        self.assertEqual(sorted(e["name"] for e in ents), ["wildtemple/pillar01", "wildtemple/wall01"])

    def test_a_short_column_is_no_stand_in_pillar(self):
        T = "expansion/wildtemple/interior.dt1"
        wall = ("w", ((T, 1, 0, 0),))
        kit = P.Kit({wall: [(0, [(entity("icecave/column_small02", 0, 0), 5.0, 5.0)])]}, [{}], top=lambda e: 2.0)
        ring = grid(".....", ".....", "..##.", "..##.", ".....")
        lay = P.Layout(2, 1, {(0, 0): wall, (1, 0): ("w", (("pd2assets/x/file2.dt1", 12, 9, 1),))}, {(1, 0): ring})
        self.assertEqual([at(e) for e in P.place(lay, kit, "s")[0]], [(5.0, 5.0)])  # the wall's own, none on (1, 0)

    def test_the_pillar_height_is_the_coverage_tall(self):
        import pd2_coverage
        pass
        self.assertEqual(pillars.PILLAR_TALL, pd2_coverage.TALL)

    def test_a_corner_only_footprint_stands_its_pillar_on_blocked_ground(self):
        corner = grid("#....", "#....", "#....", "##...", "##...")  # a 2x2 bottom-left corner and a left column
        centre = pillars.centre(corner)
        self.assertIsNotNone(centre)
        dx, dz = centre
        self.assertLess(dx, 5.0)  # in the left of the tile, not at its middle (5, 5)
        self.assertGreater(dz, 5.0)
        self.assertEqual(stands(corner), [(10.0 + dx, dz)])
        two = grid(".....", ".....", ".....", "##...", "##...")
        self.assertEqual(pillars.centre(two), (2.0, 8.0))  # the point between the four subtiles, all blocked

    def test_a_wedge_stands_its_pillar_inside_the_blocked_subtiles(self):
        dx, dz = pillars.centre(grid(".....", ".....", "#....", "##...", "###.."))
        self.assertAlmostEqual(dx, 7 / 3)  # in subtile (1, 3), which is blocked
        self.assertAlmostEqual(dz, 23 / 3)

    def test_a_pillar_never_stands_on_a_free_centre(self):
        for flags in (grid("#...#", ".....", ".....", ".....", "#...#"), grid("#...#", ".....", ".....", ".....", ".....")):
            self.assertIsNone(pillars.centre(flags))

    def test_a_single_row_or_column_is_a_wall_edge_and_stands_no_pillar(self):
        self.assertIsNone(pillars.centre(grid(".....", ".....", ".....", ".....", "#####")))
        self.assertIsNone(pillars.centre(grid("#....", "#....", "#....", "#....", "#....")))
        self.assertEqual(stands(grid(".....", ".....", ".....", ".....", "#####")), [])
        self.assertEqual(stands(grid("#....", "#....", "#....", "#....", "#....")), [])

    def test_a_single_blocked_subtile_stands_no_pillar(self):
        self.assertIsNone(pillars.centre(grid(".....", ".....", "..#..", ".....", ".....")))
        self.assertEqual(stands(grid(".....", ".....", "..#..", ".....", ".....")), [])

    def test_a_full_tile_stands_its_pillar_at_the_middle(self):
        self.assertEqual(pillars.centre(bytes([1]) * 25), (5.0, 5.0))

    def test_the_fallback_pick_is_salted_with_the_seed(self):
        wall = ("w", ((T, 1, 0, 0),))
        pieces = [(entity(f"wildtemple/pillar0{i}", 0, 0), 5.0, 5.0) for i in range(1, 4)]
        kit = P.Kit({wall: [(0, pieces)]}, [{}])
        flags = bytes([1]) * 25
        picks = {tuple(e["name"] for e in P.place(P.Layout(2, 1, {(0, 0): wall, (1, 0): PD2}, {(1, 0): flags}), kit, s)[0]
                       if at(e)[0] >= 10) for s in "abcdefghij"}
        self.assertGreater(len(picks), 1)


if __name__ == "__main__":
    unittest.main()
