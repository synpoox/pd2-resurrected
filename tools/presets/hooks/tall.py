"""A floor tile's stock pieces standing higher than FLOOR_MAX above their ground are a building's upper parts (Kurast's
pillars and roof lips stand 9-26 up), dropped as the tile may have no building in a PD2 DS1 (#682). A stock preset
whose terrain mesh does not reach a house furnishing (Westmarch's raised house floors) has no ground there: the piece
stands over the flat plane, so its ground is 0 (#871: the shields, apples and sacks of a Westmarch loft floated 14-16
units over the ground in Shadows of Westmarch). Only furnishings (FURNISHINGS) read a missing ground as 0: a mesa
ledge's skull piles, an arcane trim or a diab fence over a missing ground are the stock map's own raised scenery, and a
piece with no model has no height to float at."""
from ..consts import FLOOR_MAX
from ..layout import _floor

ORDER = 300
FURNISHINGS = ("/town/", "act2_town", "jungle_debris/apple")  # model path parts of a house's loose props


def _furnishing(e: dict) -> bool:
    return any(c["type"] == "ModelDefinitionComponent" and any(f in c.get("filename", "").lower() for f in FURNISHINGS)
               for c in e["components"])


def drop(kit, lay, p) -> bool:
    """A building's upper part over this floor: its walls are other tiles', so it would float."""
    g = p.ground_h
    if g is None:
        if not _furnishing(p.e):
            return False
        g = 0.0
    return p.y - g > FLOOR_MAX and _floor(lay.keys[p.c])
