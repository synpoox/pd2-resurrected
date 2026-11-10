"""Fidelity scores of the generated HD presets against their legacy DS1s, per edge type and floor (#732, part of #707).

The #707 bar is "matches legacy", not coverage %. For each PD2-only map (the DS1s the HD preset generator builds, #213)
this compares the HD preset with the DS1/DT1 tile by tile and scores, per edge type and for floors:

- missing: legacy has the edge (or floor), the HD preset has no piece there;
- wrong type: a piece stands there, but not of the legacy type (an ice cliff where legacy has a low wall);
- wrong height: the right type, but of the wrong height class (low: a fence, a stone wall; tall: a cliff, a wall, a
  pillar, a tree);
- spacing: along an edge run, bare holes between pieces and pieces piled on each other.

Edge types and how legacy tiles are classified (classify, the shared classifier), from the PD2 install's DT1 files, measured over their
headers (96-byte tile records, as pd2_presets.dt1_tiles reads them: orientation at +20, the tile's pixel height at +8):

- walls, cliffs, fences: a wall orientation (WALL_ORIENT: cliff1/cliff2/final_cliff*/a5_cliff_parts hold only 1-7, 10,
  11 and the lower variants 16-19; fence, stonewall, stone_fence and sewerwalls hold 1-7; 10 and 11 also appear in
  warp, waypoint and roof tiles) told apart by the DT1's name
  (INVISIBLE, FENCE, CLIFF; the rest are walls), and a cell with an upper over a lower wall tile is a cliff (cliff_cell);
- pillars: orientation 12 (pd2_presets.LIKE: "a PD2 DT1's pillar"; stone_fence, sewerchamb and seweritems hold their
  columns there);
- treelines: orientation 14 (pd2_presets.TREE_LAYER) or a wall tile of a tree DT1 (pd2_presets.draws_trees names them
  by "tree"); orientation 13 is a tree DT1's shadow (91-120 tiles at height 0 to -128) and is left out;
- water and lava edges: a wall layer of a river/pond/rivbank/pool DT1 or lava's extwalls/intwalls/rocks (classify), or
  a liquid floor tile (a mostly blocked floor, pd2_painter.blocked_floor, of a liquid DT1) with a 4-neighbour that is
  not liquid.

Heights: a wall's height class comes from its DT1 (wall_px: the median pixel height of the DT1's wall tiles, weighted
by how often the map places each; the tile's own height when the DT1 is mixed, MIXED): low when that is under LOW_PX
(see there) or the DT1 is a fence/stone wall by name (pd2_coverage.LOW_EDGE: low by design), else tall. Cliffs and
trees are always tall (384-480, 256-480 px). A piece's height is its model box top (pd2_coverage.boxes): flat below
pd2_coverage.LOW, low below TALL, else tall.

HD pieces are typed by their model's file name (model_type). A tall piece of no edge type is "other": it counts as
present, and as the wrong type for every legacy edge.

Floors: the DS1's walkable floor (pd2_coverage.walkable_floor). Missing: under no terrain box; wrong type: under a
structure (stairs, a wall; pd2_coverage.wrong_tiles). Height and spacing are not scored for floors: the terrain box of a
pasted room is the room's own, not a per-tile surface, so a floor tile's height cannot be told from it.

Outputs, from pd2_coverage.run: <out>/fidelity.md, <out>/fidelity-baseline.tsv (main's scores; `map type n score
spacing`, percentages), and per map <name>_fidelity.png. Needs the same inputs as pd2_coverage; no game files change.
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterator

import pd2_coverage as coverage
import pd2_painter as painter
import pd2_presets as presets

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
# researcher's table, V = verified in the DT1 list of merge/presets-hd.tsv):
INVISIBLE = re.compile(r"inv_wall")  # act4/mesa/inv_wall: a blocker that draws nothing
FENCE = re.compile(r"fence|chain_walls|foregate|barset|palisade|picket|graveyrd")  # mesa chain_walls, siege fence,
# fort foregate, barracks barset, outdoors/town fence
# FENCE DT1s whose walls are tall: classify still calls them fences (the scorer's types stay fixed); the fence pass
# (presets/passes/edges_fences.py) uses this to leave them to the wall pass. Median pixel height of the tiles the PD2
# maps place is 256-288 px for these (mesa and a5 chain_walls 256-288, graveyrd 256, barset 256, town/fence and descend/stone_fence 288), and
# foregate's walls are 224-416 (median 288), against 192 for the low siege, outdoors and descend fences (#757).
TALL_FENCE = re.compile(r"chain_walls|foregate|barset|graveyrd|town/fence|stone_fence")
# kurast/travincal terraces, icecave interior, hellcaves, mesa brick_walls; `corner` because corner.dt1 stands in as
# guild/outdoors/cliff.dt1 (presets-hd.tsv). Approximate: matched by name, not by every PD2 DT1 checked.
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
CULPRITS = 8  # the folders listed under the all-maps table, per type
STACK = 2.0  # pieces per tile of an edge run above which they pile on each other (stock walls span 5-10 units)
TOLERANCE = coverage.TOLERANCE
MIN_TILES = 20  # a (map, type) of fewer legacy tiles never counts a spacing drop: one hole moves it by whole points
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


def model_type(path: str) -> str | None:
    """The edge type of an HD model, by its folder and file name; None for anything else (grass, rocks, props)."""
    name = "/".join(path.lower().split("/")[-2:])
    if GROUND_COVER.search(name):
        return None
    return next((t for t, rx in HD_MODEL if rx.search(name)), None)


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
    return "low" if kind == "fence" or (kind == "wall" and (
        px < LOW_PX or coverage.LOW_EDGE.search(name) or LOW_NAME.search(name))) else "tall"


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
            out[k][c] = expected_class(k, t, wall_px(heights[t[0]], use.get(t[0]), px) or px)
    water = {c for c, (kind, tiles) in lay.keys.items() if kind == "f" and any(
        classify(t[0], 0, "floor", painter.blocked_floor(lay.flags.get(c, bytes(25)))) == "liquid" for t in tiles)}
    for c in water:
        if any((c[0] + dx, c[1] + dy) in lay.keys and (c[0] + dx, c[1] + dy) not in water for dx, dy in painter.STEPS):
            out["water"][c] = None
    return out


@dataclass
class Hd:
    """What the HD preset stands on the DS1's tiles."""
    tops: dict[str, dict[Tile, float]] = field(default_factory=lambda: {e: {} for e in EDGES})  # type -> tile -> top
    centres: dict[str, list[Tile]] = field(default_factory=lambda: {e: [] for e in EDGES})  # type -> a tile per piece
    other: dict[Tile, float] = field(default_factory=dict)  # a tall piece of no edge type -> its top
    who: dict[Tile, tuple[float, str]] = field(default_factory=dict)  # the tallest piece on a tile -> its folder


