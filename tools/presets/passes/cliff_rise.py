"""Hung cliff pieces stand on the ground (#831, part of #735): a stock cliff piece that hangs below its rim is lifted by
its hang, so its base meets the ground instead of sinking into it.

D2R's cliff rocks (Siege's `cliff_rock_*` and `cliff_ice_*`, Act 4's `act4_mesa_cliff_*`) are modelled from the rim down:
the box tops out about +1 and reaches 25 (mesa) to 78 (Siege) units below it, where the stock terrain is sculpted far
lower beside the rim. Kit.add keeps each piece's height above its stock ground, and the generated terrain is flat, so the
rock sits buried and only a lip shows: the scorer (pd2_fidelity, which measures a top from the plane under the piece) sees
flat or low where legacy has a tall cliff. Siege's walls on the rim are 67 tall, so a rock standing on the ground reads as
the stock cliff.

Runs after floor_height (ORDER 900), so the lift adds to the plane under the piece. Only the maps that opted in (MAPS)
change: the same pieces on other maps are not touched. Needs the model boxes the wall pass gets from the merge
(edges_walls.CONTEXT); a build without them lifts nothing."""
from __future__ import annotations

from ..consts import ROOM_PIECE, TILE
from ..entity import _transform

ORDER = 950
MAPS = ("frozenforest", "siege", "realmoftorment", "kyovoshad", "pandemonium")  # prefixes of the DS1 stems (#831)
HANG = 4.0  # HD units a piece's box must reach below its origin to count as hung (pd2_tiles.TALL)
TOP = 4.0  # ...while its top stays under this: a piece already standing tall is not hung


def run(b) -> None:
    if not b.name.lower().startswith(MAPS):
        return
    from . import edges_walls
    ctx = edges_walls.CONTEXT.get(b.seed)
    if ctx is None:
        return
    import pd2_tiles
    for e in b.entities:
        t = _transform(e)
        if t is None or e.get("name") == "terrain" or (e.get("name") or "").startswith(ROOM_PIECE):
            continue
        if (int(t["position"]["x"] // TILE), int(t["position"]["z"] // TILE)) in b.taken:
            continue
        model = next((x.get("filename", "") for x in e["components"] if x["type"] == "ModelDefinitionComponent"), "")
        if not model or pd2_tiles.model_type(model) != "cliff" or not (box := ctx.box(model)):
            continue
        scale = (t.get("scale") or {}).get("y", 1.0)
        if box[4] * scale < TOP and -box[1] * scale >= HANG:
            t["position"]["y"] -= box[1] * scale
