"""The wall pass (#734, part of #707): whole stock wall pieces along the legacy wall edges the per-tile kit left bare,
filled with props, or drawn at the wrong height.

The per-tile kit copies what D2R drew on a tile with the same key, so a PD2 wall tile whose key the stock never saw
gets nothing (palace cellar walls, arcane sanctuary walls), and one whose key was a barrel shelf gets a barrel. This
pass reads the wall edges of the legacy DS1 the way pd2_fidelity does (legacy_edges: the type "wall" and the height
class low/tall each tile asks for, #750), finds those the preset does not get right, and stands a D2R wall piece on
each: one harvested from a stock wall tile of the same orientation (so it is rotated right) and of the asked height
class, from the tile set's own stock first. A piece covers the tiles under its box, so tiles it covers get no second
piece (stock spacing, not a piece per tile); a piece whose box would reach walkable floor (a doorway, an arch) is not
used. The fallback past the tile set's own stock keeps to the model folders that stock draws walls from (its act and
family): a tile with no such piece stays bare rather than get another act's wall. Pieces of the wrong class on a tile
that gets a replacement are removed, unless one is the only cover on a cliff, pillar, treeline or fence tile. `act4/mesa/inv_wall` is invisible
in legacy and stays bare. A `walls:<PD2 DT1>` line of merge/presets-hd.d (#826) makes the wall tiles drawn from that PD2 DT1
(which the kit leaves as nothing) take their pieces only from stock wall tiles of the named D2R DT1, of any family. Such a
line takes that DT1's cells of the edge types wall, cliff and pillar (RULED), whatever fidelity types them, and the pass
runs for them even on a map with no legacy "wall" edges. The ground a wall stands on is wall_y(); floor_height (#742, ORDER 900) then lifts it with the plane under its tile.

The pass needs what only the merge knows (the legacy-named layout, DT1 heights, D2R's model boxes): apply() calls
configure() per DS1. A build with no configure() runs no wall pass.
"""
from __future__ import annotations

import collections
import zlib
from dataclasses import dataclass, field
from typing import Callable

from ..consts import GROUND, TILE
from ..entity import _transform, clone, is_waypoint
from ..layout import Layout, walkable

ORDER = 350
UPPER = range(1, 10)  # the wall orientations a stock piece is indexed by
LOWER_AS = {16: 1, 17: 2, 18: 3, 19: 4}  # a lower variant borrows the upper one's pieces (a5_cliff_parts pairs them)


@dataclass
class Context:
    legacy: Layout  # the DS1 named by its own DT1s (not stand-ins), as pd2_fidelity reads it
    heights: dict  # pd2_fidelity.Heights
    box: Callable  # model path -> its box, from model_lod_desc.bin
    prefab: Callable  # prefab path -> its entities
    stock: list  # every stock (layout, preset): the fallback when the tile set's own has no piece
    far: Callable = lambda e: False
    walls: dict = field(default_factory=dict)  # `walls:` lines (#826): PD2 DT1 -> the D2R DT1 its wall tiles borrow pieces from


CONTEXT: dict[str, Context] = {}  # DS1 path (Build.seed) -> its Context
_INDEX: dict[tuple, tuple] = {}  # stock ids -> (the presets, held so an id is never reused; its index)


def configure(ds1: str, ctx: Context) -> None:
    CONTEXT[ds1] = ctx


@dataclass
class Option:
    ent: dict
    dx: float
    dz: float
    rel_y: float  # above the stock ground it stood on
    cls: str  # pd2_fidelity.hd_class of its top
    foot: tuple  # tile offsets (from the tile it belongs to) under its box
    deps: dict
    dt1: str = ""  # the DT1 of the stock wall tile it stands on (a `walls:` line picks by it)


def wall_y(b, x: float, z: float) -> float:
    """The ground height walls stand on at (x, z), before floor_height (ORDER 900, #742) lifts every placed piece by the
    plane under its tile: 0, the stock ground the piece was harvested from (rel_y is relative to it)."""
    return 0.0


def model(e: dict) -> str:
    return next((c.get("filename", "") for c in e["components"] if c["type"] == "ModelDefinitionComponent"), "")


