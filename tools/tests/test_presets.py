"""pd2_presets on a synthetic DS1, DT1 and stock preset (no PD2 or D2R data)."""
import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

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


def dt1(tiles: list[tuple[int, int, int]], blocked: set = frozenset()) -> bytes:
    """A DT1 of `tiles`; the ones in `blocked` block walking on all 25 subtiles."""
    out = bytearray(276 + 96 * len(tiles))
    struct.pack_into("<2i", out, 0, 7, 6)
    struct.pack_into("<2i", out, 268, len(tiles), 276)
    for k, t in enumerate(tiles):
        struct.pack_into("<3i", out, 276 + 96 * k + 20, *t)
        if t in blocked:
            out[276 + 96 * k + 0x28:276 + 96 * k + 0x28 + 25] = b"\1" * 25
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

    def test_a_tile_keeps_its_subtile_flags_and_a_cells_unwalkable_bit(self):
        data = ds1(3, 1, [{(1, 0): (1, 2, 3)}], [{(0, 0): (0, 1), (2, 0): (0, 1)}])
        at = len(data) - 4 - 3 * 4 - 3 * 4 + 2 * 4  # the floor cell of (2, 0), before the shadow layer
        data = data[:at] + struct.pack("<I", cell(0, 1) | P.UNWALKABLE) + data[at + 4:]
        lay = P.layout(data, {"act1/a/wall.dt1": P.dt1_tiles(dt1([(1, 2, 3)], {(1, 2, 3)})),
                              "act1/a/floor.dt1": P.dt1_tiles(dt1([(0, 0, 1)]))})
        self.assertEqual(lay.flags, {(0, 0): bytes(25), (1, 0): b"\1" * 25, (2, 0): bytes(25)})
        self.assertEqual(lay.keys[(2, 0)], ("f", (FLOOR,)))
        self.assertEqual(lay.unwalkable, {(2, 0)})


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
        self.assertEqual(preset["terrain"]["components"][0]["scale"], {"x": 1.0, "y": 1.0, "z": 1.0})
        self.assertEqual(preset["biomeFilename"], "data/hd/env/biome/x.json")
        deps = {k: [d["path"] for d in v] for k, v in preset["dependencies"].items()}
        self.assertEqual(deps, {"models": ["data/hd/env/model/pillar.model",
                                           "data/hd/env/preset/act2/palace/celSE3/terrain.model"],
                                "textures": ["data/hd/env/texture/pillar_alb.texture"],
                                "json": ["data/hd/env/biome/x.json"]})
        self.assertEqual(TERRAIN["id"], 9)  # the stock entities are copied, not changed

    def test_the_ground_is_one_unscaled_plane_per_block_since_a_scaled_plane_does_not_draw(self):
        lay = P.Layout(20, 2, {(0, 0): ("f", (FLOOR,))})  # 200 x 20 units: two 170-unit blocks across
        preset, _ = P.build(lay, P.Kit(), "m", "m.ds1", "b.json", TERRAIN)
        planes = [e for e in preset["entities"] if e["name"] == "terrain"] + [preset["terrain"]]
        self.assertEqual(sorted((e["components"][0]["position"]["x"], e["components"][0]["position"]["z"])
                                for e in planes), [(0.0, 0.0), (170.0, 0.0)])
        self.assertTrue(all(e["components"][0]["scale"] == {"x": 1.0, "y": 1.0, "z": 1.0} for e in planes))
        self.assertEqual(sorted(e["id"] for e in preset["entities"] + [preset["terrain"]]), [1, 2])

    def test_a_key_keeps_d2rs_empty_instances_so_props_come_at_its_density(self):
        lay = P.Layout(2, 1, {(0, 0): ("w", (A,)), (1, 0): ("w", (A,))})
        kit = P.Kit()
        kit.add(lay, {"dependencies": {}, "entities": [entity("pillar", 3.0, 4.0)]}, {("w", (A,))})
        self.assertEqual([len(ents) for _, ents in kit.instances[("w", (A,))]], [1, 0])
        target = P.Layout(40, 1, {(x, 0): ("w", (A,)) for x in range(40)})
        preset, _ = P.build(target, kit, "m", "m.ds1", "b.json", TERRAIN)
        self.assertTrue(5 < len(preset["entities"]) < 35)
        self.assertEqual(preset, P.build(target, kit, "m", "m.ds1", "b.json", TERRAIN)[0])  # the same every build


    def test_a_far_anchored_piece_on_a_floor_tile_stays_out_of_the_kit(self):
        # D2R's townwest anchors its stairs on a plain ground tile, 4 tiles off their mesh (#679)
        lay = P.Layout(2, 1, {(0, 0): ("f", (FLOOR,)), (1, 0): ("w", (A,))})
        kit = P.Kit(far=lambda e: e["name"].startswith("stairs"))
        kit.add(lay, {"dependencies": {}, "entities": [entity("stairs", 3.0, 4.0), entity("sack", 4.0, 4.0),
                                                       entity("stairs_on_wall", 13.0, 4.0)]},
                {("f", (FLOOR,)), ("w", (A,))})
        self.assertEqual([e["name"] for _, ents in kit.instances[("f", (FLOOR,))] for e, _, _ in ents], ["sack"])
        self.assertEqual([e["name"] for _, ents in kit.instances[("w", (A,))] for e, _, _ in ents], ["stairs_on_wall"])
        found, pkit = P.patches(P.Layout(1, 1, {(0, 0): ("f", (FLOOR,))}), [(lay, {"dependencies": {}, "entities": [
            entity("stairs", 3.0, 4.0)]})], {(0, 0)}, kit.far)
        self.assertFalse(any(ents for opts in pkit.instances.values() for _, ents in opts))

    def test_a_piece_is_far_when_its_box_stands_two_tiles_clear_of_it(self):
        d = Path(tempfile.mkdtemp())
        import pd2_coverage as C
        stairs, sack = "data/hd/env/model/stairs.model", "data/hd/env/model/sack.model"
        pit = "data/hd/env/model/pit.model"  # Duriel's chamber: its centre stands far off, but it reaches its anchor
        recs = sorted([(C.fnv(stairs), (-37.0, 0.0, 28.0, 0.0, 15.0, 55.0)), (C.fnv(sack), (-2.0, 0.0, -2.0, 2.0, 3.0, 2.0)),
                       (C.fnv(pit), (-85.0, 0.0, -113.0, 7.0, 20.0, 2.0))])
        raw = struct.pack("<Ii", len(recs), 4) + b"".join(struct.pack("<I6f", k, *b) + bytes(16) for k, b in recs)
        (d / "hd").mkdir()
        (d / "hd" / "model_lod_desc.bin").write_bytes(raw)
        far = P.far_off(d)
        self.assertTrue(far(entity("stairs", 50.0, 50.0)))
        self.assertFalse(far(entity("sack", 50.0, 50.0)))
        self.assertFalse(far(entity("pit", 50.0, 50.0)))
        self.assertFalse(P.far_off(d / "none")(entity("stairs", 0.0, 0.0)))

    def test_a_waypoint_is_placed_once_per_waypoint_object_at_its_stock_offset(self):
        # D2R's siege waypoint room: its model's transform sits 4 tiles off the waypoint tiles, on plain ground (#669)
        ground, wp = ("f", (FLOOR,)), ("f", (("expansion/town/waypt.dt1", 0, 2, 44),))
        room = P.Layout(5, 5, {**{(x, y): ground for x in range(5) for y in range(5)}, (3, 3): wp})
        kit = P.Kit()
        model = entity("expansion/siege/expansion_siege_waypoints/waypoint01", -0.2, -0.04)
        brazier = entity("global/prop/objects/braziers/travincal_waypoint", 4.0, 4.0)
        kit.add(room, {"dependencies": {"models": [{"path": "wp.model"}]}, "entities": [model, brazier]},
                {ground, wp}, [("waypointwilderness", 38.0, 38.0)])
        self.assertNotIn("waypoint01", json.dumps(list(kit.instances.values())))  # no stray waypoint on plain ground
        self.assertIn("travincal_waypoint", json.dumps(list(kit.instances.values())))  # a brazier is no waypoint
        target = P.Layout(20, 20, {**{(x, y): ground for x in range(20) for y in range(20)}, (13, 13): wp})
        objects = [("waypointwilderness", 138.0, 138.0)]
        preset, _ = P.build(target, kit, "m", "m.ds1", "b.json", TERRAIN, objects=objects)
        wps = [at(e) for e in preset["entities"] if "waypoint01" in e["name"]]
        self.assertEqual([(round(x, 2), round(z, 2)) for x, z in wps], [(99.8, 99.96)])
        self.assertIn("wp.model", json.dumps(preset["dependencies"]))
        # no waypoint object: no waypoint, even on waypoint tiles
        preset, _ = P.build(target, kit, "m", "m.ds1", "b.json", TERRAIN)
        self.assertFalse([e for e in preset["entities"] if "waypoint01" in e["name"]])


    def test_a_pasted_rooms_waypoint_pieces_stay_and_a_room_terrain_needs_a_key_match(self):
        # Act 2's cel*waypoint rooms: the waypoint piece is the room's whole sculpted floor (#680 review)
        keys = {(x, y): ("w", (A,)) if (x + y) % 2 else ("w", (B,)) for x in range(4) for y in range(4)}
        room = P.Layout(4, 4, dict(keys))
        floor = entity("act2/palace/celSEwaypoint/terrain", 5.0, 5.0)
        floor["components"][1]["filename"] = "data/hd/env/preset/act2/palace/celSEwaypoint/terrain.model"
        src = {"dependencies": {}, "entities": [floor, entity("act2/waypoint01", 25.0, 25.0)]}
        kit = P.Kit()
        kit.add(room, src, set(keys.values()), [("waypointact2", 20.0, 20.0)])
        # pasted whole, with no PD2 waypoint object: the room keeps its floor
        preset, _ = P.build(P.Layout(4, 4, dict(keys)), P.Kit(), "m", "m.ds1", "b.json", TERRAIN, stock=[(room, src)])
        self.assertIn("act2/palace/celSEwaypoint/terrain", [e["name"] for e in preset["entities"]])
        # an object on the pasted room's tiles gets nothing more
        preset, _ = P.build(P.Layout(4, 4, dict(keys)), kit, "m", "m.ds1", "b.json", TERRAIN, stock=[(room, src)],
                            objects=[("waypointact2", 20.0, 20.0)])
        self.assertEqual(sum("waypoint" in e["name"] for e in preset["entities"]), 2)
        # elsewhere, an object on another key gets the model by Class, without the room's floor
        other = P.Layout(10, 10, {(5, 5): ("f", (FLOOR,))})
        names = [e["name"] for e in P.build(other, kit, "m", "m.ds1", "b.json", TERRAIN,
                                            objects=[("waypointact2", 50.0, 50.0)])[0]["entities"]]
        self.assertIn("act2/waypoint01", names)
        self.assertNotIn("act2/palace/celSEwaypoint/terrain", names)

    def test_a_waypoint_of_the_same_class_on_the_key_wins_and_a_doubled_object_counts_once(self):
        lay = P.Layout(5, 5, {(3, 3): ("f", (FLOOR,))})
        kit = P.Kit()
        for cls, model in (("waypointexp", "town/waypoint01"), ("waypointwilderness", "siege/waypoint01")):
            kit.add(lay, {"dependencies": {}, "entities": [entity(model, 30.0, 30.0)]}, set(), [(cls, 30.0, 30.0)])
        for cls, model in (("waypointexp", "town/waypoint01"), ("waypointwilderness", "siege/waypoint01")):
            for seed in "abcdef":
                names = [e["name"] for e in P.build(lay, kit, "m", seed, "b.json", TERRAIN,
                                                    objects=[(cls, 32.0, 32.0)])[0]["entities"]]
                self.assertEqual(names, [model])
        data = ds1(2, 2, [{}], [{}])
        data = data[:-4] + struct.pack("<i", 2) + struct.pack("<5i", 2, 0, 8, 8, 0) * 2
        self.assertEqual(P.waypoints(data, {(1, 0): "WaypointOutsideAct1"}), [("waypointoutsideact1", 16.0, 16.0)])


