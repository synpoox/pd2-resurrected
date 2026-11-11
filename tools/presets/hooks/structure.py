"""structure_off: Kit.structure from D2R's model boxes, and the hook that keeps a building off a plain floor tile (#871).

A stock town places each house on the floor tile its origin falls on. Harvested per tile, the house stands on every map
that shares that floor key: Tristram's `town.dt1` ground put its building01-05 houses on the floor of the Descent
(descend_2, descend_4), where the legacy view shows hell cliffs and lava. The wall tiles of a PD2 DS1 that wants a
building carry their own pieces; a plain floor tile never asks for one. (pd2_coverage.wrong_tiles scores the same
pieces as walkable floor under a structure.)
"""
from __future__ import annotations

import functools
from pathlib import Path
from typing import Callable

from ..consts import STRUCTURE, STRUCTURE_TOP
from ..entity import _transform

ORDER = 220


def structure_off(d2r_data: Path) -> Callable[[dict], bool]:
    """Kit.structure from D2R's model boxes (data/hd/model_lod_desc.bin): whether an entity's model is at least
    STRUCTURE wide along both axes and STRUCTURE_TOP high at its scale, and no room-sized mesh (pd2_coverage.ROOM). A
    prefab or a tree is none. Without the file, nothing is."""
    import pd2_coverage as coverage  # it imports this module
    models = d2r_data / coverage.MODELS
    if not models.is_file():
        return lambda e: False
    boxes = coverage.model_boxes(models.read_bytes())

    @functools.cache
    def big(path: str, sx: float, sy: float, sz: float) -> bool:
        bb = boxes.get(coverage.fnv(path))
        return bb is not None and STRUCTURE_TOP <= bb[4] * sy and (
            STRUCTURE <= (bb[3] - bb[0]) * sx <= coverage.ROOM and STRUCTURE <= (bb[5] - bb[2]) * sz <= coverage.ROOM)

    def structure(e: dict) -> bool:
        t = _transform(e)
        s = (t or {}).get("scale") or {"x": 1.0, "y": 1.0, "z": 1.0}
        return any(c["type"] == "ModelDefinitionComponent" and big(c.get("filename", ""), s["x"], s["y"], s["z"])
                   for c in e["components"])
    return structure


def drop(kit, lay, p) -> bool:
    """A building-sized piece anchored on a floor tile: the house belongs to the stock town's layout, not to the floor."""
    return p.c in p.floor and kit.structure(p.e)