def index(stock: list, ctx: Context) -> dict[int, list[Option]]:
    """The wall pieces of the stock presets by the orientation of the single wall tile they stand on."""
    import pd2_coverage as coverage
    import pd2_fidelity as fid
    key = tuple(id(p) for _, p in stock)
    held = _INDEX.get(key)
    if held and all(a is p for a, (_, p) in zip(held[0], stock)):
        return held[1]
    out: dict[int, list[Option]] = {}
    for lay, preset in stock:
        ground = preset.get(GROUND)
        for e in preset["entities"]:
            t = _transform(e)
            path = model(e)
            if t is None or not path or fid.model_type(path) != "wall" or is_waypoint(e) or ctx.far(e):
                continue
            x, y, z = t["position"]["x"], t["position"]["y"], t["position"]["z"]
            c = (int(x // TILE), int(z // TILE))
            tiles = lay.keys.get(c, ("f", ()))
            if tiles[0] != "w" or len(tiles[1]) != 1 or tiles[1][0][1] not in UPPER:
                continue
            found = list(coverage.boxes([e], ctx.box, ctx.prefab))
            if len(found) != 1:
                continue
            x0, x1, z0, z1, top, _ = found[0]
            if x1 - x0 > coverage.ROOM or z1 - z0 > coverage.ROOM:
                continue
            g = (ground.at(x, z, y) if ground else None) or 0.0
            cls = fid.hd_class(top - g)
            if cls == "flat":
                continue
            foot = tuple((tx - c[0], tz - c[1]) for tx, tz in coverage.footprint([found[0]]))
            if (0, 0) not in foot:
                continue
            out.setdefault(tiles[1][0][1], []).append(
                Option(e, x - c[0] * TILE, z - c[1] * TILE, y - g, cls, foot, preset["dependencies"],
                       tiles[1][0][0]))
    _INDEX[key] = (tuple(p for _, p in stock), out)
    return out


def folder(e: dict) -> str:
    return model(e).rsplit("/", 1)[0]


def families(stock: list) -> set[str]:
    """The model folders the stock presets draw walls from: the act and family a map's own stock uses."""
    import pd2_fidelity as fid
    return {folder(e) for _, p in stock for e in p["entities"] if model(e) and fid.model_type(model(e)) == "wall"}


def pick(options: list[Option], want: str, c: tuple, bad: set, covered: set, blocked: set, seed: str,
         classed: dict | None = None) -> Option | None:
    """The option of class `want` whose box stands on no blocked tile and covers the most uncovered bad tiles.
    `classed`: the caller's memo of each list's options by class, with their index in the list (the hash below needs
    it); one dict per pass run, so it outlives no merge."""
    best = None
    classed = {} if classed is None else classed
    key = (id(options), want)
    if key not in classed or classed[key][0] is not options:
        classed[key] = (options, [(k, o) for k, o in enumerate(options) if o.cls == want])
    c0, c1 = c
    for k, o in classed[key][1]:
        tiles = [(c0 + i, c1 + j) for i, j in o.foot]  # foot holds distinct offsets (a footprint set)
        if any(t in blocked for t in tiles):
            continue
        n = sum(t in bad and t not in covered for t in tiles)
        if best is not None and n < best[0][0]:  # the hash only breaks ties
            continue
        rank = (n, zlib.crc32(f"{seed}:{c}:{k}".encode()))
        if best is None or rank > best[0]:
            best = (rank, o)
    return best[1] if best else None


COVER = ("cliff", "pillar", "treeline", "fence")  # the other edge types a wall piece's box may be the only cover of


def trim(b, ctx: Context, legacy: dict) -> list[dict]:
    """b.loose without the wall pieces too tall for the tiles under their box: a piece whose box covers a legacy wall
    tile that asks for a low wall, and none that asks for a tall one. The fill then stands low pieces there. A piece
    stays when it is the only piece on a legacy cliff, pillar, treeline or fence tile. The top is measured from the
    ground wall_y() gives, as index() measures a stock piece from its own."""
    import pd2_coverage as coverage
    import pd2_fidelity as fid
    other: set = set()
    for kind, tiles in fid.legacy_edges(ctx.legacy, ctx.heights).items():
        if kind in COVER:
            other |= set(tiles)
    seen = {id(e): list(coverage.boxes([e], ctx.box, ctx.prefab)) for e in b.entities + b.loose}
    count = collections.Counter(c for found in seen.values() for f in found for c in coverage.footprint([f]))
    keep = []
    for e in b.loose:
        if fid.model_type(model(e)) == "wall":
            found = seen[id(e)]
            if len(found) == 1:
                x0, x1, z0, z1, top, _ = found[0]
                if fid.hd_class(top - wall_y(b, (x0 + x1) / 2, (z0 + z1) / 2)) == "tall":
                    foot = coverage.footprint([found[0]])
                    want = {legacy[c] for c in foot if c in legacy}
                    if want == {"low"} and not any(c in other and count[c] == 1 for c in foot):
                        continue
        keep.append(e)
    return keep


RULED = ("wall", "cliff", "pillar")  # the edge types a `walls:` line takes the cells of its PD2 DT1 in


def ruled_cells(ctx: Context, edges: dict, legacy: dict) -> set:
    """Adds to `legacy` (tile -> height class) the cells of the edge types RULED that a tile of a PD2 DT1 with a
    `walls:` line sits on, whatever type the fidelity scorer gives them (hellcaves DT1 walls are cliffs there). Returns
    those cells."""
    out: set = set()
    if not ctx.walls:
        return out
    for kind in RULED:
        for c, cls in edges[kind].items():
            if any(t[0] in ctx.walls for t in ctx.legacy.keys[c][1]):
                legacy.setdefault(c, cls)
                out.add(c)
    return out


def filtered(memo: dict, borrow: str, o: int, which: str, source: dict) -> list[Option]:
    """The options of orientation `o` in `source` off the stock wall tiles of the D2R DT1 `borrow`, one list each."""
    key = (borrow, o, which)
    if key not in memo:
        memo[key] = [x for x in source.get(o, []) if x.dt1 == borrow]
    return memo[key]


def check_walls(walls: dict, contexts: list[Context]) -> None:
    """The `walls:` lines must each fill something (#826): the PD2 DT1 has a tile of an edge type in RULED in some DS1
    (with an orientation a piece is indexed by), and the wall pass harvested a wall piece off the named D2R DT1. A
    ValueError names the lines that do not."""
    import pd2_fidelity as fid
    orient = {*UPPER, *LOWER_AS}
    drawn = {t[0] for ctx in contexts for _, tiles in ctx.legacy.keys.values() for t in tiles
             if t[0] in walls and t[1] in orient and fid.tile_type(t) in RULED}
    if idle := sorted(set(walls) - drawn):
        raise ValueError(f"walls lines for PD2 DT1 with no wall, cliff or pillar tile any PD2-only preset draws: "
                         f"{', '.join(idle)}")
    if contexts:
        harvested = {x.dt1 for opts in index(contexts[0].stock, contexts[0]).values() for x in opts}
        if none := sorted(d for d in set(walls.values()) if d not in harvested):
            raise ValueError(f"walls lines name D2R DT1 none of the harvested stock draws a wall piece from: "
                             f"{', '.join(none)}")


def run(b) -> None:
    ctx = CONTEXT.get(b.seed)
    if ctx is None:
        return
    import pd2_fidelity as fid
    edges = fid.legacy_edges(ctx.legacy, ctx.heights)
    legacy = dict(edges["wall"])
    ruled = ruled_cells(ctx, edges, legacy)
    if not legacy:
        return
    b.loose = trim(b, ctx, legacy)
    ents = b.entities + b.loose
    hd = fid.hd_pieces(ents, ctx.box, ctx.prefab)
    score = fid.score_type("wall", legacy, hd)
    bad = score.missing | score.wrong_type | score.wrong_height
    if not bad:
        return
    own = index(b.stock, ctx)
    family = families(b.stock)
    whole = index(ctx.stock, ctx)
    every = {o: [x for x in opts if folder(x.ent) in family] for o, opts in whole.items()}
    blocked = walkable(ctx.legacy)
    covered: set = set()
    area, near, good = {}, collections.defaultdict(int), collections.defaultdict(set)
    for k, run in enumerate(fid.runs(set(legacy))):  # per edge run: the wall pieces by it and the tiles it has, as score_type counts
        around = run | {(x + dx, y + dy) for x, y in run for dx, dy in fid.STEPS8}
        area |= {c: k for c in run}
        near[k] = sum(p in around for p in hd.centres["wall"])
        good[k] = {c for c in run if c in hd.tops["wall"]}
    added: list[dict] = []
    classed: dict = {}
    memo: dict = {}  # (D2R DT1, orientation, own or all) -> the options off that DT1, one list per pass run
    for c in sorted(bad):
        if c in covered:
            continue
        tile = max((t for t in ctx.legacy.keys[c][1] if (t[0] in ctx.walls if c in ruled else fid.tile_type(t) == "wall")
                    and fid.tile_type(t) is not None),
                   key=lambda t: ctx.heights[t[0]].get(t[1:], 0), default=None)
        if tile is None or tile[1] not in (*UPPER, *LOWER_AS):
            continue
        o = LOWER_AS.get(tile[1], tile[1])
        want = legacy[c] or "tall"  # a cliff or pillar cell can ask for no class
        mine, anyone = own.get(o, []), every.get(o, [])
        if borrow := ctx.walls.get(tile[0]):  # a `walls:` line: only pieces off that D2R DT1, of any family, none else
            mine = filtered(memo, borrow, o, "own", own)
            anyone = filtered(memo, borrow, o, "all", whole)
        choice = (pick(mine, want, c, bad, covered, blocked, b.seed, classed)
                  or pick(anyone, want, c, bad, covered, blocked, b.seed, classed))
        if choice is None:
            continue
        k = area[c]
        gained = good[k] | {(c[0] + i, c[1] + j) for i, j in choice.foot if (c[0] + i, c[1] + j) in legacy}
        if near[k] + 1 > fid.STACK * max(len(gained), 1):  # one more piece would pile on the run's own
            continue
        near[k] += 1
        good[k] = gained
        e = clone(choice.ent)
        t = _transform(e)
        t["position"]["x"], t["position"]["z"] = c[0] * TILE + choice.dx, c[1] * TILE + choice.dz
        t["position"]["y"] = wall_y(b, t["position"]["x"], t["position"]["z"]) + choice.rel_y
        for comp in e["components"]:
            if comp["type"] == "WallTransparencyComponent" and comp["wallTileLocalCoord"]["x"] >= 0:
                comp["wallTileLocalCoord"] = {"x": c[0], "y": c[1]}
        b.depend(choice.deps)
        added.append(e)
        covered |= {(c[0] + i, c[1] + j) for i, j in choice.foot}
    b.loose = b.loose + added
