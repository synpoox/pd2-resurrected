"""The ground height of every walkable tile (#742), for the passes after it (liquids, cliffs, walls, ground).

Reads: b.lay, b.taken (the pasted rooms, which draw their own ground). Sets, in `b.mine("presets.passes.height")`:
- "tiles": {(x, y): height in HD units} of the walkable DS1 tiles outside the pasted rooms (0 = the base plane);
- "sunk": {(x, y): stock ground height} of the pasted rooms' tiles lying below the plane (presets.height.sunk).
Other passes read it through `ground_at(b, c)` and `tiles_at(b)`; presets/height.py says how it is derived."""
from ..height import levels, sunk

ORDER = 150  # after paste (b.taken), before border/tiles/ground


def run(b) -> None:
    mine = b.mine(__name__)
    mine["tiles"] = levels(b.lay, b.taken)
    mine["sunk"] = sunk(b.pasted, b.stock)


def tiles_at(b) -> dict:
    """tile -> ground height for the walkable tiles outside the pasted rooms."""
    return b.mine(__name__).get("tiles", {})


def ground_at(b, c: tuple[int, int]) -> float:
    """The ground height at tile c: its walkable height, a pasted room's sunken ground, else the base plane (0)."""
    m = b.mine(__name__)
    return m.get("tiles", {}).get(c, m.get("sunk", {}).get(c, 0.0))
