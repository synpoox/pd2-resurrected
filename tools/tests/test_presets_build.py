"""pd2_presets: build(), its passes and the kit."""
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
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets_fixtures import A, B, FLOOR, TERRAIN, at, cell, dt1, ds1, entity, stock  # noqa: E402,F401


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

    def test_a_block_mostly_under_rooms_gets_the_plane_for_its_other_tiles_unless_a_room_is_sunken(self):
        # block 2 (x 17-33): a pasted room on x 17-30, loose floor on x 31-33 (#705); block 3 (x 34-50): all room
        lay = P.Layout(51, 2, {(x, y): ("f", (FLOOR,)) for x in range(51) for y in range(2)})
        taken = {(x, y) for x in range(17, 31) for y in range(2)} | {(x, y) for x in range(34, 51) for y in range(2)}

        def planes(sunk: set) -> list[float]:
            with mock.patch("presets.passes.paste.rooms", return_value=([], taken)), \
                    mock.patch("presets.passes.ground.sunken", return_value=sunk):
                preset, _ = P.build(lay, P.Kit(), "m", "m.ds1", "b.json", TERRAIN)
            return sorted(e["components"][0]["position"]["x"] for e in preset["entities"] + [preset["terrain"]]
                          if e["name"] == "terrain")
        self.assertEqual(planes(set()), [0.0, 170.0])
        self.assertEqual(planes({(20, 0)}), [0.0])  # a canal in the room: the plane would hide it

    def test_sunken_tiles_are_a_pasted_rooms_ground_below_the_plane(self):
        class Ground:
            def at(self, x, z):
                return -5.0 if x < 10 else None if x > 20 else 0.0
        room = P.Layout(3, 1, {(0, 0): ("f", (FLOOR,)), (1, 0): ("f", (FLOOR,)), (2, 0): ("f", (FLOOR,))})
        self.assertEqual(P.sunken([(0, 4, 7)], [(room, {P.GROUND: Ground()})]), {(4, 7)})
        self.assertEqual(P.sunken([(0, 4, 7)], [(room, {})]), set())  # no ground read: not sunken

    def test_a_key_keeps_d2rs_empty_instances_so_props_come_at_its_density(self):
        lay = P.Layout(2, 1, {(0, 0): ("w", (A,)), (1, 0): ("w", (A,))})
        kit = P.Kit()
        kit.add(lay, {"dependencies": {}, "entities": [entity("pillar", 3.0, 4.0)]}, {("w", (A,))})
        self.assertEqual([len(ents) for _, ents in kit.instances[("w", (A,))]], [1, 0])
        target = P.Layout(40, 1, {(x, 0): ("w", (A,)) for x in range(40)})
        preset, _ = P.build(target, kit, "m", "m.ds1", "b.json", TERRAIN)
        self.assertTrue(5 < len(preset["entities"]) < 35)
        self.assertEqual(preset, P.build(target, kit, "m", "m.ds1", "b.json", TERRAIN)[0])  # the same every build


    def test_an_indoor_map_takes_no_plants_from_an_outdoor_stock_room(self):
        # Kurast's sewer entrances share the sewers' floor tiles and hold banyans (#707)
        lay = P.Layout(1, 1, {(0, 0): ("f", (FLOOR,))})
        kit = P.Kit()
        kit.add(lay, {"dependencies": {}, "entities": [entity("banyan_tree", 3.0, 4.0), entity("sack", 4.0, 4.0)]},
                {("f", (FLOOR,))}, outdoor=True)
        kit.add(lay, {"dependencies": {}, "entities": [entity("courtyard_palm", 3.0, 4.0)]}, {("f", (FLOOR,))},
                outdoor=False)
        names = [[e["name"] for e, _, _ in ents] for _, ents in P.indoor(kit).instances[("f", (FLOOR,))]]
        self.assertEqual(names, [["sack"], ["courtyard_palm"]])
        self.assertEqual(len(kit.instances[("f", (FLOOR,))][0][1]), 2)  # the outdoor kit keeps its banyan

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
        self.assertIn(P.ROOM_PIECE + "act2/palace/celSEwaypoint/terrain", [e["name"] for e in preset["entities"]])  # marked by the height pass
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
