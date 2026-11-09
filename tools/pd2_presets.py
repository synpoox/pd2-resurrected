"""HD presets for the PD2-only presets on D2R tiles (LP3, ADR 0005): one generated preset JSON per DS1.

Two halves with one seam, the Layout. layout() reads a DS1 into a layout: per tile, a key for what lies there, its
wall tiles (one per wall layer) or, with no wall, its floor tiles. A tile is named (DT1 file, orientation, main
index, sub index), the DT1 being the first, by path, of the DS1's DT1 files that holds that tile: a PD2 preset's are
the ones its level type and Dt1Mask load, a stock DS1's the ones its header lists. build() makes the preset from a
layout and a kit, harvested from D2R's stock DS1 and presets: each stock entity belongs to the tile under its
position (one tile is TILE HD units, DS1 x along x, DS1 y along z), so a key collects every instance D2R drew on such
a tile, empty ones too. build() gives each tile one of its key's instances, picked by a hash of the DS1 path and the
tile, so props come at D2R's density. A key no stock preset has falls back to each of its tiles alone (a wall on two
layers), else the tile stays empty. The ground is D2R's stock flat plane (TERRAIN), one unscaled copy per
TERRAIN_SIZE block (a scaled plane does not draw), textured by the tile set's biome; the preset depends on what the stock presets it copies from depend on. Over the
per-tile kit, build() pastes whole stock rooms, applies the patch lookup and, for a DS1 that draws tree tiles, the
border painter (pd2_painter), all from the tile set's stock: the presets that list a DT1 its DS1 draw from. Last,
the water: a stock room draws its stream as one room-sized mesh, so outside pasted rooms a water DT1's blocked floor
tiles get a stock water plane, scaled to cover them in rectangles.

merge/presets-hd.tsv: `tile set <tab> biome`, one line per lvltypes Name the PD2-only presets on D2R tiles load. The
merge fails on a PD2-only preset on D2R tiles whose tile set has no line, and on a line no such preset needs. A
preset that loads a PD2 DT1 (pd2assets/) waits until each of them has a stand-in line (LP4/LP5), `PD2 DT1 <tab> D2R
DT1`, or `-` for none: a PD2 tile the stand-in holds too (the same orientation, main and sub index and subtile flags;
most PD2 DT1 are copies or recolours of a D2R one) is named by it, so it gets that tile's stock pieces; the rest stay
empty in HD, their
collision kept. A stand-in line no PD2-only preset loads fails the merge too. `water:<D2R DT1> <tab> <water plane
model>` names a DT1 whose blocked floor is water (WATER_SPAN gives each plane's size); a line for a DT1 no PD2-only
preset draws from fails. Needs D2R's tiles and
presets in modkit's cache, else it makes none (dev builds run without, ADR 0005):
`python -m modkit fetch --prefix data/global/tiles/ --prefix data/hd/env/preset/`.
"""
from __future__ import annotations

import copy
import functools
import json
import re
import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path

import pd2_dt1_copies as dt1_copies
import pd2_painter as painter
import pd2_terrain as terrain_ground
from pd2_ids import Table, excel, load
from pd2_levels import SLOTS, col, ds1_header, norm, object_list

TILE = 10  # HD units per DS1 tile
GROUND = "_ground"  # a stock preset's pd2_terrain.Ground, set by apply() for Kit.add; never written out
# a floor tile's stock pieces standing higher than this above their ground are a building's upper parts (Kurast's
# pillars and roof lips stand 9-26 up), dropped as the tile may have no building in a PD2 DS1 (#682)
FLOOR_MAX = 8.0
# a DS1 cell's unwalkable bit: what blocks the plain floor in PD2's dashed tree lines and the ground past the paths
# (jungle1: on 96% of the lines' gap tiles; D2R's stock jungle rooms stand 88% of their trees on such tiles)
UNWALKABLE = 0x20000
TERRAIN = "act2/palace/celse3"  # a stock preset whose terrain is a flat TERRAIN_SIZE square (D2R Reimagined's plane)
TERRAIN_SIZE = 170
ROOM_MATCH, ROOM_MIN = 0.6, 16  # a stock room of ROOM_MIN tiles or more is pasted where this share of its tiles match
SQUARE = [(i, j) for j in (-1, 0, 1) for i in (-1, 0, 1)]
CROSS = [(0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)]
PRESETS = Path("hd") / "env" / "preset"
# a stock water plane's model -> its side in HD units, centred on its position (its .physics box: +-90)
WATER_SPAN = {"data/hd/env/model/act3/kurast/act3_kurast_water/water_16x16_01.model": 180.0}
WATER_Y = 0.05  # just over the flat ground plane (y 0), which would hide water at the stock rooms' -5 to 0
STREAM = "pd2r_stream_water"  # the name of the water planes streams() places, for the merge's count
WATER_RATIO = 2  # a water rectangle longer than this many times its width is split, so its texture stretches less
FETCH = "python -m modkit fetch --prefix data/global/tiles/ --prefix data/hd/env/preset/"

