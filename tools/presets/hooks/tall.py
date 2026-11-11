"""A floor tile's stock pieces standing higher than FLOOR_MAX above their ground are a building's upper parts (Kurast's
pillars and roof lips stand 9-26 up), dropped as the tile may have no building in a PD2 DS1 (#682). A stock preset
whose terrain mesh does not reach a house furnishing (Westmarch's raised house floors) has no ground there: the piece
stands over the flat plane, so its ground is 0 (#871: the shields, apples and sacks of a Westmarch loft floated 14-16
units over the ground in Shadows of Westmarch). Only furnishings and roof litter (FURNISHINGS) read a missing ground as 0: a mesa
ledge's skull piles, an arcane trim or a diab fence over a missing ground are the stock map's own raised scenery, and a
piece with no model has no height to float at.

A furnishing over a missing ground stays when a piece that stays holds it up (settle, #909): the boards and snow on a
fortress roof, the blood on a pentagram, the decals on a Westmarch wall top. A loft's own props rest on other dropped
props, so the whole stack goes."""
from ..consts import FLOOR_MAX
from ..entity import _transform
from ..layout import _floor

ORDER = 300
SLACK = 1.0  # HD units: a piece this far into the top of another still rests on it
SLOPE = 4.0  # a decal on a sloped roof stands this far under the roof piece's box top
# model path parts of a house's loose props, its wall shields and banners, and the snow, pebbles and decals on its loft
# floor (Westmarch, Tristram)
FURNISHINGS = ("/town/", "act2_town", "expansion_town", "/tristram/", "snowprops", "pebbles", "jungle_debris/apple",
               "expansion_siege_weapons", "expansion_siege_banners", "grunge/blood/", "grunge/dirt/", "grunge/snow/",
               "grunge_straw", "wood_board")


def _furnishing(e: dict) -> bool:
    return any(c["type"] == "ModelDefinitionComponent" and any(f in c.get("filename", "").lower() for f in FURNISHINGS)
               for c in e["components"])


def drop(kit, lay, p) -> bool:
    """A building's upper part over this floor: its walls are other tiles', so it would float."""
    g = p.ground_h
    if g is None:
        return False  # a furnishing over a missing ground is judged by what holds it up: settle()
    return p.y - g > FLOOR_MAX and _floor(lay.keys[p.c])


def settle(kit, lay, c, ents):
    """The tile's pieces without a furnishing raised over FLOOR_MAX that no piece that stays holds up. A floor tile
    only: a wall tile's props are hanging.py's."""
    if not _floor(lay.keys[c]):
        return ents
    box = [(_transform(e)["position"]["y"], kit.top(e)) for e, _, _ in ents]
    box = [(y, y + (h or 0.0)) for y, h in box]
    risen = {i for i, (e, _, _) in enumerate(ents) if box[i][0] > FLOOR_MAX and _furnishing(e)}
    # a furnishing never holds another up unless it is held itself: Westmarch's snow heaps stand 8-10 over a missing
    # ground and lean on one another
    stay = {i for i, (e, _, _) in enumerate(ents) if not _furnishing(e)}
    grown = True
    while grown:  # a stack stays when its bottom piece is held by a kept piece
        grown = False
        for i in sorted(risen - stay):
            y = box[i][0]
            if any(j != i and box[j][0] < y - SLACK and y - SLACK <= box[j][1] <= y + SLOPE for j in stay):
                stay.add(i)
                grown = True
    return [x for i, x in enumerate(ents) if i in stay or i not in risen]
