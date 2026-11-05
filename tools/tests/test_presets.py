"""pd2_presets on a synthetic DS1, DT1 and stock preset (no PD2 or D2R data)."""
import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_presets as P  # noqa: E402

A, B = ("act1/a/wall.dt1", 1, 2, 3), ("act1/a/wall.dt1", 4, 5, 6)
FLOOR = ("act1/a/floor.dt1", 0, 0, 1)


def cell(main: int, sub: int) -> int:
    return main << 20 | sub << 8 | 1


def ds1(w: int, h: int, walls: list[dict], floors: list[dict], files: list[str] = ()) -> bytes:
    """A version 18 DS1 of w x h tiles: walls[k] maps (x, y) to (orientation, main, sub), floors[k] to (main, sub)."""
    head = struct.pack("<6i", 18, w - 1, h - 1, 0, 0, len(files)) + b"".join(f.encode() + b"\0" for f in files)
    head += struct.pack("<2i", len(walls), len(floors))

    def grid(f) -> bytes:
        return struct.pack(f"<{w * h}I", *(f((i % w, i // w)) for i in range(w * h)))
    body = b"".join(grid(lambda c, L=L: cell(*L[c][1:]) if c in L else 0) + grid(lambda c, L=L: L[c][0] if c in L else 0)
                    for L in walls)
    body += b"".join(grid(lambda c, L=L: cell(*L[c]) if c in L else 0) for L in floors)
    return head + body + bytes(w * h * 4) + struct.pack("<i", 0)


def dt1(tiles: list[tuple[int, int, int]]) -> bytes:
    out = bytearray(276 + 96 * len(tiles))
    struct.pack_into("<2i", out, 0, 7, 6)
    struct.pack_into("<2i", out, 268, len(tiles), 276)
    for k, t in enumerate(tiles):
        struct.pack_into("<3i", out, 276 + 96 * k + 20, *t)
    return bytes(out)


def entity(name: str, x: float, z: float, wall: tuple[int, int] | None = None) -> dict:
    comps = [{"type": "TransformDefinitionComponent", "position": {"x": x, "y": 0.0, "z": z},
              "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
             {"type": "ModelDefinitionComponent", "filename": f"data/hd/env/model/{name}.model"}]
    if wall:
        comps.append({"type": "WallTransparencyComponent", "wallTileLocalCoord": {"x": wall[0], "y": wall[1]}})
    return {"type": "Entity", "name": name, "id": 77, "components": comps}


TERRAIN = {"type": "Entity", "name": "terrain", "id": 9, "components": [
    {"type": "TransformDefinitionComponent", "position": {"x": 0.0, "y": 0.0, "z": 0.0}, "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
    {"type": "ModelDefinitionComponent", "filename": "data/hd/env/preset/act2/palace/celSE3/terrain.model"}]}


def stock() -> dict:
    return {"dependencies": {"models": [{"path": "data/hd/env/model/pillar.model"}],
                             "textures": [{"path": "data/hd/env/texture/pillar_alb.texture"}]},
            "entities": [entity("pillar", 3.0, 4.0, wall=(0, 0)), entity("torch", 14.0, 2.0),
                         {"type": "Entity", "name": "no transform", "components": []}],
            "terrain": TERRAIN, "biomeFilename": "data/hd/env/biome/x.json"}


class LayoutTests(unittest.TestCase):
    def test_tiles_are_named_by_the_first_dt1_holding_them(self):
        data = ds1(2, 1, [{(1, 0): (1, 2, 3)}, {(1, 0): (9, 9, 9)}], [{(0, 0): (0, 1), (1, 0): (0, 1)}],
                   ["\\d2\\data\\global\\tiles\\act1\\a\\wall.tg1", "DS1EDIT_SAVE_COUNT=2"])
        self.assertEqual(P.header_dt1s(data), ["act1/a/wall.dt1"])
        dt1s = {"act1/b/floor.dt1": P.dt1_tiles(dt1([(0, 0, 1)])), "act1/a/floor.dt1": P.dt1_tiles(dt1([(0, 0, 1)])),
                "act1/a/wall.dt1": P.dt1_tiles(dt1([(1, 2, 3)]))}
        lay = P.layout(data, dt1s)
        self.assertEqual((lay.width, lay.height), (2, 1))
        self.assertEqual(lay.keys, {(0, 0): ("f", (FLOOR,)), (1, 0): ("w", (A, (None, 9, 9, 9)))})


class BuildTests(unittest.TestCase):
    def kit(self) -> P.Kit:
        lay = P.Layout(2, 1, {(0, 0): ("w", (A,)), (1, 0): ("w", (B,))})
        kit = P.Kit()
        kit.add(lay, stock(), {("w", (A,)), ("w", (B,))})
        return kit

    def test_each_tile_gets_its_keys_instance_and_two_walls_fall_back_to_each_alone(self):
        lay = P.Layout(3, 1, {(2, 0): ("w", (A,)), (0, 0): ("w", (A, B)), (1, 0): ("f", (FLOOR,))})
        preset, missing = P.build(lay, self.kit(), "m", "expansion/map/m.ds1", "data/hd/env/biome/x.json", TERRAIN)
        self.assertEqual(missing, 1)  # the floor: no stock preset drew one
        placed = [(e["name"], e["id"], e["components"][0]["position"]["x"], e["components"][0]["position"]["z"])
                  for e in preset["entities"]]
        self.assertEqual(placed, [("pillar", 1, 3.0, 4.0), ("torch", 2, 4.0, 2.0), ("pillar", 3, 23.0, 4.0)])
        self.assertEqual(preset["entities"][2]["components"][2]["wallTileLocalCoord"], {"x": 2, "y": 0})
        self.assertEqual(preset["terrain"]["id"], 4)
        self.assertEqual(preset["terrain"]["components"][0]["scale"], {"x": 30 / 170, "y": 1.0, "z": 10 / 170})
        self.assertEqual(preset["biomeFilename"], "data/hd/env/biome/x.json")
        deps = {k: [d["path"] for d in v] for k, v in preset["dependencies"].items()}
        self.assertEqual(deps, {"models": ["data/hd/env/model/pillar.model",
                                           "data/hd/env/preset/act2/palace/celSE3/terrain.model"],
                                "textures": ["data/hd/env/texture/pillar_alb.texture"],
                                "json": ["data/hd/env/biome/x.json"]})
        self.assertEqual(TERRAIN["id"], 9)  # the stock entities are copied, not changed

    def test_a_key_keeps_d2rs_empty_instances_so_props_come_at_its_density(self):
        lay = P.Layout(2, 1, {(0, 0): ("w", (A,)), (1, 0): ("w", (A,))})
        kit = P.Kit()
        kit.add(lay, {"dependencies": {}, "entities": [entity("pillar", 3.0, 4.0)]}, {("w", (A,))})
        self.assertEqual([len(ents) for _, ents in kit.instances[("w", (A,))]], [1, 0])
        target = P.Layout(40, 1, {(x, 0): ("w", (A,)) for x in range(40)})
        preset, _ = P.build(target, kit, "m", "m.ds1", "b.json", TERRAIN)
        self.assertTrue(5 < len(preset["entities"]) < 35)
        self.assertEqual(preset, P.build(target, kit, "m", "m.ds1", "b.json", TERRAIN)[0])  # the same every build


class ApplyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.rule, self.d2r, self.out = t / "presets-hd.tsv", t / "d2r", t / "out"

        def write(path: Path, data):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data if isinstance(data, bytes) else data.replace("|", "\t").encode())
        write(self.d2r / "global/excel/lvlprest.txt", "Name|LevelId|Dt1Mask|File1\nTown|0|1|Act1/S.ds1\n")
        write(self.out / "global/excel/lvlprest.txt", "Name|LevelId|Dt1Mask|File1|File2|File3|File4|File5|File6\n"
              "Town|0|1|Act1/S.ds1|||||\nMap|1|1|Expansion/Map/M.ds1|0||||\nPd2 Map|2|1|Expansion/Map/P.ds1|||||\n")
        write(self.out / "global/excel/levels.txt", "Name|LevelType\nTown|0\nMap|0\nPd2 Map|1\n")
        write(self.out / "global/excel/lvltypes.txt", "Name|File 1\nAct 1 - Town|Act1/A/Wall.dt1\n"
              "Pd2 Set|pd2assets/x.dt1\n")
        write(self.d2r / "global/tiles/act1/a/wall.dt1", dt1([A[1:], B[1:]]))
        write(self.d2r / "global/tiles/act1/s.ds1", ds1(2, 1, [{(0, 0): A[1:], (1, 0): B[1:]}], [{}],
                                                       ["\\d2\\data\\global\\tiles\\act1\\a\\wall.tg1"]))
        write(self.d2r / "hd/env/preset/act1/s.json", json.dumps(stock()).encode())
        write(self.d2r / f"hd/env/preset/{P.TERRAIN}.json", json.dumps({"terrain": TERRAIN}).encode())
        write(self.out / "global/tiles/expansion/map/m.ds1", ds1(1, 2, [{(0, 1): B[1:]}], [{}]))
        write(self.out / "global/tiles/expansion/map/p.ds1", ds1(1, 1, [{(0, 0): B[1:]}], [{}]))

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_preset_on_d2r_tiles_gets_its_kit_and_one_on_pd2_tiles_none(self):
        self.rule.write_text("# tile set\tbiome\nAct 1 - Town\tdata/hd/env/biome/act1_town.json\n")
        with redirect_stdout(io.StringIO()) as log:
            P.apply(self.rule, Path(), self.d2r, self.out)
        preset = json.loads((self.out / "hd/env/preset/expansion/map/m.json").read_text())
        self.assertEqual([(e["name"], e["components"][0]["position"]["z"]) for e in preset["entities"]],
                         [("torch", 12.0)])
        self.assertEqual(preset["biomeFilename"], "data/hd/env/biome/act1_town.json")
        self.assertFalse((self.out / "hd/env/preset/expansion/map/p.json").exists())
        self.assertIn("1 presets on PD2 tiles have none", log.getvalue())

    def test_a_tile_set_with_no_line_fails(self):
        self.rule.write_text("# tile set\tbiome\n")
        with self.assertRaisesRegex(ValueError, "with no line: Act 1 - Town"):
            P.apply(self.rule, Path(), self.d2r, self.out)

    def test_a_line_no_preset_needs_fails(self):
        self.rule.write_text("Act 1 - Town\tb.json\nAct 9 - Gone\tb.json\n")
        with self.assertRaisesRegex(ValueError, "no PD2-only preset on D2R tiles loads: act 9 - gone"):
            P.apply(self.rule, Path(), self.d2r, self.out)

    def test_without_d2rs_presets_in_the_cache_it_makes_none(self):
        self.rule.write_text("Act 1 - Town\tb.json\n")
        (self.d2r / f"hd/env/preset/{P.TERRAIN}.json").unlink()
        (self.d2r / "hd/env/preset/act1/s.json").unlink()
        for d in ("hd/env/preset/act2/palace", "hd/env/preset/act2", "hd/env/preset/act1", "hd/env/preset"):
            (self.d2r / d).rmdir()
        with redirect_stdout(io.StringIO()) as log:
            P.apply(self.rule, Path(), self.d2r, self.out)
        self.assertIn("skipped", log.getvalue())
        self.assertFalse((self.out / "hd").exists())


if __name__ == "__main__":
    unittest.main()
