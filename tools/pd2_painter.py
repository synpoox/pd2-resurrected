"""The border painter of pd2_presets (LP3, ADR 0005): trees on the ground a player can't reach, placed the way a tile
set's stock presets place them.

Subtile grid: 5x5 per tile, blocked = DT1 subtile flag bit 0 (Layout.flags). harvest() learns, per stock tree, its
distance (subtiles) to the nearest blocked subtile and the direction to it (8 buckets), and how far it stood from its
room's playable ground; paint() puts trees on the subtiles past the playable area, nearest first, the stock spacing apart,
each copied from a stock tree with the same direction and distance."""
from __future__ import annotations

import collections
import copy
import json
import math
import re
import zlib

TREE = re.compile(r"tree_wall|treewall|treeline|small_cluster|_trees_")  # treelines, tree walls, foliage tree models
SPACING = 9  # subtiles between tree origins when the stock has fewer than two trees
DEPTH = 30  # paint unreachable ground up to this many subtiles from the playable area
CAP = 15  # distances to the blocked ground are capped here
LINE = 10  # wall tiles in an 8-connected group this big are a tree line; smaller groups stand in a clearing
STEPS = ((1, 0), (-1, 0), (0, 1), (0, -1))
AROUND = [(i, j) for i in (-1, 0, 1) for j in (-1, 0, 1)]


def transform(e: dict) -> dict | None:
    return next((c for c in e["components"] if c["type"] == "TransformDefinitionComponent"), None)


def prefab(e: dict) -> str | None:
    return next((c["prefab"] for c in e["components"] if c["type"] == "PrefabPlacementDefinitionComponent"), None)


def is_tree(e: dict) -> bool:
    return bool(TREE.search(json.dumps(e["components"])))


def blocked_floor(flags: bytes) -> bool:
    """A floor tile mostly blocked: water."""
    return sum(b & 1 for b in flags) >= 13


def subtiles(tiles) -> set:
    return {(x * 5 + i, y * 5 + j) for x, y in tiles for i in range(5) for j in range(5)}


def spread(start, w: int, h: int, depth: int, free=None) -> dict:
    """BFS distance (subtiles) from `start` over the w x h subtile grid (or only `free`), up to depth."""
    reach, q = {p: 0 for p in start}, collections.deque(start)
    while q:
        p = q.popleft()
        if reach[p] >= depth:
            continue
        for dx, dy in STEPS:
            n = (p[0] + dx, p[1] + dy)
            if n not in reach and (n in free if free is not None else 0 <= n[0] < w and 0 <= n[1] < h):
                reach[n] = reach[p] + 1
                q.append(n)
    return reach


