"""The hooks Kit.add and indoor() run. A hook is a module here with an integer `ORDER` and any of these functions, found
by name, so adding one edits no shared list:

    tile(lay, key, alike) -> bool          the stock tile `key` is harvested though not wanted (alike DT1s' LIKE keys)
    locate(lay, c, x, z) -> c              the tile a piece at (x, z) over tile c belongs to
    drop(kit, lay, p) -> bool              the piece p (a kit.Piece) is left out of the kit
    indoor(kit, src, ents) -> ents         the pieces an indoor map's kit keeps of a stock source's tile instance
"""
from __future__ import annotations

import sys
from typing import Callable

from .. import discover


def each(name: str) -> list[Callable]:
    """The hook functions called `name`, in ORDER."""
    return [getattr(m, name) for m in discover.modules(sys.modules[__name__]) if hasattr(m, name)]
