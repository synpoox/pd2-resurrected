"""Floor height (#742): the ground height of every tile, the raised and sunken ground planes, and every piece settled on
the ground it stands on. Derivation: presets/height.py.

Runs late (ORDER 900, after the ground planes and every pass that places pieces), so no other pass changes:
1. each ground plane the ground pass drew (and the preset's terrain) is set to the height most of its block's walkable
   tiles have; a block holding any tile of a pasted room never goes above 0, so the plane never covers the room's pieces
   (it is still drawn in a block less than half taken; the ground pass keeps deciding which blocks get a plane);
2. each placed entity with a position (floor pieces, walls, pillars, rocks, trees, waypoint pads, water planes, and also
   lights, emitters and decals) is lifted by the height of the plane under its tile, so it stands on the ground that is
   drawn, not buried in a raised block or floating over a sunk one. Entities on the pasted rooms' tiles (b.taken) keep
   their own heights, the rooms draw their own ground; they are named with ROOM_PIECE in front, which the coverage scorer
   reads to measure them from ground 0.

API for later passes and tickets (#734 walls, #740 liquids, #741 cliffs). Everything is computed on first use from b.lay,
b.taken, b.pasted and b.stock, so call it from any pass ordered after paste (100):
- ground_at(b, c): the height of tile c's walkable ground, in HD units; on a wall or a blocked liquid tile the nearest
  walkable ground's (the lower on a tie, so a cliff face stands on the low ground); a pasted room's sunken ground; else 0;
- tiles_at(b): {tile: height} of the walkable tiles outside the pasted rooms;
- plane_at(b, c): the height the ground plane is drawn at under tile c (0 where none is drawn, or before this pass ran);
  it differs from ground_at in a block that holds two levels: the plane stands at the majority one;
- face_side(b, c): the (high, low) neighbour offsets of the cliff face on tile c (one offset each for a face along x or y,
  three each for a corner or end), or None where tile c holds no face. The high side is toward smaller x and y."""
import collections
import copy
import json

from ..consts import ROOM_PIECE, TERRAIN_SIZE, TILE
from ..entity import _transform
from ..height import faces, levels, nearest, sunk

ORDER = 900  # after ground (800): the planes and every piece exist
SPAN = TERRAIN_SIZE // TILE  # tiles per ground plane


def _state(b) -> dict:
    m = b.mine(__name__)
    if "tiles" not in m:
        m["tiles"] = levels(b.lay, b.taken)
        m["sunk"] = sunk(b.pasted, b.stock)
    return m


def tiles_at(b) -> dict:
    return _state(b)["tiles"]


def ground_at(b, c: tuple[int, int]) -> float:
    m = _state(b)
    if c in m["tiles"]:
        return m["tiles"][c]
    if c in m["sunk"]:
        return m["sunk"][c]
    return 0.0 if c in b.taken or c not in b.lay.keys else nearest(b.lay, m["tiles"], c)


def face_side(b, c: tuple[int, int]) -> tuple | None:
    m = _state(b)
    if "faces" not in m:
        m["faces"] = faces(b.lay)
    return m["faces"].get(c)


def plane_at(b, c: tuple[int, int]) -> float:
    return b.mine(__name__).get("planes", {}).get(_block(c), 0.0)


def _block(c: tuple[int, int]) -> tuple[int, int]:
    return c[0] // SPAN, c[1] // SPAN


def _level(b, block: tuple[int, int]) -> float:
    """The height the plane of a block stands at: the majority level of its walkable tiles (the lower on a tie), at most 0
    where any tile of a pasted room is in the block: the room stands on its own ground at 0, and the ground pass still draws
    a plane in a block less than half taken, which a raised plane would put over the room's floor and walls."""
    m = _state(b)
    cells = [(x, y) for x in range(block[0] * SPAN, (block[0] + 1) * SPAN)
             for y in range(block[1] * SPAN, (block[1] + 1) * SPAN)]
    heights = collections.Counter(m["tiles"][c] for c in cells if c in m["tiles"])
    y = min((h for h, n in heights.items() if n == max(heights.values())), default=0.0)
    return min(y, 0.0) if any(c in b.taken for c in cells) else y


def _shape(components: list) -> str:
    """Components without the position: a ground plane is a copy of the terrain at another position."""
    comps = copy.deepcopy(components)
    for c in comps:
        if c["type"] == "TransformDefinitionComponent":
            c["position"] = None
    return json.dumps(comps, sort_keys=True)


def run(b) -> None:
    plane, planes = _shape(b.terrain["components"]), {}
    b.terrain = copy.deepcopy(b.terrain)  # the caller's dict stays flat; build() writes b.terrain out
    planes[(0, 0)] = _transform(b.terrain)["position"]["y"] = _level(b, (0, 0))
    rest = []
    for e in b.entities:
        t = _transform(e)
        if t is not None and e.get("name") == "terrain" and _shape(e["components"]) == plane:  # a plane the ground drew
            blk = (round(t["position"]["x"] / TERRAIN_SIZE), round(t["position"]["z"] / TERRAIN_SIZE))
            planes[blk] = t["position"]["y"] = _level(b, blk)
        elif t is not None:
            rest.append((e, t))
    b.mine(__name__)["planes"] = planes
    for e, t in rest:
        c = (int(t["position"]["x"] // TILE), int(t["position"]["z"] // TILE))
        if c not in b.taken:
            t["position"]["y"] += planes.get(_block(c), 0.0)
        elif e.get("name") != "terrain":  # the scorer's pd2_tiles.plane_ground reads the mark; a room's terrain keeps its name
            e["name"] = ROOM_PIECE + (e.get("name") or "")
