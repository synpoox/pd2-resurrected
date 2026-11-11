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
        shield = raised("shield01", 3.0, 3.0, 13.9)
        shield["components"][1]["filename"] = "data/hd/env/town/act2_town_shields/shield01.model"
        disc = raised("shield02", 5.0, 5.0, 14.0)  # Westmarch's loft shields, banners and decals
        disc["components"][1]["filename"] = "data/hd/env/model/global/prop/siege/expansion_siege_weapons/shield01.model"
        ledge = raised("skull_pile", 3.0, 5.0, 13.9)  # a mesa ledge's own raised scenery stays
        light = raised("light", 5.0, 3.0, 13.9)
        light["components"].pop(1)  # and a piece with no model has no height
        kit.add(lay, {"dependencies": {}, "entities": [shield, disc, ledge, light, entity("sack", 4.0, 4.0)]},
                {("f", (FLOOR,))})
        self.assertEqual(names(kit, ("f", (FLOOR,))), ["skull_pile", "light", "sack"])

    def test_a_furnishing_held_up_by_a_kept_piece_stays_but_one_on_a_dropped_stack_goes(self):
        # a fortress roof's snow rests on its roof piece; a loft's shield rests on a sack that is itself in the air (#909)
        lay = P.Layout(1, 1, {(0, 0): ("f", (FLOOR,))})
        heights = {"roof": 12.0, "snow": 1.0, "sack": 2.0, "disc": 1.0, "heap": 3.0, "heap2": 3.0}

        def snow(name: str, x: float, y: float) -> dict:
            e = raised(name, x, 4.0, y)
            e["components"][1]["filename"] = "data/hd/env/model/snowprops/" + name + ".model"
            return e
        kit = P.Kit(top=lambda e: heights[e["name"]])
        kit.add(lay, {"dependencies": {}, "entities": [entity("roof", 3.0, 4.0), snow("snow", 3.0, 11.0),
                                                       snow("sack", 6.0, 20.0), snow("disc", 6.0, 21.5),
                                                       snow("heap", 8.0, 7.0), snow("heap2", 8.0, 21.0)]},
                {("f", (FLOOR,))})
        # the heap lies at 7 (low, stays); heap2 is 9 over the roof's top
        self.assertEqual(names(kit, ("f", (FLOOR,))), ["roof", "snow", "heap"])

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
        d = self.d2r({"building": (-26.0, -0.8, -20.0, 27.0, 28.8, 25.0), "tristram_buildings/building": (-26.0, -0.8, -20.0, 27.0, 28.8, 25.0), "sack": (-2.0, 0.0, -2.0, 2.0, 3.0, 2.0),
                      "slab": (-14.0, 0.0, -14.0, 14.0, 2.0, 14.0)})
        s = structure_off(d)

        def house(x: float, z: float) -> dict:
            e = entity("building", x, z)
            e["components"][1]["filename"] = "data/hd/env/model/tristram_buildings/building.model"
            return e
        self.assertFalse(s(entity("building", 0.0, 0.0)))  # a big piece outside a `_buildings` folder is scenery
        self.assertTrue(s(house(0.0, 0.0)))
        self.assertFalse(s(entity("sack", 0.0, 0.0)))
        self.assertFalse(s(entity("slab", 0.0, 0.0)))  # a flat decal is no structure
        self.assertFalse(structure_off(d / "none")(house(0.0, 0.0)))
        lay = P.Layout(2, 1, {(0, 0): ("f", (FLOOR,)), (1, 0): ("w", (A,))})
        kit = P.Kit(structure=s)
        kit.add(lay, {"dependencies": {}, "entities": [house(3.0, 4.0), entity("sack", 4.0, 4.0), house(13.0, 4.0)]},
                {("f", (FLOOR,)), ("w", (A,))})
        self.assertEqual(names(kit, ("f", (FLOOR,))), ["sack"])
        self.assertEqual(names(kit, ("w", (A,))), ["building"])
        _, pkit = P.patches(P.Layout(1, 1, {(0, 0): ("f", (FLOOR,))}),
                            [(lay, {"dependencies": {}, "entities": [house(3.0, 4.0)]})], {(0, 0)}, structure=s)
        self.assertFalse(any(ents for opts in pkit.instances.values() for _, ents in opts))
        # patches() passes `top` to its Kit: a beam the wall reaches (top 14 vs base 13) stays
        beam = raised("burnt_beam01", 4.0, 4.0, 13.0)
        beam["components"][1]["filename"] = "data/hd/env/model/burnt_beam01.model"
        wl = P.Layout(1, 1, {(0, 0): ("w", (A,))})
        pl, pk = P.patches(wl, [(wl, {"dependencies": {}, "entities": [entity("wall", 3.0, 3.0), beam]})], {(0, 0)},
                          top=lambda e: 14.0 if e["name"] == "wall" else 2.0)
        self.assertEqual(names(pk, pl.keys[(0, 0)]), ["wall", "burnt_beam01"])


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