Tile = tuple  # (DT1 path or None, orientation, main, sub)
Key = tuple  # ("w" or "f", tiles)


def dt1_tiles(data: bytes) -> dict[tuple[int, int, int], bytes]:
    """The (orientation, main, sub) of every tile in a DT1 -> its 25 subtile flags (bit 0 blocks walking), the
    first tile's for a key held twice."""
    count, at = struct.unpack_from("<2i", data, 268)
    out = {}
    for k in range(count):
        out.setdefault(struct.unpack_from("<3i", data, at + k * 96 + 20), data[at + k * 96 + 40:at + k * 96 + 65])
    return out


def header_dt1s(data: bytes) -> list[str]:
    """The DT1 paths (under global/tiles) a DS1's header lists: the editor's .tg1 names."""
    version, o = struct.unpack_from("<i", data)[0], 12 + 4 * (struct.unpack_from("<i", data)[0] >= 8)
    o += 4 * (version >= 10)
    out = []
    if version >= 3:
        n, o = struct.unpack_from("<i", data, o)[0], o + 4
        for _ in range(n):
            end = data.index(b"\0", o)
            name, o = norm(data[o:end].decode("latin-1")), end + 1
            if "/tiles/" in name:
                out.append(name.split("/tiles/", 1)[1].rsplit(".", 1)[0] + ".dt1")
    return out


@dataclass
class Layout:
    width: int  # tiles
    height: int
    keys: dict[tuple[int, int], Key]  # (x, y) -> key; tiles with nothing are left out
    flags: dict[tuple[int, int], bytes] = field(default_factory=dict)  # (x, y) -> its layers' subtile flags, OR'd
    unwalkable: frozenset = frozenset()  # the tiles a wall or floor cell marks unwalkable (UNWALKABLE)


