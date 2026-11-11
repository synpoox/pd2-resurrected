"""The tile classifier and HD-piece geometry the preset generator and the scorers (pd2_fidelity, pd2_coverage) share (#855).

The generator (tools/presets/) imports this module and never the scorers, so a scorer edit leaves the generator's
merge-cache key alone. Moved here unchanged from pd2_fidelity.py (the legacy edge classifier, Heights, Hd, score_type) and
pd2_coverage.py (model boxes, footprints). Both scorers re-export every name, so `fidelity.classify` and `boxes` still work."""
from __future__ import annotations

import math
import os
import re
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterator

import pd2_painter as painter
import pd2_presets as presets
from pd2_ids import toolkit

ROOT = Path(__file__).resolve().parent.parent

D2R_DATA = Path(os.environ.get("MODKIT_CACHE", toolkit(ROOT) / "d2r-data")) / "93847" / "data"
MODELS = Path("hd") / "model_lod_desc.bin"
TALL = 4.0  # HD units: a piece this high hides a wall in game
# fences and shores are low by design (stone walls, graveyard railings, river banks): a piece this high covers them
LOW = 1.0
LOW_EDGE = re.compile(r"fence|stonewall|palisade|picket|graveyrd|river|rivbank|pool|swamp|pond|puddle")
ROOM = 400  # HD units: a box wider than this is a room-sized mesh (water, sculpted terrain), not a wall piece
DEPTH = 2  # prefab levels followed
EDGE = 1e-3  # tiles: how far a ground box may cross a tile edge and still end on it
IDENTITY = {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}

