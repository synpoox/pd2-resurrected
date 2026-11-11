"""What one DS1's build can see of the shared State (#861): the slice build_ds1 runs on and ds1cache.ds1_key hashes.

The kit holds every stock piece of every tile key, but a build of one layout only looks up its own keys, the single-layer
LIKE keys of the DT1 it draws (place.like), the stock pillars of those DT1 (pillars.standing) and the waypoint groups of
its own objects. Cutting the kit to that, with the pieces and their order unchanged and the stock sources renumbered by
rank (place sorts them), gives the same preset as the whole kit: a stock of 1,000 presets then no longer takes part in the
key of a DS1 that draws none of it.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from .consts import TILE
from .kit import Kit, indoor
from .layout import Layout, wanted
from .waypoints import waypoints


def drawn_dt1s(lay: Layout) -> set:
    """The DT1s a layout draws from, as place.py reads them (None, a tile no DT1 holds, included)."""
    return {t[0] for k in lay.keys.values() for t in k[1] if t}


def slice_kit(kit: Kit, lay: Layout, objects: list) -> Kit:
    """The part of the kit a build of `lay` can see (module docstring). Shares the kit's pieces, `far`, `top` and test memo."""
    drawn, want = drawn_dt1s(lay), wanted(lay)
    inst = {k: v for k, v in kit.instances.items()
            if k in want or (k[1] and all(t and t[0] in drawn for t in k[1]))}
    groups = {}
    for cls, x, z in objects:  # waypoints.waypoint_options reads these three keys
        key = lay.keys.get((int(x // TILE), int(z // TILE)))
        for k in (("kc", key, cls), ("k", key), ("c", cls)):
            if k in kit.waypoints:
                groups[k] = kit.waypoints[k]
    used = sorted({s for v in inst.values() for s, _ in v} | {s for v in groups.values() for s, _ in v})
    rank = {s: i for i, s in enumerate(used)}
    return Kit(instances={k: [(rank[s], e) for s, e in v] for k, v in inst.items()},
               sources=[kit.sources[s] for s in used], far=kit.far, top=kit.top,
               waypoints={k: [(rank[s], e) for s, e in v] for k, v in groups.items()},
               outdoor={rank[s] for s in used if s in kit.outdoor},
               flags={k: kit.flags[k] for k in inst if k in kit.flags}, memo=kit.memo)


def slice_water(water: dict, lay: Layout) -> dict:
    """The water planes of the DT1 the layout draws: liquids.mask and water.streams only act on those."""
    drawn = drawn_dt1s(lay)
    return {d: v for d, v in water.items() if d in drawn}


@dataclass
class Slice:
    """One DS1's view of the State: its waypoint objects, its kit and its water; `inside` is indoor(kit), made when a DS1
    that draws no trees asks."""
    objects: list
    kit: Kit
    water: dict
    inside: Kit | None = field(default=None)


def sliced(st, ds1: str) -> Slice:
    """The DS1's Slice of the State st, once."""
    held = st.slices.get(ds1)
    if held is None:
        lay = st.layouts[ds1]
        objects = waypoints((st.plan.out_data / "global" / "tiles" / ds1).read_bytes(), st.mod_classes)
        held = st.slices[ds1] = Slice(objects, slice_kit(st.kit, lay, objects), slice_water(st.water, lay))
    return held


def inside(sl: Slice) -> Kit:
    if sl.inside is None:
        sl.inside = indoor(sl.kit)
    return sl.inside