def at(e: dict) -> tuple[float, float]:
    return e["components"][0]["position"]["x"], e["components"][0]["position"]["z"]


class LayerTests(unittest.TestCase):
    """The layers over the per-tile kit: pasted stock rooms, the patch lookup and the border painter."""
    F, WA, WB = ("f", (FLOOR,)), ("w", (A,)), ("w", (B,))

    def test_a_stock_room_matching_60_percent_is_pasted_whole_with_its_terrain(self):
        room = P.Layout(4, 4, {(x, y): self.WA if (x, y) == (0, 0) else self.F for x in range(4) for y in range(4)})
        keys = {(x, y): self.WB for x in range(4) for y in range(4)}
        keys.update({(x + 4, y): k for (x, y), k in room.keys.items()})
        keys[(7, 3)] = self.WB  # 15 of 16 tiles match
        preset, _ = P.build(P.Layout(8, 4, keys), P.Kit(), "m", "m.ds1", "b.json", TERRAIN, stock=[(room, stock())])
        placed = sorted((e["name"], at(e)) for e in preset["entities"])
        self.assertEqual(placed, [("pillar", (43.0, 4.0)), ("terrain", (40.0, 0.0)), ("torch", (54.0, 2.0))])
        pillar = next(e for e in preset["entities"] if e["name"] == "pillar")
        self.assertEqual(pillar["components"][2]["wallTileLocalCoord"], {"x": 4, "y": 0})
        self.assertIn("data/hd/env/texture/pillar_alb.texture",
                      [d["path"] for d in preset["dependencies"]["textures"]])

    def test_a_tile_whose_3x3_appears_in_a_stock_preset_copies_its_centre(self):
        patch = {(x, y): ("w", ((f"p{x}{y}.dt1", 1, 0, 0),)) for x in range(3) for y in range(3)}
        keys = {(x, y): self.WB for x in range(6) for y in range(3)}
        keys.update({(x + 2, y): k for (x, y), k in patch.items()})
        src = {"dependencies": {}, "entities": [entity("torch", 14.0, 12.0)]}
        kit = P.Kit()  # the per-tile kit draws a pillar on that key
        kit.add(P.Layout(1, 1, {(0, 0): patch[(1, 1)]}), {"dependencies": {}, "entities": [entity("pillar", 1, 1)]},
                {patch[(1, 1)]})
        preset, _ = P.build(P.Layout(6, 3, keys), kit, "m", "m.ds1", "b.json", TERRAIN,
                            stock=[(P.Layout(3, 3, patch), src)])
        self.assertEqual([(e["name"], at(e)) for e in preset["entities"]], [("torch", (34.0, 12.0))])

    @mock.patch.object(P.painter, "TALL", 0)  # every tree here is camera-near of the path; the next test covers that
    def test_the_painter_plants_trees_only_past_the_path_clusters_then_singles_then_canopy(self):
        blocked = {(x, 5) for x in range(30)}  # a tree line on row 5; rows 0-4 are the path, rows 6-9 outside

        def lay(w, h):  # the DS1 marks rows 6-9 unwalkable
            keys = {(x, y): self.WA if (x, y) in blocked else self.F for x in range(w) for y in range(h)}
            return P.Layout(w, h, keys, {c: (b"\1" if c in blocked else b"\0") * 25 for c in keys},
                            frozenset(c for c in keys if c[1] >= 6))

        def tree(name, x, z, model, prefab=None):
            comps = [entity(name, x, z)["components"][0], {"type": "ModelDefinitionComponent", "filename": model}]
            if prefab:
                comps.append({"type": "PrefabPlacementDefinitionComponent", "prefab": prefab})
            return {"type": "Entity", "name": name, "id": 1, "components": comps}
        single = "data/hd/env/foliage/act3/model/act3_treeline/small_cluster01.model"
        canopy = "data/hd/env/foliage/act3/model/act3_treeline/act3_treeline_canopy01.model"
        stock = [tree("cluster", x, z, "", "data/hd/env/foliage/act3/model/prefab/pf_tree_wall01.json")
                 for x in (10.0, 50.0, 90.0) for z in (65.0, 95.0)]
        stock += [tree("single", x, z, single) for x in (30.0, 70.0) for z in (55.0, 95.0)]  # one on the line
        stock += [tree("canopy", x, 80.0, canopy) for x in (30.0, 70.0)]
        kit = P.painter.harvest([(lay(10, 10), {"dependencies": {"models": [{"path": single}]}, "entities": stock})])
        strays = P.Kit()  # the per-tile kit puts a tree on every floor tile and on a lone wall tile
        strays.add(P.Layout(1, 1, {(0, 0): self.F}), {"dependencies": {}, "entities": [tree("stray", 5, 5, single)]},
                   {self.F})
        strays.add(P.Layout(1, 1, {(0, 0): self.WB}), {"dependencies": {}, "entities": [tree("lone", 5, 5, single)]},
                   {self.WB})
        target = lay(30, 10)
        target.keys[(12, 2)], target.flags[(12, 2)] = self.WB, b"\1" * 25  # a lone tree on the path
        cluster = {"trees": kit, "prefabs": lambda path: [(0.0, 0.0), (0.0, -20.0)]}  # a cluster reaches 10 subtiles up
        preset, _ = P.build(target, strays, "m", "m.ds1", "b.json", TERRAIN, **cluster)
        names = [e["name"] for e in preset["entities"]]
        where = {n: [at(e) for e in preset["entities"] if e["name"] == n] for n in set(names)}
        # no tree stands on the path's floor (#396), only the lone tree's; the strays past it are dropped and painted again
        self.assertNotIn("stray", where)
        self.assertEqual(where["lone"], [(125.0, 25.0)])
        for n in ("cluster", "single", "canopy"):
            self.assertTrue(where.get(n), n)
            self.assertTrue(all(z >= 50 for _, z in where[n]), (n, where[n]))  # on the line (row 5) or past it
        last = {n: max(i for i, m in enumerate(names) if m == n) for n in ("cluster", "single")}
        first = {n: min(i for i, m in enumerate(names) if m == n) for n in ("single", "canopy")}
        self.assertLess(last["cluster"], first["single"])  # singles fill the band too thin for a cluster
        self.assertLess(last["single"], first["canopy"])
        self.assertIn(single, [d["path"] for d in preset["dependencies"]["models"]])
        self.assertEqual(preset, P.build(target, strays, "m", "m.ds1", "b.json", TERRAIN, **cluster)[0])

    def test_only_roots_and_foreground_tree_walls_stand_on_the_camera_near_side_of_the_path(self):
        keys = {(x, y): self.F for x in range(8) for y in range(16)}  # rows 0-3 the path, rows 4-15 sealed, south of it
        lay = P.Layout(8, 16, keys, {c: b"\0" * 25 for c in keys}, frozenset(c for c in keys if c[1] >= 4))
        front, wall = "pf_tree_wall01_foreground.json", "pf_tree_wall01.json"
        roots = "jungle_roots09_treewall.model"
        trees = [{"type": "Entity", "name": w, "id": 1, "components": [
            entity(w, 0, 0)["components"][0], {"type": "PrefabPlacementDefinitionComponent", "prefab": w}]}
                 for w in (front, wall)]
        trees.append({"type": "Entity", "name": roots, "id": 1, "components": [
            entity(roots, 0, 0)["components"][0], {"type": "ModelDefinitionComponent", "filename": roots}]})
        # trees south of the path: the path is up (bucket 6), no blocked subtile within reach (distance CAP)
        kit = {(6, P.painter.CAP): [(e, 0.0, 0.0, 0, {}) for e in trees]}
        painted, _ = P.painter.paint(lay, (kit, 3.0), "m.ds1", set(), lambda path: [(0.0, 0.0)])
        inner = [e for e, _ in painted if at(e)[0] / 2 >= 30]  # the path spans x up-screen of these
        rows = {n: {int(at(e)[1] / 2) - 20 for e in inner if e["name"] == n} for n in (front, wall, roots)}
        # r subtiles south, a tree is 2r - 8 rows (x + y) below the nearest path subtile within WIDE columns
        self.assertLess(min(rows[front]), 24)  # a foreground wall stands in the border's first rows
        self.assertGreater(min(rows[wall]) * 2 - 8, P.painter.TALL)

    def test_a_pasted_rooms_trees_reaching_walkable_floor_are_dropped(self):
        keys = {(x, y): self.F for x in range(4) for y in range(4)}
        room = P.Layout(4, 4, keys, {c: b"\0" * 25 for c in keys}, frozenset({(3, 3)}))  # the DS1 seals (3, 3)
        trees = [{"type": "Entity", "name": n, "id": 1, "components": [
            entity(n, x, z)["components"][0], {"type": "ModelDefinitionComponent", "filename": "a/small_cluster01.model"}]
            + ([{"type": "PrefabPlacementDefinitionComponent", "prefab": "a/pf_tree_wall01.json"}] if n == "spill" else [])}
            for n, x, z in (("on_floor", 15.0, 15.0), ("sealed", 35.0, 35.0), ("spill", 35.0, 35.0))]
        src = {"dependencies": {}, "entities": trees + [entity("torch", 14.0, 2.0)]}
        preset, _ = P.build(room, P.Kit(), "m", "m.ds1", "b.json", TERRAIN, stock=[(room, src)], trees=({}, 9.0),
                            prefabs=lambda path: [(0.0, 0.0), (-20.0, 0.0)])  # the prefab reaches 2 tiles west
        self.assertEqual(sorted(e["name"] for e in preset["entities"]), ["sealed", "torch"])

    def test_a_dashed_lines_gap_is_playable_only_where_the_ds1_leaves_it_walkable(self):
        keys = {(x, y): self.WA if y == 1 and x % 4 else self.F for x in range(12) for y in range(3)}
        flags = {c: (b"\1" if k == self.WA else b"\0") * 25 for c, k in keys.items()}
        sealed = P.Layout(12, 3, keys, flags, frozenset({(0, 1), (4, 1), (8, 1)} | {(x, 2) for x in range(12)}))
        self.assertEqual(P.painter.playable(sealed), {c for c, k in keys.items() if c[1] == 0 or k == self.WA})
        self.assertIn((4, 1), P.painter.playable(P.Layout(12, 3, keys, flags)))

    def test_blocked_floor_is_a_playable_stream_only_where_it_touches_walkable_floor(self):
        # rows 0-1 ground, row 2 a stream, row 3 ground, row 4 a solid edge wall, rows 5-6 blocked floor past it
        keys = {(x, y): self.WA if y == 4 else self.F for x in range(6) for y in range(7)}
        flags = {c: (b"\1" if c[1] in (2, 4, 5, 6) else b"\0") * 25 for c in keys}
        play = P.painter.playable(P.Layout(6, 7, keys, flags))
        self.assertTrue({(x, 2) for x in range(6)} <= play)
        self.assertFalse({(x, y) for x in range(6) for y in (5, 6)} & play)

    def fill(self, sealed, pillar=(25.0, 25.0), fading=False):
        """The posts a kit lays on a 4x4 stock room pasted whole, where PD2 seals `sealed` with a wall it has not."""
        room = P.Layout(4, 4, {(x, y): self.WA if (x, y) == (0, 0) else self.F for x in range(4) for y in range(4)})
        src = {"dependencies": {}, "entities": [entity("pillar", *pillar)], "terrain": TERRAIN}
        keys = dict(room.keys)
        for c in sealed:
            keys[c] = self.WB
        lay = P.Layout(4, 4, keys, {c: b"\0" * 25 for c in keys}, frozenset({sealed[0]}))
        post = entity("post", 0, 0, (0, 0) if fading else None)
        kit = P.Kit({self.WB: [(0, [(post, 5.0, 5.0)])]}, [{}])
        preset, _ = P.build(lay, kit, "m", "m.ds1", "b.json", TERRAIN, stock=[(room, src)], trees=({}, 9.0))
        return [(P._transform(e)["position"]["x"], P._transform(e)["position"]["z"])
                for e in preset["entities"] if e["name"] == "post"]

    def test_a_border_tile_a_pasted_room_does_not_match_takes_the_kits_piece(self):
        # (0, 0) is on the border, with the playable ground screen-down of it; (1, 1) is playable, not border
        self.assertEqual(self.fill([(0, 0), (1, 1)]), [(5.0, 5.0)])

    def test_a_filled_border_tile_in_front_of_the_play_area_takes_only_a_fading_piece(self):
        # (3, 3) has the playable ground at (2, 3), (3, 2) and (2, 2) screen-up of it: a solid wall would hide it
        self.assertEqual(self.fill([(3, 3)]), [])
        self.assertEqual(self.fill([(3, 3)], fading=True), [(35.0, 35.0)])

    def test_a_border_tile_holding_a_pasted_rooms_piece_gets_no_second_one(self):
        self.assertEqual(self.fill([(0, 0)], pillar=(3.0, 4.0)), [])

    def test_a_stream_dt1_past_the_tree_line_stays_water(self):
        # row 0 ground, row 1 sealed floor (unwalkable), row 2 a stream that touches only the sealed floor
        river = ("f", (("act3/river/rivbank.dt1", 0, 0, 1),))
        keys = {(x, y): river if y == 2 else self.F for x in range(4) for y in range(3)}
        flags = {c: (b"\1" if c[1] == 2 else b"\0") * 25 for c in keys}
        lay = P.Layout(4, 3, keys, flags, frozenset((x, 1) for x in range(4)))
        stream = {(x, 2) for x in range(4)}
        self.assertFalse(stream & P.painter.playable(lay))  # no walkable floor next to it: border ground
        self.assertTrue(stream <= P.painter.playable(lay, frozenset({"act3/river/rivbank.dt1"})))

    def test_an_edge_tile_picks_a_kit_instance_that_holds_a_piece(self):
        kit = P.Kit({self.WA: [(0, []), (0, [(entity("wall", 0, 0), 1.0, 1.0)])]}, [{}])
        lay = P.Layout(1, 1, {(0, 0): self.WA})
        picks = {len(P.place(lay, kit, f"s{i}")[0]) for i in range(20)}
        self.assertEqual(picks, {0, 1})  # off the edge, the empty stock instance is one of the picks
        self.assertEqual({len(P.place(lay, kit, f"s{i}", frozenset({(0, 0)}))[0]) for i in range(20)}, {1})

    def test_a_tree_tile_never_picks_a_stone_wall_or_cliff_instance(self):
        tree = ("w", (("expansion/siege/trees.dt1", 14, 0, 6),))
        kit = P.Kit({tree: [(0, [(entity("siege/wall_stone_large01", 0, 0), 1.0, 1.0)]),
                            (0, [(entity("siege/cliff_rock_large01", 0, 0), 1.0, 1.0)]),
                            (0, [(entity("siege/expansion_tree_dead03", 0, 0), 1.0, 1.0)])]}, [{}])
        lay = P.Layout(1, 1, {(0, 0): tree})
        names = {e["name"] for i in range(20) for e in P.place(lay, kit, f"s{i}")[0]}
        self.assertEqual(names, {"siege/expansion_tree_dead03"})

    def test_only_a_ds1_drawing_a_tree_dt1_is_painted(self):
        outdoor = P.Layout(2, 1, {(0, 0): self.F, (1, 0): ("w", (("act3/jungle/treegrp.dt1", 1, 0, 0),))})
        indoor = P.Layout(2, 1, {(0, 0): self.F, (1, 0): self.WA, (2, 0): ("w", ((None, 1, 0, 0),))})
        self.assertTrue(P.draws_trees(outdoor))
        self.assertFalse(P.draws_trees(indoor))

    def test_rectangles_cover_each_cell_once(self):
        cells = {(0, 0), (1, 0), (2, 0), (0, 1), (1, 1), (2, 1), (1, 2), (5, 5)}
        rects = P.rectangles(cells)
        self.assertEqual(rects, [(0, 0, 3, 2), (1, 2, 1, 1), (5, 5, 1, 1)])
        covered = [(x + i, y + j) for x, y, w, h in rects for i in range(w) for j in range(h)]
        self.assertEqual(sorted(covered), sorted(cells))

    def test_a_long_water_rectangle_is_cut_into_pieces_at_most_twice_as_long_as_wide(self):
        self.assertEqual(P.squarish((0, 0, 1, 5)), [(0, 0, 1, 1), (0, 1, 1, 2), (0, 3, 1, 2)])
        self.assertEqual(P.squarish((4, 2, 9, 2)), [(4, 2, 3, 2), (7, 2, 3, 2), (10, 2, 3, 2)])
        self.assertEqual(P.squarish((0, 0, 3, 2)), [(0, 0, 3, 2)])

    def test_a_water_dt1s_blocked_floor_outside_pasted_rooms_gets_scaled_water_planes(self):
        model = next(iter(P.WATER_SPAN))
        plane = {"type": "Entity", "name": "water", "components": [
            {"type": "TransformDefinitionComponent", "position": {"x": 90.0, "y": 0.0, "z": 80.0},
             "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}, "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
            {"type": "ModelDefinitionComponent", "filename": model}]}
        of = {"textures": [{"path": "data/hd/env/texture/water_alb.texture"}]}
        river, dry = ("act3/river/rivbank.dt1", 0, 1, 0), ("act3/river/rivbank.dt1", 0, 2, 0)
        keys = {(x, y): ("f", (river,)) for x in range(4) for y in range(2)}
        keys[(3, 1)] = ("f", (dry,))  # a bank tile: walkable, no water
        keys[(0, 2)] = ("f", (FLOOR,))  # another DT1's blocked floor (a void, a lava pool): no water
        keys[(1, 2)] = ("f", (river, ("act3/river/pool.dt1", 0, 1, 0)))  # two water DT1 on one tile: one plane
        flags = {c: (bytes(25) if k[1][0] == dry else b"\1" * 25) for c, k in keys.items()}
        preset, _ = P.build(P.Layout(4, 3, keys, flags), P.Kit(), "m", "m.ds1", "b.json", TERRAIN,
                            water={"act3/river/rivbank.dt1": (plane, of), "act3/river/pool.dt1": (plane, of)})
        waters = [e["components"][0] for e in preset["entities"] if e["name"] == P.STREAM]

        def tile(x, z, w):
            return {"x": x, "y": P.WATER_Y, "z": z}, {"x": w * 10 / 180, "y": 1.0, "z": 10 / 180}
        # the pool's tile first (its DT1 sorts first), then the river's rows, each cut at twice its width
        self.assertEqual([(t["position"], t["scale"]) for t in waters],
                         [tile(15.0, 25.0, 1), tile(10.0, 5.0, 2), tile(30.0, 5.0, 2), tile(5.0, 15.0, 1),
                          tile(20.0, 15.0, 2)])
        self.assertIn("data/hd/env/texture/water_alb.texture", [d["path"] for d in preset["dependencies"]["textures"]])
        self.assertEqual(plane["components"][0]["position"]["x"], 90.0)  # the stock plane is copied, not changed


class ApplyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.rule, self.d2r, self.out = t / "presets-hd.tsv", t / "d2r", t / "out"

        def write(path: Path, data):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data if isinstance(data, bytes) else data.replace("|", "\t").encode())
        write(self.d2r / "global/excel/lvlprest.txt", "Name|LevelId|Dt1Mask|File1\nTown|0|1|Act1/S.ds1\n")
        for root in (self.d2r, self.out):
            write(root / "global/excel/objpreset.txt", "Index|Act|ObjectClass\n0|1|WaypointOutsideAct1\n")
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
        self.assertIn("1 presets on PD2 tiles wait for stand-ins", log.getvalue())

    def test_a_preset_on_pd2_tiles_with_stand_ins_draws_the_stand_ins_stock_pieces(self):
        # the PD2 DT1 copies tile B of act1/a/wall.dt1 and adds one D2R lacks; p.ds1 draws both
        (self.out / "global/tiles/pd2assets").mkdir(parents=True)
        (self.out / "global/tiles/pd2assets/x.dt1").write_bytes(dt1([B[1:], (1, 7, 7)], {(1, 7, 7)}))
        (self.out / "global/tiles/expansion/map/p.ds1").write_bytes(ds1(2, 1, [{(0, 0): B[1:], (1, 0): (1, 7, 7)}], [{}]))
        self.rule.write_text("Act 1 - Town\tdata/hd/env/biome/act1_town.json\nPd2 Set\tdata/hd/env/biome/b.json\n"
                             "pd2assets/X.dt1\tact1/a/wall.dt1\n")
        with redirect_stdout(io.StringIO()) as log:
            P.apply(self.rule, Path(), self.d2r, self.out)
        preset = json.loads((self.out / "hd/env/preset/expansion/map/p.json").read_text())
        self.assertEqual([e["name"] for e in preset["entities"]], ["torch"])  # tile B's piece; the new tile stays empty
        self.assertEqual(preset["biomeFilename"], "data/hd/env/biome/b.json")
        self.assertIn("0 presets on PD2 tiles wait", log.getvalue())
        lay = P.layout((self.out / "global/tiles/expansion/map/p.ds1").read_bytes(),
                       P.stand_in(["pd2assets/x.dt1"], P.Tiles(self.d2r / "global/tiles"),
                                  P.Tiles(self.out / "global/tiles"), {"pd2assets/x.dt1": "act1/a/wall.dt1"}))
        self.assertEqual(lay.keys[(1, 0)], ("w", (("pd2assets/x.dt1", 1, 7, 7),)))
        self.assertEqual(lay.flags[(1, 0)], b"\1" * 25)  # PD2's collision stays

    def test_a_stand_in_line_no_preset_loads_fails(self):
        self.rule.write_text("Act 1 - Town\tb.json\npd2assets/gone.dt1\t-\n")
        with self.assertRaisesRegex(ValueError, "stand-in lines for DT1 no PD2-only preset loads: pd2assets/gone.dt1"):
            P.apply(self.rule, Path(), self.d2r, self.out)

    def test_a_water_line_no_preset_draws_from_or_naming_an_unknown_plane_fails(self):
        self.rule.write_text(f"Act 1 - Town\tb.json\nwater:act3/river/gone.dt1\t{next(iter(P.WATER_SPAN))}\n")
        with self.assertRaisesRegex(ValueError, "water lines for DT1 no PD2-only preset draws from: act3/river/gone.dt1"):
            P.apply(self.rule, Path(), self.d2r, self.out)
        self.rule.write_text("Act 1 - Town\tb.json\nwater:act1/a/wall.dt1\tdata/hd/env/model/pond.model\n")
        with self.assertRaisesRegex(ValueError, "no water plane WATER_SPAN knows"):
            P.apply(self.rule, Path(), self.d2r, self.out)

    def test_a_tile_set_with_no_line_fails(self):
        self.rule.write_text("# tile set\tbiome\n")
        with self.assertRaisesRegex(ValueError, "with no line: Act 1 - Town"):
            P.apply(self.rule, Path(), self.d2r, self.out)

    def test_a_line_no_preset_needs_fails(self):
        self.rule.write_text("Act 1 - Town\tb.json\nAct 9 - Gone\tb.json\n")
        with self.assertRaisesRegex(ValueError, "on D2R tiles or stand-ins loads: act 9 - gone"):
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