Tile = tuple[int, int]
EDGES = ("wall", "cliff", "fence", "pillar", "water", "treeline")
TYPES = EDGES + ("floor",)
# orientations of the DT1 tiles that stand as walls (see the module doc: measured on the PD2 DT1 files)
# Orientations (measured on the PD2 DT1 files; #707's shared-classifier findings): 1-9 and 16-18 are walls (16-19
# the lower variants of a wall: a5_cliff_parts holds 1-4, 7 over 16-19; 10 and 11 appear in cliff1/cliff2 only),
# 12 pillars (pd2_presets.LIKE), 13 shadows (trees.dt1: 91 tiles of height 0 to -128), 14 trees (presets.TREE_LAYER),
# 0 floors.
WALL_ORIENT = frozenset(range(1, 12)) | {16, 17, 18, 19}
HEIGHT_ORIENT = WALL_ORIENT - {10, 11}  # 10 and 11 are warp and roof tiles: no wall height
UPPER_ORIENT, LOWER_ORIENT = frozenset(range(1, 10)), frozenset(range(16, 20))
PILLAR_ORIENT = presets.LIKE  # 12
TREE_ORIENT = presets.TREE_LAYER  # 14
# Walls, fences and cliffs share orientations, so a wall layer is told apart by its DT1's name first (the #707
# researcher's table, V = verified in the DT1 lists of merge/presets-hd.d/):
INVISIBLE = re.compile(r"inv_wall")  # act4/mesa/inv_wall: a blocker that draws nothing
FENCE = re.compile(r"fence|chain_walls|foregate|barset|palisade|picket|graveyrd")  # mesa chain_walls, siege fence,
# fort foregate, barracks barset, outdoors/town fence
# FENCE DT1s whose walls are tall: classify still calls them fences (the passes' shared classifier stays fixed); the fence
# pass (presets/passes/edges_fences.py) leaves them to the wall pass, and the scorer types them as walls (tile_type,
# #822). Median pixel height of the tiles the PD2
# maps place is 256-288 px for these (mesa and a5 chain_walls 256-288, graveyrd 256, barset 256, town/fence and descend/stone_fence 288), and
# foregate's walls are 224-416 (median 288), against 192 for the low siege, outdoors and descend fences (#757).
TALL_FENCE = re.compile(r"chain_walls|foregate|barset|graveyrd|town/fence|stone_fence")
# kurast/travincal terraces, icecave interior, hellcaves, mesa brick_walls; `corner` because corner.dt1 stands in as
# guild/outdoors/cliff.dt1 (merge/presets-hd.d/). Approximate: matched by name, not by every PD2 DT1 checked.
CLIFF = re.compile(r"cliff|corner|terrace|brick_walls|hellcaves|icecave.*interior|interior.*icecave")
TREE = re.compile(r"tree")  # the jungle's treegrp/treegroups wall lines, trees.dt1 walls
# river, pond, rivbank and pool wall layers; lava's extwalls and intwalls, and its rocks
WATER_EDGE = re.compile(r"river|rivbank|pond|pool|extwalls|intwalls|lava[^/]*/rocks|lava.*rocks")
LIQUID = re.compile(r"water|river|rivbank|pond|pool|swamp|puddle|lake|lava|canal|oasis")  # a blocked floor of these
# A wall DT1 is low when the median pixel height of its wall tiles is under LOW_PX, else tall. Measured over the 161
# PD2 and the stock DT1s: the known low families sit at 160 (stonewall, borq4, torturehalls/file2 and file3) and 192
# (fence), and every tall dungeon or keep wall at 224 or more (act2/sewer/walls and sewerwalls 224, a4_worldstone 224,
# new_chamb 224, act3/jungle/dungeon 224, lair_mishift 256, torturehalls/file1 256, dtprivate/underwall 256,
# act2/tomb/tomb 288, aer_palace 288). A per-tile cutoff does not work for them: they mix a few 128-192 px end pieces
# in with their walls (2-14% of the placed tiles), and a median over the DT1 sees past them. A DT1 whose placed tiles
# split nearly evenly (MIXED) has no such majority: its median is a coin toss, so each tile counts as its own.
LOW_PX = 224
MIXED = 0.25  # the minority share of a DT1's placed wall tiles, either side of LOW_PX, from which it is mixed
LOW_NAME = re.compile(r"barricade")  # a low barricade, by name (median 224 in siege/barricade.dt1: its tiles are mixed)
HOLE_PAIRS = (((-1, 0), (1, 0)), ((0, -1), (0, 1)), ((-1, -1), (1, 1)), ((-1, 1), (1, -1)))
STACK = 2.0  # pieces per tile of an edge run above which they pile on each other (stock walls span 5-10 units)
STEPS8 = [(i, j) for i in (-1, 0, 1) for j in (-1, 0, 1) if i or j]
# the HD side: a model is typed by its folder and file name together, as D2R groups its kits in folders
# (act3_treewall_combined, act4_mesa_cliff_supports, act2_sewer_pillars, expansion_baallair_rails, act2_sewer_walls);
# a file name alone ("straight01", "cap01", "corner01") says nothing. The folders are those of the 465 the generated
# presets stand pieces from. Ground cover is never an edge: a grass clump in a water folder is grass.
HD_MODEL = (("treeline", re.compile(r"tree|palm")),
            ("cliff", re.compile(r"cliff")),
            ("fence", re.compile(r"fence|palisade|picket|rails?_|_rails?")),
            ("pillar", re.compile(r"pillar|column")),
            ("water", re.compile(r"water|river|pond|swamp|lake|lava_(?:kits|edge|flow)|dry_flow|diab_lava_edges|puddle(?!wall)")),
            ("wall", re.compile(r"wall")))
GROUND_COVER = re.compile(r"grass|clump|weed|reed|cattail|bush|cactus|mushroom")


Box = tuple[float, float, float, float, float, float]  # x0, x1, z0, z1, top, bottom


def fnv(path: str) -> int:
    """model_lod_desc.bin's key for a model path: 32-bit FNV-1a of the lower-cased path."""
    h = 0x811C9DC5
    for b in path.lower().encode():
        h = ((h ^ b) * 0x1000193) & 0xFFFFFFFF
    return h


