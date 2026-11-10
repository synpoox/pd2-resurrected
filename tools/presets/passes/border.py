"""The map's border: the ring just past the playable area, which the automap draws, and the tiles of pasted rooms the
per-tile kit fills."""
import pd2_painter as painter

from ..consts import wet_dt1s

ORDER = 200


def run(b) -> None:
    lay, trees, taken = b.lay, b.trees, b.taken
    b.wet = wet_dt1s(b.water)  # stream DT1: their blocked floor stays water past the tree line
    if trees:  # the ring just past the playable area: the border the automap draws
        b.play = painter.playable(lay, b.wet)
        b.edge = frozenset({(x + dx, y + dy) for x, y in b.play for dx, dy in painter.STEPS} - b.play)
        # its far side: tiles whose tree is drawn over no playable ground (#454 keeps trees off the near side)
        b.shades = painter.shader(painter.subtiles(b.play))
        b.far = frozenset(c for c in b.edge if not b.shades((c[0] * 5 + 2, c[1] * 5 + 2)))  # the tile's centre subtile
    # a room pastes whole at a ROOM_MATCH match: a border tile in it that its stock tile doesn't match (or that its
    # stock leaves empty) and that holds none of the room's pieces takes the kit's pick too (#686: Fortress's wall
    # islands); one with playable ground screen-up of it takes only a fading piece, as the room's near side does
    was = {(x + ox, y + oy): k for n, ox, oy in b.pasted for (x, y), k in b.stock[n][0].keys.items()}
    fill = {c for c in b.edge & taken if was.get(c) != lay.keys.get(c) and c not in b.held}
    b.near = frozenset(c for c in fill if {(c[0] - 1, c[1]), (c[0], c[1] - 1), (c[0] - 1, c[1] - 1)} & b.play)
    b.rest = {c: k for c, k in lay.keys.items() if c not in taken or c in fill}
