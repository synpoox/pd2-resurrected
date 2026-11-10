"""The ground: the flat plane once per TERRAIN_SIZE block not mostly under pasted rooms, unscaled (a scaled plane does not
draw); the first copy is the preset's terrain, the rest are entities. A block mostly under rooms gets it too when it has
tiles outside them, which the rooms' terrain does not reach (#705), unless a room in it has ground below the plane (a
canal, a sunken court), which the plane would hide.

Height (#742): a plane stands at the level most of its block's walkable tiles have (passes/height.py: a terrace or
platform is a raised region). One plane covers the whole block, so a block that holds two levels is drawn at its
majority level (b.mine: "mixed" counts the walkable tiles left off their level). A block with a sunken pasted room in
it goes just under the room's lowest ground, so the plane never hides the room (Siege's sunken room, #712). The
block at 0,0 is the preset's terrain (build() writes it last): its height is set on `b.terrain`."""
import collections
import copy

from ..consts import TERRAIN_SIZE, TILE
from ..entity import _transform
from ..rooms import sunken
from .height import tiles_at

ORDER = 800
SUNK_GAP = 0.05  # HD units: how far under a sunken room's ground its block's plane stands


def run(b) -> None:
    lay, taken = b.lay, b.taken
    span = TERRAIN_SIZE // TILE
    sunk = sunken(b.pasted, b.stock)
    level, below = tiles_at(b), b.mine("presets.passes.height").get("sunk", {})
    mixed = 0
    b.terrain = copy.deepcopy(b.terrain)
    for bx in range(0, lay.width, span):
        for by in range(0, lay.height, span):
            block = [(x, y) for x in range(bx, min(bx + span, lay.width)) for y in range(by, min(by + span, lay.height))]
            heights = collections.Counter(level[c] for c in block if c in level)
            y = min((h for h, n in heights.items() if n == max(heights.values())), default=0.0)
            if (lowest := min((below[c] for c in block if c in below), default=None)) is not None:
                y = min(y, lowest - SUNK_GAP)
            if not (bx or by):
                _transform(b.terrain)["position"]["y"] = y
                mixed += sum(n for h, n in heights.items() if h != y)
            elif sum(c in taken for c in block) * 2 < len(block) or any(
                    c in lay.keys and c not in taken for c in block) and not any(c in sunk for c in block):
                plane = copy.deepcopy(b.terrain)
                _transform(plane)["position"].update({"x": float(bx * TILE), "y": y, "z": float(by * TILE)})
                b.entities.append(plane)
                mixed += sum(n for h, n in heights.items() if h != y)
    b.mine(__name__)["mixed"] = mixed
