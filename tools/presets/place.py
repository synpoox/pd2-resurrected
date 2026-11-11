"""Place each tile's pick of its key's kit instances."""
from __future__ import annotations

import json
import re
import zlib

import pd2_painter as painter

from .consts import LIKE, TILE, TREE_LAYER, Key
from .entity import _transform, clone
from .kit import Kit
from . import pillars
from .layout import Layout

STRUCTURE = re.compile(r"/(?:wall|cliff)_[^/\"]*\.(?:model|json)")  # stone walls and cliffs, by their file name



def structure(ents: list) -> bool:
    """Whether a kit instance holds a wall or cliff piece."""
    return any(STRUCTURE.search(json.dumps(e["components"])) for e, _, _ in ents)


POST = re.compile(r"/post\d")  # a stone post: nemyrmonastery's wall bars are rows of posts and fences


def carries(ents: list) -> bool:
    """Whether a kit instance holds a real wall: a wall or cliff piece (structure), or a model pd2_fidelity types as
    a wall, cliff or fence (nemyrmonastery's bars
    are fences). A tall prop (a shrine, a cage) is no wall, whatever its height (#737)."""
    import pd2_fidelity  # late: it reads the presets package's constants
    return structure(ents) or any(pd2_fidelity.model_type(pillars.model(e)) in ("wall", "cliff", "fence")
                                  or POST.search(pillars.model(e)) for e, _, _ in ents)


def fades(e: dict) -> bool:
    """Whether a piece is see-through in front of the player: a wall that fades, or a low root or foreground wall."""
    return (any(c["type"] == "WallTransparencyComponent" for c in e["components"])
            or bool(painter.LOW.search(json.dumps(e["components"]))))


