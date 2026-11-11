"""far_off: Kit.far from D2R's model boxes."""
from __future__ import annotations

import functools
import json
from pathlib import Path
from typing import Callable

import pd2_painter as painter

from ..consts import FAR
from ..entity import _transform


def far_off(d2r_data: Path) -> Callable[[dict], bool]:
    """Kit.far from D2R's model boxes (data/hd/model_lod_desc.bin): whether a model at least pd2_coverage.TALL high,
    not a tree, a prefab nor room-sized, stands FAR or more clear of its position along x or z (Harrogath's stairs).
    A mesh that reaches its position stays, however far its centre: Duriel's chamber is one piece.
    Without the file, no piece is."""
    import pd2_coverage as coverage  # it imports this module
    models = d2r_data / coverage.MODELS
    if not models.is_file():
        return lambda e: False
    boxes = coverage.model_boxes(models.read_bytes())

    @functools.cache
    def prefab(path: str) -> list[dict]:
        f = d2r_data / path.removeprefix("data/")
        return json.loads(f.read_text("utf-8-sig")).get("entities", []) if f.is_file() else []

    seen: dict[int, tuple[dict, bool]] = {}  # once per stock entity (Kit.add asks again per DS1)

    def far(e: dict) -> bool:
        held = seen.get(id(e))
        if held is None or held[0] is not e:
            held = seen[id(e)] = (e, _far(e))
        return held[1]

    def _far(e: dict) -> bool:
        # a prefab (Act 1's riverbanks) stays: on a floor key it still closes border gaps (pd2_coverage)
        if painter.is_tree(e) or any(c["type"] == "PrefabPlacementDefinitionComponent" for c in e["components"]):
            return False
        p = _transform(e)["position"]
        return any(top >= coverage.TALL and max(x1 - x0, z1 - z0) <= coverage.ROOM
                   and max(x0 - p["x"], p["x"] - x1, z0 - p["z"], p["z"] - z1) >= FAR
                   for x0, x1, z0, z1, top, _ in coverage.boxes([e], lambda m: boxes.get(coverage.fnv(m)), prefab))
    return far


ORDER = 200


def drop(kit, lay, p) -> bool:
    """A `far` piece anchored on a floor tile: its mesh belongs to a structure elsewhere in the room, and on a common
    floor key it would stand on any plain ground (Harrogath's stairs on Westmarch's floor, #679)."""
    return p.c in p.floor and kit.far(p.e)
