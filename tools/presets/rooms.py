"""Stock rooms pasted whole."""
from __future__ import annotations

from .consts import GROUND, ROOM_BARE, ROOM_MATCH, ROOM_MIN, SINK, TILE, Key
from .layout import Layout, walk_set


def rooms(lay: Layout, stock: list[tuple[Layout, dict]]) -> tuple[list[tuple[int, int, int]], set]:
    """The stock rooms to paste whole, [(stock index, x, y)], best match first and none overlapping, and their tiles."""
    at: dict[Key, list] = {}
    for c, k in lay.keys.items():
        at.setdefault(k, []).append(c)
    found, walk = [], walk_set(lay)
    for n, (s, _) in enumerate(stock):
        items = list(s.keys.items())
        if len(items) < ROOM_MIN:
            continue
        (ax, ay), anchor = min(items, key=lambda it: len(at.get(it[1], ())) or 10 ** 9)
        sw = walk_set(s)
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
                # the DS1's walkable floor must land on the room's: where it lands on the room's lava, wall or
                # nothing, the room draws no ground under a walkable tile (Lava, #702), but for ROOM_BARE of it
                bare = sum((x + ox, y + oy) in walk and (x, y) not in sw for x in range(s.width) for y in range(s.height))
                if bare <= ROOM_BARE * len(items):
                    found.append((-hit, n, ox, oy))
    out, taken = [], set()
    for _, n, ox, oy in sorted(found):
        cells = {(x, y) for x in range(ox, ox + stock[n][0].width) for y in range(oy, oy + stock[n][0].height)}
        if not cells & taken:
            taken |= cells
            out.append((n, ox, oy))
    return out, taken


def sunken(pasted: list[tuple[int, int, int]], stock: list[tuple[Layout, dict]]) -> set[tuple[int, int]]:
    """The tiles of the pasted rooms whose stock ground (GROUND, at the tile's centre) lies more than SINK below the
    flat plane: a plane over them would hide the room's floor or water."""
    out = set()
    for n, ox, oy in pasted:
        s, preset = stock[n]
        ground = preset.get(GROUND)
        if ground is None:
            continue
        for x, y in s.keys:
            h = ground.at(x * TILE + TILE / 2, y * TILE + TILE / 2)
            if h is not None and h < -SINK:
                out.add((x + ox, y + oy))
    return out
