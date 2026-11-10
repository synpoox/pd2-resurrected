"""presets: the ground height pass (#742)."""
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets import height  # noqa: E402
from presets.build import Build  # noqa: E402
from presets.passes import height as height_pass  # noqa: E402
from presets_fixtures import FLOOR, TERRAIN  # noqa: E402

FACE = ("act1/a/cliff.dt1", 16, 1, 1)  # a lower wall: a face
UPPER = ("act1/a/cliff.dt1", 1, 1, 2)  # a left wall of a cliff DT1: also a face
PLAIN = ("act1/a/wall.dt1", 1, 1, 2)  # a left wall of a plain DT1: no height
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

    def test_the_pass_exposes_the_levels_to_later_passes(self):
        b = Build(terrace(), P.Kit(), "m", "m.ds1", "b.json", TERRAIN, [], None, None, {}, [])
        height_pass.run(b)
        self.assertEqual(height_pass.ground_at(b, (2, 1)), height.STEP)
        self.assertEqual(height_pass.ground_at(b, (5, 1)), 0.0)  # off the floor: the base
        self.assertEqual(height_pass.tiles_at(b)[(10, 1)], 0.0)


class GroundHeightTests(unittest.TestCase):
    def planes(self, lay, sunk=None):
        with mock.patch("presets.passes.paste.rooms", return_value=([], set())), \
                mock.patch("presets.passes.height.sunk", return_value=sunk or {}), \
                mock.patch("presets.passes.ground.sunken", return_value=set(sunk or {})):
            preset, _ = P.build(lay, P.Kit(), "m", "m.ds1", "b.json", TERRAIN)
        return sorted((e["components"][0]["position"]["x"], e["components"][0]["position"]["y"])
                      for e in preset["entities"] + [preset["terrain"]] if e["name"] == "terrain")

    def test_a_raised_block_gets_a_raised_plane_and_the_rest_stay_flat(self):
        # x 17-33 stands behind the face column x 34, the larger floor x 35-60 in front of it
        lay = P.Layout(61, ROWS, {**floor(17, 61), **column(34, FACE)})
        self.assertEqual(self.planes(lay), [(0.0, 0.0), (170.0, height.STEP), (340.0, 0.0), (510.0, 0.0)])

    def test_the_terrain_plane_at_the_origin_is_raised_too(self):
        lay = P.Layout(40, ROWS, {**floor(0, 17), **column(17, FACE), **floor(18, 40)})
        self.assertEqual(self.planes(lay)[0], (0.0, height.STEP))

    def test_a_plane_over_a_sunken_room_goes_under_the_rooms_ground(self):
        lay = P.Layout(20, 2, {(0, 0): ("f", (FLOOR,)), (18, 0): ("f", (FLOOR,))})
        planes = self.planes(lay, sunk={(3, 0): -5.0})  # the first block holds a canal 5 units down
        self.assertEqual(planes[0], (0.0, -5.0 - 0.05))
        self.assertEqual(planes[1], (170.0, 0.0))


if __name__ == "__main__":
    unittest.main()
