"""pd2_presets: pasted rooms, patches, the border painter, like(), place() and water."""
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

    def test_a_big_stock_room_pastes_over_one_bare_tile_in_a_thousand(self):
        # Uber Tristram's 2156-tile town: one PD2 edit leaves one walkable tile on the room's wall (#713 review)
        n = 40  # 1600 tiles: one bare tile is within ROOM_BARE, two are not
        room = P.Layout(n, n, {(x, y): self.WA if (x, y) in ((0, 0), (5, 5), (6, 6)) else self.F
                               for x in range(n) for y in range(n)})
        one = {**room.keys, (5, 5): self.F}
        two = {**one, (6, 6): self.F}
        self.assertEqual(len(P.rooms(P.Layout(n, n, one), [(room, {})])[0]), 1)
        self.assertEqual(len(P.rooms(P.Layout(n, n, two), [(room, {})])[0]), 0)

    def test_a_stock_room_is_not_pasted_where_the_ds1s_walkable_floor_lands_off_the_rooms(self):
        room = P.Layout(4, 4, {(x, y): self.WA if (x, y) == (0, 0) else self.F for x in range(4) for y in range(4)},
                        unwalkable=frozenset({(3, 3)}))  # the room's lava corner
        keys = dict(room.keys)
        self.assertEqual(len(P.rooms(P.Layout(4, 4, keys, unwalkable=frozenset({(3, 3)})), [(room, {})])[0]), 1)
        # the DS1 walks on that corner: pasted, the room would draw no ground under it
        self.assertEqual(P.rooms(P.Layout(4, 4, keys), [(room, {})])[0], [])
        keys[(3, 3)] = self.WB  # a wall there instead: the room matches 15 of 16 and is pasted
        self.assertEqual(len(P.rooms(P.Layout(4, 4, keys), [(room, {})])[0]), 1)

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

    @mock.patch.object(P.painter, "TALL", 0)
    def test_a_kit_tree_on_the_edge_stays_next_to_painted_trees(self):
        blocked = {(x, 5) for x in range(30)}  # the tree line on row 5 is the edge; rows 6-9 are painted

        def lay(w, h):
            keys = {(x, y): self.WA if (x, y) in blocked else self.F for x in range(w) for y in range(h)}
            return P.Layout(w, h, keys, {c: (b"\1" if c in blocked else b"\0") * 25 for c in keys},
                            frozenset(c for c in keys if c[1] >= 6))
        model = "data/hd/env/foliage/act3/model/act3_treeline/small_cluster01.model"

        def tree(name, x, z):
            return {"type": "Entity", "name": name, "id": 1, "components": [
                entity(name, x, z)["components"][0], {"type": "ModelDefinitionComponent", "filename": model}]}
        kit = P.painter.harvest([(lay(10, 10), {"dependencies": {}, "entities": [
            tree("painted", x, z) for x in (10.0, 50.0, 90.0) for z in (75.0, 95.0)]})])
        edge = P.Kit()  # the per-tile kit stands a tree on each wall tile of the line
        edge.add(P.Layout(1, 1, {(0, 0): self.WA}), {"dependencies": {}, "entities": [tree("edge", 5, 5)]}, {self.WA})
        preset, _ = P.build(lay(30, 10), edge, "m", "m.ds1", "b.json", TERRAIN, trees=kit)
        where = {n: [at(e) for e in preset["entities"] if e["name"] == n] for n in ("painted", "edge")}
        self.assertTrue(where["painted"])
        self.assertEqual(sorted(where["edge"]), [(x * 10 + 5.0, 55.0) for x in range(30)])  # none dropped

    def test_a_kit_tree_on_the_near_edge_is_dropped_but_its_roots_stay(self):
        blocked = {(x, 5) for x in range(30)}  # row 5, the edge, lies camera-near of the path (rows 0-4)

        def lay(w, h):
            keys = {(x, y): self.WA if (x, y) in blocked else self.F for x in range(w) for y in range(h)}
            return P.Layout(w, h, keys, {c: (b"\1" if c in blocked else b"\0") * 25 for c in keys},
                            frozenset(c for c in keys if c[1] >= 6))

        def tree(name, x, z, model):
            return {"type": "Entity", "name": name, "id": 1, "components": [
                entity(name, x, z)["components"][0], {"type": "ModelDefinitionComponent", "filename": model}]}
        front = "data/hd/env/foliage/act3/model/prefab/pf_tree_wall01_foreground.json"
        kit = P.painter.harvest([(lay(10, 10), {"dependencies": {}, "entities": [
            tree("painted", x, z, front) for x in (10.0, 50.0, 90.0) for z in (65.0, 85.0)]})])
        edge = P.Kit()  # the per-tile kit stands a pine and roots on each wall tile of the line
        edge.add(P.Layout(1, 1, {(0, 0): self.WA}), {"dependencies": {}, "entities": [
            tree("pine", 5, 5, "a/small_cluster01.model"), tree("roots", 5, 5, "a/jungle_roots01_treewall.model")]},
            {self.WA})
        preset, _ = P.build(lay(30, 10), edge, "m", "m.ds1", "b.json", TERRAIN, trees=kit)
        names = [e["name"] for e in preset["entities"]]
        self.assertIn("painted", names)  # a foreground wall near the line: the line's pines go to the painter
        self.assertNotIn("pine", names)
        self.assertEqual(names.count("roots"), 30)

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

    def test_a_pd2_pillar_tile_without_a_stand_in_borrows_the_stock_pillar_of_its_footprint(self):
        T = "expansion/wildtemple/interior.dt1"
        short, tall = ("w", ((T, 12, 4, 3),)), ("w", ((T, 12, 4, 5),))
        solid, ring = bytes([1]) * 25, bytes(12) + bytes([1]) + bytes(12)  # a full tile, and a pillar on its centre subtile
        kit = P.Kit({short: [(0, [(entity("short01", 0, 0), 5.0, 5.0)])], tall: [(0, [(entity("tall01", 0, 0), 5.0, 5.0)])]},
                    [{}], flags={short: solid, tall: ring})
        pd2 = ("w", (("pd2assets/torturehalls/file2.dt1", 12, 9, 1),))
        lay = P.Layout(2, 1, {(0, 0): ("w", ((T, 1, 0, 0),)), (1, 0): pd2}, {(0, 0): solid, (1, 0): ring})
        ents, _, missing = P.place(lay, kit, "s")
        self.assertEqual([(e["name"], P._transform(e)["position"]["x"]) for e in ents], [("tall01", 15.0)])
        self.assertEqual(missing, 1)  # (0, 0): no LIKE layer, so it borrows nothing

    def test_a_pd2_tile_whose_footprint_shares_too_little_borrows_nothing(self):
        T = "expansion/wildtemple/interior.dt1"
        chair = ("w", ((T, 12, 9, 9),))
        kit = P.Kit({chair: [(0, [(entity("chair01", 0, 0), 5.0, 5.0)])]}, [{}], flags={chair: bytes(25)})
        pd2 = ("w", (("pd2assets/torturehalls/file2.dt1", 12, 9, 1),))
        block = bytes([1]) * 5 + bytes(20)
        for flags in (bytes(25), block):  # blocks nothing; blocks what the chair (nothing) does not
            lay = P.Layout(2, 1, {(0, 0): ("w", ((T, 1, 0, 0),)), (1, 0): pd2}, {(1, 0): flags})
            self.assertEqual(P.place(lay, kit, "s")[0], [])

    def test_the_kit_keeps_the_like_keys_of_the_drawn_dt1s(self):
        T = "expansion/wildtemple/interior.dt1"
        lay = P.Layout(2, 1, {(0, 0): ("w", ((T, 12, 4, 5),)), (1, 0): ("w", ((T, 1, 0, 0),))})
        kit = P.Kit()
        kit.add(lay, {"dependencies": {}, "entities": [entity("tall01", 3.0, 4.0), entity("rug", 13.0, 4.0)]},
                set(), alike=frozenset({T}))
        self.assertEqual(list(kit.instances), [("w", ((T, 12, 4, 5),))])

    def test_build_gives_place_the_maps_subtile_flags(self):
        T = "expansion/wildtemple/interior.dt1"
        tall = ("w", ((T, 12, 4, 5),))
        ring = bytes(12) + bytes([1]) + bytes(12)
        kit = P.Kit({tall: [(0, [(entity("tall01", 0, 0), 5.0, 5.0)])]}, [{}], flags={tall: ring})
        lay = P.Layout(2, 1, {(0, 0): ("w", (("pd2assets/torturehalls/file2.dt1", 12, 9, 1),)),
                              (1, 0): ("w", ((T, 1, 0, 0),))}, {(0, 0): ring})
        preset, _ = P.build(lay, kit, "m", "m.ds1", "b.json", TERRAIN)
        self.assertIn("tall01", [e["name"] for e in preset["entities"]])

    def test_an_edge_tile_picks_a_kit_instance_that_holds_a_piece(self):
        kit = P.Kit({self.WA: [(0, []), (0, [(entity("wall", 0, 0), 1.0, 1.0)])]}, [{}])
        lay = P.Layout(1, 1, {(0, 0): self.WA})
        picks = {len(P.place(lay, kit, f"s{i}")[0]) for i in range(20)}
        self.assertEqual(picks, {0, 1})  # off the edge, the empty stock instance is one of the picks
        self.assertEqual({len(P.place(lay, kit, f"s{i}", frozenset({(0, 0)}))[0]) for i in range(20)}, {1})

    def test_an_edge_tile_picks_a_kit_instance_that_holds_a_tree(self):
        tree = entity("act5_tree_wall01", 0, 0)
        kit = P.Kit({self.WA: [(0, [(entity("rock", 0, 0), 1.0, 1.0)]), (0, [(tree, 1.0, 1.0)])]}, [{}])
        lay = P.Layout(1, 1, {(0, 0): self.WA})
        edge = frozenset({(0, 0)})
        names = {e["name"] for i in range(20) for e in P.place(lay, kit, f"s{i}", edge, far=edge)[0]}
        self.assertEqual(names, {"act5_tree_wall01"})  # the rock alone is a piece too low to close the border
        # on the near side (not `far`) a tree would be drawn over the path: the pick stays the stock one (#454)
        self.assertEqual({e["name"] for i in range(20) for e in P.place(lay, kit, f"s{i}", edge)[0]},
                         {"act5_tree_wall01", "rock"})
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
