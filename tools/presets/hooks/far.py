"""far_off: Kit.far from D2R's model boxes."""
from __future__ import annotations

import functools
import json
from pathlib import Path
from typing import Callable

import pd2_cache
import pd2_painter as painter

from ..consts import FAR
from ..entity import _transform


def far_off(d2r_data: Path) -> Callable[[dict], bool]:
    """Kit.far from D2R's model boxes (data/hd/model_lod_desc.bin): whether a model at least pd2_tiles.TALL high,
    not a tree, a prefab nor room-sized, stands FAR or more clear of its position along x or z (Harrogath's stairs).
    A mesh that reaches its position stays, however far its centre: Duriel's chamber is one piece.
    Without the file, no piece is."""
    import pd2_tiles as tiles  # late: it imports the presets package
    models = d2r_data / tiles.MODELS
    if not models.is_file():
        return lambda e: False
    boxes = tiles.model_boxes(models.read_bytes())

    @functools.cache
    def load(path: str) -> list[dict]:
        f = d2r_data / path.removeprefix("data/")
        return json.loads(f.read_text("utf-8-sig")).get("entities", []) if f.is_file() else []

    def prefab(path: str) -> list[dict]:
        pd2_cache.note(d2r_data / path.removeprefix("data/"))  # the memo hides the read from the DS1 that asks next (#861)
        return load(path)

    seen: dict[int, tuple[dict, bool, list]] = {}  # once per stock entity (Kit.add asks again per DS1)

    def far(e: dict) -> bool:
        held = seen.get(id(e))
        if held is None or held[0] is not e:
            opened: list = []
            held = seen[id(e)] = (e, _far(e, opened), opened)
        for path in held[2]:  # the answer is memoised, the prefabs it read are every asker's
            pd2_cache.note(d2r_data / path.removeprefix("data/"))
        return held[1]

    def _far(e: dict, opened: list) -> bool:
        # a prefab (Act 1's riverbanks) stays: on a floor key it still closes border gaps (pd2_tiles)
        if painter.is_tree(e) or any(c["type"] == "PrefabPlacementDefinitionComponent" for c in e["components"]):
            return False
        p = _transform(e)["position"]
        return any(top >= tiles.TALL and max(x1 - x0, z1 - z0) <= tiles.ROOM
                   and max(x0 - p["x"], p["x"] - x1, z0 - p["z"], p["z"] - z1) >= FAR
                   for x0, x1, z0, z1, top, _ in tiles.boxes([e], lambda m: boxes.get(tiles.fnv(m)),
                                                             lambda path: opened.append(path) or prefab(path)))
    return far


ORDER = 200


def drop(kit, lay, p) -> bool:
    """A `far` piece anchored on a floor tile: its mesh belongs to a structure elsewhere in the room, and on a common
    floor key it would stand on any plain ground (Harrogath's stairs on Westmarch's floor, #679)."""
    return p.c in p.floor and kit.far(p.e)
