"""vis_box blockers (#717): the flat black occluders of D2R's barracks rooms stay out of the kit.

A stock courtyard room places vis_box models (act1_barracks_blocker01, ...blocker02: a plane 24 units up, 87x131 units
big) over its own tiles. Harvested per tile they stand on any map that shares a floor or wall key with the room, and
draw a large flat black shape over the floor, wall and gate there (the Library, #717).
"""
from __future__ import annotations

ORDER = 210


def blocker(e: dict) -> bool:
    return any(c["type"] == "ModelDefinitionComponent" and "/vis_box/" in c.get("filename", "").lower()
               for c in e["components"])


def drop(kit, lay, p) -> bool:
    return blocker(p.e)
