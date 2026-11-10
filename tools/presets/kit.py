"""The Kit: the stock pieces harvested per tile key."""
from __future__ import annotations

import dataclasses
import functools
from dataclasses import dataclass, field
from typing import Callable

from . import hooks
from .consts import GROUND, TILE, Key
from .entity import _transform, clone, is_waypoint, room_terrain
from .layout import Layout, draws_trees


@dataclass
class Piece:
    """A stock piece Kit.add is about to keep, for the `drop` hooks (hooks/): its tile c, entity e, position (x, y, z),
    the stock preset's `floor` tiles and `ground` (pd2_terrain), and ground_h, the ground's height under it."""
    c: tuple[int, int]
    e: dict
    x: float
    y: float
    z: float
    floor: set
    ground: object

    @functools.cached_property
    def ground_h(self) -> float | None:
        return self.ground.at(self.x, self.z, self.y) if self.ground else None


@dataclass
class Kit:
    instances: dict[Key, list[tuple[int, list[tuple[dict, float, float]]]]] = field(default_factory=dict)
    sources: list[dict[str, list[dict]]] = field(default_factory=list)  # each stock preset's dependencies
    far: Callable[[dict], bool] = lambda e: False  # far_off(): a piece whose mesh stands FAR or more clear of its anchor
    top: Callable[[dict], float | None] = lambda e: None  # pillars.model_top(): a piece's model height, None if unknown
    # a waypoint's pieces, as offsets from its object: under ("kc", key of the tile under the object, objects Class),
    # ("k", key) and, without a room terrain, ("c", Class)
    waypoints: dict[Key | str, list[tuple[int, list[tuple[dict, float, float]]]]] = field(default_factory=dict)
    outdoor: set[int] = field(default_factory=set)  # the sources whose stock DS1 draws trees (draws_trees())
    flags: dict[Key, bytes] = field(default_factory=dict)  # a key's subtile flags in the first stock DS1 that has it

    def add(self, lay: Layout, preset: dict, keep: set[Key], objects: list[tuple[str, float, float]] = (),
            floor: set | None = None, alike: frozenset[str] = frozenset(), outdoor: bool | None = None) -> None:
        """Harvest one stock (layout, preset) pair, only the keys in `keep`, and the single-layer LIKE keys of the
        `alike` DT1s, which like() lends to tiles the kit has no key for. A waypoint's pieces stay out of the
        per-tile kit, as D2R puts their transform up to 4 tiles off the waypoint's tiles (#669): each joins the
        nearest of the DS1's waypoint `objects` (waypoints()), or is dropped if it has none. A pasted room keeps
        its own (build()). With the preset's ground (GROUND, from pd2_terrain), each piece keeps its height above
        that ground, as the generated terrain is flat. A `far` piece anchored on a floor tile (`floor`, else the
        layout's "f" keys) is left out: its mesh belongs to a structure elsewhere in the room, and on a common floor key
        it would stand on any plain ground (Harrogath's stairs on Westmarch's floor, #679). `outdoor`: whether the stock DS1
        draws trees (else draws_trees(lay)); indoor() leaves such a room's plants out."""
        src = len(self.sources)
        self.sources.append(preset["dependencies"])
        ground = preset.get(GROUND)
        if draws_trees(lay) if outdoor is None else outdoor:
            self.outdoor.add(src)
        wants = hooks.each("tile")  # hooks/: harvest a tile although not in `keep`
        per: dict[tuple[int, int], list] = {c: [] for c, k in lay.keys.items()
                                            if k in keep or any(h(lay, k, alike) for h in wants)}
        floor = {c for c, k in lay.keys.items() if k[0] == "f"} if floor is None else floor
        groups: dict[int, list] = {}
        locate, drops = hooks.each("locate"), hooks.each("drop")
        for e in preset["entities"]:
            t = _transform(e)
            if t is None:
                continue
            x, z = t["position"]["x"], t["position"]["z"]
            if is_waypoint(e):
                if objects:
                    n = min(range(len(objects)), key=lambda k: (objects[k][1] - x) ** 2 + (objects[k][2] - z) ** 2)
                    groups.setdefault(n, []).append((e, x - objects[n][1], z - objects[n][2]))
                continue
            c = (min(max(int(x // TILE), 0), lay.width - 1), min(max(int(z // TILE), 0), lay.height - 1))
            for h in locate:
                c = h(lay, c, x, z)
            if c in per:
                piece = Piece(c, e, x, t["position"]["y"], z, floor, ground)
                if any(h(self, lay, piece) for h in drops):
                    continue
                g = piece.ground_h
                if g:
                    e = clone(e)
                    _transform(e)["position"]["y"] -= g
                per[c].append((e, x - c[0] * TILE, z - c[1] * TILE))
        for c, ents in per.items():
            self.instances.setdefault(lay.keys[c], []).append((src, ents))
            self.flags.setdefault(lay.keys[c], lay.flags.get(c, bytes(25)))
        for n, ents in groups.items():
            cls, x, z = objects[n]
            key = lay.keys.get((int(x // TILE), int(z // TILE)))
            models = [p for p in ents if not room_terrain(p[0])]
            for k, group in ((("kc", key, cls), ents), (("k", key), ents), (("c", cls), models)):
                if group and k[1]:
                    self.waypoints.setdefault(k, []).append((src, group))


def indoor(kit: Kit) -> Kit:
    """The kit for a map that draws no trees: without the plants of outdoor stock rooms. Kurast's sewer entrances
    share the sewers' floor tiles, and put their banyans and vines on the Sewers map's floor (#707)."""
    keep = hooks.each("indoor")

    def pieces(src: int, ents: list) -> list:
        for h in keep:
            ents = h(kit, src, ents)
        return ents
    return dataclasses.replace(kit, instances={
        k: [(src, pieces(src, ents)) for src, ents in opts] for k, opts in kit.instances.items()})
