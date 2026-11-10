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
    flags), or a liquid base floor layer under a liquid overlay (lava1's crust), with only liquid floor layers (a stone
    platform's tile over lava is no liquid). A liquid DT1 is
    pd2_fidelity.classify "liquid" or one of `extra`, the DT1s the rule names as water. Pasted rooms are not excluded:
    this is the legacy map's liquid, whatever HD draws there."""
    import pd2_fidelity  # imports the presets package: not at the top

    def liquid(d: str) -> bool:
        return d in extra or pd2_fidelity.classify(d, 0, "floor", True) == "liquid"
    blocked, overlaid, out = set(), {}, {}
    for c, k in lay.keys.items():
        if k[0] != "f":
            continue
        dt1s = {t[0] for t in k[1] if t[0]}
        if not dt1s or not all(liquid(d) for d in dt1s):
            continue  # a stone platform's tile over lava is no liquid
        if painter.blocked_floor(lay.flags.get(c, bytes(25))) or c in lay.unwalkable:
            out[c] = tuple(sorted(dt1s))
            blocked.add(k[1][0])
        elif len(k[1]) > 1 and k[1][0][0]:
            overlaid[c] = k[1][0], tuple(sorted(dt1s))
    # a liquid overlay (lava1's crust, flags all zero) carries no blocked flag of its own: its base floor layer decides,
    # a tile that is blocked liquid elsewhere in the layout
    out.update((c, d) for c, (base, d) in overlaid.items() if base in blocked)
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
