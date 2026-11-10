"""The wall pass (#734, part of #707): whole stock wall pieces along the legacy wall edges the per-tile kit left bare,
filled with props, or drawn at the wrong height.

The per-tile kit copies what D2R drew on a tile with the same key, so a PD2 wall tile whose key the stock never saw
gets nothing (palace cellar walls, arcane sanctuary walls), and one whose key was a barrel shelf gets a barrel. This
pass reads the wall edges of the legacy DS1 the way pd2_fidelity does (legacy_edges: the type "wall" and the height
class low/tall each tile asks for, #750), finds those the preset does not get right, and stands a D2R wall piece on
each: one harvested from a stock wall tile of the same orientation (so it is rotated right) and of the asked height
class, from the tile set's own stock first. A piece covers the tiles under its box, so tiles it covers get no second
piece (stock spacing, not a piece per tile); a piece whose box would reach walkable floor (a doorway, an arch) is not
used. Pieces of the wrong class on a tile that gets a replacement are removed. `act4/mesa/inv_wall` is invisible
in legacy and stays bare. The ground a wall stands on is wall_y(); floor_height (#742, ORDER 900) then lifts it with the plane under its tile.

The pass needs what only the merge knows (the legacy-named layout, DT1 heights, D2R's model boxes): apply() calls
configure() per DS1. A build with no configure() runs no wall pass.
"""
from __future__ import annotations

import copy
import zlib
from dataclasses import dataclass
from typing import Callable

from ..consts import GROUND, TILE
from ..entity import _transform, is_waypoint
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


CONTEXT: dict[str, Context] = {}  # DS1 path (Build.seed) -> its Context
_INDEX: dict[tuple, dict] = {}


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
    if key in _INDEX:
        return _INDEX[key]
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
                Option(e, x - c[0] * TILE, z - c[1] * TILE, y - g, cls, foot, preset["dependencies"]))
    _INDEX[key] = out
    return out


def pick(options: list[Option], want: str, c: tuple, bad: set, covered: set, blocked: set, seed: str) -> Option | None:
    """The option of class `want` whose box stands on no blocked tile and covers the most uncovered bad tiles."""
    best = None
    for k, o in enumerate(options):
        if o.cls != want:
            continue
        tiles = {(c[0] + i, c[1] + j) for i, j in o.foot}
        if tiles & blocked:
            continue
        rank = (len(tiles & bad - covered), zlib.crc32(f"{seed}:{c}:{k}".encode()))
        if best is None or rank > best[0]:
            best = (rank, o)
    return best[1] if best else None


def trim(b, ctx: Context, legacy: dict) -> list[dict]:
    """b.loose without the wall pieces too tall for the tiles under their box: a piece whose box covers a legacy wall
    tile that asks for a low wall, and none that asks for a tall one. The fill then stands low pieces there."""
    import pd2_coverage as coverage
    import pd2_fidelity as fid
    keep = []
    for e in b.loose:
        if fid.model_type(model(e)) == "wall":
            found = list(coverage.boxes([e], ctx.box, ctx.prefab))
            if len(found) == 1 and fid.hd_class(found[0][4]) == "tall":
                want = {legacy[c] for c in coverage.footprint([found[0]]) if c in legacy}
                if want == {"low"}:
                    continue
        keep.append(e)
    return keep


def run(b) -> None:
    ctx = CONTEXT.get(b.seed)
    if ctx is None:
        return
    import pd2_fidelity as fid
    legacy = fid.legacy_edges(ctx.legacy, ctx.heights)["wall"]
    if not legacy:
        return
    b.loose = trim(b, ctx, legacy)
    ents = b.entities + b.loose
    hd = fid.hd_pieces(ents, ctx.box, ctx.prefab)
    score = fid.score_type("wall", legacy, hd)
    bad = score.missing | score.wrong_type | score.wrong_height
    if not bad:
        return
    own, every = index(b.stock, ctx), index(ctx.stock, ctx)
    blocked = walkable(ctx.legacy)
    covered: set = set()
    added: list[dict] = []
    for c in sorted(bad):
        if c in covered:
            continue
        tile = max((t for t in ctx.legacy.keys[c][1] if fid.tile_type(t) == "wall"),
                   key=lambda t: ctx.heights[t[0]].get(t[1:], 0), default=None)
        if tile is None or tile[1] not in (*UPPER, *LOWER_AS):
            continue
        o = LOWER_AS.get(tile[1], tile[1])
        want = legacy[c]
        choice = (pick(own.get(o, []), want, c, bad, covered, blocked, b.seed)
                  or pick(every.get(o, []), want, c, bad, covered, blocked, b.seed))
        if choice is None:
            continue
        e = copy.deepcopy(choice.ent)
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
