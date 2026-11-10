"""presets: the floor height pass (#742)."""
import copy
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets import height  # noqa: E402
from presets.build import Build  # noqa: E402
from presets.passes import floor_height as height_pass  # noqa: E402
from presets_fixtures import FLOOR, TERRAIN, entity  # noqa: E402

FACE = ("act1/a/cliff.dt1", 16, 1, 1)  # a lower wall of a cliff DT1: a face
UPPER = ("act1/a/cliff.dt1", 1, 1, 2)  # a left wall of a cliff DT1: also a face
PLAIN = ("act1/a/wall.dt1", 1, 1, 2)  # a left wall of a plain DT1: no height
PLAIN_LOW = ("act1/a/wall.dt1", 16, 1, 3)  # a lower wall of a plain DT1: a face only where it is a ledge
FENCE = ("act1/a/fence.dt1", 16, 1, 3)
WATER = ("act1/a/river.dt1", 16, 1, 3)
ROWS = 6


def floor(x0, x1, rows=ROWS) -> dict:
    return {(x, y): ("f", (FLOOR,)) for x in range(x0, x1) for y in range(rows)}


def column(x, tile, rows=ROWS) -> dict:
    return {(x, y): ("w", (tile,)) for y in range(rows)}


def terrace(tile=FACE) -> P.Layout:
    """x 0-4 a floor, x 5 a face column, x 6-14 the larger floor (the high side is toward smaller x)."""
    return P.Layout(15, ROWS, {**floor(0, 15), **column(5, tile)})


class HeightTests(unittest.TestCase):
    def test_the_side_behind_a_lower_wall_stands_a_step_above_the_larger_floor(self):
        h = height.levels(terrace())
        self.assertEqual(h[(2, 1)], height.STEP)
        self.assertEqual(h[(10, 1)], 0.0)  # the larger region is the base
        self.assertNotIn((5, 1), h)  # the face itself is no floor

    def test_an_upper_wall_of_a_cliff_dt1_is_a_face_too_and_a_plain_wall_is_not(self):
        self.assertEqual(height.levels(terrace(UPPER))[(2, 1)], height.STEP)
        self.assertEqual(set(height.levels(terrace(PLAIN)).values()), {0.0})

    def test_a_region_in_front_of_a_face_is_sunken(self):
        lay = P.Layout(15, ROWS, {**floor(0, 10), **column(10, FACE), **floor(11, 15)})  # x 0-9 larger than x 11-14
        h = height.levels(lay)
        self.assertEqual((h[(2, 1)], h[(12, 1)]), (0.0, -height.STEP))

    def test_a_short_face_or_a_small_region_makes_no_height(self):
        short = P.Layout(15, ROWS, {**floor(0, 15), **column(5, FACE, 2)})
        self.assertEqual(set(height.levels(short).values()), {0.0})  # two face tiles: a wall piece, not a cliff
        nook = P.Layout(15, 2, {**floor(0, 15, 2), **column(5, FACE, 2)})
        self.assertEqual(set(height.levels(nook).values()), {0.0})  # regions of 10 and 18 tiles: doorways

    def test_a_level_never_passes_max_levels(self):
        keys, x = {}, 0
        for _ in range(6):  # six equal terraces, each behind a face: the first is the base, the rest step down
            keys.update(floor(x, x + 5))
            keys.update(column(x + 5, FACE))
            x += 6
        h = height.levels(P.Layout(x, ROWS, keys))
        self.assertEqual((max(h.values()), min(h.values())), (0.0, -height.MAX_LEVELS * height.STEP))

    def test_the_y_axis_faces_2_and_17_lift_the_side_toward_smaller_y(self):
        for orient in (2, 17):
            tile = ("act1/a/cliff.dt1", orient, 1, 1)
            keys = {**{(x, y): ("f", (FLOOR,)) for x in range(6) for y in range(15) if y != 5},
                    **{(x, 5): ("w", (tile,)) for x in range(6)}}  # rows 0-4 above the face, rows 6-14 below
            h = height.levels(P.Layout(6, 15, keys))
            self.assertEqual((h[(2, 1)], h[(2, 10)]), (height.STEP, 0.0), orient)

    def test_the_corner_faces_18_and_19_lift_the_side_toward_smaller_x_and_y(self):
        for orient in (18, 19):
            h = height.levels(terrace(("act1/a/cliff.dt1", orient, 1, 1)))
            self.assertEqual((h[(2, 1)], h[(10, 1)]), (height.STEP, 0.0), orient)

    def test_faces_only_where_the_classifier_says_cliff_or_a_lower_wall_is_a_ledge(self):
        for tile in (PLAIN_LOW, FENCE, WATER):  # a plain lower wall, a fence and a water edge are no ledge
            self.assertEqual(set(height.levels(terrace(tile)).values()), {0.0}, tile)
            self.assertEqual(height.faces(terrace(tile)), {}, tile)
        ledge = {(5, y): ("w", (UPPER, PLAIN_LOW)) for y in range(ROWS)}  # an upper wall over it: a cliff cell
        lay = P.Layout(15, ROWS, {**floor(0, 15), **ledge})
        self.assertEqual(height.levels(lay)[(2, 1)], height.STEP)
        self.assertEqual(set(height.faces(lay)), set(ledge))

    def test_a_plain_dt1_lower_wall_over_a_plain_upper_wall_is_a_ledge_too(self):
        ledge = {(5, y): ("w", (PLAIN, PLAIN_LOW)) for y in range(ROWS)}  # cliff_cell: upper over lower, any DT1
        lay = P.Layout(15, ROWS, {**floor(0, 15), **ledge})
        self.assertEqual(height.levels(lay)[(2, 1)], height.STEP)

    def test_faces_that_disagree_cancel_when_tied_and_the_longer_one_wins_otherwise(self):
        lay = P.Layout(15, 8, {**floor(0, 5, 8), **floor(6, 15, 8), **column(5, FACE, 8)})
        fwd, rev = (((-1, 0),), ((1, 0),)), (((1, 0),), ((-1, 0),))

        def solve(forward_rows):
            faces = {(5, y): fwd if y < forward_rows else rev for y in range(8)}
            with mock.patch("presets.height.faces", return_value=faces):
                return height.levels(lay)
        self.assertEqual(set(solve(4).values()), {0.0})  # four tiles each way: a tie, no height
        h = solve(6)  # six against two: the left side (40 tiles) is high over the right (72), the base
        self.assertEqual((h[(2, 1)], h[(10, 1)]), (height.STEP, 0.0))

    def test_separate_groups_each_take_their_own_base(self):
        keys = {**terrace().keys, **{(x + 20, y): k for (x, y), k in terrace().keys.items()}}  # five empty tiles between
        h = height.levels(P.Layout(35, ROWS, keys))
        self.assertEqual((h[(2, 1)], h[(10, 1)], h[(22, 1)], h[(30, 1)]), (height.STEP, 0.0, height.STEP, 0.0))