def like(lay: Layout, kit: Kit, seed: str = "") -> dict[tuple[int, int], Key]:
    """For each tile whose key the kit lacks, whole and layer by layer, and that has a LIKE layer: the single-layer
    LIKE key, from a DT1 the layout draws and with a piece in some instance, whose subtile collision is nearest the
    tile's (then the one with the most such instances), if the two footprints share at least half their blocked
    subtiles; a tile that blocks nothing, or shares too little, borrows nothing (a stock chair is no pillar). A key
    that holds a pillar (pillars.holds) goes before any other, whatever its collision: the tile is a pillar (#737). A
    tile that blocks something and has no such key stands one of the kit's stand-alone stock pillars
    (pillars.standing), picked by the seed and its position, if pillars.centre finds a place for it and no earlier lone
    pillar stands on one of its 8 neighbours (#714: Torture Halls' flesh pillars). A 3x3 block of such tiles can still
    stand up to four, and a 1xN run one every other tile."""
    drawn = {t[0] for k in lay.keys.values() for t in k[1] if t}
    pool = [(k, kit.flags.get(k, bytes(25)), sum(bool(o[1]) for o in v), any(kit.tested("holds", o[1], lambda e: pillars.holds(kit, e)) for o in v),
             any(kit.tested("carries", o[1], carries) for o in v))
            for k, v in kit.instances.items()
            if len(k[1]) == 1 and k[1][0] and k[1][0][1] == LIKE and k[1][0][0] in drawn and any(o[1] for o in v)]
    stand = pillars.stand_keys(kit)
    out, lone = {}, set()
    bars = pillars.bars(lay, kit)
    for c, key in sorted(lay.keys.items()):
        whole = key in kit.instances or any((key[0], (t,)) in kit.instances for t in key[1])
        if stand and c in bars:
            out[c] = stand[zlib.crc32(f"{seed}:{c}:bar".encode()) % len(stand)]  # a bare wall bar beside a pillar tile
            continue
        if not (pool or stand) or whole or not any(t and t[1] == LIKE for t in key[1]):
            continue
        f = {i for i, v in enumerate(lay.flags.get(c, bytes(25))) if v & 1}
        fits = [(not pillar, len(f ^ g), -n, k, wall) for k, flags, n, pillar, wall in pool
                if 2 * len(f & (g := {i for i, v in enumerate(flags) if v & 1})) >= len(f | g) and f & g]
        best = min(fits) if fits else None
        if fits and (nearest := min(fits, key=lambda t: t[1:3]))[4]:
            best = nearest  # the nearest footprint carries walls: it keeps them, pillar or not (nemyrmonastery)
        if best and (not best[0] or not stand or best[4] or not pillars.centre(lay.flags.get(c, bytes(25)))):
            out[c] = best[3]  # a pillar key, or the nearest footprint of a tile that cannot stand a pillar (a wall edge)
        elif stand and pillars.centre(lay.flags.get(c, bytes(25))) and not any(
                (c[0] + dx, c[1] + dy) in lone for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
            lone.add(c)  # no lone pillar beside an earlier one (8-adjacent): a 2x2 cluster is one column
            out[c] = stand[zlib.crc32(f"{seed}:{c}:pillar".encode()) % len(stand)]
    return out


def place(lay: Layout, kit: Kit, seed: str, edge: frozenset = frozenset(), near: frozenset = frozenset(),
          far: frozenset = frozenset()) -> tuple[list[dict], set[int], int]:
    """Each tile's pick of its key's instances (no ids yet), the kit sources used, and how many tiles stayed empty
    for a key the kit lacks. An `edge` tile (the map's border, where the automap draws its edge) picks among the
    instances that hold a piece: a stock tile left empty there leans on its neighbour's piece, which another pick
    may not have, and the border would show a gap. A tree tile picks among the instances that hold no wall or cliff:
    a stock piece belongs to the tile its origin falls on, and a stone wall standing on a tree tile there would draw
    a wall where the legacy view shows a tree (#686: Fortress's waypoint court). A `near` tile (drawn over the
    playable ground screen-up of it) picks only an instance whose pieces all fade, as D2R's rooms do on their near
    side, or stays empty. A `far` edge tile (one whose tree is drawn over no playable ground) picks among those that
    hold a tree (painter.is_tree) first, as a lone rock or shrub is too low to close the border (Frozen Forest,
    #688). A tile the kit has no key for takes its like() key's pick."""
    entities, used, missing, lone = [], set(), 0, []
    kit = pillars.with_stand_ins(kit, pillars.standing(kit, {t[0] for k in lay.keys.values() for t in k[1] if t}))
    borrow = like(lay, kit, seed)
    def test(kind: str, ents: list, fn) -> bool:  # the json.dumps-based tests, once per kit instance
        return kit.tested(kind, ents, fn)
    has_tree = lambda ents: any(painter.is_tree(e) for e, _, _ in ents)  # noqa: E731
    all_fade = lambda ents: all(fades(e) for e, _, _ in ents)  # noqa: E731

    def one(x: int, y: int, key: Key, salt: str, at: tuple[float, float] = (0.0, 0.0),
            only: tuple[float, float] | None = None) -> bool:
        options = kit.instances.get(key)
        if not options:
            return False
        if key[1] and all(t and t[1] == TREE_LAYER for t in key[1]):
            options = [o for o in options if not test("s", o[1], structure)] or options
        if salt == ":like":  # a borrowed pillar key: the instances that hold the pillar
            options = [o for o in options if pillars.holds(kit, o[1])] or options
        if (x, y) in far:
            options = [o for o in options if test("t", o[1], has_tree)] or options
        if (x, y) in near:
            options = [o for o in options if test("f", o[1], all_fade)]
            if not options:
                return True
        if (x, y) in edge:
            options = [o for o in options if o[1]] or options
        src, ents = options[zlib.crc32(f"{seed}:{x}:{y}{salt}".encode()) % len(options)]
        used.add(src)
        if only:  # a borrowed pillar key stands one pillar, the instance's nearest the tile's blocked centre (as a
            held = [p for p in ents if pillars.is_pillar(kit, p[0])]  # KIND key), not every column of a stock row
            if len(held) > 1:
                cx, cz = only
                keep = min(held, key=lambda p: (p[1] - cx) ** 2 + (p[2] - cz) ** 2)
                ents = [p for p in ents if p is keep or p not in held]
        for e, dx, dz in ents:
            e = clone(e)
            t = _transform(e)
            t["position"]["x"], t["position"]["z"] = x * TILE + dx + at[0], y * TILE + dz + at[1]
            for c in e["components"]:  # a wall's transparency names its tile
                if c["type"] == "WallTransparencyComponent" and c["wallTileLocalCoord"]["x"] >= 0:
                    c["wallTileLocalCoord"] = {"x": x, "y": y}
            entities.append(e)
        return True
    for (x, y), key in sorted(lay.keys.items()):
        if borrow.get((x, y), (None,))[0] == pillars.KIND and not any(t and t[1] == LIKE for t in key[1]):
            lone.append((x, y, borrow[(x, y)]))  # a wall bar the kit has no piece for (pillars.bars)
            continue
        if one(x, y, key, ""):
            continue
        alone = [(k, (key[0], (t,))) for k, t in enumerate(key[1]) if (key[0], (t,)) in kit.instances]
        for k, single in alone:
            one(x, y, single, f":{k}")
        if not alone and (x, y) in borrow:
            k = borrow[(x, y)]
            if k[0] == pillars.KIND:  # a lone pillar waits until the walls' own pillar pieces are down
                lone.append((x, y, k))
                continue
            c = pillars.centre(lay.flags.get((x, y), bytes(25))) or (TILE / 2, TILE / 2)
            pillar = any(kit.tested("holds", o[1], lambda e: pillars.holds(kit, e))
                         and not kit.tested("carries", o[1], carries) for o in kit.instances[k])  # only a pillar key (no wall) is cut to one column:
            alone = [one(x, y, k, ":like", (0.0, 0.0), c if pillar else None)]  # a wall's own row stays whole
        missing += not alone
    crowd: dict[tuple[int, int], int] = {}  # pillar-model pieces per tile
    for e in entities:
        if pillars.is_pillar(kit, e):
            p = _transform(e)["position"]
            t = (int(p["x"] // TILE), int(p["z"] // TILE))
            crowd[t] = crowd.get(t, 0) + 1
    for x, y, k in lone:  # a lone pillar stands where fewer than STACK pillar pieces are within two tiles of it: more
        around = sum(crowd.get((x + dx, y + dy), 0) for dx in range(-2, 3) for dy in range(-2, 3))  # would pile (#737)
        before = len(entities)
        bar = not any(t and t[1] == LIKE for t in lay.keys[(x, y)][1])  # a wall bar's stand-in pillar piles no pillar
        if around < pillars.STACK and one(x, y, k, ":like", pillars.centre(lay.flags.get((x, y), bytes(25)), bar)):
            crowd[(x, y)] = crowd.get((x, y), 0) + len(entities) - before
        else:
            missing += 1
    return entities, used, missing
