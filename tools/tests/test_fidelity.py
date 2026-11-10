"""pd2_fidelity on hand-made layouts and pieces (no PD2 or D2R data)."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_coverage as C  # noqa: E402
import pd2_fidelity as F  # noqa: E402
import pd2_presets as P  # noqa: E402

CLIFF = "act1/outdoors/cliff1.dt1"
FENCE = "act1/outdoors/fence.dt1"
WALLS = "act1/cathedrl/walls.dt1"
TREES = "act1/outdoors/trees.dt1"
RIVER = "act1/outdoors/river.dt1"
FLOORS = "act1/outdoors/floor.dt1"


def w(path: str, orientation: int = 1, main: int = 0) -> tuple:
    return "w", ((path, orientation, main, 0),)


def lay(keys: dict, flags: dict | None = None) -> P.Layout:
    width, height = max(x for x, _ in keys) + 1, max(y for _, y in keys) + 1
    return P.Layout(width, height, keys, flags or {c: bytes(25) for c in keys}, frozenset())


def heights(**by_path) -> dict:
    return {p: {(1, 0, 0): px, (12, 0, 0): px, (14, 0, 0): px} for p, px in by_path.items()}


def piece(kind_dir: str, x0: float, x1: float, top: float, z: float = 5.0) -> dict:
    """An entity standing a model of folder `kind_dir`; the fake box is (x1 - x0) wide, `top` high."""
    return {"name": "e", "components": [
        {"type": "TransformDefinitionComponent", "position": {"x": (x0 + x1) / 2, "y": 0.0, "z": z}},
        {"type": "ModelDefinitionComponent", "filename": f"data/hd/{kind_dir}/{int(x1 - x0)}x{int(top * 10)}.model"}]}


def box(path: str):
    w_, h = (int(n) for n in path.rsplit("/", 1)[-1].removesuffix(".model").split("x"))
    return -w_ / 2, 0.0, -4.9, w_ / 2, h / 10, 4.9


def pieces(*entities: dict) -> F.Hd:
    return F.hd_pieces(list(entities), box, lambda p: [])


def dt1(tiles: list[tuple[int, int, int, int]]) -> bytes:
    """A DT1 holding records (orientation, main, sub, pixel height)."""
    at = 400
    head = bytearray(at)
    struct.pack_into("<2i", head, 268, len(tiles), at)
    body = bytearray()
    for o, m, s, h in tiles:
        rec = bytearray(96)
        struct.pack_into("<i", rec, 8, -h)
        struct.pack_into("<3i", rec, 20, o, m, s)
        body += rec
    return bytes(head + body)


class WallCover(unittest.TestCase):
    """The coverage wall score reads walls by the fidelity height class (#787)."""

    def cover(self, keys: dict, h: dict, *entities: dict, tall: set = frozenset()) -> set:
        return F.wall_cover(F.legacy_edges(lay(keys), h), pieces(*entities), set(tall))

    def test_low_wall_drawn_low_is_covered_and_drawn_tall_is_not(self):
        keys, h = {(0, 0): w(FENCE), (1, 0): w(FENCE)}, heights(**{FENCE: 160})
        self.assertEqual(self.cover(keys, h, piece("act1/outdoors/fences", 1, 9, 2.0)), {(0, 0)})
        self.assertEqual(self.cover(keys, h, piece("act1/outdoors/fences", 1, 9, 5.0)), set())

    def test_tall_fence_dt1_expects_a_tall_piece(self):
        for dt1_path in ("act1/barracks/barset.dt1", "act1/graveyrd.dt1", "act4/mesa/a5_chain_walls.dt1"):
            keys, h = {(0, 0): w(dt1_path)}, heights(**{dt1_path: 288})
            self.assertEqual(self.cover(keys, h, piece("act1/barracks/jail", 1, 9, 5.0)), {(0, 0)}, dt1_path)
            self.assertEqual(self.cover(keys, h, piece("act1/barracks/jail", 1, 9, 2.0)), set(), dt1_path)
        self.assertEqual(F.expected_class("fence", (FENCE, 1, 0, 0), 160), "low")

    def test_tall_wall_needs_a_tall_piece(self):
        keys, h = {(0, 0): w(WALLS)}, heights(**{WALLS: 288})
        self.assertEqual(self.cover(keys, h, piece("act1/cathedrl/walls", 1, 9, 5.0)), {(0, 0)})
        self.assertEqual(self.cover(keys, h, piece("act1/cathedrl/walls", 1, 9, 2.0)), set())

    def test_pillar_by_its_class(self):
        keys, h = {(0, 0): w("act2/sewer/pillars.dt1", 12)}, heights(**{"act2/sewer/pillars.dt1": 288})
        self.assertEqual(self.cover(keys, h, piece("act2/sewer/pillars", 1, 9, 5.0)), {(0, 0)})

    def test_tile_without_an_edge_type_keeps_the_tall_rule(self):
        keys, h = {(0, 0): w("act4/mesa/inv_wall.dt1"), (1, 0): w("act4/mesa/inv_wall.dt1")}, heights()
        self.assertEqual(self.cover(keys, h, tall={(0, 0)}), {(0, 0)})


class Typing(unittest.TestCase):
    def test_model_type_by_folder_and_name(self):
        for path, kind in [("act2/sewer/act2_sewer_walls/cap01.model", "wall"),
                           ("act4/mesa/act4_mesa_cliff_supports/a.model", "cliff"),
                           ("act3/jungle/act3_treewall_combined/a.model", "treeline"),
                           ("act2/model/act2_trees_palm/act2_tree_palm02.model", "treeline"),
                           ("act1/outdoors/act1_outdoors_fences/a.model", "fence"),
                           ("expansion/baallair/expansion_baallair_rails/rail01.model", "fence"),
                           ("act2/sewer/act2_sewer_pillars/pillar04.model", "pillar"),
                           ("act2/sewer/act2_sewer_water/water_channel_ledge01.model", "water"),
                           ("act4/lava/act4_lava_walls/a.model", "wall"),
                           ("act3/jungle/act3_jungle_dungeon_puddlewall/a.model", "wall"),
                           ("act3/jungle/act3_jungle_grass_clump_water/a.model", None),
                           ("act2/outdoors/act2_outdoors_rocks/rock_large01.model", None),
                           ("act2/model/act2_bushes/act2_bush04.model", None)]:
            self.assertEqual(F.model_type(path), kind, path)

    def test_dt1_heights(self):
        self.assertEqual(F.dt1_heights(dt1([(1, 0, 0, 480), (14, 3, 1, 352)])), {(1, 0, 0): 480, (14, 3, 1): 352})

    def test_heights_reads_a_folder_and_an_absent_file_holds_nothing(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "a.dt1").write_bytes(dt1([(1, 0, 0, 192)]))
            h = F.Heights(Path(d))
            self.assertEqual(h["a.dt1"], {(1, 0, 0): 192})
            self.assertEqual(h["b.dt1"], {})

    def test_tile_type(self):
        self.assertEqual(F.tile_type((CLIFF, 1, 0, 0)), "cliff")
        self.assertEqual(F.tile_type(("pd2assets/final_corner.dt1", 3, 0, 0)), "cliff")
        self.assertEqual(F.tile_type((FENCE, 2, 0, 0)), "fence")
        self.assertEqual(F.tile_type((WALLS, 4, 0, 0)), "wall")
        self.assertEqual(F.tile_type((WALLS, 12, 0, 0)), "pillar")
        self.assertEqual(F.tile_type((TREES, 14, 0, 0)), "treeline")
        self.assertEqual(F.tile_type((TREES, 2, 0, 0)), "treeline")  # a tree DT1's wall tile
        for ignored in [(WALLS, 13, 0, 0), (WALLS, 15, 0, 0), (WALLS, 0, 0, 0), (None, 1, 0, 0),
                        ("act4/mesa/inv_wall.dt1", 1, 0, 0)]:
            self.assertIsNone(F.tile_type(ignored), ignored)

    def test_classify_by_name_first(self):
        c = F.classify
        self.assertEqual(c("act4/mesa/inv_wall.dt1", 2), "invisible")
        self.assertEqual(c("act4/mesa/chain_walls.dt1", 1), "fence")
        self.assertEqual(c("expansion/siege/fence.dt1", 1), "fence")
        self.assertEqual(c("act4/fort/foregate.dt1", 3), "fence")
        self.assertEqual(c("act1/barracks/barset.dt1", 1), "fence")
        self.assertEqual(c("act1/outdoors/fence.dt1", 1), "fence")  # 192 px
        self.assertEqual(c("act4/mesa/brick_walls.dt1", 1), "cliff")
        self.assertEqual(c("act3/travincal/terraces.dt1", 2), "cliff")
        self.assertEqual(c("act3/kurast/walls.dt1", 1), "wall")
        self.assertEqual(c("act3/kurast/walls.dt1", 9), "wall")
        self.assertEqual(c("act3/kurast/walls.dt1", 16), "wall")
        self.assertEqual(c("act1/outdoors/river.dt1", 1), "water")
        self.assertEqual(c("act1/outdoors/rivbank.dt1", 2), "water")
        self.assertEqual(c("act4/lava/extwalls.dt1", 1), "water")
        self.assertEqual(c("act4/lava/intwalls.dt1", 1), "water")
        self.assertEqual(c("act4/lava/rocks.dt1", 1), "water")
        self.assertEqual(c("act1/outdoors/rocks.dt1", 1), "wall")
        self.assertEqual(c("act3/jungle/treegrp.dt1", 2), "treeline")

    def test_icecave_interior_is_a_cliff(self):
        for path in ["expansion/icecave/interior.dt1", "act1/icecave/interior.dt1", "expansion/icecave_interior.dt1"]:
            self.assertEqual(F.classify(path, 1), "cliff", path)
        self.assertEqual(F.classify("expansion/icecave/floor.dt1", 1), "wall")

    def test_classify_by_orientation(self):
        c = F.classify
        self.assertEqual(c("act3/kurast/walls.dt1", 12), "pillar")
        self.assertEqual(c("act1/outdoors/trees.dt1", 14), "treeline")
        self.assertEqual(c("act3/kurast/walls.dt1", 0, "floor"), "floor")
        self.assertEqual(c("act3/kurast/walls.dt1", 1, "floor"), "floor")
        self.assertEqual(c("act1/outdoors/river.dt1", 0, "floor"), "floor")
        self.assertEqual(c("act1/outdoors/river.dt1", 0, "floor", blocked=True), "liquid")
        self.assertEqual(c("act4/lava/lavafloor.dt1", 0, "floor", blocked=True), "liquid")
        self.assertEqual(c("act1/outdoors/floor.dt1", 0, "floor", blocked=True), "floor")
        for none in [("act3/kurast/walls.dt1", 13), ("act3/kurast/walls.dt1", 15), (None, 1)]:
            self.assertIsNone(c(*none), none)

    def test_cliff_cell_is_upper_over_lower(self):
        up, low = (WALLS, 2, 0, 0), (WALLS, 17, 0, 0)
        self.assertTrue(F.cliff_cell((up, low)))
        self.assertFalse(F.cliff_cell((up,)))
        self.assertFalse(F.cliff_cell((low,)))

    def test_expected_class(self):
        self.assertEqual(F.expected_class("fence", (FENCE, 1, 0, 0), 192), "low")
        self.assertEqual(F.expected_class("wall", ("act1/outdoors/stonewall.dt1", 1, 0, 0), 224), "low")
        self.assertEqual(F.expected_class("wall", (WALLS, 1, 0, 0), 288), "tall")
        self.assertEqual(F.expected_class("wall", (WALLS, 1, 0, 0), 200), "low")
        self.assertEqual(F.expected_class("cliff", (CLIFF, 1, 0, 0), 450), "tall")
        self.assertIsNone(F.expected_class("water", (RIVER, 0, 0, 0), 128))


class Legacy(unittest.TestCase):
    def test_low_or_tall_by_the_dt1s_median_wall_height(self):
        """Real DT1s (wall-tile heights measured on the PD2 and D2R files): the low families are low, the tall
        dungeon and keep walls tall, though each mixes 128-192 px pieces in."""
        def median_of(*pxs):
            return F.wall_px({(1, i, 0): px for i, px in enumerate(pxs)})
        low = {"stonewall": [128] + [160] * 26 + [192] * 8 + [224] * 3, "borq4": [128] * 10 + [160] * 11 + [192],
               "torturehalls/file2": [128] + [160] * 13 + [192] * 10 + [224] + [256], "fence": [160] * 6 + [192] * 17}
        tall = {"torturehalls/file1": [128] * 3 + [160] * 9 + [192] * 12 + [224] * 5 + [256] * 11 + [288] * 30,
                "lair_mishift": [160] * 6 + [192] * 4 + [224] * 7 + [256] * 12 + [288] * 5,
                "baallair/underwall": [160] * 15 + [224] * 6 + [256] * 8 + [288] * 20,
                "act2/tomb/tomb": [160] * 3 + [288] * 37, "act3/sewer/walls": [192] * 2 + [224] * 9,
                "jungle/dungeon": [160] * 7 + [192] * 14 + [224] * 50 + [256] * 13,
                "new_chamb": [128, 160, 192] + [224] * 6 + [288] * 2}
        for name, pxs in low.items():
            self.assertEqual(F.expected_class("wall", (f"a/{name}.dt1", 1, 0, 0), median_of(*pxs)), "low", name)
        for name, pxs in tall.items():
            self.assertEqual(F.expected_class("wall", (f"a/{name}.dt1", 1, 0, 0), median_of(*pxs)), "tall", name)

    def test_wall_px_counts_wall_orientations_only(self):
        self.assertEqual(F.wall_px({(1, 0, 0): 160, (2, 0, 0): 288, (3, 0, 0): 288, (12, 0, 0): 900, (14, 0, 0): 900}), 288)
        self.assertEqual(F.wall_px({(12, 0, 0): 900}), 0)

    def test_wall_px_skips_warp_and_roof_orientations(self):
        self.assertEqual(F.wall_px({(10, 0, 0): 128, (11, 1, 0): 128}), 0)
        self.assertEqual(F.wall_px({(10, 0, 0): 128, (11, 1, 0): 128, (1, 2, 0): 288}), 288)

    def test_wall_px_weights_the_median_by_use(self):
        """act1/catacomb/basewalls.dt1: {160: 8, 192: 8, 256: 7, 288: 5} is low unweighted, yet the maps place 256+."""
        base = {(1, i, 0): px for i, px in enumerate([160] * 8 + [192] * 8 + [256] * 7 + [288] * 5)}
        self.assertEqual(F.wall_px(base), 192)
        use = {(1, 16, 0): 100, (1, 23, 0): 28, (1, 0, 0): 2}
        self.assertEqual(F.wall_px(base, use), 256)
        self.assertEqual(F.expected_class("wall", ("act1/catacomb/basewalls.dt1", 1, 16, 0), F.wall_px(base, use)), "tall")

    def test_legacy_edges_class_a_mixed_dt1_by_the_tiles_the_map_uses(self):
        h = {WALLS: {(1, i, 0): px for i, px in enumerate([160] * 8 + [192] * 8 + [256] * 7 + [288] * 5)}}
        keys = {(i, 0): w(WALLS, 1, 16 + i % 7) for i in range(10)}
        self.assertEqual(set(F.legacy_edges(lay(keys), h)["wall"].values()), {"tall"})

    def test_legacy_edges_class_a_mixed_dt1_by_each_tile(self):
        """expansion/siege/battle.dt1: 46% of the placed cells are 160-192 px barricades, the rest 256+ px towers."""
        h = {WALLS: {(1, i, 0): px for i, px in enumerate([160] * 3 + [192] * 3 + [256] * 4 + [288] * 3)}}
        keys = {(i, 0): w(WALLS, 1, i) for i in range(13)}
        got = F.legacy_edges(lay(keys), h)["wall"]
        self.assertEqual([got[(i, 0)] for i in range(13)], ["low"] * 6 + ["tall"] * 7)

    def test_legacy_edges_keep_a_tall_dt1_tall_when_a_few_end_pieces_are_short(self):
        """act1/catacomb/basewalls.dt1: 128 of 130 placed cells are 256+ px; the 2 short ones stay tall."""
        h = {WALLS: {(1, i, 0): px for i, px in enumerate([160] + [256] * 10 + [288] * 10)}}
        keys = {(i, 0): w(WALLS, 1, i % 21) for i in range(21)}
        self.assertEqual(set(F.legacy_edges(lay(keys), h)["wall"].values()), {"tall"})

    def test_wall_px_returns_the_tile_only_for_a_mixed_dt1(self):
        mixed = {(1, i, 0): px for i, px in enumerate([160, 160, 288, 288])}
        self.assertEqual(F.wall_px(mixed, own=160), 160)
        self.assertEqual(F.wall_px(mixed), 288)  # no tile asked
        tall = {(1, i, 0): px for i, px in enumerate([160] + [288] * 9)}
        self.assertEqual(F.wall_px(tall, own=160), 288)
        self.assertEqual(F.wall_px({(1, i, 0): 288 for i in range(3)}, own=288), 288)

    def test_legacy_edges_use_the_dt1s_median_not_the_tile(self):
        keys = {(0, 0): w(WALLS, 1, 0)} | {(i, 0): w(WALLS, 1, 1) for i in range(1, 6)}  # 1 short cell in 6
        h = {WALLS: {(1, 0, 0): 128, (1, 1, 0): 288}}
        self.assertEqual(set(F.legacy_edges(lay(keys), h)["wall"].values()), {"tall"})

    def test_heights_fall_back_to_the_next_folder(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            (Path(a) / "x.dt1").write_bytes(dt1([(1, 0, 0, 160)]))
            (Path(b) / "x.dt1").write_bytes(dt1([(1, 0, 0, 999)]))
            (Path(b) / "y.dt1").write_bytes(dt1([(1, 0, 0, 288)]))
            h = F.Heights(Path(a), Path(b))
            self.assertEqual(h["x.dt1"], {(1, 0, 0): 160})
            self.assertEqual(h["y.dt1"], {(1, 0, 0): 288})
            self.assertEqual(h["z.dt1"], {})

    def test_edges_by_type_with_the_tallest_layer(self):
        keys = {(0, 0): w(CLIFF), (1, 0): ("w", ((WALLS, 1, 0, 0), (CLIFF, 1, 0, 0))), (2, 0): w(FENCE),
                (3, 0): w(WALLS, 12), (4, 0): w(TREES, 14), (5, 0): ("f", ((FLOORS, 0, 0, 0),))}
        h = heights(**{CLIFF: 450, WALLS: 200, FENCE: 160, TREES: 400})
        got = F.legacy_edges(lay(keys), h)
        self.assertEqual(got["cliff"], {(0, 0): "tall", (1, 0): "tall"})
        self.assertEqual(got["wall"], {(1, 0): "low"})
        stacked = {(0, 0): ("w", ((WALLS, 2, 0, 0), (WALLS, 17, 0, 0)))}
        self.assertEqual(F.legacy_edges(lay(stacked), heights(**{WALLS: 300}))["cliff"], {(0, 0): "tall"})
        self.assertEqual(got["fence"], {(2, 0): "low"})
        self.assertEqual(got["pillar"], {(3, 0): "tall"})
        self.assertEqual(got["treeline"], {(4, 0): "tall"})
        self.assertEqual(got["water"], {})

    def test_water_edge_is_a_blocked_water_floor_next_to_dry_ground(self):
        keys = {(x, 0): ("f", ((RIVER, 0, 0, 0),)) for x in range(3)} | {(3, 0): ("f", ((FLOORS, 0, 0, 0),))}
        blocked = bytes([1] * 25)
        flags = {c: blocked if c[0] < 3 else bytes(25) for c in keys}
        self.assertEqual(set(F.legacy_edges(lay(keys, flags), heights())["water"]), {(2, 0)})

    def test_dry_floor_of_a_river_dt1_is_no_water(self):
        keys = {(0, 0): ("f", ((RIVER, 0, 0, 0),)), (1, 0): ("f", ((FLOORS, 0, 0, 0),))}
        self.assertEqual(F.legacy_edges(lay(keys), heights())["water"], {})


class HdPieces(unittest.TestCase):
    def test_pieces_by_type_with_tops_and_centres(self):
        hd = pieces(piece("act2_sewer_walls", 0, 20, 6.0), piece("act1_outdoors_fences", 20, 30, 1.5),
                    piece("act2_outdoors_rocks", 30, 40, 5.0), piece("act2_outdoors_rocks", 40, 50, 0.5))
        self.assertEqual(hd.tops["wall"], {(0, 0): 6.0, (1, 0): 6.0, (2, 0): 6.0})
        self.assertEqual(hd.tops["fence"], {(2, 0): 1.5, (3, 0): 1.5})
        self.assertEqual(hd.centres["wall"], [(1, 0)])
        self.assertEqual(hd.other, {(3, 0): 5.0, (4, 0): 5.0})  # the low rock is dropped

    def test_room_sized_boxes_are_left_out(self):
        self.assertEqual(pieces(piece("act2_sewer_walls", 0, 500, 6.0)).tops["wall"], {})

    def test_height_class(self):
        self.assertEqual([F.hd_class(t) for t in (0.2, 1.0, 3.9, 4.0)], ["flat", "low", "low", "tall"])


def hd_of(wall=(), fence=(), tree=(), other=()) -> F.Hd:
    hd = F.Hd()
    for name, tiles in (("wall", wall), ("fence", fence), ("treeline", tree)):
        for c, top in tiles:
            hd.tops[name][c] = top
            hd.centres[name].append(c)
    hd.other.update(dict(other))
    return hd


class Scoring(unittest.TestCase):
    row = {(x, 0): "tall" for x in range(5)}

    def test_all_right(self):
        s = F.score_type("wall", self.row, hd_of(wall=[((x, 0), 6.0) for x in range(5)]))
        self.assertEqual((s.n, s.score, s.spacing), (5, 1.0, 1.0))

    def test_missing_wrong_type_and_wrong_height(self):
        hd = hd_of(wall=[((0, 0), 6.0), ((1, 0), 2.0)], fence=[((2, 0), 1.5)], other=[((3, 0), 5.0)])
        s = F.score_type("wall", self.row, hd)
        self.assertEqual(s.wrong_height, {(1, 0)})
        self.assertEqual(s.wrong_type, {(2, 0), (3, 0)})
        self.assertEqual(s.missing, {(4, 0)})
        self.assertAlmostEqual(s.score, 1 / 5)

    def test_a_low_edge_asked_low_is_wrong_when_tall(self):
        s = F.score_type("fence", {(0, 0): "low", (1, 0): "low"}, hd_of(fence=[((0, 0), 1.5), ((1, 0), 5.0)]))
        self.assertEqual(s.wrong_height, {(1, 0)})

    def test_water_has_no_height(self):
        hd = F.Hd()
        hd.tops["water"][(0, 0)] = 0.05
        self.assertEqual(F.score_type("water", {(0, 0): None}, hd).score, 1.0)

    def test_hole_between_pieces(self):
        hd = hd_of(wall=[((x, 0), 6.0) for x in (0, 1, 3, 4)])
        s = F.score_type("wall", self.row, hd)
        self.assertEqual(s.holes, {(2, 0)})
        self.assertEqual(s.missing, {(2, 0)})
        self.assertAlmostEqual(s.spacing, 1 - 1 / 5)

    def test_a_bare_end_is_no_hole(self):
        s = F.score_type("wall", self.row, hd_of(wall=[((x, 0), 6.0) for x in range(3)]))
        self.assertEqual((s.holes, s.missing), (set(), {(3, 0), (4, 0)}))

    def test_piled_pieces(self):
        hd = hd_of(wall=[((x, 0), 6.0) for x in range(5)])
        hd.centres["wall"] += [(2, 0)] * 12  # 17 pieces on 5 tiles: 7 beyond two a tile
        s = F.score_type("wall", self.row, hd)
        self.assertEqual(s.piled, 7)
        self.assertAlmostEqual(s.spacing, 0.0)

    def test_runs_split_where_tiles_are_not_neighbours(self):
        got = F.runs({(0, 0), (1, 1), (5, 5)})
        self.assertEqual(sorted(map(len, got)), [1, 2])

    def test_score_map_scores_the_types_legacy_has_and_the_floor(self):
        keys = {(0, 0): w(CLIFF), (1, 0): w(CLIFF)}
        got = F.score_map(lay(keys), heights(**{CLIFF: 450}), hd_of(), 100, {(0, 0), (1, 1)}, {(1, 1), (2, 2)})
        self.assertEqual(set(got), {"cliff", "floor"})
        self.assertEqual(got["cliff"].missing, {(0, 0), (1, 0)})
        self.assertEqual(got["floor"].missing, {(0, 0), (1, 1)})
        self.assertEqual(got["floor"].wrong_type, {(2, 2)})
        self.assertIsNone(got["floor"].spacing)
        self.assertAlmostEqual(got["floor"].score, 0.97)

    def test_no_floor_no_floor_row(self):
        self.assertNotIn("floor", F.score_map(lay({(0, 0): w(CLIFF)}), heights(**{CLIFF: 450}), hd_of(), 0, set(), set()))


class Classify(unittest.TestCase):
    def now(self, missing=(), holes=(), spill=()):
        return F.TypeScore(40, set(missing), holes=set(holes), spill=set(spill))

    def test_a_hole_beside_a_tile_main_had_wrong_that_is_now_right(self):
        main = {"bad": {(3, 3)}, "holes": set(), "spill": set()}
        got = F.classify_drop(True, self.now(holes={(4, 3)}), main)  # (3,3) is right now; (4,3) a new hole
        self.assertEqual(got, {F.HOLE_KIND: 1})

    def test_a_tile_main_already_had_wrong_that_is_now_a_hole_beside_a_new_piece(self):
        main = {"bad": {(3, 3), (4, 3)}, "holes": set(), "spill": set()}
        self.assertEqual(F.classify_drop(True, self.now(holes={(4, 3)}, missing={(4, 3)}), main), {F.HOLE_KIND: 1})

    def test_a_tile_main_covered_only_by_spill(self):
        main = {"bad": set(), "holes": set(), "spill": {(7, 7)}}
        self.assertEqual(F.classify_drop(False, self.now(missing={(7, 7)}), main), {F.SPILL_KIND: 1})

    def test_anything_else_and_old_holes(self):
        main = {"bad": set(), "holes": {(1, 1)}, "spill": set()}
        got = F.classify_drop(True, self.now(holes={(1, 1), (9, 9)}), main)
        self.assertEqual(got, {F.OTHER_KIND: 1})  # (1,1) was a hole on main: not a drop
        self.assertEqual(F.classify_drop(False, self.now(), main), {})  # nothing newly bad

    def test_no_tiles_on_main_no_answer(self):
        self.assertEqual(F.classify_drop(False, self.now(missing={(1, 1)}), None), {})
        self.assertEqual(F.kinds_text({}), "-")
        self.assertEqual(F.kinds_text({F.SPILL_KIND: 1, F.HOLE_KIND: 3}), f"3 {F.HOLE_KIND}, 1 {F.SPILL_KIND}")

    def test_tiles_round_trip(self):
        rows = {"m": {"wall": F.TypeScore(5, {(1, 2)}, holes={(3, 4)}, spill={(0, 0), (5, 6)})}}
        self.assertEqual(F.read_tiles(F.tiles_text(rows)), {("m", "wall"): {
            "bad": {(1, 2)}, "holes": {(3, 4)}, "spill": {(0, 0), (5, 6)}}})

    def test_spill_is_right_tiles_under_no_core(self):
        hd = hd_of(wall=[((0, 0), 6.0), ((1, 0), 6.0)])
        hd.core["wall"] = {(0, 0)}
        s = F.score_type("wall", {(0, 0): "tall", (1, 0): "tall"}, hd)
        self.assertEqual(s.spill, {(1, 0)})

    def test_pieces_record_their_core_tiles(self):
        hd = pieces(piece("act2_sewer_walls", 0, 30, 6.0))  # tiles 0-3, but the box only touches the edge of 3
        self.assertEqual(hd.core["wall"], {(t, 0) for t in range(3)})
        self.assertIn((3, 0), hd.tops["wall"])


class Reports(unittest.TestCase):
    def rows(self, missing=0, n=10) -> F.Rows:
        s = F.TypeScore(n, {(i, 0) for i in range(missing)})
        return {"mapa": {"wall": s, "floor": F.TypeScore(20, scored=False)}}

    def test_baseline_roundtrip(self):
        text = F.baseline_text(self.rows(2))
        self.assertEqual(F.read_baseline(text), {("mapa", "wall"): (80.0, 100.0), ("mapa", "floor"): (100.0, None)})

    def test_changes_flag_drops_per_type(self):
        base = F.read_baseline(F.baseline_text(self.rows(0, F.MIN_TILES)))
        found = F.changes(self.rows(3, F.MIN_TILES), base)
        self.assertEqual([(c.name, c.kind, c.drop) for c in found], [("mapa", "wall", True)])
        self.assertEqual(F.by_type(found), {"wall": 1})
        self.assertIn("wall 1", F.changes_md(found))
        self.assertIn("**drop**", F.changes_md(found))

    def test_small_drop_within_tolerance_and_gain_are_no_drop(self):
        base = {("mapa", "wall"): (80.5, 100.0)}
        self.assertFalse(F.changes(self.rows(2), base)[0].drop)  # 80.5 -> 80.0
        self.assertFalse(F.changes(self.rows(0), {("mapa", "wall"): (50.0, 100.0)})[0].drop)

    def test_a_type_no_longer_scored_drops_and_a_new_one_does_not(self):
        found = F.changes(self.rows(0), {("mapa", "pillar"): (90.0, 100.0), ("mapa", "wall"): (100.0, 100.0)})
        self.assertEqual({(c.kind, c.drop) for c in found}, {("pillar", True), ("floor", False)})

    def test_a_small_type_still_drops_on_its_score(self):
        rows = {"m": {"fence": F.TypeScore(F.MIN_TILES - 1, missing={(0, 0)}), "wall": F.TypeScore(F.MIN_TILES, missing={(0, 0), (1, 0)})}}
        base = {("m", "fence"): (100.0, 100.0), ("m", "wall"): (100.0, 100.0)}
        got = {(c.kind, c.drop) for c in F.changes(rows, base)}
        self.assertEqual(got, {("fence", True), ("wall", True)})  # a small pair still drops on its score

    def test_a_small_type_may_lose_spacing_only(self):
        rows = {"m": {"fence": F.TypeScore(F.MIN_TILES - 1)}}
        self.assertEqual([c.drop for c in F.changes(rows, {("m", "fence"): (100.0, 80.0)})], [False])

    def test_a_fall_within_the_tolerance_is_marked_and_listed(self):
        base = {("mapa", "wall"): (100.0, 100.0)}
        found = F.changes(self.rows(1, 40), base)  # 97.5: -2.5
        self.assertEqual([(c.drop, c.within) for c in found if c.kind == "wall"], [(False, True)])
        self.assertIn("within tolerance", F.changes_md(found))
        self.assertTrue(F.changes(self.rows(3, 40), base)[0].drop)  # 92.5: -7.5, past the 5-point default

    def test_unchanged_reports_nothing(self):
        base = F.read_baseline(F.baseline_text(self.rows(1)))
        self.assertEqual(F.changes(self.rows(1), base), [])
        self.assertIn("No map's fidelity changed", F.changes_md([]))

    def test_report_and_write(self):
        with tempfile.TemporaryDirectory() as d:
            found = F.write(Path(d), self.rows(1), "abc", {})
            self.assertEqual(sorted(c.kind for c in found), ["floor", "wall"])
            text = (Path(d) / "fidelity.md").read_text("utf-8")
            self.assertIn("(abc)", text)
            self.assertIn("| mapa | wall | 10 | 90.0% | 1 | 0 | 0 | 100.0% |", text)
            self.assertIn("| floor | 20 | 100.0%", text)
            self.assertTrue((Path(d) / "fidelity-baseline.tsv").is_file())
            self.assertTrue((Path(d) / "fidelity-changes.md").is_file())

    def test_image_colours_each_status(self):
        keys = {(x, 0): w(WALLS) for x in range(5)}
        legacy = {"wall": {(x, 0): "tall" for x in range(5)}}
        s = F.TypeScore(5, {(0, 0)}, {(1, 0)}, {(2, 0)}, {(3, 0)})
        png = F.image(lay(keys), legacy, {"wall": s}, "m")["m_fidelity.png"]
        self.assertTrue(png.startswith(b"\x89PNG"))


if __name__ == "__main__":
    unittest.main()
