"""Helpers on a stock preset's entities."""
from __future__ import annotations

import copy


_SCALARS = (str, int, float, bool, type(None))


def clone(o):
    """copy.deepcopy for JSON data (dicts, lists and scalars, no shared parts), ~3x faster. Any other type
    (a tuple, a set) goes to copy.deepcopy."""
    t = type(o)
    if t is dict:
        return {k: clone(v) for k, v in o.items()}
    if t is list:
        return [clone(v) for v in o]
    return o if t in _SCALARS else copy.deepcopy(o)


def _transform(entity: dict) -> dict | None:
    return next((c for c in entity["components"] if c["type"] == "TransformDefinitionComponent"), None)


def is_waypoint(entity: dict) -> bool:
    """A waypoint's piece: its model, or its stock room's sculpted terrain (SiegeWaypoint1/terrain.model); not Act 3's
    travincal_waypoint braziers."""
    return any(c["type"] == "ModelDefinitionComponent" and "waypoint" in c["filename"].lower()
               and "/braziers/" not in c["filename"].lower() for c in entity["components"])


def room_terrain(entity: dict) -> bool:
    """A stock room's whole sculpted floor (its terrain.model): Act 2's cel*waypoint rooms reach 14 tiles off the
    waypoint, so only a waypoint on the same tile key gets one (place_waypoints)."""
    return any(c["type"] == "ModelDefinitionComponent" and c["filename"].lower().endswith("/terrain.model")
               for c in entity["components"])
