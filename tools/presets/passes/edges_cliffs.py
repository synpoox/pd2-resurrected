"""The cliff pass (#830, part of #735): whole D2R Act 3 stone pieces along the legacy cliff edges of the Travincal and
Kurast terraces that the per-tile kit left bare or covered only by pieces of another type.

Scope (explicit opt-in): the DS1s in MAPS (riverofblood 1-4, kurastopenwide 1-3) and, in them, the cells of the terrace
DT1s in TERRACES. Nothing else changes: other cliff maps belong to other issues (hellcaves #876, throne #832).

A legacy cliff cell in scope (pd2_tiles.legacy_edges) is bad when no piece the scorer counts as a cliff stands on it:
HD pieces typed by model name and by the stock vote of tools/hd-folder-types.tsv (read here: the generator does not
import the scorer, #855). A piece the vote counts as a cliff already covers its cell, so none is added beside it. A bad
cell gets one piece harvested from a stock tile of the same orientation, from the Act 3 stone folders STONE only (the
ones D2R's own presets stand on cliff edges), at least TALL high and standing on its ground (a piece hanging off it
would read as floating). The tile set's own stock comes first, then every stock preset's. A piece covers the tiles
under its box, which then get no second piece; its box never reaches walkable floor. A cell with no Act 3 stone piece of
its orientation stays bare (LEFT lists them per DS1). Its ground is the stock ground it was harvested from
(floor_height, ORDER 900, then lifts it with the plane under its tile).

The pass needs the wall pass's context (the legacy-named layout, DT1 heights, D2R's model boxes): edges_walls.configure()
sets it per DS1, so a build without it runs no cliff pass.
"""
from __future__ import annotations

import collections
from pathlib import Path

import pd2_cache

from ..consts import GROUND, TILE
from ..entity import _transform, clone, is_waypoint
from ..layout import walkable
from . import edges_walls as walls

ORDER = 360  # after the wall pass (350), before the fence pass (450) and floor_height (900)
UPPER = walls.UPPER
LOWER_AS = walls.LOWER_AS
MAPS = frozenset({"riverofblood", "riverofblood2", "riverofblood3", "riverofblood4",
                  "kurastopenwide", "kurastopenwide2", "kurastopenwide3"})  # DS1 stems the pass runs on
TERRACES = frozenset({"act3/travincal/terraces.dt1", "act3/kurast/terraces.dt1"})  # the cliff DT1s it stands pieces for
STONE = frozenset({"act3_kurast_walls", "act3_travincal_exterior_trim_combined", "act3_travincal_trench"})  # model
# folders (pd2_tiles.folder_of) D2R's stock presets stand on Act 3 cliff edges: the only pieces it uses
FLOAT = 1.0  # HD units of air under a piece's base above which the scorer reads it as floating (pd2_parity.FLOAT)
VOTES = Path(__file__).resolve().parents[2] / "hd-folder-types.tsv"
STOCK_MIN, STOCK_SHARE, STOCK_EDGE = 10, 0.5, 0.1  # pd2_fidelity's, held equal by test_edges_cliffs
LEFT: dict[str, set] = {}  # DS1 stem -> the cells of its last run that no Act 3 stone piece of their orientation fits

_INDEX: dict[tuple, tuple] = {}  # stock ids -> (the presets, held so an id is never reused; its index; the files it read)


def stock_types() -> dict[str, str]:
    """Folder -> the edge type the stock vote gives it (what pd2_fidelity.stock_types gives the scorer), read from the
    committed tools/hd-folder-types.tsv."""
    import pd2_tiles as fid
    pd2_cache.note(VOTES)  # a changed vote changes the pieces this pass adds: the per-DS1 cache must see it
    out = {}
    for line in VOTES.read_text("utf-8").splitlines():
        if line and not line.startswith("#"):
            f, *counts = line.split("\t")
            v = {k: int(n) for k, n in zip(("none",) + fid.EDGES, counts) if int(n)}
            edge = {k: n for k, n in v.items() if k in fid.EDGES}
            if edge and (e := sum(edge.values())) >= STOCK_MIN and e >= STOCK_EDGE * sum(v.values()):
                k = max(sorted(edge), key=edge.get)
                if edge[k] >= STOCK_SHARE * e:
                    out[f] = k
    return out


def orientation(tiles: tuple, fid) -> int | None:
    """The orientation (1-9) a stock cell's piece is indexed by: the lone upper wall tile, or the upper tile of a cliff
    cell (upper over lower)."""
    if len(tiles) == 1 and tiles[0][1] in UPPER:
        return tiles[0][1]
    if fid.cliff_cell(tiles):
        return next(t[1] for t in tiles if t[1] in UPPER)
    return None


def index(stock: list, ctx: walls.Context) -> dict[int, list[walls.Option]]:
    """The Act 3 stone pieces (STONE) of the stock presets by the orientation of the wall tile they stand on."""
    import pd2_tiles as fid
    key = tuple(id(p) for _, p in stock)
    held = _INDEX.get(key)
    if held and all(a is p for a, (_, p) in zip(held[0], stock)):
        pd2_cache.adopt_reads(held[2])  # the memo hides the prefab reads it made: every asker's DS1 read them
        return held[1]
    with pd2_cache.sub_trace() as sub:
        out = _build(stock, ctx, fid)
    _INDEX[key] = (tuple(p for _, p in stock), out, dict(sub.reads) if sub is not None else {})
    return out


def _build(stock: list, ctx: walls.Context, fid) -> dict[int, list[walls.Option]]:
    out: dict[int, list[walls.Option]] = {}
    for lay, preset in stock:
        ground = preset.get(GROUND)
        for e in preset["entities"]:
            t = _transform(e)
            path = walls.model(e)
            if t is None or not path or fid.folder_of(path) not in STONE or is_waypoint(e) or ctx.far(e):
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
            x0, x1, z0, z1, top, bottom = found[0]
            if x1 - x0 > fid.ROOM or z1 - z0 > fid.ROOM:
                continue
            g = (ground.at(x, z, y) if ground else None) or 0.0
            if fid.hd_class(top - g) != "tall" or bottom - g >= FLOAT:  # a cliff is tall, and stands on its ground
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


def in_scope(tiles: tuple, fid) -> bool:
    """Whether a legacy cell's cliff layer is one of the TERRACES DT1s."""
    return any(t[0] and t[0].lower().replace("\\", "/") in TERRACES for t in tiles if fid.tile_type(t) == "cliff"
               or (fid.cliff_cell(tiles) and t[1] in UPPER))


def run(b) -> None:
    ctx = walls.CONTEXT.get(b.seed)
    if ctx is None or Path(b.seed).stem.lower() not in MAPS:
        return
    import pd2_tiles as fid
    legacy = fid.legacy_edges(ctx.legacy, ctx.heights)["cliff"]
    if not legacy:
        return
    types = stock_types()
    hd = fid.hd_pieces(b.entities + b.loose, ctx.box, ctx.prefab, stock=types)
    score = fid.score_type("cliff", legacy, hd)
    bad = {c for c in score.missing | score.wrong_type if in_scope(ctx.legacy.keys[c][1], fid)}
    LEFT[Path(b.seed).stem.lower()] = set()
    if not bad:
        return
    own = index(b.stock, ctx)
    every = index(ctx.stock, ctx)
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
        choice = o and (walls.pick(own.get(o, []), "tall", c, bad, covered, blocked, b.seed, classed)
                        or walls.pick(every.get(o, []), "tall", c, bad, covered, blocked, b.seed, classed))
        if not choice:
            LEFT[Path(b.seed).stem.lower()].add(c)
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