def build(lay, entities=(), taken=frozenset(), sunk=None):
    b = Build(lay, P.Kit(), "m", "m.ds1", "b.json", copy.deepcopy(TERRAIN), [], None, None, {}, [])
    b.entities, b.taken = list(entities), set(taken)
    with mock.patch("presets.passes.floor_height.sunk", return_value=sunk or {}):
        height_pass.tiles_at(b)  # fills the state while the stock rooms are mocked away
    return b


def plane(bx, by=0) -> dict:
    e = copy.deepcopy(TERRAIN)
    e["components"][0]["position"].update({"x": 170.0 * bx, "z": 170.0 * by})
    return e


def y_of(e) -> float:
    return e["components"][0]["position"]["y"]


class FloorHeightPassTests(unittest.TestCase):
    def raised(self):
        """Block 1 (x 17-33) stands behind the face column x 34, block 2 (x 34-50) is the larger floor in front of it."""
        return P.Layout(61, ROWS, {**floor(17, 61), **column(34, FACE)})

    def test_the_api_covers_floors_walls_planes_and_faces(self):
        b = build(terrace())
        self.assertEqual(height_pass.ground_at(b, (2, 1)), height.STEP)
        self.assertEqual(height_pass.ground_at(b, (5, 1)), 0.0)  # a face tile: the lower side (a tie between the sides)
        self.assertEqual(height_pass.ground_at(b, (50, 50)), 0.0)  # off the map: the base
        self.assertEqual(height_pass.tiles_at(b)[(10, 1)], 0.0)
        self.assertEqual(height_pass.face_side(b, (5, 1)), (((-1, 0),), ((1, 0),)))  # high side toward smaller x
        self.assertIsNone(height_pass.face_side(b, (2, 1)))

    def test_a_wall_tile_takes_the_height_of_the_nearest_walkable_ground(self):
        keys = {**terrace().keys, (3, 6): ("w", (PLAIN,)), (12, 6): ("w", (PLAIN,)), (30, 30): ("w", (PLAIN,))}
        b = build(P.Layout(40, 40, keys))
        self.assertEqual(height_pass.ground_at(b, (3, 6)), height.STEP)  # below the high side's floor
        self.assertEqual(height_pass.ground_at(b, (12, 6)), 0.0)
        self.assertEqual(height_pass.ground_at(b, (30, 30)), 0.0)  # nothing walkable within reach

    def test_a_blocked_liquid_tile_takes_the_height_of_the_ground_round_it(self):
        keys = {**terrace().keys, (2, 2): ("f", (FLOOR,))}
        b = build(P.Layout(15, ROWS, keys, unwalkable=frozenset({(2, 2)})))  # a hole in the high side's floor
        self.assertNotIn((2, 2), height_pass.tiles_at(b))
        self.assertEqual(height_pass.ground_at(b, (2, 2)), height.STEP)

    def test_every_kind_of_piece_is_lifted_by_the_plane_under_it_and_the_planes_are_set(self):
        pieces = [entity(n, x, 30.0) for n, x in (("floor", 175.0), ("rock", 200.0), ("tree", 250.0), ("pad", 280.0))]
        water = entity("pd2r_stream_water", 180.0, 40.0)
        flat = entity("far", 360.0, 30.0)  # block 2, the base
        b = build(self.raised(), [plane(1), plane(2), *pieces, water, flat])
        height_pass.run(b)
        self.assertEqual([y_of(e) for e in b.entities[:2]], [height.STEP, 0.0])
        self.assertEqual([y_of(e) for e in pieces], [height.STEP] * 4)
        self.assertEqual((y_of(water), y_of(flat), y_of(b.terrain)), (height.STEP, 0.0, 0.0))
        self.assertEqual((height_pass.plane_at(b, (20, 1)), height_pass.plane_at(b, (40, 1))), (height.STEP, 0.0))

    def test_pieces_of_a_sunk_block_are_lowered_with_its_plane(self):
        keys = {**floor(0, 34), **column(34, FACE), **floor(35, 51)}  # x 35-50 stands in front of the face: sunk
        piece = entity("p", 360.0, 30.0)
        b = build(P.Layout(51, ROWS, keys), [plane(2), piece])
        height_pass.run(b)
        self.assertEqual((y_of(b.entities[0]), y_of(piece)), (-height.STEP, -height.STEP))

    def test_pieces_of_a_pasted_room_keep_their_height(self):
        b = build(self.raised(), [plane(1), entity("room piece", 175.0, 30.0)], taken={(17, 3)})
        height_pass.run(b)
        self.assertEqual(y_of(b.entities[1]), 0.0)

    def test_a_sunken_room_never_raises_its_block_and_the_other_blocks_are_unchanged(self):
        b = build(self.raised(), [plane(1), plane(2), entity("a", 175.0, 30.0), entity("b", 360.0, 30.0)],
                  taken={(17, 3)}, sunk={(17, 3): -5.0})
        height_pass.run(b)
        self.assertEqual([y_of(e) for e in b.entities], [0.0] * 4)  # block 1 capped at 0 (not sunk to -5), block 2 flat
        self.assertEqual(height_pass.ground_at(b, (17, 3)), -5.0)

    def test_the_callers_terrain_is_not_changed_and_the_preset_gets_the_raised_plane(self):
        lay = P.Layout(40, ROWS, {**floor(0, 17), **column(17, FACE), **floor(18, 40)})
        terrain = copy.deepcopy(TERRAIN)
        with mock.patch("presets.passes.paste.rooms", return_value=([], set())):
            preset, _ = P.build(lay, P.Kit(), "m", "m.ds1", "b.json", terrain)
        self.assertEqual(terrain, TERRAIN)
        self.assertEqual(y_of(preset["terrain"]), height.STEP)
        planes = sorted((e["components"][0]["position"]["x"], y_of(e)) for e in preset["entities"] if e["name"] == "terrain")
        self.assertEqual(planes, [(170.0, 0.0), (340.0, 0.0)])


if __name__ == "__main__":
    unittest.main()
