"""Waypoint objects and the stock waypoints placed on them."""
from __future__ import annotations

import struct
import zlib

from pd2_levels import object_list

from .consts import TILE, Key
from .entity import _transform, clone
from .kit import Kit
from .layout import Layout


def waypoints(data: bytes, classes: dict[tuple[int, int], str]) -> list[tuple[str, float, float]]:
    """A DS1's waypoint objects, (objects Class, x, z) in HD units; `classes` is objpreset's (Act, Index) -> ObjectClass."""
    act, at, count, size = object_list(data)
    out = []
    for k in range(count):
        kind, oid, x, y = struct.unpack_from("<4i", data, at + 4 + k * size)
        cls = classes.get((act + 1, oid), "") if kind == 2 else ""
        at_ = (cls.lower(), x * TILE / 5, y * TILE / 5)  # 5 subtiles per tile
        if "waypoint" in cls.lower() and at_[1:] not in [o[1:] for o in out]:  # frozenriver3 lists one twice
            out.append(at_)
    return out


def waypoint_options(lay: Layout, kit: Kit, cls: str, x: float, z: float) -> list:
    """The stock waypoint groups for a waypoint object: from a stock object of its Class on the same tile key, else
    of any Class on that key, else of its Class (models only)."""
    key = lay.keys.get((int(x // TILE), int(z // TILE)))
    return kit.waypoints.get(("kc", key, cls)) or kit.waypoints.get(("k", key)) or kit.waypoints.get(("c", cls), [])


def place_waypoints(lay: Layout, kit: Kit, seed: str, objects: list[tuple[str, float, float]], taken: set = frozenset()
                    ) -> tuple[list[dict], set[int]]:
    """One stock waypoint per waypoint object, at its stock offset (waypoint_options), but none on a pasted room's
    tiles (`taken`), whose own pieces stand there; and the kit sources used."""
    entities, used = [], set()
    for cls, x, z in objects:
        options = waypoint_options(lay, kit, cls, x, z)
        if not options or (int(x // TILE), int(z // TILE)) in taken:
            continue
        src, ents = options[zlib.crc32(f"{seed}:waypoint:{x}:{z}".encode()) % len(options)]
        used.add(src)
        for e, dx, dz in ents:
            e = clone(e)
            _transform(e)["position"]["x"], _transform(e)["position"]["z"] = x + dx, z + dz
            entities.append(e)
    return entities, used
