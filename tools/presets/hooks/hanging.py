"""Hanging pieces (#871): a prop raised above its ground, with nothing of its tile's instance under it, stays out of the kit.

A stock wall tile carries the props mounted on its wall: a torch, a banner, a beam end. Those stand on the wall piece of
the same tile instance. A beam or a shield anchored on a tile whose wall piece sits on a neighbouring tile has nothing
under it once the tile stands in another map, and floats (Descent's burnt beams over hell cliffs). A floor tile's
raised pieces go in tall.py. Only beams (BEAMS) are judged: chains, arches, trims, supports and statues that a stock
map hangs on purpose stay, as do foliage and a piece with no model (a light, a fog volume has no height to float at).
"""
from __future__ import annotations

from ..consts import FLOOR_MAX
from ..entity import _transform
from ..layout import _floor

ORDER = 310
BEAMS = ("_beam",)  # Tristram's burnt_beam and wall_beam: the props that floated over Descent's hell cliffs
SLACK = 1.0  # HD units: a piece this far into the top of another still rests on it


def _model(e: dict) -> str:
    return next((c.get("filename", "").lower() for c in e["components"] if c["type"] == "ModelDefinitionComponent"), "")


def settle(kit, lay, c, ents):
    """The tile's pieces without a model-bearing prop over FLOOR_MAX that no other piece of the instance reaches."""
    if _floor(lay.keys[c]):  # tall.py drops a floor tile's raised pieces
        return ents
    spans = [(_transform(e)["position"]["y"], kit.top(e)) for e, _, _ in ents]
    tops = [(y, y + (h or 0.0)) for y, h in spans]

    def floats(i: int) -> bool:
        e = ents[i][0]
        y = tops[i][0]
        m = _model(e)
        return (y > FLOOR_MAX and any(b in m for b in BEAMS)
                and not any(j != i and base < y - SLACK and top >= y - SLACK for j, (base, top) in enumerate(tops)))
    return [x for i, x in enumerate(ents) if not floats(i)]
