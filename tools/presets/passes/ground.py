"""The ground: the flat plane once per TERRAIN_SIZE block not mostly under pasted rooms, unscaled (a scaled plane does not
draw); the first copy is the preset's terrain, the rest are entities. A block mostly under rooms gets it too when it has
tiles outside them, which the rooms' terrain does not reach (#705), unless a room in it has ground below the plane (a
canal, a sunken court), which the plane would hide."""
import copy

from ..consts import TERRAIN_SIZE, TILE
from ..entity import _transform
from ..rooms import sunken

ORDER = 800


def run(b) -> None:
    lay, taken = b.lay, b.taken
    span = TERRAIN_SIZE // TILE
    sunk = sunken(b.pasted, b.stock)
    for bx in range(0, lay.width, span):
        for by in range(0, lay.height, span):
            block = [(x, y) for x in range(bx, min(bx + span, lay.width)) for y in range(by, min(by + span, lay.height))]
            if (bx or by) and (sum(c in taken for c in block) * 2 < len(block) or any(
                    c in lay.keys and c not in taken for c in block) and not any(c in sunk for c in block)):
                plane = copy.deepcopy(b.terrain)
                _transform(plane)["position"].update({"x": float(bx * TILE), "z": float(by * TILE)})
                b.entities.append(plane)
