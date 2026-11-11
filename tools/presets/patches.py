"""The patch lookup."""
from __future__ import annotations

from .consts import CROSS, SQUARE, Key
from .kit import Kit
from .layout import Layout, draws_trees


def patches(lay: Layout, stock: list[tuple[Layout, dict]], tiles: set, far=lambda e: False) -> tuple[Layout, Kit]:
    """The patch lookup: each of `tiles` whose 3x3 neighbourhood (else its 4-neighbour cross) appears in a stock
    preset, keyed so that the kit copies what that preset placed on the centre tile, empty included; `far` as
    Kit.far."""
    def patch(keys: dict, c: tuple, name: str, cells: list) -> Key:
        return name, tuple(keys.get((c[0] + i, c[1] + j)) for i, j in cells)
    shapes = (("sq", SQUARE), ("cross", CROSS))
    want = {c: [patch(lay.keys, c, *k) for k in shapes] for c in tiles}
    keep = {k for ks in want.values() for k in ks}
    kit = Kit(far=far)
    for s, preset in stock:
        floor = {c for c, k in s.keys.items() if k[0] == "f"}
        get = s.keys.get
        for name, cells in shapes:  # patch() of every tile, inlined: the hottest line of the merge
            around = {c: (name, tuple([get((c[0] + i, c[1] + j)) for i, j in cells])) for c in s.keys}
            kit.add(Layout(s.width, s.height, around), preset, keep, floor=floor,
                    outdoor=draws_trees(s))
    return Layout(lay.width, lay.height, {c: next(k for k in ks if k in kit.instances)
                                          for c, ks in want.items() if any(k in kit.instances for k in ks)}), kit
