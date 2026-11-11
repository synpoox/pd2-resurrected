"""Floating and foreign pieces (#871): the kit hooks that keep them out, and the coverage report that finds them."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_coverage as C  # noqa: E402
import pd2_presets as P  # noqa: E402
from presets.hooks.structure import structure_off  # noqa: E402
from presets_fixtures import FLOOR, A, entity  # noqa: E402


def raised(name: str, x: float, z: float, y: float) -> dict:
    e = entity(name, x, z)
    e["components"][0]["position"]["y"] = y
    return e


def names(kit, key) -> list:
    return [e["name"] for _, ents in kit.instances[key] for e, _, _ in ents]


class Raised(unittest.TestCase):
    def test_a_raised_piece_on_a_floor_tile_stays_out_even_without_a_ground_mesh(self):
        # Westmarch's loft pieces (shields, apples, sacks) stand 14-16 up where the stock terrain mesh does not reach
        lay = P.Layout(1, 1, {(0, 0): ("f", (FLOOR,))})
        kit = P.Kit()
        kit.add(lay, {"dependencies": {}, "entities": [raised("shield01", 3.0, 3.0, 13.9), entity("sack", 4.0, 4.0)]},
                {("f", (FLOOR,))})
        self.assertEqual(names(kit, ("f", (FLOOR,))), ["sack"])

    def test_a_prop_with_nothing_under_it_on_a_wall_tile_stays_out_but_a_mounted_one_stays(self):
        lay = P.Layout(2, 1, {(0, 0): ("w", (A,)), (1, 0): ("w", (A,))})
        heights = {"wall": 12.0, "torch": 1.0, "burnt_beam01": 2.0}
        wall, beam, torch = entity("wall", 3.0, 3.0), raised("burnt_beam01", 4.0, 4.0, 16.9), raised("torch", 5.0, 3.0, 9.0)
        kit = P.Kit(top=lambda e: heights[e["name"]])
        kit.add(lay, {"dependencies": {}, "entities": [wall, torch]}, {("w", (A,))})
        self.assertEqual(names(kit, ("w", (A,))), ["wall", "torch"])  # the wall reaches the torch's base
        kit = P.Kit(top=lambda e: heights[e["name"]])
        kit.add(lay, {"dependencies": {}, "entities": [wall, beam]}, {("w", (A,))})
        self.assertEqual(names(kit, ("w", (A,))), ["wall"])  # a 12-high wall does not reach a beam 17 up

    def test_foliage_and_modelless_pieces_hang_free(self):
        lay = P.Layout(1, 1, {(0, 0): ("w", (A,))})
        ivy = raised("ivy", 3.0, 3.0, 14.0)
        ivy["components"][1]["filename"] = "data/hd/env/foliage/act1_ivy/ivy_hanging02.model"
        light = raised("light", 4.0, 4.0, 14.0)
        light["components"].pop(1)
        kit = P.Kit(top=lambda e: None)
        kit.add(lay, {"dependencies": {}, "entities": [ivy, light]}, {("w", (A,))})
        self.assertEqual(names(kit, ("w", (A,))), ["ivy", "light"])


class Structures(unittest.TestCase):
    def d2r(self, boxes: dict) -> Path:
        d = Path(tempfile.mkdtemp())
        recs = sorted((C.fnv(f"data/hd/env/model/{n}.model"), b) for n, b in boxes.items())
        raw = struct.pack("<Ii", len(recs), 4) + b"".join(struct.pack("<I6f", k, *b) + bytes(16) for k, b in recs)
        (d / "hd").mkdir()
        (d / "hd" / "model_lod_desc.bin").write_bytes(raw)
        return d

    def test_a_house_anchored_on_a_floor_tile_stays_out_of_the_kit_but_not_off_a_wall_tile(self):
        # Tristram's building01 is 53 x 46 x 29: it stood on the Descent's plain floor (#871)
        d = self.d2r({"building": (-26.0, -0.8, -20.0, 27.0, 28.8, 25.0), "sack": (-2.0, 0.0, -2.0, 2.0, 3.0, 2.0),
                      "slab": (-14.0, 0.0, -14.0, 14.0, 2.0, 14.0)})
        s = structure_off(d)
        self.assertTrue(s(entity("building", 0.0, 0.0)))
        self.assertFalse(s(entity("sack", 0.0, 0.0)))
        self.assertFalse(s(entity("slab", 0.0, 0.0)))  # a flat decal is no structure
        self.assertFalse(structure_off(d / "none")(entity("building", 0.0, 0.0)))
        lay = P.Layout(2, 1, {(0, 0): ("f", (FLOOR,)), (1, 0): ("w", (A,))})
        kit = P.Kit(structure=s)
        kit.add(lay, {"dependencies": {}, "entities": [entity("building", 3.0, 4.0), entity("sack", 4.0, 4.0),
                                                       entity("building", 13.0, 4.0)]}, {("f", (FLOOR,)), ("w", (A,))})
        self.assertEqual(names(kit, ("f", (FLOOR,))), ["sack"])
        self.assertEqual(names(kit, ("w", (A,))), ["building"])
        _, pkit = P.patches(P.Layout(1, 1, {(0, 0): ("f", (FLOOR,))}),
                            [(lay, {"dependencies": {}, "entities": [entity("building", 3.0, 4.0)]})], {(0, 0)},
                            structure=s)
        self.assertFalse(any(ents for opts in pkit.instances.values() for _, ents in opts))


class Report(unittest.TestCase):
    BOXES = {"shield": (-2.0, 0.0, -2.0, 2.0, 2.0, 2.0), "wall": (-5.0, 0.0, -5.0, 5.0, 20.0, 5.0)}

    def ents(self, *specs):
        return [{"name": name, "components": [
            {"type": "TransformDefinitionComponent", "position": {"x": x, "y": y, "z": z}},
            {"type": "ModelDefinitionComponent", "filename": f"data/hd/env/model/{name}.model"}]}
            for name, x, z, y in specs]

    def box(self, path):
        return self.BOXES.get(path.rsplit("/", 1)[-1].removesuffix(".model"))

    def test_floating_pieces_are_those_with_nothing_under_them(self):
        ents = self.ents(("shield", 15, 15, 14.0),  # over empty ground
                         ("wall", 55, 55, 0.0), ("shield", 55, 55, 12.0),  # mounted on a wall that reaches it
                         ("shield", 85, 85, 3.0))  # low
        found = C.floating_pieces(ents, self.box, lambda p: [])
        self.assertEqual(found, [((1, 1), "data/hd/env/model/shield.model")])

    def test_foliage_and_room_pieces_are_not_floating(self):
        ents = self.ents(("shield", 15, 15, 14.0), ("shield", 35, 35, 14.0))
        ents[0]["components"][1]["filename"] = "data/hd/env/foliage/x/shield.model"
        ents[1]["name"] = P.ROOM_PIECE + "shield"
        self.assertEqual(C.floating_pieces(ents, self.box, lambda p: []), [])

    def test_the_report_lists_floating_pieces_per_map(self):
        s = C.Score("a", "A", 3, {(0, 0)}, floating=(((1, 1), "x/shield.model"), ((2, 1), "x/shield.model")))
        text = C.report([s, C.Score("b", "B", 3, set())])
        self.assertIn("## Floating pieces", text)
        self.assertIn("| a | 2 | shield x2 |", text)
        self.assertIn("report-only", text)


if __name__ == "__main__":
    unittest.main()
