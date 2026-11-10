"""A floor tile's stock pieces standing higher than FLOOR_MAX above their ground are a building's upper parts (Kurast's
pillars and roof lips stand 9-26 up), dropped as the tile may have no building in a PD2 DS1 (#682)."""
from ..consts import FLOOR_MAX
from ..layout import _floor

ORDER = 300


def drop(kit, lay, p) -> bool:
    """A building's upper part over this floor: its walls are other tiles', so it would float."""
    g = p.ground_h
    return g is not None and p.y - g > FLOOR_MAX and _floor(lay.keys[p.c])