def nearest_blocked(lay) -> tuple[dict, dict]:
    """For every subtile up to CAP from the blocked ones: the distance and the blocked subtile reached."""
    blocked = [(x * 5 + k % 5, y * 5 + k // 5) for (x, y), f in lay.flags.items() for k in range(25) if f[k] & 1]
    dist, src, q = {b: 0 for b in blocked}, {b: b for b in blocked}, collections.deque(blocked)
    while q:
        p = q.popleft()
        if dist[p] >= CAP:
            continue
        for dx, dy in STEPS:
            n = (p[0] + dx, p[1] + dy)
            if 0 <= n[0] < lay.width * 5 and 0 <= n[1] < lay.height * 5 and n not in dist:
                dist[n], src[n] = dist[p] + 1, src[p]
                q.append(n)
    return dist, src


def bucket(p, s) -> int:
    return -1 if s is None or s == p else round(math.atan2(s[1] - p[1], s[0] - p[0]) / (math.pi / 4)) % 8


def dilate(tiles: set) -> set:
    return {(x + i, y + j) for x, y in tiles for i, j in AROUND}


def components(tiles: set, steps=STEPS):
    """The connected groups of `tiles`."""
    seen = set()
    for s0 in tiles:
        if s0 in seen:
            continue
        comp, q = {s0}, collections.deque([s0])
        seen.add(s0)
        while q:
            p = q.popleft()
            for dx, dy in steps:
                m = (p[0] + dx, p[1] + dy)
                if m in tiles and m not in seen:
                    seen.add(m)
                    comp.add(m)
                    q.append(m)
        yield comp


def lines(walls: set) -> set:
    """The wall tiles that belong to tree lines (8-connected groups of LINE or more)."""
    return set().union(*(c for c in components(walls, AROUND) if len(c) >= LINE))


def harvest(stock) -> tuple[dict, float] | None:
    """The tree kit from stock [(Layout, preset)]: {(bucket, distance): [(entity, dx, dz, reach, dependencies)]} and
    the stock spacing (median nearest-neighbour, subtiles); None when the stock has no tree."""
    out, gaps = collections.defaultdict(list), []
    for lay, preset in stock:
        dist, src = nearest_blocked(lay)
        reach = spread(subtiles(playable(lay)), lay.width * 5, lay.height * 5, 10 ** 6)
        pts = []
        for e in preset["entities"]:
            t = transform(e)
            if t is None or not is_tree(e):
                continue
            p = (int(t["position"]["x"] / 2), int(t["position"]["z"] / 2))  # 10 units per tile, 5 subtiles
            if not (0 <= p[0] < lay.width * 5 and 0 <= p[1] < lay.height * 5):
                continue
            pts.append(p)
            out[(bucket(p, src.get(p)), dist.get(p, CAP))].append(
                (e, t["position"]["x"] - p[0] * 2, t["position"]["z"] - p[1] * 2, reach.get(p, 10 ** 6),
                 preset["dependencies"]))
        gaps += [min(math.dist(a, b) for b in pts if b != a) for a in pts if len(set(pts)) > 1]
    if not out:
        return None
    gaps.sort()
    return dict(out), gaps[len(gaps) // 2] if gaps else SPACING


def playable(lay) -> set:
    """The tiles a player reaches: those no DS1 cell marks unwalkable (Layout.unwalkable, which seals PD2's dashed
    tree lines), and the streams (blocked floor). Line tiles are never playable."""
    water = {c for c, k in lay.keys.items() if k[0] == "f" and blocked_floor(lay.flags[c])}
    return ((set(lay.keys) - lay.unwalkable) | water) - lines({c for c, k in lay.keys.items() if k[0] == "w"})


def paint(lay, trees: tuple[dict, float], seed: str, skip: set, prefabs) -> tuple[list[dict], set]:
    """Tree entities (not yet given ids) past the playable area, and the playable tiles. `skip`: tiles left alone
    (pasted rooms); `prefabs(path)`: the (x, z) of a prefab's pieces, for its footprint."""
    kit, spacing = trees
    play = playable(lay)
    best = subtiles(play)
    reach = spread(best, lay.width * 5, lay.height * 5, DEPTH)
    keep_off = best | subtiles(skip)
    dist, src = nearest_blocked(lay)
    cand = sorted((p for p, r in reach.items() if r and (p[0] // 5, p[1] // 5) in lay.keys
                   and (p[0] // 5, p[1] // 5) not in skip), key=lambda p: (reach[p], zlib.crc32(f"{seed}:{p}".encode())))
    feet = {}

    def footprint(o) -> frozenset:
        """Subtile offsets from the origin subtile a kit tree's pieces cover (a prefab spreads over tiles), counting
        its offset within that subtile (o[1], o[2])."""
        if id(o) not in feet:
            feet[id(o)] = frozenset((math.floor((o[1] + x) / 2), math.floor((o[2] + z) / 2))
                                    for x, z in pieces(o[0], prefabs))
        return feet[id(o)]

    def clear(p, o) -> bool:
        return not any((p[0] + dx, p[1] + dz) in keep_off for dx, dz in footprint(o))

    def run(sub: dict, singles: dict, grid) -> list:
        ents, cell = [], max(1, int(spacing))
        for p in cand:
            g = (p[0] // cell, p[1] // cell)
            if any(math.dist(p, o) < spacing for gx, gy in AROUND for o in grid[(g[0] + gx, g[1] + gy)]):
                continue
            b, d = bucket(p, src.get(p)), dist.get(p, CAP)
            ok = ([o for o in sub.get((b, dd), ()) if o[3] <= reach[p]]  # no closer to the path than the stock tree
                  for dd in sorted(range(CAP + 1), key=lambda dd: abs(dd - d)))
            options = next((oo for oo in ok if oo), None)
            if not options:
                continue
            k0 = zlib.crc32(f"{seed}:{p}:t".encode())
            pick = next((o for i in range(len(options)) if clear(p, o := options[(k0 + i) % len(options)])), None)
            if pick is None:  # a band too thin for a tree cluster: a single tree model from the same direction
                small = [o for o in singles.get(b, ()) if o[3] <= reach[p] and clear(p, o)]
                if not small:
                    continue
                pick = small[k0 % len(small)]
            e = copy.deepcopy(pick[0])
            t = transform(e)
            t["position"]["x"], t["position"]["z"] = p[0] * 2 + pick[1], p[1] * 2 + pick[2]
            for c in e["components"]:  # a wall's transparency names its tile
                if c["type"] == "WallTransparencyComponent" and c["wallTileLocalCoord"]["x"] >= 0:
                    c["wallTileLocalCoord"] = {"x": p[0] // 5, "y": p[1] // 5}
            ents.append((e, pick[4]))
            grid[g].append(p)
        return ents

    # tree clusters first, then single tree models in the gaps (same spacing), then canopy on its own spacing
    tops = {id(o) for v in kit.values() for o in v if "canopy" in json.dumps(o[0]["components"])}
    canopy = {k: [o for o in v if id(o) in tops] for k, v in kit.items()}
    single = {k: [o for o in v if not prefab(o[0]) and id(o) not in tops] for k, v in kit.items()}
    singles = collections.defaultdict(list)
    for (b, _), v in single.items():
        singles[b] += v
    walls = collections.defaultdict(list)
    out = run({k: [o for o in v if prefab(o[0])] for k, v in kit.items()}, {}, walls)
    out += run(single, singles, walls)
    out += run(canopy, {}, collections.defaultdict(list))
    return out, play


def pieces(e: dict, prefabs) -> list[tuple[float, float]]:
    """The (x, z) offsets from the entity's position of its pieces (a prefab's, scaled and turned; else its own)."""
    t, pf = transform(e), prefab(e)
    sc, q = t.get("scale") or {"x": 1, "z": 1}, t.get("orientation") or {"x": 0, "y": 0, "z": 0, "w": 1}
    return [(v[0], v[2]) for x, z in (prefabs(pf) if pf else [(0.0, 0.0)])
            for v in [rotate(q, (x * sc["x"], 0.0, z * sc["z"]))]]


def rotate(q: dict, v: tuple) -> tuple:
    """v rotated by the quaternion q (x, y, z, w)."""
    qx, qy, qz, qw = q["x"], q["y"], q["z"], q["w"]
    tx, ty, tz = 2 * (qy * v[2] - qz * v[1]), 2 * (qz * v[0] - qx * v[2]), 2 * (qx * v[1] - qy * v[0])
    return (v[0] + qw * tx + (qy * tz - qz * ty), v[1] + qw * ty + (qz * tx - qx * tz),
            v[2] + qw * tz + (qx * ty - qy * tx))
