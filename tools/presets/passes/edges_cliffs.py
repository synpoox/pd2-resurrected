"""The cliff pass (#830, part of #735): whole D2R cliff pieces along the legacy cliff edges the per-tile kit left typed
as something else (Travincal's terraces under pillars, gore and trim pieces; Kurast's under tree walls) or bare.

The wall pass (edges_walls) stands wall pieces on legacy "wall" edges only. A legacy cliff cell (a DT1 the scorer types
a cliff: travincal and kurast terraces, an upper wall tile over a lower one; pd2_tiles.tile_type, cliff_cell) gets
whatever the stock tile of its key drew, and a PD2 key the stock never saw gets a prop of whatever stood there. This
pass finds the cliff cells the preset does not get right (missing, or covered only by pieces of another type, by model
name alone: the generator does not read the scorer's stock vote, #855) and stands a D2R piece on each: one harvested from a stock tile of the same
orientation whose model is a cliff (pd2_tiles.model_types: a "cliff" folder or file name),
at least TALL high, so it is turned right. The tile set's own stock comes first, then every stock preset's: Act 3 has
no cliff folder of its own, so Travincal takes the rock cliffs of the other acts. A piece covers the tiles under its
box, which then get no second piece; a piece whose box would reach walkable floor is never used. Its ground is the stock
ground it was harvested from (floor_height, ORDER 900, then lifts it with the plane under its tile).

The pass needs the wall pass's context (the legacy-named layout, DT1 heights, D2R's model boxes): edges_walls.configure()
sets it per DS1, so a build without it runs no cliff pass.
"""
from __future__ import annotations

import collections

import pd2_cache

from ..consts import GROUND, TILE
from ..entity import _transform, clone, is_waypoint
from ..layout import walkable
from . import edges_walls as walls

ORDER = 360  # after the wall pass (350), before the fence pass (450) and floor_height (900)
UPPER = walls.UPPER
LOWER_AS = walls.LOWER_AS

_INDEX: dict[tuple, tuple] = {}  # stock ids -> (the presets, held so an id is never reused; its index; the files it read)


def orientation(tiles: tuple, fid) -> int | None:
    """The orientation (1-9) a stock cell's piece is indexed by: the lone upper wall tile, or the upper tile of a cliff
    cell (upper over lower)."""
    if len(tiles) == 1 and tiles[0][1] in UPPER:
        return tiles[0][1]
    if fid.cliff_cell(tiles):
        return next(t[1] for t in tiles if t[1] in UPPER)
    return None


def index(stock: list, ctx: walls.Context, types: dict | None) -> dict[int, list[walls.Option]]:
    """The cliff pieces of the stock presets by the orientation of the wall tile they stand on."""
    import pd2_tiles as fid
    key = tuple(id(p) for _, p in stock)
    held = _INDEX.get(key)
    if held and all(a is p for a, (_, p) in zip(held[0], stock)):
        pd2_cache.adopt_reads(held[2])  # the memo hides the prefab reads it made: every asker's DS1 read them
        return held[1]
    with pd2_cache.sub_trace() as sub:
        out = _build(stock, ctx, types, fid)
    _INDEX[key] = (tuple(p for _, p in stock), out, dict(sub.reads) if sub is not None else {})
    return out


def _build(stock: list, ctx: walls.Context, types: dict | None, fid) -> dict[int, list[walls.Option]]:
    out: dict[int, list[walls.Option]] = {}
    for lay, preset in stock:
        ground = preset.get(GROUND)
        for e in preset["entities"]:
            t = _transform(e)
            path = walls.model(e)
            if t is None or not path or "cliff" not in fid.model_types(path, types) or is_waypoint(e) or ctx.far(e):
                continue
            x, y, z = t["position"]["x"], t["position"]["y"], t["position"]["z"]
            c = (int(x // TILE), int(z // TILE))
            kind, tiles = lay.keys.get(c, ("f", ()))
            o = orientation(tiles, fid) if kind == "w" else None
            if o is None:
                continue
            found = list(fid.boxes([e], ctx.box, ctx.prefab))
            if len(found) != 1:
                continue
            x0, x1, z0, z1, top, _ = found[0]
            if x1 - x0 > fid.ROOM or z1 - z0 > fid.ROOM:
                continue
            g = (ground.at(x, z, y) if ground else None) or 0.0
            if fid.hd_class(top - g) != "tall":  # the scorer wants a cliff tall
                continue
            foot = tuple((tx - c[0], tz - c[1]) for tx, tz in fid.footprint([found[0]]))
            if (0, 0) not in foot:
                continue
            out.setdefault(o, []).append(walls.Option(e, x - c[0] * TILE, z - c[1] * TILE, y - g, "tall", foot,
                                                      preset["dependencies"], tiles[0][0]))
    return out


def cell_orientation(tiles: tuple, heights, fid) -> int | None:
    """The orientation of the stock piece a legacy cliff cell asks for: its upper tile's (a lower one borrows the upper
    one's pieces, as the wall pass does); the tallest cliff-typed layer otherwise."""
    if fid.cliff_cell(tiles):
        return next(t[1] for t in tiles if t[1] in UPPER)
    mine = [t for t in tiles if fid.tile_type(t) == "cliff" and t[1] in (*UPPER, *LOWER_AS)]
    if not mine:
        return None
    t = max(mine, key=lambda t: heights[t[0]].get(t[1:], 0))
    return LOWER_AS.get(t[1], t[1])


def run(b) -> None:
    ctx = walls.CONTEXT.get(b.seed)
    if ctx is None:
        return
    import pd2_tiles as fid
    legacy = fid.legacy_edges(ctx.legacy, ctx.heights)["cliff"]
    if not legacy:
        return
    types = None  # model names only: the generator does not read the scorer's stock vote (#855)
    hd = fid.hd_pieces(b.entities + b.loose, ctx.box, ctx.prefab, stock=types)
    score = fid.score_type("cliff", legacy, hd)
    bad = score.missing | score.wrong_type
    if not bad:
        return
    own = index(b.stock, ctx, types)
    every = index(ctx.stock, ctx, types)
    blocked = walkable(ctx.legacy)
    covered: set = set()
    area, near, good = {}, collections.defaultdict(int), collections.defaultdict(set)
    for k, run_ in enumerate(fid.runs(set(legacy))):  # per edge run: the cliff pieces by it, as score_type counts
        around = run_ | {(x + dx, y + dy) for x, y in run_ for dx, dy in fid.STEPS8}
        area |= {c: k for c in run_}
        near[k] = sum(p in around for p in hd.centres["cliff"])
        good[k] = {c for c in run_ if c in hd.tops["cliff"]}
    added: list[dict] = []
    classed: dict = {}
    for c in sorted(bad):
        if c in covered:
            continue
        o = cell_orientation(ctx.legacy.keys[c][1], ctx.heights, fid)
        if o is None:
            continue
        choice = (walls.pick(own.get(o, []), "tall", c, bad, covered, blocked, b.seed, classed)
                  or walls.pick(every.get(o, []), "tall", c, bad, covered, blocked, b.seed, classed))
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
        t["position"]["y"] = walls.wall_y(b, t["position"]["x"], t["position"]["z"]) + choice.rel_y
        for comp in e["components"]:
            if comp["type"] == "WallTransparencyComponent" and comp["wallTileLocalCoord"]["x"] >= 0:
                comp["wallTileLocalCoord"] = {"x": c[0], "y": c[1]}
        b.depend(choice.deps)
        added.append(e)
        covered |= {(c[0] + i, c[1] + j) for i, j in choice.foot}
    b.loose = b.loose + added

