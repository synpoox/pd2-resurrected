"""Hanging pieces (#871): a prop raised above its ground, with nothing of its tile's instance under it, stays out of the kit.

A stock wall tile carries the props mounted on its wall: a torch, a banner, a beam end. Those stand on the wall piece of
the same tile instance. A beam or a shield anchored on a tile whose wall piece sits on a neighbouring tile has nothing
under it once the tile stands in another map, and floats (Descent's burnt beams over hell cliffs). A floor tile's
raised pieces go in tall.py. Foliage hangs from its tree, which the kit keeps whole, and a piece with no model (a
light, a fog volume) has no height to float at.
"""
from __future__ import annotations

from ..consts import FLOOR_MAX
from ..entity import _transform
from ..layout import _floor

ORDER = 310
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
        return (y > FLOOR_MAX and bool(m) and "/foliage/" not in m
                and not any(j != i and base < y - SLACK and top >= y - SLACK for j, (base, top) in enumerate(tops)))
    return [x for i, x in enumerate(ents) if not floats(i)]
