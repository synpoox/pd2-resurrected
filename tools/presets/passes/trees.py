"""The border painter: drop the trees past the playable area and paint them again from it out."""
import json

import pd2_painter as painter

from ..consts import TILE
from ..entity import _transform

ORDER = 400


def run(b) -> None:
    lay, prefabs, edge, far, shades = b.lay, b.prefabs, b.edge, b.far, b.shades
    if not b.trees:
        return
    painted, play = painter.paint(lay, b.trees, b.seed, b.taken, prefabs, b.wet)
    line = painter.lines({c for c, k in lay.keys.items() if k[0] == "w"})

    def tile(e: dict) -> tuple[int, int]:
        return int(_transform(e)["position"]["x"] // TILE), int(_transform(e)["position"]["z"] // TILE)
    near = painter.dilate(painter.dilate({tile(e) for e, _ in painted}))
    # walkable floor: no stock tree, kit or pasted room, puts a piece on it, as one there hides the player (#396)
    ground = {c for c in play - lay.unwalkable if lay.keys[c][0] != "w"}

    def on_ground(e: dict) -> bool:
        p = _transform(e)["position"]
        return any((int((p["x"] + x) // TILE), int((p["z"] + z) // TILE)) in ground
                   for x, z in painter.pieces(e, prefabs))
    b.entities = [e for e in b.entities if not painter.is_tree(e) or not on_ground(e)]

    def keep(e: dict) -> bool:
        """A lone tree (a wall tile on the path) stays, and a stream's; so does a line's where no painted tree
        stands within two tiles (a line with playable ground on both sides leaves the painter no room). A far edge
        tile's tree stays where it is drawn over no playable ground: the painter plants little on the ring, so a
        dropped edge tree left a gap (#688). On the near side roots and `_foreground` walls stay too (#454)."""
        if not painter.is_tree(e):
            return True
        if on_ground(e):
            return False
        if tile(e) in far:
            p = _transform(e)["position"]
            return not shades((int(p["x"] / 2), int(p["z"] / 2)))  # its origin subtile, as the painter's
        return (tile(e) in play or tile(e) in line - near
                or tile(e) in edge and bool(painter.LOW.search(json.dumps(e["components"]))))
    b.loose = [e for e in b.loose if keep(e)]
    assert not any(on_ground(e) for e, _ in painted), f"{b.name}: a painted tree reaches walkable floor"
    for e, of in painted:
        b.loose.append(e)
        b.depend(of)
