"""The presets rule's cache per DS1 (#861): a change to one area rebuilds that area, not all of the DS1.

The rule cache (pd2_cache.rule) keeps or replays the whole rule. This keeps each DS1's preset on its own, under a key that
is a function of what that DS1's build can see, so a rule run that misses as a whole still replays the DS1 whose inputs did
not change. apply() asks build_all(); without a running rule cache (pd2_cache.scope() is None: the unit tests, a bare
apply(), a CI run without a scope) it only builds, as before.

The build runs on a slice of the shared State (slices.py), so the key cannot miss an input the build reads: the layout, the
waypoint objects, the kit pieces for its tile keys, the stock presets of its tile set, the stock as a whole (the wall pass
borrows pieces from all of it), the wall pass's legacy layout and DT1 heights, the water planes of the DT1 it draws, the
terrain, the biome, and the rule's code (pd2_cache.sources: the data files beside it too). The key holds no path and
nothing of the machine, only content.

The files a build opens while it runs (a prefab's JSON) are not known in advance: the build runs in a sub-trace
(pd2_cache.sub_trace) and the entry keeps what it read, checked again on replay (pd2_cache.part_get); a replay hands them to
the rule's own trace, so the rule's entry is found again by everything its DS1 read. An entry is the DS1's preset JSON and
its apply.DS1Built.
"""
from __future__ import annotations

import gc
import hashlib
import json
from pathlib import Path

import pd2_cache

from . import parallel
from .consts import GROUND, PRESETS, wet_dt1s
from .kit import Kit
from .layout import Layout, draws_trees
from .slices import drawn_dt1s

PART = "ds1"  # the key's first part: a DS1 entry is never a rule's


def _enc(o):
    if isinstance(o, (bytes, bytearray)):
        return o.hex()
    if isinstance(o, (set, frozenset)):
        return sorted(o, key=repr)
    if hasattr(o, "digest"):  # pd2_terrain.Ground: the hash of its mesh file
        return o.digest
    raise TypeError(f"cannot hash {type(o).__name__}")


def digest(*parts) -> str:
    """A hash of JSON-able parts (a tuple is a list, bytes are hex, a set is sorted): equal values, equal digest."""
    return hashlib.sha256(json.dumps(parts, separators=(",", ":"), default=_enc).encode()).hexdigest()


def memo(held: dict, o, make) -> str:
    """make(o)'s digest, once per object o in `held` (a stock preset is in the key of many DS1). The object is held, so its
    id stays its own."""
    h = held.get(id(o))
    if h is None or h[0] is not o:
        h = held[id(o)] = (o, make(o))
    return h[1]


def layout_digest(lay: Layout) -> str:
    return digest(lay.width, lay.height, sorted(lay.keys.items()), sorted(lay.flags.items()), sorted(lay.unwalkable))


def preset_digest(preset: dict) -> str:
    return digest({k: v for k, v in preset.items() if k != GROUND}, preset.get(GROUND))


def kit_digest(kit: Kit, held: dict) -> str:
    """The kit's content in its order, as the passes read it (the order of the keys and of a key's instances picks pieces)."""
    return digest(
        [[k, [[s, memo(held, e, digest)] for s, e in v]] for k, v in kit.instances.items()],
        [memo(held, s, digest) for s in kit.sources], sorted(kit.outdoor),
        sorted(([k, [[s, memo(held, g, digest)] for s, g in v]] for k, v in kit.waypoints.items()), key=repr),
        sorted(kit.flags.items(), key=repr))


def file_digest(path: Path) -> str:
    try:
        with open(path, "rb") as f:
            return hashlib.file_digest(f, "sha256").hexdigest()
    except OSError:
        return ""


def global_digest(st) -> str:
    """What every DS1 of the State shares: the stock presets (the wall pass borrows pieces from all of them), D2R's model
    boxes and the typing vote (fidelity) the wall pass and Kit.far/top read, the terrain, the stream DT1s."""
    if "global" not in st.digests:
        import pd2_coverage as coverage
        import pd2_fidelity as fidelity
        pl, held = st.plan, st.digests
        stock = digest([[memo(held, lay, layout_digest), memo(held, p, preset_digest), sorted(listed)]
                        for lay, p, listed in st.stock])
        st.digests["global"] = (None, digest(stock, file_digest(pl.d2r_data / coverage.MODELS),
                                             file_digest(fidelity.VOTES_FILE), pl.terrain, sorted(wet_dt1s(pl.water_rule))))
    return st.digests["global"][1]


def ds1_key(st, tile_set: str, ds1: str, rule_key: str) -> str:
    """The key of one DS1's build: everything build_ds1 reads that is not a file it opens while it runs."""
    from . import apply
    from .passes import edges_walls
    sl, held, lay = apply.slices.sliced(st, ds1), st.digests, st.layouts[ds1]
    ctx = edges_walls.CONTEXT.get(ds1)
    wall = None
    if ctx is not None:  # the wall pass: its legacy layout, those DT1's heights, their `walls:` lines
        used = sorted(drawn_dt1s(ctx.legacy) - {None})
        wall = [memo(held, ctx.legacy, layout_digest), {p: sorted(ctx.heights[p].items()) for p in used},
                {p: v for p, v in sorted(ctx.walls.items()) if p in used}]
    own = [[memo(held, lay_, layout_digest), memo(held, p, preset_digest)] for lay_, p in apply.tile_stock(st, tile_set)]
    return digest(PART, rule_key, global_digest(st), ds1, tile_set, st.plan.sets[tile_set.lower()],
                  memo(held, lay, layout_digest), sl.objects, wall, kit_digest(sl.kit, held), own, draws_trees(lay),
                  {d: [memo(held, v[0], digest), memo(held, v[1], digest)] for d, v in sorted(sl.water.items())})[:32]


def encode(built) -> dict:
    return {"row": list(built.row), "bare": built.bare}


def decode(extra: dict):
    from .apply import DS1Built
    return DS1Built(tuple(extra["row"]), list(extra["bare"]))


def build_all(pl, tasks: list[tuple[str, str]]) -> tuple[list, int]:
    """apply.DS1Built of every (tile set, DS1) task, in task order, and the number of stock presets the kit harvested.
    Under a running rule cache each DS1 with an entry for its key is replayed; the others are built (in workers when
    there are enough of them, parallel.py) and kept."""
    sc = pd2_cache.scope()
    gen = (sc.roots["p"] / "generated").resolve() if sc else None
    try:
        keepable = sc is not None and bool(tasks) and pl.out_data.resolve().is_relative_to(gen)
    except OSError:
        keepable = False
    if not keepable:
        built, sources = parallel.build_all(pl, tasks)
        return [b for b, _ in built], sources
    from . import apply
    st = apply.setup(pl)
    keys = {t: ds1_key(st, *t, sc.key) for t in tasks}
    sources, got = len(st.kit.sources), {}
    for t in tasks:
        if (extra := pd2_cache.part_get(sc, keys[t])) is not None:
            got[t] = decode(extra)
    print(f"  presets-hd: {len(got)} of {len(tasks)} DS1 replayed")
    if misses := [t for t in tasks if t not in got]:
        box = [st]
        del st
        built, _ = parallel.build_all(pl, misses, box)
        for t, (b, reads) in zip(misses, built):
            got[t] = b
            out = (pl.out_data / PRESETS / Path(t[1]).with_suffix(".json")).resolve().relative_to(gen)
            pd2_cache.part_put(sc, keys[t], [out.as_posix()], encode(b), reads)
    gc.collect()
    return [got[t] for t in tasks], sources
