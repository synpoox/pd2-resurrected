"""The liquid mask: the legacy DS1's blocked floor tiles of a liquid DT1 (water, lava, swamp), as a shared result of the
streams pass. #738 (water and lava edges) reads it with `of(b)` to trace the walkable-to-liquid boundary; the
streams pass draws a plane over it. The liquid DT1s are the shared classifier's (pd2_fidelity.classify, #732)."""
from __future__ import annotations

import re

import pd2_painter as painter

from .consts import WATER_Y
from .layout import Layout

LAVA = re.compile(r"lava")
SWAMP = re.compile(r"swamp")


def kind(dt1: str) -> str:
    """The liquid a DT1 draws: "lava", "swamp" or "water"."""
    name = dt1.lower()
    return "lava" if LAVA.search(name) else "swamp" if SWAMP.search(name) else "water"


def mask(lay: Layout, extra: frozenset = frozenset()) -> dict[tuple[int, int], tuple[str, ...]]:
    """Every blocked floor tile of the layout with a liquid DT1 (pd2_fidelity.classify "liquid", or one of `extra`, the
    DT1s the rule names as water) -> the liquid DT1s on it, sorted. Pasted rooms are not excluded: this is the legacy
    map's liquid, whatever HD draws there."""
    import pd2_fidelity  # imports the presets package: not at the top
    out = {}
    for c, k in lay.keys.items():
        if k[0] != "f" or not painter.blocked_floor(lay.flags.get(c, bytes(25))):
            continue
        dt1s = sorted({t[0] for t in k[1] if t[0] and (t[0] in extra or pd2_fidelity.classify(t[0], 0, "floor", True) == "liquid")})
        if dt1s:
            out[c] = tuple(dt1s)
    return out


def of(b) -> dict[tuple[int, int], tuple[str, ...]]:
    """The build's liquid mask (mask(), with the rule's water DT1s), made once and shared by the passes."""
    state = b.mine(__name__)
    if "mask" not in state:
        state["mask"] = mask(b.lay, frozenset(b.water))
    return state["mask"]


def liquid_y(tile: tuple[int, int]) -> float:
    """The height of the liquid plane over a tile. One flat level until #742 gives each tile a ground height: then this
    is the only line to change."""
    return WATER_Y
