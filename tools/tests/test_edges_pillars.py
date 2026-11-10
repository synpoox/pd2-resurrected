"""presets.pillars (#737): the stock pillar a pillar tile stands, and where."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets import pillars  # noqa: E402
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
        wall = ("w", ((T, 1, 0, 0),))
        kit = P.Kit({wall: [(0, [(entity("icecave/column_small02", 0, 0), 5.0, 5.0)])]}, [{}], top=lambda e: 2.0)
        ring = grid(".....", ".....", "..##.", "..##.", ".....")
        lay = P.Layout(2, 1, {(0, 0): wall, (1, 0): ("w", (("pd2assets/x/file2.dt1", 12, 9, 1),))}, {(1, 0): ring})
        self.assertEqual([at(e) for e in P.place(lay, kit, "s")[0]], [(5.0, 5.0)])  # the wall's own, none on (1, 0)

    def test_the_pillar_height_is_the_coverage_tall(self):
        import pd2_coverage
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

    def test_a_wall_bearing_best_key_is_kept_when_stand_ins_exist(self):
        wall = ("w", ((T, 1, 0, 0),))
        bar = ("w", ((T, 12, 7, 7),))
        flags = grid(".....", ".###.", ".###.", ".###.", ".....")
        kit = P.Kit({wall: [(0, [(entity("wildtemple/pillar01", 0, 0), 5.0, 5.0)])],
                     bar: [(0, [(entity("wildtemple/wall_bar01", 0, 0), 5.0, 5.0)])]}, [{}], flags={bar: flags})
        lay = P.Layout(2, 1, {(0, 0): wall, (1, 0): PD2}, {(1, 0): flags})
        for seed in "abc":
            self.assertEqual([e["name"] for e in P.place(lay, kit, seed)[0] if at(e)[0] >= 10], ["wildtemple/wall_bar01"])

    def test_a_tall_prop_with_the_nearest_footprint_loses_to_a_pillar(self):
        wall = ("w", ((T, 1, 0, 0),))
        shrine, pillar = ("w", ((T, 12, 7, 7),)), ("w", ((T, 12, 4, 5),))
        near = grid(".....", ".##..", ".##..", ".....", ".....")
        far = grid(".....", ".###.", ".###.", ".###.", ".....")
        kit = P.Kit({wall: [(0, [(entity("wildtemple/wall01", 0, 0), 5.0, 5.0)])],
                     shrine: [(0, [(entity("wildtemple_shrines/shrine04", 0, 0), 5.0, 5.0)])],
                     pillar: [(0, [(entity("wildtemple/pillar01", 0, 0), 5.0, 5.0)])]},
                    [{}], flags={shrine: near, pillar: far}, top=lambda e: 8.0)
        lay = P.Layout(2, 1, {(0, 0): wall, (1, 0): PD2}, {(1, 0): near})
        for seed in "abc":  # an 8.0 high shrine is no wall: the pillar key goes first
            self.assertEqual([e["name"] for e in P.place(lay, kit, seed)[0] if at(e)[0] >= 10], ["wildtemple/pillar01"])

    def test_a_fence_bearing_key_is_kept_as_a_wall(self):
        bar = ("w", ((T, 12, 7, 7),))
        flags = grid(".....", ".###.", ".###.", ".###.", ".....")
        kit = P.Kit({("w", ((T, 1, 0, 0),)): [(0, [(entity("wildtemple/pillar01", 0, 0), 5.0, 5.0)])],
                     bar: [(0, [(entity("icecave_fences/corner_fence01", 0, 0), 5.0, 5.0)])]}, [{}], flags={bar: flags})
        lay = P.Layout(2, 1, {(0, 0): ("w", ((T, 1, 0, 0),)), (1, 0): PD2}, {(1, 0): flags})
        self.assertEqual([e["name"] for e in P.place(lay, kit, "s")[0] if at(e)[0] >= 10], ["icecave_fences/corner_fence01"])

    def test_a_post_bearing_key_is_kept_as_a_wall(self):
        bar = ("w", ((T, 12, 7, 7),))
        flags = grid(".....", ".###.", ".###.", ".###.", ".....")
        kit = P.Kit({("w", ((T, 1, 0, 0),)): [(0, [(entity("wildtemple/pillar01", 0, 0), 5.0, 5.0)])],
                     bar: [(0, [(entity("icecave_architecture/post01", 0, 0), 5.0, 5.0)])]}, [{}], flags={bar: flags},
                    top=lambda e: 8.0)
        lay = P.Layout(2, 1, {(0, 0): ("w", ((T, 1, 0, 0),)), (1, 0): PD2}, {(1, 0): flags})
        self.assertEqual([e["name"] for e in P.place(lay, kit, "s")[0] if at(e)[0] >= 10], ["icecave_architecture/post01"])

    def test_a_wall_bearing_key_keeps_all_its_columns(self):
        bar = ("w", ((T, 12, 7, 7),))
        row = [(entity("wildtemple/wall_bar01", 0, 0), x, 5.0) for x in (1.0, 5.0)]
        row += [(entity("wildtemple/pillar01", 0, 0), x, 5.0) for x in (3.0, 7.0)]
        flags = bytes([1]) * 25
        kit = P.Kit({bar: [(0, row)], ("w", ((T, 1, 0, 0),)): [(0, [(entity("wildtemple/pillar01", 0, 0), 5.0, 5.0)])]},
                    [{}], flags={bar: flags})
        lay = P.Layout(2, 1, {(0, 0): ("w", ((T, 1, 0, 0),)), (1, 0): PD2}, {(1, 0): flags})
        self.assertEqual(len([e for e in P.place(lay, kit, "s")[0] if at(e)[0] >= 10]), 4)

    def test_a_borrowed_multi_pillar_instance_places_one_piece(self):
        pillar = ("w", ((T, 12, 4, 5),))
        row = [(entity("wildtemple/pillar01", 0, 0), x, 5.0) for x in (1.0, 5.0, 9.0)]
        kit = P.Kit({pillar: [(0, row)]}, [{}], flags={pillar: bytes([1]) * 25})
        lay = P.Layout(2, 1, {(0, 0): ("w", ((T, 1, 0, 0),)), (1, 0): PD2}, {(1, 0): bytes([1]) * 25})
        self.assertEqual([at(e) for e in P.place(lay, kit, "s")[0]], [(15.0, 5.0)])

    def test_a_bare_wall_bar_beside_a_pillar_tile_stands_a_pillar(self):
        # nemyrmonastery (#737): a stock corpse on the pillar tile covered the wall row beside it by its box; a stock
        # column does not, so the bare wall bar (a whole row blocked, no piece for its key) stands one itself, under
        # the crowd rule like any lone pillar
        wall = ("w", ((T, 1, 0, 0),))
        bar = ("w", (("pd2assets/x/file2.dt1", 2, 0, 9),))
        row = grid(".....", ".....", "#####", ".....", ".....")
        kit = P.Kit({wall: [(0, [(entity("wildtemple/pillar01", 0, 0), 5.0, 5.0)])]}, [{}])
        col = grid("..##.", "..##.", ".....", ".....", ".....")
        lay = P.Layout(5, 1, {(0, 0): wall, (3, 0): PD2, (4, 0): bar}, {(3, 0): col, (4, 0): row})
        ents = P.place(lay, kit, "s")[0]
        self.assertEqual(sorted(round(at(e)[0] // 10) for e in ents), [0, 3, 4])  # wall's own, pillar tile, bar
        near = P.Layout(3, 1, {(0, 0): wall, (1, 0): PD2, (2, 0): bar}, {(1, 0): col, (2, 0): row})
        self.assertEqual(sorted(round(at(e)[0] // 10) for e in P.place(near, kit, "s")[0]), [0, 1])  # crowded: no bar pillar
        far = P.Layout(5, 1, {(0, 0): wall, (1, 0): PD2, (4, 0): bar}, {(4, 0): row})  # no pillar tile within two
        self.assertEqual(sorted(round(at(e)[0] // 10) for e in P.place(far, kit, "s")[0]), [0])
        self.assertEqual(pillars.bars(far, kit), set())

    def test_a_blocked_floor_line_beside_a_pillar_tile_gets_no_bar_pillar(self):
        # a river or water edge is a floor key with a blocked line: no wall, so no pillar stands on it (#737)
        wall = ("w", ((T, 1, 0, 0),))
        floor = ("f", (("pd2assets/x/file2.dt1", 2, 0, 9),))
        row = grid(".....", ".....", "#####", ".....", ".....")
        kit = P.Kit({wall: [(0, [(entity("wildtemple/pillar01", 0, 0), 5.0, 5.0)])]}, [{}])
        lay = P.Layout(3, 1, {(0, 0): wall, (1, 0): PD2, (2, 0): floor}, {(1, 0): grid("..##.", "..##.", ".....", ".....", "....."),
                                                                          (2, 0): row})
        self.assertEqual(pillars.bars(lay, kit), set())
        self.assertEqual(sorted(round(at(e)[0] // 10) for e in P.place(lay, kit, "s")[0]), [0, 1])

    def test_a_2x2_cluster_stands_one_pillar(self):
        wall = ("w", ((T, 1, 0, 0),))
        kit = P.Kit({wall: [(0, [(entity("wildtemple/pillar01", 0, 0), 5.0, 5.0)])]}, [{}])
        full = bytes([1]) * 25
        keys = {(0, 0): wall, **{(x, y): PD2 for x in (1, 2) for y in (0, 1)}}
        lay = P.Layout(3, 2, keys, {(x, y): full for x in (1, 2) for y in (0, 1)})
        self.assertEqual(len([e for e in P.place(lay, kit, "s")[0] if at(e)[0] >= 10]), 1)

    def test_a_lone_pillar_is_skipped_beside_two_pillar_pieces(self):
        wall = ("w", ((T, 1, 0, 0),))
        two = [(entity("wildtemple/pillar01", 0, 0), x, 5.0) for x in (1.0, 9.0)]
        kit = P.Kit({wall: [(0, two)]}, [{}])
        lay = P.Layout(2, 1, {(0, 0): wall, (1, 0): PD2}, {(1, 0): bytes([1]) * 25})
        self.assertEqual([at(e) for e in P.place(lay, kit, "s")[0]], [(1.0, 5.0), (9.0, 5.0)])  # no third beside them

    def test_the_fallback_pick_is_salted_with_the_seed(self):
        wall = ("w", ((T, 1, 0, 0),))
        pieces = [(entity(f"wildtemple/pillar0{i}", 0, 0), 5.0, 5.0) for i in range(1, 4)]
        kit = P.Kit({wall: [(0, pieces)]}, [{}])
        flags = bytes([1]) * 25
        picks = {tuple(e["name"] for e in P.place(P.Layout(4, 1, {(0, 0): wall, (3, 0): PD2}, {(3, 0): flags}), kit, s)[0]
                       if at(e)[0] >= 30) for s in "abcdefghij"}
        self.assertGreater(len(picks), 1)


if __name__ == "__main__":
    unittest.main()
