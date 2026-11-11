"""A floor tile's stock pieces standing higher than FLOOR_MAX above their ground are a building's upper parts (Kurast's
pillars and roof lips stand 9-26 up), dropped as the tile may have no building in a PD2 DS1 (#682). A stock preset
whose terrain mesh does not reach the piece (Westmarch's raised house floors) has no ground there: the piece stands
over the flat plane, so its ground is 0 (#871: the shields, apples and sacks of a Westmarch loft floated 14-16 units
over the ground in Shadows of Westmarch)."""
from ..consts import FLOOR_MAX
from ..layout import _floor

ORDER = 300


def drop(kit, lay, p) -> bool:
    """A building's upper part over this floor: its walls are other tiles', so it would float."""
    g = p.ground_h
    return p.y - (0.0 if g is None else g) > FLOOR_MAX and _floor(lay.keys[p.c])