def model_boxes(raw: bytes) -> dict[int, tuple[float, ...]]:
    """model_lod_desc.bin: `u32 count | i32 offset`, then from 4 + offset, 44-byte records
    `u32 key | f32 bbMin[3] | f32 bbMax[3] | ...`. key -> (min x, y, z, max x, y, z)."""
    count, off = struct.unpack_from("<Ii", raw, 0)
    return {r[0]: r[1:] for r in (struct.unpack_from("<I6f", raw, 4 + off + 44 * i) for i in range(count))}


def qmul(a: dict, b: dict) -> dict:
    """The quaternion a * b (b turned, then a)."""
    ax, ay, az, aw = a["x"], a["y"], a["z"], a["w"]
    bx, by, bz, bw = b["x"], b["y"], b["z"], b["w"]
    return {"w": aw * bw - ax * bx - ay * by - az * bz, "x": aw * bx + ax * bw + ay * bz - az * by,
            "y": aw * by - ax * bz + ay * bw + az * bx, "z": aw * bz + ax * by - ay * bx + az * bw}


Ground = Callable[[float, float], float]  # world x, z -> the height of the ground plane under it


def plane_ground(preset: dict) -> Ground:
    """The ground plane height under a world position: the y of the preset's flat terrain plane of that TERRAIN_SIZE
    block (#742 raises and sinks the planes and the pieces on them), 0 where no plane is drawn."""
    ys = {}
    for e in preset["entities"] + [preset.get("terrain") or {}]:
        t = presets._transform(e) if "components" in e else None
        if t is not None and e.get("name") == "terrain" and any(
                f"/{presets.TERRAIN}/" in c.get("filename", "").lower() for c in e["components"]):
            p = t["position"]
            ys[(round(p["x"] / presets.TERRAIN_SIZE), round(p["z"] / presets.TERRAIN_SIZE))] = p["y"]
    return lambda x, z: ys.get((math.floor(x / presets.TERRAIN_SIZE), math.floor(z / presets.TERRAIN_SIZE)), 0.0)


def boxes(entities: list[dict], box: Callable[[str], tuple | None], prefab: Callable[[str], list[dict]],
          base: tuple | None = None, depth: int = 0, ground: Ground | None = None, meta: bool = False,
          room: bool = False) -> Iterator[Box]:
    """The world boxes of the entities' models; a prefab's entities are placed by the prefab's transform. With
    `ground`, heights are measured from the ground plane under the entity (a wall on a sunken plane is as tall as one on
    the flat ground; a pasted room's piece, named with ROOM_PIECE, is measured from 0: #742 does not move it); a
    prefab's entities stand on the plane under the prefab. With `meta`, each box is followed by its model path (lower
    case) and whether it is a pasted room's piece (also inside a prefab placed by one): tools/pd2_parity.py (#838)."""
    for e in entities:
        t = presets._transform(e)
        if t is None or e.get("name") == "terrain":
            continue
        p = (t["position"]["x"], t["position"]["y"], t["position"]["z"])
        in_room = room or (e.get("name") or "").startswith(presets.ROOM_PIECE)
        g = ground(p[0], p[2]) if ground and not in_room else 0.0  # pasted room: ground 0
        q, sc = t.get("orientation") or IDENTITY, t.get("scale") or {"x": 1, "y": 1, "z": 1}
        s = (sc["x"], sc["y"], sc["z"])
        if base:
            bp, bq, bs = base
            v = painter.rotate(bq, (p[0] * bs[0], p[1] * bs[1], p[2] * bs[2]))
            p, q, s = (bp[0] + v[0], bp[1] + v[1], bp[2] + v[2]), qmul(bq, q), (s[0] * bs[0], s[1] * bs[1], s[2] * bs[2])
        for c in e["components"]:
            if c["type"] == "ModelDefinitionComponent" and (bb := box(c.get("filename", ""))):
                corners = [painter.rotate(q, (x * s[0], y * s[1], z * s[2]))
                           for x in (bb[0], bb[3]) for y in (bb[1], bb[4]) for z in (bb[2], bb[5])]
                xs, ys, zs = ([p[i] + v[i] for v in corners] for i in range(3))
                b = (min(xs), max(xs), min(zs), max(zs), max(ys) - g, min(ys) - g)
                yield b + (c["filename"].lower(), in_room) if meta else b
            elif c["type"] == "PrefabPlacementDefinitionComponent" and c.get("prefab") and depth < DEPTH:
                yield from boxes(prefab(c["prefab"]), box, prefab, (p, q, s), depth + 1,
                                 (lambda x, z: g) if ground else None, meta, in_room)

