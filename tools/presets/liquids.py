"""The liquid mask: the legacy DS1's blocked floor tiles of a liquid DT1 (water, lava, swamp), as a shared result of the
streams pass. #738 (water and lava edges) reads it with `of(b)` to trace the walkable-to-liquid boundary; the
streams pass draws a plane over it. The liquid DT1s are the shared classifier's (pd2_fidelity.classify, #732)."""
from __future__ import annotations

import pd2_painter as painter

from .consts import WATER_Y
from .layout import Layout

def mask(lay: Layout, extra: frozenset = frozenset()) -> dict[tuple[int, int], tuple[str, ...]]:
    """Every floor tile of the layout that a liquid DT1 draws and nothing walkable covers -> its liquid DT1s, sorted:
    blocked by its DT1 flags (water) or marked unwalkable by its DS1 cell (PD2's lava seas, whose lava floor has walkable
    flags), with only liquid floor layers (a stone platform's tile over lava is no liquid). A liquid DT1 is
    pd2_fidelity.classify "liquid" or one of `extra`, the DT1s the rule names as water. Pasted rooms are not excluded:
    this is the legacy map's liquid, whatever HD draws there."""
    import pd2_fidelity  # imports the presets package: not at the top
    out = {}
    for c, k in lay.keys.items():
        if k[0] != "f" or not (painter.blocked_floor(lay.flags.get(c, bytes(25))) or c in lay.unwalkable):
            continue
        dt1s = {t[0] for t in k[1] if t[0]}
        if dt1s and all(d in extra or pd2_fidelity.classify(d, 0, "floor", True) == "liquid" for d in dt1s):
            out[c] = tuple(sorted(dt1s))
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
