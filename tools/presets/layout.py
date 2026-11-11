"""The Layout: a DS1 read into per-tile keys."""
from __future__ import annotations

import functools
import struct
from dataclasses import dataclass, field

import pd2_painter as painter
from pd2_ds1 import ds1_header

from .consts import UNWALKABLE, Key, Tile


@dataclass
class Layout:
    width: int  # tiles
    height: int
    keys: dict[tuple[int, int], Key]  # (x, y) -> key; tiles with nothing are left out
    flags: dict[tuple[int, int], bytes] = field(default_factory=dict)  # (x, y) -> its layers' subtile flags, OR'd
    unwalkable: frozenset = frozenset()  # the tiles a wall or floor cell marks unwalkable (UNWALKABLE)
    walk: set | None = field(default=None, compare=False, repr=False)  # walkable(), once: see walk_set()


def layout(data: bytes, dt1s: dict[str, dict], copies: dict[Tile, Tile] | None = None) -> Layout:
    """The DS1's tiles, named in the DT1 files `dt1s` (path -> dt1_tiles); a tile in `copies` (pd2_dt1_copies) is
    named by the D2R tile it copies, its flags kept."""
    copies = copies or {}
    version, w, h, _act, _tag, o, walls, floors = ds1_header(data)
    if version < 4:
        raise ValueError(f"DS1 version {version}: only 4 and up keep the tile layers in the order layout() reads")
    n = w * h
    grids = [struct.unpack_from(f"<{n}I", data, o + k * n * 4) for k in range(2 * walls + floors)]
    order = sorted(dt1s)

    named: dict[tuple, Tile] = {}  # each distinct (orientation, main, sub) is looked up once

    def tile(orientation: int, cell: int) -> Tile:
        t = (orientation, cell >> 20 & 0x3F, cell >> 8 & 0xFF)
        if t in named:
            return named[t]
        # a copied tile is named from its own PD2 DT1 first: an earlier DT1 holding the same index would steal it
        # (Zhar Library's arcane walls became palace cellar tiles, #678 review)
        named[t] = (next((p for p in order if t in dt1s[p] and (p, *t) in copies), None)
                    or next((p for p in order if t in dt1s[p]), None), *t)
        return named[t]
    keys, flags, ored = {}, {}, {}
    for i in range(n):
        wall = tuple(tile(grids[2 * k + 1][i] & 0xFF, grids[2 * k][i]) for k in range(walls) if grids[2 * k][i])
        floor = tuple(tile(0, g[i]) for g in grids[2 * walls:] if g[i])
        if wall or floor:
            keys[(i % w, i // w)] = ("w", tuple(copies.get(t, t) for t in wall)) if wall else \
                ("f", tuple(copies.get(t, t) for t in floor))
            if (both := wall + floor) not in ored:
                f = [dt1s[t[0]][t[1:]] for t in both if t[0]]
                ored[both] = bytes(functools.reduce(lambda u, v: [x | y for x, y in zip(u, v)], f, bytes(25)))
            flags[(i % w, i // w)] = ored[both]
    cells = [grids[2 * k] for k in range(walls)] + list(grids[2 * walls:])
    unwalkable = frozenset((i % w, i // w) for i in range(n) if any(g[i] & UNWALKABLE for g in cells))
    return Layout(w, h, keys, flags, unwalkable)


def wanted(lay: Layout) -> set[Key]:
    """The keys build() may look up for a layout: each key and, for its fallback, each of its tiles alone."""
    return {k for key in lay.keys.values() for k in (key, *((key[0], (t,)) for t in key[1]))}


def _floor(key: Key) -> bool:
    """A floor tile's key, or a patch key (patches()) whose centre tile is a floor."""
    if key[0] in ("sq", "cross"):
        key = key[1][4 if key[0] == "sq" else 0]
    return key is not None and key[0] == "f"


def walkable(lay: Layout) -> set[tuple[int, int]]:
    """The DS1's walkable floor: floor tiles (no wall), not marked unwalkable, not mostly blocked (water)."""
    return {c for c, k in lay.keys.items() if k[0] == "f" and c not in lay.unwalkable
            and not painter.blocked_floor(lay.flags.get(c, bytes(25)))}


def walk_set(lay: Layout) -> set[tuple[int, int]]:
    """walkable(lay), made once and kept on the layout (a stock layout is asked in every DS1). Do not change it."""
    if lay.walk is None:
        lay.walk = walkable(lay)
    return lay.walk


def draws_trees(lay: Layout) -> bool:
    """Whether a DS1 draws a tile from a tree DT1 (`trees.dt1`, `treegrp.dt1`, `treegroups.dt1`): only those are
    painted, as an indoor tile set's stock may hold a courtyard's trees."""
    return any(t[0] and "tree" in t[0].rsplit("/", 1)[-1] for k in lay.keys.values() for t in k[1])
