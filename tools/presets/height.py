"""Ground height per DS1 tile (#742), derived from the legacy structure: the DS1 stores no height.

A lower wall (orientation 16-19: the face of a cliff or terrace, see pd2_fidelity.LOWER_ORIENT) stands between a high
side and a low side. The camera looks up the map, so a face is seen from the low side, and the high ground lies behind
it, toward smaller x and y (HIGH_SIDE). The walkable floor splits into regions (4-connected, walls cut them); each face
asks its high region to stand STEP above its low region. The regions are then solved as a graph and the largest region of
each group stands at 0 (the stock plane height, so a map with no faces is unchanged). Stairs keep the floor of their
region (a ramp inside a region is not modelled); a region the faces never reach stays at 0.

Pasted stock rooms (b.taken) draw their own sculpted ground: their tiles are not in `levels`, only in `sunk` (the tiles
whose stock ground lies more than SINK below the plane, with that ground's height).
"""
from __future__ import annotations

import collections

from .consts import GROUND, SINK, TILE
from .layout import Layout, walkable

STEP = 6.0  # HD units one cliff face lifts the ground: D2R's own steps are 6 (siege strip) to 7.5 (icecave riverice) up or down
MAX_LEVELS = 2  # a region stands at most this many STEPs from the base
MIN_REGION = 20  # a walkable region of fewer tiles is a doorway or a nook, not a terrace: it keeps its neighbour's level
MIN_RUN = 3  # a face between two regions must run this many tiles, or it is a wall piece, not a cliff
LOWER = frozenset(range(16, 20))  # pd2_fidelity.LOWER_ORIENT
FACE_ORIENT = frozenset({1, 2, 3, 4}) | LOWER  # the orientations that can hold a face: upper walls of a cliff, lower walls
SIDES = {  # orientation -> (tiles behind the face: higher, tiles in front: lower); the camera looks up the map
    1: (((-1, 0),), ((1, 0),)), 16: (((-1, 0),), ((1, 0),)),
    2: (((0, -1),), ((0, 1),)), 17: (((0, -1),), ((0, 1),)),
}
BOTH = (((-1, 0), (0, -1), (-1, -1)), ((1, 0), (0, 1), (1, 1)))  # corners (3, 4, 18, 19) and ends
STEPS = ((1, 0), (-1, 0), (0, 1), (0, -1))
REACH = 4  # tiles: how far from a wall or a blocked liquid tile the nearest walkable ground still gives its height


def faces(lay: Layout) -> dict[tuple[int, int], tuple]:
    """The tiles holding a cliff face; tile -> the (high, low) offsets of its orientation. The shared classifier
    (pd2_fidelity.classify, #732) decides: a wall layer of type "cliff" in a face orientation counts; a lower wall (16-19)
    of the plain "wall" type counts only where it is a real ledge (pd2_fidelity.cliff_cell: an upper wall over it in the
    same cell). Fences, water edges, invisible blockers and plain walls on their own are no ledge."""
    import pd2_fidelity as fid  # imported here: pd2_fidelity imports pd2_presets, which imports this package
    out = {}
    for c, (kind, tiles) in lay.keys.items():
        if kind != "w":
            continue
        ledge = fid.cliff_cell(tiles)
        for t in tiles:
            if t[1] not in FACE_ORIENT:
                continue
            k = fid.classify(t[0], t[1], "wall")
            if k == "cliff" or (k == "wall" and t[1] in LOWER and ledge):
                out[c] = SIDES.get(t[1], BOTH)
                break
    return out


def nearest(lay: Layout, height: dict, c: tuple[int, int]) -> float:
    """The ground height at a tile with no walkable ground of its own (a wall, a blocked liquid tile): the commonest
    height among the walkable tiles in the nearest ring (up to REACH tiles away) round it, the lower on a tie, so a cliff
    face stands on the low ground; 0 where none is near."""
    for r in range(1, REACH + 1):
        ring = [height[m] for dx in range(-r, r + 1) for dy in range(-r, r + 1) if max(abs(dx), abs(dy)) == r
                and (m := (c[0] + dx, c[1] + dy)) in height]
        if ring:
            n = collections.Counter(ring)
            return min(h for h, k in n.items() if k == max(n.values()))
    return 0.0


def regions(tiles: set) -> dict[tuple[int, int], int]:
    """tile -> region number, over the 4-connected groups of `tiles`."""
    out, n = {}, 0
    for s in sorted(tiles):
        if s in out:
            continue
        out[s] = n
        q = collections.deque([s])
        while q:
            x, y = q.popleft()
            for dx, dy in STEPS:
                m = (x + dx, y + dy)
                if m in tiles and m not in out:
                    out[m] = n
                    q.append(m)
        n += 1
    return out


def levels(lay: Layout, skip: set = frozenset()) -> dict[tuple[int, int], float]:
    """The ground height of each walkable tile outside `skip`, in HD units: 0 (the base) or a multiple of STEP."""
    walk = walkable(lay) - skip
    reg = regions(walk)
    size = collections.Counter(reg.values())
    runs: dict[tuple[int, int], int] = collections.Counter()  # (high region, low region) -> face tiles between them
    for f, (high, low) in sorted(faces(lay).items()):
        hi = {reg[(f[0] + dx, f[1] + dy)] for dx, dy in high if (f[0] + dx, f[1] + dy) in reg}
        lo = {reg[(f[0] + dx, f[1] + dy)] for dx, dy in low if (f[0] + dx, f[1] + dy) in reg}
        for h in hi:
            for lw in lo - {h}:
                if size[h] >= MIN_REGION and size[lw] >= MIN_REGION:
                    runs[(h, lw)] += 1
    edges: dict[int, dict[int, float]] = collections.defaultdict(dict)  # region -> {neighbour: its height - mine}
    for (h, lw), n in sorted(runs.items(), key=lambda kv: (-kv[1], kv[0])):
        if n >= MIN_RUN and runs.get((lw, h), 0) < n:  # the longer face of a pair decides which side is high
            edges[lw].setdefault(h, STEP)
            edges[h].setdefault(lw, -STEP)
    height: dict[int, float] = {}
    for r in sorted(size, key=lambda r: (-size[r], r)):  # the largest region of each group is its base
        if r in height:
            continue
        height[r] = 0.0
        q = collections.deque([r])
        while q:
            a = q.popleft()
            for b, d in sorted(edges[a].items()):
                if b not in height:
                    height[b] = max(-MAX_LEVELS * STEP, min(MAX_LEVELS * STEP, height[a] + d))
                    q.append(b)
    return {c: height[r] for c, r in reg.items()}


def sunk(pasted, stock) -> dict[tuple[int, int], float]:
    """The tiles of the pasted rooms whose stock ground lies more than SINK below the plane (rooms.sunken), with the
    ground height there."""
    out = {}
    for n, ox, oy in pasted:
        s, preset = stock[n]
        ground = preset.get(GROUND)
        if ground is None:
            continue
        for x, y in s.keys:
            h = ground.at(x * TILE + TILE / 2, y * TILE + TILE / 2)
            if h is not None and h < -SINK:
                out[(x + ox, y + oy)] = h
    return out