def footprint(found: Iterator[Box], exclusive: bool = False) -> set[tuple[int, int]]:
    """The tiles under the boxes. `exclusive`: a box ending on a tile edge does not reach the tile past it (a
    170-unit ground plane covers 17 tiles, not 18); the wall score keeps the tile it ends on (tall_tiles)."""
    out = set()
    T = presets.TILE

    def span(a: float, b: float) -> range:
        if not exclusive:
            return range(math.floor(a / T), math.floor(b / T) + 1)
        lo = math.floor(a / T + EDGE)  # the stock plane's box spills past its edges by float noise (170.00003)
        return range(lo, max(math.ceil(b / T - EDGE), lo + 1))
    for x0, x1, z0, z1, *_ in found:
        out.update((tx, tz) for tx in span(x0, x1) for tz in span(z0, z1))
    return out


CORE = 0.5  # share of a tile's width a box must cover, both ways, for the tile to be under its core (not a spill)


def core_footprint(b: Box) -> set[tuple[int, int]]:
    """The tiles a box covers by at least CORE of their width on both axes, and always the tile under its centre: the
    rest of footprint(b) is spill, tiles the box only grazes. A wide or turned box spills onto its neighbours (#800)."""
    T = presets.TILE

    def span(a: float, b_: float) -> set[int]:
        mid = math.floor((a + b_) / 2 / T)
        return {t for t in range(math.floor(a / T), math.floor(b_ / T) + 1)
                if min(b_, (t + 1) * T) - max(a, t * T) >= CORE * T} | {mid}
    return {(tx, tz) for tx in span(b[0], b[1]) for tz in span(b[2], b[3])}


def folder_of(path: str) -> str:
    """The HD model folder of a model path (act3_treewall_combined), lower case; "" for a bare file name."""
    return path.lower().rsplit("/", 2)[-2] if path.count("/") else ""


def model_type(path: str) -> str | None:
    """The edge type of an HD model, by its folder and file name; None for anything else (grass, rocks, props)."""
    name = "/".join(path.lower().split("/")[-2:])
    if GROUND_COVER.search(name):
        return None
    return next((t for t, rx in HD_MODEL if rx.search(name)), None)


def model_types(path: str, stock: dict | None = None) -> tuple[str, ...]:
    """Every edge type an HD model counts as: its name's (model_type) and its folder's by the stock vote (`stock`:
    stock_types, #822), which is the legacy edge D2R's own presets stand the folder on (a tomb fence is a wall there,
    an icecave wall a cliff). A piece is right for a legacy edge when either says so. Ground cover is none."""
    named = model_type(path)
    if named is None and GROUND_COVER.search("/".join(path.lower().split("/")[-2:])):
        return ()
    voted = (stock or {}).get(folder_of(path))
    return tuple(dict.fromkeys(t for t in (named, voted) if t))


def dt1_heights(data: bytes) -> dict[tuple[int, int, int], int]:
    """A DT1's (orientation, main, sub) -> the tile's pixel height (record +8, negative upwards; as a size)."""
    count, at = struct.unpack_from("<2i", data, 268)
    out = {}
    for k in range(count):
        o = at + k * 96
        out.setdefault(struct.unpack_from("<3i", data, o + 20), abs(struct.unpack_from("<i", data, o + 8)[0]))
    return out


