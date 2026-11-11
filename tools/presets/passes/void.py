"""Void (#783): where legacy draws nothing, HD draws no ground. Runs after the ground pass (800), before the floor
heights (900) lift every piece onto its plane.

1. A ground plane whose whole 17 x 17 block is empty in legacy, and that no piece stands in, is dropped (the preset's own
   terrain at the origin cannot be).
2. The empty tiles within SIGHT of walkable ground that a plane still lies under get the stock flat black plane
   (presets/void.black) just above it, in rectangles: a pit reads as a pit, not as ground to walk on. Tiles of pasted
   rooms keep the room's own ground."""
import copy

from .. import void
from ..consts import TERRAIN_SIZE, TILE
from ..entity import _transform
from ..layout import walk_set
from ..water import rectangles, tiled

ORDER = 850
SPAN = TERRAIN_SIZE // TILE
LIFT = 0.1  # HD units over the plane (water is 0.05)
CAP = 20  # tiles a cover spans at most per side: scale 2, as stock D2R scales this plane (act4/mesa borders: 2.5)
RIM = 0.2  # HD units a cover spills past its tiles, so two covers leave no seam
SIDE = 100.0  # the model's sides in HD units
CENTRE = (1.4571, 0.1769)  # the model's centre off its origin along x and z (box -48.54..51.46, -49.82..50.18)


def _plane_model(b) -> str:
    return next(c["filename"] for c in b.terrain["components"] if c["type"] == "ModelDefinitionComponent")


def _block(t: dict) -> tuple[int, int]:
    return round(t["position"]["x"] / TERRAIN_SIZE), round(t["position"]["z"] / TERRAIN_SIZE)


def run(b) -> None:
    cells = void.of(b)
    if not cells or not void.STOCK:
        return
    model, lay = _plane_model(b), b.lay
    planes, held = {}, set()
    for i, e in enumerate(b.entities):
        t = _transform(e)
        if t is None:
            continue
        if e.get("name") == "terrain" and any(c.get("filename") == model for c in e["components"]):
            planes[_block(t)] = i
        else:
            held.add((int(t["position"]["x"] // TILE) // SPAN, int(t["position"]["z"] // TILE) // SPAN))
    dropped = set()
    for blk, i in planes.items():
        x0, y0 = blk[0] * SPAN, blk[1] * SPAN
        block = [(x, y) for x in range(x0, x0 + SPAN) for y in range(y0, y0 + SPAN)]
        if blk not in held and not any(c in b.taken for c in block) and all(
                c in cells or not (0 <= c[0] < lay.width and 0 <= c[1] < lay.height) for c in block):
            dropped.add(i)
    gone = {blk for blk, i in planes.items() if i in dropped}
    if dropped:
        b.entities = [e for i, e in enumerate(b.entities) if i not in dropped]
    walk = walk_set(lay)
    near = {(x + dx, y + dy) for x, y in walk for dx in range(-void.SIGHT, void.SIGHT + 1)
            for dy in range(-void.SIGHT, void.SIGHT + 1)}
    cover = {c for c in cells & near if c not in b.taken and (c[0] // SPAN, c[1] // SPAN) not in gone}
    stock, deps = void.STOCK[0]
    for x, y, w, h in (r for rect in rectangles(cover) for r in tiled(rect, CAP, CAP)):
        e = copy.deepcopy(stock)
        t = _transform(e)
        sx, sz = (w * TILE + RIM) / SIDE, (h * TILE + RIM) / SIDE
        t["position"] = {"x": (x + w / 2) * TILE - sx * CENTRE[0], "y": LIFT, "z": (y + h / 2) * TILE - sz * CENTRE[1]}
        t["orientation"] = {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}
        t["scale"] = {"x": sx, "y": 1.0, "z": sz}
        e["name"] = void.BLACK_PIECE
        b.entities.append(e)
        b.depend(deps)