def layout(data: bytes, dt1s: dict[str, dict], copies: dict[Tile, Tile] | None = None) -> Layout:
    """The DS1's tiles, named in the DT1 files `dt1s` (path -> dt1_tiles); a tile in `copies` (pd2_dt1_copies) is
    named by the D2R tile it copies, its flags kept."""
    copies = copies or {}
    version, w, h, _act, _tag, o, walls, floors = ds1_header(data)
    if version < 4:
        raise ValueError(f"DS1 version {version}: only 4 and up keep the tile layers in the order layout() reads")
    n = w * h
    grids = [struct.unpack_from(f"<{n}I", data, o + k * n * 4) for k in range(2 * walls + floors)]
    order = sorted(dt1s)

    def tile(orientation: int, cell: int) -> Tile:
        t = (orientation, cell >> 20 & 0x3F, cell >> 8 & 0xFF)
        # a copied tile is named from its own PD2 DT1 first: an earlier DT1 holding the same index would steal it
        # (Zhar Library's arcane walls became palace cellar tiles, #678 review)
        return (next((p for p in order if t in dt1s[p] and (p, *t) in copies), None)
                or next((p for p in order if t in dt1s[p]), None), *t)
    keys, flags = {}, {}
    for i in range(n):
        wall = tuple(tile(grids[2 * k + 1][i] & 0xFF, grids[2 * k][i]) for k in range(walls) if grids[2 * k][i])
        floor = tuple(tile(0, g[i]) for g in grids[2 * walls:] if g[i])
        if wall or floor:
            keys[(i % w, i // w)] = ("w", tuple(copies.get(t, t) for t in wall)) if wall else \
                ("f", tuple(copies.get(t, t) for t in floor))
            f = [dt1s[t[0]][t[1:]] for t in wall + floor if t[0]]
            flags[(i % w, i // w)] = bytes(functools.reduce(lambda u, v: [x | y for x, y in zip(u, v)], f, bytes(25)))
    cells = [grids[2 * k] for k in range(walls)] + list(grids[2 * walls:])
    unwalkable = frozenset((i % w, i // w) for i in range(n) if any(g[i] & UNWALKABLE for g in cells))
    return Layout(w, h, keys, flags, unwalkable)


def _transform(entity: dict) -> dict | None:
    return next((c for c in entity["components"] if c["type"] == "TransformDefinitionComponent"), None)


def is_waypoint(entity: dict) -> bool:
    """A waypoint's piece: its model, or its stock room's sculpted terrain (SiegeWaypoint1/terrain.model); not Act 3's
    travincal_waypoint braziers."""
    return any(c["type"] == "ModelDefinitionComponent" and "waypoint" in c["filename"].lower()
               and "/braziers/" not in c["filename"].lower() for c in entity["components"])


def waypoints(data: bytes, classes: dict[tuple[int, int], str]) -> list[tuple[str, float, float]]:
    """A DS1's waypoint objects, (objects Class, x, z) in HD units; `classes` is objpreset's (Act, Index) -> ObjectClass."""
    act, at, count, size = object_list(data)
    out = []
    for k in range(count):
        kind, oid, x, y = struct.unpack_from("<4i", data, at + 4 + k * size)
        cls = classes.get((act + 1, oid), "") if kind == 2 else ""
        at_ = (cls.lower(), x * TILE / 5, y * TILE / 5)  # 5 subtiles per tile
        if "waypoint" in cls.lower() and at_[1:] not in [o[1:] for o in out]:  # frozenriver3 lists one twice
            out.append(at_)
    return out


def room_terrain(entity: dict) -> bool:
    """A stock room's whole sculpted floor (its terrain.model): Act 2's cel*waypoint rooms reach 14 tiles off the
    waypoint, so only a waypoint on the same tile key gets one (place_waypoints)."""
    return any(c["type"] == "ModelDefinitionComponent" and c["filename"].lower().endswith("/terrain.model")
               for c in entity["components"])


def wanted(lay: Layout) -> set[Key]:
    """The keys build() may look up for a layout: each key and, for its fallback, each of its tiles alone."""
    return {k for key in lay.keys.values() for k in (key, *((key[0], (t,)) for t in key[1]))}


def _floor(key: Key) -> bool:
    """A floor tile's key, or a patch key (patches()) whose centre tile is a floor."""
    if key[0] in ("sq", "cross"):
        key = key[1][4 if key[0] == "sq" else 0]
    return key is not None and key[0] == "f"


@dataclass
class Kit:
    instances: dict[Key, list[tuple[int, list[tuple[dict, float, float]]]]] = field(default_factory=dict)
    sources: list[dict[str, list[dict]]] = field(default_factory=list)  # each stock preset's dependencies
    # a waypoint's pieces, as offsets from its object: under ("kc", key of the tile under the object, objects Class),
    # ("k", key) and, without a room terrain, ("c", Class)
    waypoints: dict[Key | str, list[tuple[int, list[tuple[dict, float, float]]]]] = field(default_factory=dict)

    def add(self, lay: Layout, preset: dict, keep: set[Key], objects: list[tuple[str, float, float]] = ()) -> None:
        """Harvest one stock (layout, preset) pair, only the keys in `keep`. A waypoint's pieces stay out of the
        per-tile kit, as D2R puts their transform up to 4 tiles off the waypoint's tiles (#669): each joins the
        nearest of the DS1's waypoint `objects` (waypoints()), or is dropped if it has none. A pasted room keeps
        its own (build()). With the preset's ground (GROUND, from pd2_terrain), each piece keeps its height above
        that ground, as the generated terrain is flat."""
        src = len(self.sources)
        self.sources.append(preset["dependencies"])
        ground = preset.get(GROUND)
        per: dict[tuple[int, int], list] = {c: [] for c, k in lay.keys.items() if k in keep}
        groups: dict[int, list] = {}
        for e in preset["entities"]:
            t = _transform(e)
            if t is None:
                continue
            x, z = t["position"]["x"], t["position"]["z"]
            if is_waypoint(e):
                if objects:
                    n = min(range(len(objects)), key=lambda k: (objects[k][1] - x) ** 2 + (objects[k][2] - z) ** 2)
                    groups.setdefault(n, []).append((e, x - objects[n][1], z - objects[n][2]))
                continue
            c = (min(max(int(x // TILE), 0), lay.width - 1), min(max(int(z // TILE), 0), lay.height - 1))
            if c in per:
                g = ground.at(x, z, t["position"]["y"]) if ground else None
                if g is not None and t["position"]["y"] - g > FLOOR_MAX and _floor(lay.keys[c]):
                    continue  # a building's upper part over this floor: its walls are other tiles', so it would float
                if g:
                    e = copy.deepcopy(e)
                    _transform(e)["position"]["y"] -= g
                per[c].append((e, x - c[0] * TILE, z - c[1] * TILE))
        for c, ents in per.items():
            self.instances.setdefault(lay.keys[c], []).append((src, ents))
        for n, ents in groups.items():
            cls, x, z = objects[n]
            key = lay.keys.get((int(x // TILE), int(z // TILE)))
            models = [p for p in ents if not room_terrain(p[0])]
            for k, group in ((("kc", key, cls), ents), (("k", key), ents), (("c", cls), models)):
                if group and k[1]:
                    self.waypoints.setdefault(k, []).append((src, group))


def waypoint_options(lay: Layout, kit: Kit, cls: str, x: float, z: float) -> list:
    """The stock waypoint groups for a waypoint object: from a stock object of its Class on the same tile key, else
    of any Class on that key, else of its Class (models only)."""
    key = lay.keys.get((int(x // TILE), int(z // TILE)))
    return kit.waypoints.get(("kc", key, cls)) or kit.waypoints.get(("k", key)) or kit.waypoints.get(("c", cls), [])


def place_waypoints(lay: Layout, kit: Kit, seed: str, objects: list[tuple[str, float, float]], taken: set = frozenset()
                    ) -> tuple[list[dict], set[int]]:
    """One stock waypoint per waypoint object, at its stock offset (waypoint_options), but none on a pasted room's
    tiles (`taken`), whose own pieces stand there; and the kit sources used."""
    entities, used = [], set()
    for cls, x, z in objects:
        options = waypoint_options(lay, kit, cls, x, z)
        if not options or (int(x // TILE), int(z // TILE)) in taken:
            continue
        src, ents = options[zlib.crc32(f"{seed}:waypoint:{x}:{z}".encode()) % len(options)]
        used.add(src)
        for e, dx, dz in ents:
            e = copy.deepcopy(e)
            _transform(e)["position"]["x"], _transform(e)["position"]["z"] = x + dx, z + dz
            entities.append(e)
    return entities, used


def place(lay: Layout, kit: Kit, seed: str, edge: frozenset = frozenset()) -> tuple[list[dict], set[int], int]:
    """Each tile's pick of its key's instances (no ids yet), the kit sources used, and how many tiles stayed empty
    for a key the kit lacks. An `edge` tile (the map's border, where the automap draws its edge) picks among the
    instances that hold a piece: a stock tile left empty there leans on its neighbour's piece, which another pick
    may not have, and the border would show a gap."""
    entities, used, missing = [], set(), 0

    def one(x: int, y: int, key: Key, salt: str) -> bool:
        options = kit.instances.get(key)
        if not options:
            return False
        if (x, y) in edge:
            options = [o for o in options if o[1]] or options
        src, ents = options[zlib.crc32(f"{seed}:{x}:{y}{salt}".encode()) % len(options)]
        used.add(src)
        for e, dx, dz in ents:
            e = copy.deepcopy(e)
            t = _transform(e)
            t["position"]["x"], t["position"]["z"] = x * TILE + dx, y * TILE + dz
            for c in e["components"]:  # a wall's transparency names its tile
                if c["type"] == "WallTransparencyComponent" and c["wallTileLocalCoord"]["x"] >= 0:
                    c["wallTileLocalCoord"] = {"x": x, "y": y}
            entities.append(e)
        return True
    for (x, y), key in sorted(lay.keys.items()):
        if one(x, y, key, ""):
            continue
        alone = [(k, (key[0], (t,))) for k, t in enumerate(key[1]) if (key[0], (t,)) in kit.instances]
        for k, single in alone:
            one(x, y, single, f":{k}")
        missing += not alone
    return entities, used, missing


def rooms(lay: Layout, stock: list[tuple[Layout, dict]]) -> tuple[list[tuple[int, int, int]], set]:
    """The stock rooms to paste whole, [(stock index, x, y)], best match first and none overlapping, and their tiles."""
    at: dict[Key, list] = {}
    for c, k in lay.keys.items():
        at.setdefault(k, []).append(c)
    found = []
    for n, (s, _) in enumerate(stock):
        items = list(s.keys.items())
        if len(items) < ROOM_MIN:
            continue
        (ax, ay), anchor = min(items, key=lambda it: len(at.get(it[1], ())) or 10 ** 9)
        misses = len(items) - ROOM_MATCH * len(items)
        for px, py in at.get(anchor, ()):
            ox, oy = px - ax, py - ay
            if ox < 0 or oy < 0 or ox + s.width > lay.width or oy + s.height > lay.height:
                continue
            hit = 0
            for m, ((x, y), key) in enumerate(items):
                hit += lay.keys.get((x + ox, y + oy)) == key
                if m + 1 - hit > misses:
                    break
            else:
                found.append((-hit, n, ox, oy))
    out, taken = [], set()
    for _, n, ox, oy in sorted(found):
        cells = {(x, y) for x in range(ox, ox + stock[n][0].width) for y in range(oy, oy + stock[n][0].height)}
        if not cells & taken:
            taken |= cells
            out.append((n, ox, oy))
    return out, taken


def patches(lay: Layout, stock: list[tuple[Layout, dict]], tiles: set) -> tuple[Layout, Kit]:
    """The patch lookup: each of `tiles` whose 3x3 neighbourhood (else its 4-neighbour cross) appears in a stock
    preset, keyed so that the kit copies what that preset placed on the centre tile, empty included."""
    def patch(keys: dict, c: tuple, name: str, cells: list) -> Key:
        return name, tuple(keys.get((c[0] + i, c[1] + j)) for i, j in cells)
    shapes = (("sq", SQUARE), ("cross", CROSS))
    want = {c: [patch(lay.keys, c, *k) for k in shapes] for c in tiles}
    keep = {k for ks in want.values() for k in ks}
    kit = Kit()
    for s, preset in stock:
        for k in shapes:
            kit.add(Layout(s.width, s.height, {c: patch(s.keys, c, *k) for c in s.keys}), preset, keep)
    return Layout(lay.width, lay.height, {c: next(k for k in ks if k in kit.instances)
                                          for c, ks in want.items() if any(k in kit.instances for k in ks)}), kit


def build(lay: Layout, kit: Kit, name: str, seed: str, biome: str, terrain: dict,
          stock: list[tuple[Layout, dict]] = (), trees: tuple | None = None, prefabs=lambda path: [(0.0, 0.0)],
          water: dict[str, tuple[dict, dict]] | None = None, objects: list[tuple[str, float, float]] = ()
          ) -> tuple[dict, int]:
    """The preset for a layout, and how many of its tiles stayed empty for a key the kit lacks. Over the per-tile kit,
    in order: stock rooms pasted whole, the patch lookup (both from `stock`), then the border painter with `trees`
    (pd2_painter.harvest) repaints the trees past the playable area; `prefabs(path)` gives a prefab's piece positions.
    Last, `water` (water DT1 -> a stock water plane entity and its preset's dependencies) covers that DT1's blocked
    floor outside pasted rooms. Each of the DS1's waypoint `objects` (waypoints()) gets one stock waypoint
    (place_waypoints)."""
    entities: list[dict] = []
    # shortcut: every dependency of each stock preset copied from, as a texture can't be traced to its model; trim it
    # if the frame-rate or load-time check in Jungle Map fails
    deps: dict[str, dict[str, dict]] = {}

    def depend(of: dict) -> None:
        for kind, items in of.items():
            for d in items:
                deps.setdefault(kind, {}).setdefault(d["path"].lower(), d)
    stock = list(stock)
    pasted, taken = rooms(lay, stock)
    for n, ox, oy in pasted:
        preset = stock[n][1]
        depend(preset["dependencies"])
        for e in preset["entities"] + [preset.get("terrain")]:  # the room's own sculpted terrain, water and bridges
            if not e or "components" not in e or _transform(e) is None:  # some stock terrain is empty
                continue
            e = copy.deepcopy(e)
            _transform(e)["position"]["x"] += ox * TILE
            _transform(e)["position"]["z"] += oy * TILE
            for c in e["components"]:
                if c["type"] == "WallTransparencyComponent" and c["wallTileLocalCoord"]["x"] >= 0:
                    c["wallTileLocalCoord"] = {"x": c["wallTileLocalCoord"]["x"] + ox,
                                               "y": c["wallTileLocalCoord"]["y"] + oy}
            entities.append(e)
    rest = {c: k for c, k in lay.keys.items() if c not in taken}
    wet = frozenset(water or {})  # stream DT1: their blocked floor stays water past the tree line
    edge = frozenset()
    if trees:  # the ring just past the playable area: the border the automap draws
        play = painter.playable(lay, wet)
        edge = frozenset({(x + dx, y + dy) for x, y in play for dx, dy in painter.STEPS} - play)
    found, pkit = patches(lay, stock, set(rest))
    loose, used, _ = place(found, pkit, seed, edge)
    for src in sorted(used):
        depend(pkit.sources[src])
    more, used, missing = place(Layout(lay.width, lay.height, {c: k for c, k in rest.items() if c not in found.keys}),
                                kit, seed, edge)
    for src in sorted(used):
        depend(kit.sources[src])
    loose += more
    if trees:  # drop the trees past the playable area and paint them again from it out
        painted, play = painter.paint(lay, trees, seed, taken, prefabs, wet)
        line = painter.lines({c for c, k in lay.keys.items() if k[0] == "w"})

        def tile(e: dict) -> tuple[int, int]:
            return int(_transform(e)["position"]["x"] // TILE), int(_transform(e)["position"]["z"] // TILE)
        near = painter.dilate(painter.dilate({tile(e) for e, _ in painted}))
        # walkable floor: no stock tree, kit or pasted room, puts a piece on it, as one there hides the player (#396)
        ground = {c for c in play - lay.unwalkable if lay.keys[c][0] != "w"}

        def on_ground(e: dict) -> bool:
            p = _transform(e)["position"]
            return any((int((p["x"] + x) // TILE), int((p["z"] + z) // TILE)) in ground
                       for x, z in painter.pieces(e, prefabs))
        entities = [e for e in entities if not painter.is_tree(e) or not on_ground(e)]
        # a lone tree (a wall tile on the path) stays, and a stream's; so does a line's where no painted tree stands
        # within two tiles (a line with playable ground on both sides leaves the painter no room)
        loose = [e for e in loose if not painter.is_tree(e)
                 or not on_ground(e) and (tile(e) in play or tile(e) in line - near)]
        assert not any(on_ground(e) for e, _ in painted), f"{name}: a painted tree reaches walkable floor"
        for e, of in painted:
            loose.append(e)
            depend(of)
    entities += loose
    placed, used = place_waypoints(lay, kit, seed, objects, taken)
    for src in sorted(used):
        depend(kit.sources[src])
    entities += placed
    planes = streams(lay, water or {}, taken)
    for e, of in planes:
        entities.append(e)
        depend(of)
    # the ground: the flat plane once per TERRAIN_SIZE block not mostly under pasted rooms, unscaled (a scaled plane
    # does not draw); the first copy is the preset's terrain, the rest are entities
    span = TERRAIN_SIZE // TILE
    for bx in range(0, lay.width, span):
        for by in range(0, lay.height, span):
            block = [(x, y) for x in range(bx, min(bx + span, lay.width)) for y in range(by, min(by + span, lay.height))]
            if (bx or by) and sum(c in taken for c in block) * 2 < len(block):
                plane = copy.deepcopy(terrain)
                _transform(plane)["position"].update({"x": float(bx * TILE), "z": float(by * TILE)})
                entities.append(plane)
    for i, e in enumerate(entities):
        e["id"] = i + 1
    terrain = copy.deepcopy(terrain)
    terrain["id"] = len(entities) + 1
    for kind, path in [("json", biome)] + [("models" if p.endswith(".model") else "physics", p)
                                          for p in re.findall(r'"filename"\s*:\s*"([^"]+)"', json.dumps(terrain))]:
        deps.setdefault(kind, {}).setdefault(path.lower(), {"path": path})
    return {"dependencies": {k: list(v.values()) for k, v in deps.items()}, "type": "Preset", "name": name,
            "entities": entities, "terrain": terrain, "biomeFilename": biome, "perTileBiomeOverrides": [],
            "specialTiles": {}}, missing


def rectangles(cells: set) -> list[tuple[int, int, int, int]]:
    """`cells` as (x, y, width, height) rectangles, greedily: the longest run along x, grown along y."""
    left, out = set(cells), []
    for x, y in sorted(cells, key=lambda c: (c[1], c[0])):
        if (x, y) not in left:
            continue
        w = 1
        while (x + w, y) in left:
            w += 1
        h = 1
        while all((x + i, y + h) in left for i in range(w)):
            h += 1
        left -= {(x + i, y + j) for i in range(w) for j in range(h)}
        out.append((x, y, w, h))
    return out


def squarish(rect: tuple[int, int, int, int]) -> list[tuple[int, int, int, int]]:
    """A rectangle cut along its long side into pieces at most WATER_RATIO times as long as they are wide."""
    x, y, w, h = rect
    if w > WATER_RATIO * h:
        n = -(-w // (WATER_RATIO * h))
        return [(x + w * i // n, y, w * (i + 1) // n - w * i // n, h) for i in range(n)]
    if h > WATER_RATIO * w:
        n = -(-h // (WATER_RATIO * w))
        return [(x, y + h * i // n, w, h * (i + 1) // n - h * i // n) for i in range(n)]
    return [rect]


def streams(lay: Layout, water: dict[str, tuple[dict, dict]], taken: set) -> list[tuple[dict, dict]]:
    """Stock water planes over a water DT1's blocked floor tiles outside the pasted rooms (whose own mesh draws theirs),
    one plane scaled to each near-square rectangle of tiles, with the dependencies of the preset it came from. A tile
    whose floor layers hold two water DT1 gets the first one's plane only."""
    out, done = [], set()
    for dt1 in sorted(water):
        plane, of = water[dt1]
        cells = {c for c, k in lay.keys.items() if c not in taken and c not in done and k[0] == "f"
                 and any(t[0] == dt1 for t in k[1]) and painter.blocked_floor(lay.flags.get(c, bytes(25)))}
        done |= cells
        span = WATER_SPAN[next(c["filename"] for c in plane["components"] if c["type"] == "ModelDefinitionComponent")]
        for x, y, w, h in (r for rect in rectangles(cells) for r in squarish(rect)):
            e = copy.deepcopy(plane)
            t = _transform(e)
            t["position"] = {"x": (x + w / 2) * TILE, "y": WATER_Y, "z": (y + h / 2) * TILE}
            t["orientation"] = {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}
            t["scale"] = {"x": w * TILE / span, "y": 1.0, "z": h * TILE / span}
            e["name"] = STREAM
            out.append((e, of))
    return out


def draws_trees(lay: Layout) -> bool:
    """Whether a DS1 draws a tile from a tree DT1 (`trees.dt1`, `treegrp.dt1`, `treegroups.dt1`): only those are
    painted, as an indoor tile set's stock may hold a courtyard's trees."""
    return any(t[0] and "tree" in t[0].rsplit("/", 1)[-1] for k in lay.keys.values() for t in k[1])


def read_rule(rule: Path) -> tuple[dict[str, str], dict[str, str | None], dict[str, str]]:
    """(tile set -> biome, PD2 DT1 -> its D2R stand-in DT1 or None, water DT1 -> water plane model): a first column
    `water:<DT1>` is a water line, one ending in .dt1 a stand-in."""
    sets, standins, water = {}, {}, {}
    for line in rule.read_text("utf-8").splitlines():
        if line and not line.startswith("#"):
            name, value = line.split("\t")
            if name.lower().startswith("water:"):
                if value not in WATER_SPAN:
                    raise ValueError(f"water line {name}: {value} is no water plane WATER_SPAN knows")
                water[norm(name[6:])] = value
            elif name.lower().endswith(".dt1"):
                standins[norm(name)] = None if value == "-" else norm(value)
            else:
                sets[name.lower()] = value
    return sets, standins, water


def stand_in(files: list[str], tiles: "Tiles", pd2_tiles: "Tiles", standins: dict[str, str | None],
             copied: set | dict = frozenset()) -> dict[str, dict]:
    """The DT1 files a PD2-only preset loads, for layout(): a PD2 tile its stand-in also holds, with the same subtile
    flags, is named by the stand-in; the rest keep the PD2 DT1's name, which no stock preset draws (empty in HD), but
    for the `copied` tiles (pd2_dt1_copies), which layout() names by the D2R tile they copy."""
    out: dict[str, dict] = {}
    for f in files:
        if f not in standins:
            out.setdefault(f, {}).update(tiles[f])
            continue
        s, own = standins[f], dict(pd2_tiles[f])
        if s:
            out.setdefault(s, {}).update({t: own.pop(t) for t in list(own)
                                          if (f, *t) not in copied and tiles[s].get(t) == own[t]})
        out[f] = own
    return out


def pd2_only_presets(out_data: Path, d2r_lvlprest: int) -> list[tuple[str, str, list[str], list[str]]]:
    """The appended lvlprest rows: (name, tile set, the DT1 paths it loads, DS1 paths)."""
    lp, ty, lv = (Table(excel(out_data) / f"{t}.txt") for t in ("lvlprest", "lvltypes", "levels"))
    out = []
    for k in lp.records()[d2r_lvlprest:]:
        r = lp.rows[k]
        t = lv.rows[lv.records()[int(r[col(lp, "LevelId")])]][col(lv, "LevelType")]
        if not t:  # PD2's blank `Unused` row
            continue
        trow, mask = ty.rows[ty.records()[int(t)]], int(r[col(lp, "Dt1Mask")] or 0)
        out.append((r[0], trow[0], [norm(trow[col(ty, f"File {s + 1}")]) for s in range(SLOTS) if mask >> s & 1],
                    [norm(r[col(lp, f"File{n}")]) for n in range(1, 7) if r[col(lp, f"File{n}")] not in ("", "0")]))
    return out


class Tiles(dict):
    """DT1 path -> dt1_tiles, read from D2R's tiles on first use (an absent file holds nothing)."""
    def __init__(self, root: Path):
        super().__init__()
        self.root = root

    def __missing__(self, path: str) -> dict:
        f = self.root / path
        self[path] = dt1_tiles(f.read_bytes()) if f.is_file() else {}
        return self[path]

    def of(self, paths: list[str]) -> dict[str, dict]:
        return {p: self[p] for p in paths}


def water_planes(d2r_data: Path, rule: dict[str, str]) -> dict[str, tuple[dict, dict]]:
    """Water DT1 -> (the first entity drawing its line's model in the smallest stock preset that has one, that
    preset's dependencies)."""
    found: dict[str, tuple[dict, dict]] = {}
    for p in sorted((d2r_data / PRESETS).rglob("*.json"), key=lambda p: (p.stat().st_size, p)):
        if len(found) == len(set(rule.values())):
            break
        text = p.read_text("utf-8-sig")
        for model in set(rule.values()) - set(found):
            if model in text:
                preset = json.loads(text)
                # the model may be named only in the dependencies or a prefab: then a later preset holds it
                e = next((e for e in preset["entities"] if any(c.get("filename") == model for c in e["components"])), None)
                if e:
                    found[model] = (e, preset["dependencies"])
    if lost := sorted(set(rule.values()) - set(found)):
        raise ValueError(f"no stock preset draws the water plane {', '.join(lost)}: run `{FETCH}`")
    return {dt1: found[model] for dt1, model in rule.items()}


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/presets-hd.tsv (see the module docstring)."""
    sets, standins, water_rule = read_rule(rule)
    tiles_dir = d2r_data / "global" / "tiles"
    if not (d2r_data / PRESETS).is_dir() or not any(tiles_dir.rglob("*.dt1")):
        print(f"  presets-hd: skipped, no D2R presets or DT1 under {d2r_data} (dev builds run without; `{FETCH}`)")
        return
    presets = pd2_only_presets(out_data, len(load(d2r_data, "lvlprest").records()))
    # a preset on PD2 tiles is built once every PD2 DT1 it loads has a stand-in line (LP4/LP5); the others wait
    on_d2r = [p for p in presets if all(f in standins for f in p[2] if f.startswith("pd2assets/"))]
    if unruled := sorted({s for _, s, _, _ in on_d2r if s.lower() not in sets}):
        raise ValueError(f"PD2-only presets on D2R tiles or stand-ins in tile sets with no line: {', '.join(unruled)}")
    if stale := sorted(set(sets) - {s.lower() for _, s, _, _ in on_d2r}):
        raise ValueError(f"lines for tile sets no PD2-only preset on D2R tiles or stand-ins loads: {', '.join(stale)}")
    if stale := sorted(set(standins) - {f for p in presets for f in p[2]}):
        raise ValueError(f"stand-in lines for DT1 no PD2-only preset loads: {', '.join(stale)}")
    terrain_src = d2r_data / PRESETS / f"{TERRAIN}.json"
    if not terrain_src.is_file():
        raise ValueError(f"no {terrain_src}: run `{FETCH}`")
    terrain = json.loads(terrain_src.read_text("utf-8-sig"))["terrain"]

    tiles, pd2_tiles = Tiles(tiles_dir), Tiles(out_data / "global" / "tiles")
    pd2_files = {f: out_data / "global" / "tiles" / f for _, _, files, _ in on_d2r for f in files
                 if f.startswith("pd2assets/")}
    copied = dt1_copies.copies({f: p.read_bytes() for f, p in sorted(pd2_files.items()) if p.is_file()},
                               dt1_copies.d2r_images(tiles_dir), standins) if pd2_files else {}

    def classes(data: Path) -> dict[tuple[int, int], str]:  # objpreset's (Act, Index) -> ObjectClass
        t = Table(excel(data) / "objpreset.txt")
        c = [col(t, n) for n in ("Act", "Index", "ObjectClass")]
        return {(int(r[c[0]]), int(r[c[1]])): r[c[2]] for r in t.rows if r[c[0]] and r[c[1]]}
    stock_classes, mod_classes = classes(d2r_data), classes(out_data)
    layouts = {ds1: layout((out_data / "global" / "tiles" / ds1).read_bytes(),
                           stand_in(files, tiles, pd2_tiles, standins, copied), copied)
               for _, _, files, ds1s in on_d2r for ds1 in ds1s}
    keep = set().union(*map(wanted, layouts.values()))
    loaded = {t[0] for k in keep for t in k[1]}
    if stale := sorted(set(water_rule) - loaded):
        raise ValueError(f"water lines for DT1 no PD2-only preset draws from: {', '.join(stale)}")
    water = water_planes(d2r_data, water_rule)
    kit, stock = Kit(), []
    for p in sorted((d2r_data / PRESETS).rglob("*.json")):
        ds1 = tiles_dir / p.relative_to(d2r_data / PRESETS).with_suffix(".ds1")
        if not ds1.is_file():  # a few stock presets have no DS1 (D2R's HD-only rooms)
            continue
        data = ds1.read_bytes()
        listed = header_dt1s(data)
        if loaded.isdisjoint(listed):
            continue
        preset = json.loads(p.read_text("utf-8-sig"))
        preset[GROUND] = terrain_ground.of_preset(preset, d2r_data)
        stock.append((layout(data, tiles.of(listed)), preset, set(listed)))
        kit.add(*stock[-1][:2], keep, waypoints(data, stock_classes))

    @functools.cache
    def prefab(path: str) -> list[tuple[float, float]]:
        f = d2r_data / path.removeprefix("data/")
        ents = json.loads(f.read_text("utf-8-sig")).get("entities", []) if f.is_file() else []
        return [(t["position"]["x"], t["position"]["z"]) for t in map(_transform, ents) if t] or [(0.0, 0.0)]
    report, groups, bare = [], {}, []
    for _, tile_set, _, ds1s in on_d2r:
        groups.setdefault(tile_set, []).extend(ds1s)
    for tile_set, ds1s in groups.items():
        # the tile set's stock: the presets that list a DT1 its DS1 draw from; the tree kit is harvested from them
        drawn = {t[0] for ds1 in ds1s for k in wanted(layouts[ds1]) for t in k[1]}
        own = [(lay, preset) for lay, preset, listed in stock if not drawn.isdisjoint(listed)]
        trees = painter.harvest(own, frozenset(water_rule))
        for ds1 in ds1s:
            lay = layouts[ds1]
            outdoor = draws_trees(lay)
            objects = waypoints((out_data / "global" / "tiles" / ds1).read_bytes(), mod_classes)
            bare += [f"{Path(ds1).stem} {o[0]}" for o in objects if not waypoint_options(lay, kit, *o)]
            preset, missing = build(lay, kit, Path(ds1).stem, ds1, sets[tile_set.lower()], terrain, own,
                                    trees if outdoor else None, prefab, water, objects)
            dest = out_data / PRESETS / Path(ds1).with_suffix(".json")
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(json.dumps(preset, separators=(",", ":")), "utf-8")
            report.append((len(lay.keys), missing, len(preset["entities"]), bool(outdoor and trees),
                           sum(e["name"] == STREAM for e in preset["entities"])))
    print(f"  presets-hd: {len(report)} HD presets from {len(kit.sources)} stock presets, "
          f"{sum(r[2] for r in report)} entities, {sum(r[3] for r in report)} with painted tree lines, {sum(r[4] for r in report)} water planes; {sum(r[1] for r in report)} of {sum(r[0] for r in report)} tiles "
          f"have a key no stock preset draws; {len(presets) - len(on_d2r)} presets on PD2 tiles wait for stand-ins (LP4/LP5); "
          f"{len(bare)} waypoint objects have no stock waypoint ({', '.join(sorted(bare)) or 'none'})")