class Heights(dict):
    """DT1 path -> dt1_heights, read from the first of the folders of tiles that has the file, on first use (an absent
    file holds nothing). The merge ships only PD2's own DT1s, so the stock D2R folder comes second."""
    def __init__(self, *roots: Path):
        super().__init__()
        self.roots = roots

    def __missing__(self, path: str) -> dict:
        f = next((r / path for r in self.roots if (r / path).is_file()), None)
        self[path] = dt1_heights(f.read_bytes()) if f else {}
        return self[path]

    def own_flags(self, path: str) -> dict[tuple[int, int, int], bytes]:
        """A DT1's (orientation, main, sub) -> the tile's own 25 subtile flags (presets.dt1_tiles), once per file; an
        absent file holds nothing."""
        if not hasattr(self, "_flags"):
            self._flags: dict[str, dict] = {}
        if path not in self._flags:
            f = next((r / path for r in self.roots if (r / path).is_file()), None)
            self._flags[path] = presets.dt1_tiles(f.read_bytes()) if f else {}
        return self._flags[path]


def classify(dt1: str | None, orientation: int, layer: str = "wall", blocked: bool = False) -> str | None:
    """The shared edge classifier (#732; every edge and floor pass uses it): the type of one layer of a legacy cell,
    from its DT1's name, its orientation and its `layer` ("wall" or "floor"; `blocked`: a floor layer whose subtiles
    block walking). One of "wall", "cliff", "fence", "pillar", "water" (a water or lava edge), "treeline", "floor",
    "liquid" (a blocked floor of a liquid DT1), "invisible" (act4/mesa/inv_wall); None for what is no edge or floor:
    shadows (13), roofs (15), a layer with no DT1.

    Walls, fences and cliffs share orientations, so the name decides first (invisible, then water edge, fence, cliff,
    tree); the rest of the wall orientations are walls. A single tile cannot tell a tall cliff from a wall by name
    alone; cliff_cell() does, from the layers of one cell."""
    if not dt1:
        return None
    name = dt1.lower()
    if layer == "floor" or orientation == 0:
        return "liquid" if blocked and LIQUID.search(name) else "floor"
    if INVISIBLE.search(name):
        return "invisible"
    if orientation == TREE_ORIENT:
        return "treeline"
    if orientation == PILLAR_ORIENT:
        return "pillar"
    if orientation not in WALL_ORIENT:
        return None
    if WATER_EDGE.search(name):
        return "water"
    if FENCE.search(name):
        return "fence"
    if TREE.search(name.rsplit("/", 1)[-1]):
        return "treeline"
    return "cliff" if CLIFF.search(name) else "wall"


def cliff_cell(tiles: tuple) -> bool:
    """A tall cliff: one cell holding an upper wall tile (orientation 1-9) over a lower one (16-19), as a5_cliff_parts
    stacks them (the researcher's height class for cliffs)."""
    return any(t[1] in UPPER_ORIENT for t in tiles) and any(t[1] in LOWER_ORIENT for t in tiles)


def tile_type(t: tuple) -> str | None:
    """classify() of a wall layer t = (DT1 path or None, orientation, main, sub), as an edge type (EDGES) or None."""
    k = classify(t[0], t[1], "wall")
    if k == "fence" and TALL_FENCE.search(t[0].lower().replace("\\", "/")):
        return "wall"  # #822: the passes stand the tall fence DT1s as walls, so the scorer asks for walls
    return k if k in EDGES else None