def hd_pieces(entities: list[dict], box: Callable[[str], tuple | None], prefab: Callable[[str], list[dict]],
              ground: coverage.Ground | None = None) -> Hd:
    """The preset's pieces by edge type (model_type) with the tiles under each (room-sized boxes left out). With
    `ground` (coverage.plane_ground), a piece's top is measured from the ground plane under it."""
    hd, kind, folder = Hd(), [None], [""]

    def typed(path: str):
        b = box(path)
        folder[0] = path.rsplit("/", 2)[-2] if path.count("/") else ""
        kind[0] = model_type(path)  # boxes() yields right after this call, so the box it yields is this model's
        return b
    for b in coverage.boxes(entities, typed, prefab, ground=ground):
        if b[1] - b[0] > coverage.ROOM or b[3] - b[2] > coverage.ROOM:
            continue
        top, t = b[4], kind[0]
        if t is None and top < coverage.TALL:
            continue
        tiles = coverage.footprint([b])
        for c in tiles:
            if top >= hd.who.get(c, (0.0, ""))[0]:
                hd.who[c] = (top, folder[0])
        if t is None:
            for c in tiles:
                hd.other[c] = max(top, hd.other.get(c, top))
            continue
        for c in tiles:
            hd.tops[t][c] = max(top, hd.tops[t].get(c, top))
        hd.centres[t].append((int((b[0] + b[1]) / 2 // presets.TILE), int((b[2] + b[3]) / 2 // presets.TILE)))
    return hd


def hd_class(top: float) -> str:
    return "tall" if top >= coverage.TALL else "low" if top >= coverage.LOW else "flat"


def wall_cover(legacy: dict[str, dict[Tile, str | None]], hd: Hd, tall: set[Tile]) -> set[Tile]:
    """The DS1 wall tiles the coverage wall score counts as drawn (#787), by the fidelity classes: a legacy edge tile
    (legacy_edges) is drawn when an HD piece of the height class it asks for ("low", "tall"; any piece for water)
    stands on it, whatever the piece's type; of a tile holding several edge types, one right is enough. A wall tile
    with no edge type (invisible blockers, a layer classify() skips) keeps the old rule: under a `tall` tile."""
    classes: dict[Tile, set[str]] = {}
    for tops in list(hd.tops.values()) + [hd.other]:
        for c, top in tops.items():
            classes.setdefault(c, set()).add(hd_class(top))
    typed = {c for tiles in legacy.values() for c in tiles}
    right = {c for tiles in legacy.values() for c, want in tiles.items()
             if (want in classes.get(c, ())) or (want is None and classes.get(c, set()) - {"flat"})}
    return right | (tall - typed)


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
            if want and (got := hd_class(mine[c])) != want:
                s.wrong_height.add(c)
        elif any(c in tops for tops in hd.tops.values()) or c in hd.other:
            s.wrong_type.add(c)
            folder = hd.who.get(c, (0.0, "?"))[1]
            s.culprits[folder] = s.culprits.get(folder, 0) + 1
        else:
            s.missing.add(c)
    for run in runs(set(legacy)):
        good = {c for c in run if c in mine}
        s.holes |= {c for c in run if c not in mine and any(
            (c[0] + a[0], c[1] + a[1]) in good and (c[0] + b[0], c[1] + b[1]) in good for a, b in HOLE_PAIRS)}
        near = run | {(x + dx, y + dy) for x, y in run for dx, dy in STEPS8}
        pieces = sum(p in near for p in hd.centres[kind])
        s.piled += max(0, int(pieces - STACK * max(len(good), 1)))
    return s


def score_map(lay: presets.Layout, heights: dict[str, dict], hd: Hd, floors: int, bare: set, wrong: set
              ) -> dict[str, TypeScore]:
    """Every type of one map that the legacy DS1 has (floor first-class, but without height and spacing)."""
    out = {k: score_type(k, tiles, hd) for k, tiles in legacy_edges(lay, heights).items() if tiles}
    if floors:
        out["floor"] = TypeScore(floors, set(bare), set(wrong - bare), scored=False)
    return out


COLOURS = {"ok": (60, 210, 60), "missing": (230, 40, 40), "type": (240, 150, 30), "height": (235, 220, 40),
           "hole": (230, 40, 200)}


def image(lay: presets.Layout, legacy: dict[str, dict[Tile, str | None]], scores: dict[str, TypeScore],
          name: str) -> dict[str, bytes]:
    """<name>_fidelity.png: the legacy edge tiles green where right, red missing, orange the wrong type, yellow the
    wrong height, magenta a hole in a run; the rest of the DS1 dim."""
    px = {c: (45, 42, 36) for c in lay.keys}
    for kind, tiles in legacy.items():
        s = scores.get(kind)
        if not s:
            continue
        for c in tiles:
            px[c] = (COLOURS["hole"] if c in s.holes else COLOURS["missing"] if c in s.missing
                     else COLOURS["type"] if c in s.wrong_type else COLOURS["height"] if c in s.wrong_height
                     else COLOURS["ok"])
    return {f"{name}_fidelity.png": coverage.png(lay.width, lay.height, px, zoom=4)}


# --- reports -----------------------------------------------------------------------------------------------------

Rows = dict[str, dict[str, TypeScore]]  # map -> type -> score


def pct(v: float | None) -> float | None:
    return None if v is None else round(100 * v, 1)


def baseline_text(rows: Rows) -> str:
    lines = ["# HD fidelity on main (tools/pd2_fidelity.py): map, type, legacy tiles, score %, spacing % or -"]
    for name in sorted(rows):
        for kind in TYPES:
            if s := rows[name].get(kind):
                sp = pct(s.spacing)
                lines.append(f"{name}\t{kind}\t{s.n}\t{pct(s.score)}\t{'-' if sp is None else sp}")
    return "\n".join(lines) + "\n"


def read_baseline(text: str) -> dict[tuple[str, str], tuple[float, float | None]]:
    out = {}
    for line in text.splitlines():
        if line and not line.startswith("#"):
            name, kind, _n, score, spacing = line.split("\t")
            out[name, kind] = (float(score), None if spacing == "-" else float(spacing))
    return out


@dataclass
class Change:
    name: str
    kind: str
    score: tuple[float | None, float | None]  # (baseline, now)
    spacing: tuple[float | None, float | None]
    drop: bool


def changes(rows: Rows, base: dict, tolerance: float = TOLERANCE) -> list[Change]:
    """The (map, type) pairs whose score or spacing differ from the baseline, worst drop first. A pair the baseline
    has and the run no longer scores is a drop; a pair of under MIN_TILES legacy tiles is exempt for its spacing, not
    its score."""
    now = {(n, k): (pct(s.score), pct(s.spacing)) for n, r in rows.items() for k, s in r.items()}
    small = {(n, k) for n, r in rows.items() for k, s in r.items() if s.n < MIN_TILES}
    out = []
    for key in sorted(set(now) | set(base)):
        (s0, p0), (s1, p1) = base.get(key, (None, None)), now.get(key, (None, None))
        if key in base and key in now and (s0, p0) == (s1, p1):
            continue
        # a small pair is exempt from spacing only (one hole moves it by whole points), never from its score
        pairs = ((s0, s1),) if key in small else ((s0, s1), (p0, p1))
        fell = any(o is not None and n is not None and o - n > tolerance for o, n in pairs)
        drop = key not in now or fell or (key not in small and p0 is not None and p1 is None)
        out.append(Change(*key, (s0, s1), (p0, p1), drop))

    def worst(c: Change) -> float:
        return min((n - o for o, n in (c.score, c.spacing) if o is not None and n is not None), default=0.0)
    return sorted(out, key=lambda c: (not c.drop, worst(c), c.name, c.kind))


def by_type(found: list[Change]) -> dict[str, int]:
    return {k: sum(c.drop for c in found if c.kind == k) for k in TYPES if any(c.drop and c.kind == k for c in found)}


def changes_md(found: list[Change], tolerance: float = TOLERANCE) -> str:
    def cell(old, new) -> str:
        if old is None and new is None:
            return "-"
        if old is None:
            return f"new {new}%"
        if new is None:
            return f"{old}% -> gone"
        return f"{old}% -> {new}% ({new - old:+.1f})" if old != new else f"{new}%"
    lines = ["## Fidelity changes against the baseline", ""]
    if not found:
        return "\n".join(lines + ["No map's fidelity changed.", ""])
    drops = by_type(found)
    lines += [f"{len(found)} (map, type) scores changed; {sum(drops.values())} dropped by more than the "
              f"{tolerance:g}-point tolerance or are no longer scored"
              + (": " + ", ".join(f"{k} {n}" for k, n in drops.items()) if drops else "") + ".", "",
              "| Map | Type | Score | Spacing | |", "|---|---|---|---|---|"]
    lines += [f"| {c.name} | {c.kind} | {cell(*c.score)} | {cell(*c.spacing)} | {'**drop**' if c.drop else ''} |"
              for c in found]
    return "\n".join(lines) + "\n"


def report(rows: Rows, commit: str = "", found: list[Change] | None = None) -> str:
    p = lambda v: "-" if v is None else f"{100 * v:.1f}%"  # noqa: E731
    lines = ["# HD fidelity against legacy" + (f" ({commit})" if commit else ""), "",
             f"{len(rows)} generated maps. Per edge type and floor: share of legacy tiles the HD preset gets right "
             "(score), the tiles it misses, draws as another type, or at the wrong height class, and spacing (holes "
             "in an edge run and piled pieces). Floors score missing and wrong type only. Overlays: "
             "`<map>_fidelity.png`, green right, red missing, orange wrong type, yellow wrong height, magenta a hole.",
             ""]
    if found is not None:
        lines += [changes_md(found), ""]
    lines += ["## All maps", "", "| Type | Legacy tiles | Score | Missing | Wrong type | Wrong height | Spacing |",
              "|---|---|---|---|---|---|---|"]
    for kind in TYPES:
        ss = [r[kind] for r in rows.values() if kind in r]
        if n := sum(s.n for s in ss):
            wrong = sum(len(s.missing | s.wrong_type | s.wrong_height) for s in ss)
            sp = sum(len(s.holes) + s.piled for s in ss)
            lines.append(f"| {kind} | {n} | {p(1 - wrong / n)} | {sum(len(s.missing) for s in ss)} | "
                         f"{sum(len(s.wrong_type) for s in ss)} | {sum(len(s.wrong_height) for s in ss)} | "
                         + (p(max(0.0, 1 - sp / n)) if ss[0].scored else "-") + " |")
    for kind in EDGES:
        total: dict[str, int] = {}
        for r in rows.values():
            for f, k in (r[kind].culprits if kind in r else {}).items():
                total[f] = total.get(f, 0) + k
        if total:
            top = sorted(total.items(), key=lambda kv: (-kv[1], kv[0]))[:CULPRITS]
            lines += ["", f"Wrong {kind}: " + ", ".join(f"`{f}` {k}" for f, k in top)]
    lines += ["", "## Per map", "", "| Map | Type | Legacy tiles | Score | Missing | Wrong type | Wrong height | Spacing |",
              "|---|---|---|---|---|---|---|---|"]
    for name in sorted(rows, key=lambda n: (min((s.score for s in rows[n].values() if s.score is not None), default=1), n)):
        for kind in TYPES:
            if s := rows[name].get(kind):
                lines.append(f"| {name} | {kind} | {s.n} | {p(s.score)} | {len(s.missing)} | {len(s.wrong_type)} | "
                             f"{len(s.wrong_height)} | {p(s.spacing)} |")
    return "\n".join(lines) + "\n"


def write(out: Path, rows: Rows, commit: str = "", base: dict | None = None) -> list[Change] | None:
    """<out>/fidelity.md and fidelity-baseline.tsv; the changes against `base` (read_baseline) when given."""
    found = None if base is None else changes(rows, base)
    (out / "fidelity.md").write_text(report(rows, commit, found), "utf-8")
    (out / "fidelity-baseline.tsv").write_text(baseline_text(rows), "utf-8", newline="\n")
    if found is not None:
        (out / "fidelity-changes.md").write_text(changes_md(found), "utf-8")
    return found
