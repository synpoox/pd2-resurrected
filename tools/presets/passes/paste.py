"""Paste the stock rooms that match the layout whole: their pieces, sculpted terrain, water and bridges."""
import copy

from ..consts import TILE
from ..entity import _transform
from ..rooms import rooms

ORDER = 100


def run(b) -> None:
    b.pasted, b.taken = rooms(b.lay, b.stock)
    for n, ox, oy in b.pasted:
        preset = b.stock[n][1]
        b.depend(preset["dependencies"])
        for e in preset["entities"] + [preset.get("terrain")]:  # the room's own sculpted terrain, water and bridges
            if not e or "components" not in e or _transform(e) is None:  # some stock terrain is empty
                continue
            piece = e is not preset.get("terrain")
            e = copy.deepcopy(e)
            _transform(e)["position"]["x"] += ox * TILE
            _transform(e)["position"]["z"] += oy * TILE
            if piece:
                b.held.add((int(_transform(e)["position"]["x"] // TILE), int(_transform(e)["position"]["z"] // TILE)))
            for c in e["components"]:
                if c["type"] == "WallTransparencyComponent" and c["wallTileLocalCoord"]["x"] >= 0:
                    c["wallTileLocalCoord"] = {"x": c["wallTileLocalCoord"]["x"] + ox,
                                               "y": c["wallTileLocalCoord"]["y"] + oy}
            b.entities.append(e)