def wall_px(tiles: dict, use: dict | None = None, own: int = 0) -> int:
    """The wall height a tile of a DT1 asks for (dt1_heights): the median pixel height of the DT1's wall tiles, each
    counted as often as `use` (tile key -> times the maps place it) says, so a mixed DT1 takes the class of the tiles
    its maps use (catacomb basewalls: median 192 over its 28 tiles, but 128 of the 130 placed are 256+). Orientations
    10 and 11 (warp and roof tiles) never count. Without `use`, or when none of the DT1's wall tiles is used, each
    counts once; 0 for a DT1 with none (or an unread one).

    When the placed tiles are mixed (at least MIXED of them, low or tall, on the other side of LOW_PX from the rest)
    and `own` (the tile's own height) is given, that is returned instead: the median of such a DT1 is a coin toss that
    tips whole maps (siege/battle: 46% of the placed cells are 160-192 px barricades and trench walls, the rest
    256+ px towers; median exactly LOW_PX), and the tile's own height is what the game draws. A DT1 with a small
    minority keeps its median, so a few short end pieces do not turn a tall wall low (catacomb basewalls 2%, act2
    palace/cellar 6%, sewer walls 9%, arcane sanctuary 14%; the mixed ones sit at 31-46%: torturehalls/file1, tomb,
    battle, rocks, camp, town_reindexed)."""
    walls = {k: v for k, v in tiles.items() if k[0] in HEIGHT_ORIENT}
    if use and any(use.get(k) for k in walls):
        walls = {k: v for k, v in walls.items() if use.get(k)}
    h = sorted(v for k, v in walls.items() for _ in range(use.get(k, 1) if use else 1))
    if own and h and min(low := sum(v < LOW_PX for v in h), len(h) - low) >= MIXED * len(h):
        return own
    return h[len(h) // 2] if h else 0


def expected_class(kind: str, t: tuple, px: int) -> str | None:
    """The height class a legacy tile asks of the HD piece: "low" or "tall"; None for water (a surface, no height).
    `px` is the typical wall height of the tile's DT1 (wall_px), see LOW_PX."""
    if kind == "water":
        return None
    name = (t[0] or "").rsplit("/", 1)[-1].lower()
    if kind in ("fence", "wall") and TALL_FENCE.search((t[0] or "").lower().replace("\\", "/")):
        return "tall"  # a tall fence DT1 (#787, #822): 256-288 px walls, whatever LOW_EDGE says of its name
    return "low" if kind == "fence" or (kind == "wall" and (
        px < LOW_PX or LOW_EDGE.search(name) or LOW_NAME.search(name))) else "tall"


def liquid_floor(t: tuple, cell_flags: bytes, heights: dict) -> bool:
    """Whether a floor layer t = (DT1 path or None, orientation, main, sub) is liquid, decided by the tile itself (#822):
    a liquid DT1 (classify) whose own tile is mostly blocked in the DT1 (oasis.dt1's water tiles; its sand tiles,
    which a blocked layer above can make the cell's flags blocked, are no water). A tile the DT1 does not hold
    (`heights` is no Heights, or its stand-in has no such tile) falls back to the cell's flags `cell_flags`."""
    own = heights.own_flags(t[0]).get(t[1:]) if t[0] and hasattr(heights, "own_flags") else None
    return classify(t[0], 0, "floor", painter.blocked_floor(cell_flags if own is None else own)) == "liquid"


def legacy_edges(lay: presets.Layout, heights: dict[str, dict]) -> dict[str, dict[Tile, str | None]]:
    """For each edge type, its legacy tiles -> the height class they ask for. `lay` names the PD2 DT1s (no stand-ins),
    `heights` is a Heights."""
    out: dict[str, dict[Tile, str | None]] = {e: {} for e in EDGES}
    use: dict[str, dict[tuple, int]] = {}  # DT1 -> (orientation, main, sub) -> times the layout places it
    for kind, tiles in lay.keys.values():
        for t in tiles if kind == "w" else ():
            if t[0]:
                use.setdefault(t[0], {}).setdefault(t[1:], 0)
                use[t[0]][t[1:]] += 1
    typical: dict[tuple, int] = {}
    for c, (kind, tiles) in lay.keys.items():
        best: dict[str, tuple[int, tuple]] = {}
        if kind == "w":
            for t in tiles:
                if (k := tile_type(t)):
                    if k == "wall" and cliff_cell(tiles):
                        k = "cliff"
                    px = heights[t[0]].get(t[1:], 0)  # the tile's own height ranks the layers of a cell
                    if k not in best or px > best[k][0]:
                        best[k] = (px, t)
        for k, (px, t) in best.items():
            if (t[0], px) not in typical:  # wall_px once per DT1 and own height, not per tile
                typical[t[0], px] = wall_px(heights[t[0]], use.get(t[0]), px)
            out[k][c] = expected_class(k, t, typical[t[0], px] or px)
    water = {c for c, (kind, tiles) in lay.keys.items() if kind == "f" and any(
        liquid_floor(t, lay.flags.get(c, bytes(25)), heights) for t in tiles)}
    for c in water:
        if any((c[0] + dx, c[1] + dy) in lay.keys and (c[0] + dx, c[1] + dy) not in water for dx, dy in painter.STEPS):
            out["water"][c] = None
    return out


@dataclass
class Hd:
    """What the HD preset stands on the DS1's tiles. Fill it through add()."""
    tops: dict[str, dict[Tile, float]] = field(default_factory=lambda: {e: {} for e in EDGES})  # type -> tile -> top
    centres: dict[str, list[Tile]] = field(default_factory=lambda: {e: [] for e in EDGES})  # type -> a tile per piece
    other: dict[Tile, float] = field(default_factory=dict)  # a tall piece of no edge type -> its top
    who: dict[Tile, tuple[float, str]] = field(default_factory=dict)  # the tallest piece on a tile -> its folder
    nametops: dict[str, dict[Tile, float]] = field(default_factory=lambda: {e: {} for e in EDGES})  # type -> tile -> top
    # of the pieces its model name types (model_type) alone. The height is scored from these: a tile in `tops` but not
    # here is the stock vote's (#822: the height D2R's own presets draw the legacy edge at is the right one), and a
    # voted piece on a named one never changes its height
    stocktops: dict[Tile, float] = field(default_factory=dict)  # a piece typed only by the stock vote -> its top, apart
    # from `tops` so a tall voted piece never hides the low name-typed piece under it from wall_cover's height classes
    core: dict[str, set] = field(default_factory=lambda: {e: set() for e in EDGES})  # type -> tiles a piece mainly
    # covers (core_footprint); a tile in `tops` but not here is covered only by a neighbouring box's spill

    def add(self, b: tuple, types: tuple, named: str | None, folder: str) -> None:
        """Stand one piece (a boxes box `b`) of `folder` on the tiles. `types` are the edge types it counts as
        (model_types), `named` its name's type (model_type). A piece of no type under TALL is left out; a tall
        one of none goes to `other`. Only the name's type adds to `centres` (spacing): a prop kit the stock vote types
        is not a run of pieces. Shared with tools that build an Hd of their own (#843)."""
        top = b[4]
        if not types and top < TALL:
            return
        tiles = footprint([b])
        for c in tiles:
            if top >= self.who.get(c, (0.0, ""))[0]:
                self.who[c] = (top, folder)
        if not types:
            for c in tiles:
                self.other[c] = max(top, self.other.get(c, top))
            return
        if named not in types:
            for c in tiles:
                self.stocktops[c] = max(top, self.stocktops.get(c, top))
        for t in types:
            for c in tiles:
                self.tops[t][c] = max(top, self.tops[t].get(c, top))
            self.core[t] |= core_footprint(b)
            if t == named:
                for c in tiles:
                    self.nametops[t][c] = max(top, self.nametops[t].get(c, top))
                self.centres[t].append((int((b[0] + b[1]) / 2 // presets.TILE), int((b[2] + b[3]) / 2 // presets.TILE)))


def hd_pieces(entities: list[dict], box: Callable[[str], tuple | None], prefab: Callable[[str], list[dict]],
              ground: Ground | None = None, stock: dict | None = None) -> Hd:
    """The preset's pieces by edge type (model_type) with the tiles under each (room-sized boxes left out). With
    `ground` (plane_ground), a piece's top is measured from the ground plane under it; `stock` (stock_types) adds the types the stock vote gives its folders
    (model_types)."""
    hd, kind, folder, byname = Hd(), [()], [""], [None]

    def typed(path: str):
        b = box(path)
        folder[0] = folder_of(path)
        byname[0] = model_type(path)
        kind[0] = model_types(path, stock)  # boxes() yields right after this call, so the box it yields is this model's
        return b
    for b in boxes(entities, typed, prefab, ground=ground):
        if b[1] - b[0] > ROOM or b[3] - b[2] > ROOM:
            continue
        hd.add(b, kind[0], byname[0], folder[0])
    return hd



def hd_class(top: float) -> str:
    return "tall" if top >= TALL else "low" if top >= LOW else "flat"



def runs(tiles: set[Tile]) -> list[set[Tile]]:
    """The 8-connected groups of tiles: an edge run."""
    left, out = set(tiles), []
    while left:
        stack, run = [left.pop()], set()
        while stack:
            c = stack.pop()
            run.add(c)
            for dx, dy in STEPS8:
                n = (c[0] + dx, c[1] + dy)
                if n in left:
                    left.discard(n)
                    stack.append(n)
        out.append(run)
    return out


@dataclass
class TypeScore:
    n: int = 0  # legacy tiles of the type
    missing: set = field(default_factory=set)
    wrong_type: set = field(default_factory=set)
    wrong_height: set = field(default_factory=set)
    holes: set = field(default_factory=set)  # missing tiles between pieces of an edge run
    culprits: dict = field(default_factory=dict)  # folder -> wrong-type tiles its pieces stand on
    piled: int = 0  # pieces beyond STACK per tile of a run
    spill: set = field(default_factory=set)  # right tiles covered only by a box's spill, never by its core
    scored: bool = True  # False: height and spacing are not scored (floors)

    @property
    def score(self) -> float | None:
        return None if not self.n else 1 - len(self.missing | self.wrong_type | self.wrong_height) / self.n

    @property
    def spacing(self) -> float | None:
        return None if not self.n or not self.scored else max(0.0, 1 - (len(self.holes) + self.piled) / self.n)


def score_type(kind: str, legacy: dict[Tile, str | None], hd: Hd) -> TypeScore:
    """One edge type of one map. A legacy tile is present when an HD piece of any edge type, or a tall `other` piece,
    stands on it; right when the piece's type is `kind` (an `other` piece is never right)."""
    s = TypeScore(len(legacy))
    mine = hd.tops[kind]
    for c, want in legacy.items():
        if c in mine:
            if want and c in hd.nametops[kind] and hd_class(hd.nametops[kind][c]) != want:
                s.wrong_height.add(c)
        elif any(c in tops for tops in hd.tops.values()) or c in hd.other:
            s.wrong_type.add(c)
            folder = hd.who.get(c, (0.0, "?"))[1]
            s.culprits[folder] = s.culprits.get(folder, 0) + 1
        else:
            s.missing.add(c)
    s.spill = {c for c in mine if c in legacy and c not in s.wrong_height and c not in hd.core[kind]}
    for run in runs(set(legacy)):
        good = {c for c in run if c in mine}
        s.holes |= {c for c in run if c not in mine and any(
            (c[0] + a[0], c[1] + a[1]) in good and (c[0] + b[0], c[1] + b[1]) in good for a, b in HOLE_PAIRS)}
        near = run | {(x + dx, y + dy) for x, y in run for dx, dy in STEPS8}
        pieces = sum(p in near for p in hd.centres[kind])
        s.piled += max(0, int(pieces - STACK * max(len(good), 1)))
    return s


