"""pd2_merge's presets-hd rule: read the rule, harvest the stock, build and write every PD2-only preset."""
from __future__ import annotations

import functools
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, NamedTuple

import pd2_dt1_copies as dt1_copies
import pd2_painter as painter
import pd2_terrain as terrain_ground
from pd2_ids import Table, excel, load
from pd2_levels import SLOTS, col, norm

from .build import build
from .consts import FETCH, GROUND, PRESETS, STREAM, TERRAIN, WATER_SPAN, wet_dt1s
from .dt1 import Tiles, header_dt1s
from .entity import _transform
from .hooks.far import far_off
from .kit import Kit, indoor
from .layout import draws_trees, layout, wanted
from .pillars import model_top
from . import void
from .waypoints import waypoint_options, waypoints
from .water import water_planes


def rule_files(rule: Path) -> list[Path]:
    """The files that hold the rule's lines, in the order read: `rule` itself (merge/presets-hd.tsv, the anchor
    pd2_merge's driver runs the rule by, so it is a file; its lines, if any, come first), then the *.tsv of the folder
    beside it (merge/presets-hd.d/), sorted by file name (#789). THIS is where the read order is defined; the lint and
    the tools read the rule through here. A key may appear once (read_rule), so the order cannot change the output."""
    d = rule.with_name(rule.stem + ".d")
    return [rule, *(sorted(d.glob("*.tsv")) if d.is_dir() else [])]


def read_rule(rule: Path) -> tuple[dict[str, str], dict[str, str | None], dict[str, str]]:
    """(tile set -> biome, PD2 DT1 -> its D2R stand-in DT1 or None, water DT1 -> water plane model): a first column
    `water:<DT1>` is a water line, one ending in .dt1 a stand-in. The `walls:<DT1>` lines are in read_rule_all. The
    lines are in rule_files(rule). A ValueError names the two files of a key that appears twice, in one file or in two."""
    return read_rule_all(rule)[:3]


def read_rule_all(rule: Path) -> tuple[dict[str, str], dict[str, str | None], dict[str, str], dict[str, str]]:
    """read_rule's three maps and, fourth, the walls lines: PD2 DT1 -> the D2R DT1 whose stock wall pieces stand on the
    wall tiles of that PD2 DT1 (a first column `walls:<PD2 DT1>`, the value the D2R DT1; the wall pass, #826)."""
    sets, standins, water, walls = {}, {}, {}, {}
    seen: dict[tuple[str, str], str] = {}  # (kind, key) -> the file of its line
    for f in rule_files(rule):
        for line in f.read_text("utf-8").splitlines():
            if line and not line.startswith("#"):
                name, value, *_reason = line.split("	")  # a third column is the line's `# reason` (pd2_presets_lint)
                low = name.lower()
                kind, key = (("water", norm(name[6:])) if low.startswith("water:") else
                             ("walls", norm(name[6:])) if low.startswith("walls:") else
                             ("standin", norm(name)) if low.endswith(".dt1") else ("set", low))
                if (kind, key) in seen:
                    raise ValueError(f"{kind} line {name!r} is in {seen[kind, key]} and in {f.name}: a key has one line")
                seen[kind, key] = f.name
                if kind == "water":
                    if value not in WATER_SPAN:
                        raise ValueError(f"water line {name}: {value} is no water plane WATER_SPAN knows")
                    water[key] = value
                elif kind == "walls":
                    if not key.endswith(".dt1") or not norm(value).endswith(".dt1"):
                        raise ValueError(f"walls line {name!r}: both the PD2 DT1 and the D2R DT1 must be .dt1 paths")
                    walls[key] = norm(value)
                elif kind == "standin":
                    standins[key] = None if value == "-" else norm(value)
                else:
                    sets[key] = value
    return sets, standins, water, walls


def stand_in(files: list[str], tiles: "Tiles", pd2_tiles: "Tiles", standins: dict[str, str | None],
             copied: set | dict = frozenset()) -> dict[str, dict]:
    """The DT1 files a PD2-only preset loads, for layout(): a PD2 tile its stand-in also holds, with the same subtile
    flags, is named by the stand-in; the rest keep the PD2 DT1's name, which no stock preset draws (empty in HD), but
    for the `copied` tiles (pd2_dt1_copies), which layout() names by the D2R tile they copy."""
    out: dict[str, dict] = {}
    for f in files:
        if f not in standins:
            out.setdefault(f, {}).update(tiles[f])
            continue
        s, own = standins[f], dict(pd2_tiles[f])
        if s:
            out.setdefault(s, {}).update({t: own.pop(t) for t in list(own)
                                          if (f, *t) not in copied and tiles[s].get(t) == own[t]})
        out[f] = own
    return out


def pd2_only_presets(out_data: Path, d2r_lvlprest: int) -> list[tuple[str, str, list[str], list[str]]]:
    """The appended lvlprest rows: (name, tile set, the DT1 paths it loads, DS1 paths)."""
    lp, ty, lv = (Table(excel(out_data) / f"{t}.txt") for t in ("lvlprest", "lvltypes", "levels"))
    out = []
    for k in lp.records()[d2r_lvlprest:]:
        r = lp.rows[k]
        t = lv.rows[lv.records()[int(r[col(lp, "LevelId")])]][col(lv, "LevelType")]
        if not t:  # PD2's blank `Unused` row
            continue
        trow, mask = ty.rows[ty.records()[int(t)]], int(r[col(lp, "Dt1Mask")] or 0)
        out.append((r[0], trow[0], [norm(trow[col(ty, f"File {s + 1}")]) for s in range(SLOTS) if mask >> s & 1],
                    [norm(r[col(lp, f"File{n}")]) for n in range(1, 7) if r[col(lp, f"File{n}")] not in ("", "0")]))
    return out


def wall_pass(on_d2r: list, tiles: "Tiles", pd2_tiles: "Tiles", out_data: Path, d2r_data: Path, tiles_dir: Path,
              stock: list, walls: dict[str, str] | None = None) -> None:
    """Give the wall pass (passes/edges_walls) what only the merge has: each DS1 named by its own DT1s, their heights
    and D2R's model boxes. Without D2R's model boxes (dev builds) there is no wall pass."""
    import pd2_tiles as shared
    from .passes import edges_walls
    models = d2r_data / shared.MODELS
    if not models.is_file():
        return
    found = shared.model_boxes(models.read_bytes())

    @functools.cache
    def prefab(path: str) -> list[dict]:
        f = d2r_data / path.removeprefix("data/")
        return json.loads(f.read_text("utf-8-sig")).get("entities", []) if f.is_file() else []
    heights = shared.Heights(out_data / "global" / "tiles", tiles_dir)
    far = far_off(d2r_data)
    contexts = []
    for _, _, files, ds1s in on_d2r:
        for ds1 in ds1s:
            legacy = layout((out_data / "global" / "tiles" / ds1).read_bytes(),
                            {p: pd2_tiles[p] or tiles[p] for p in files})
            contexts.append(edges_walls.Context(legacy, heights, lambda m: found.get(shared.fnv(m)),
                                                prefab, stock, far, walls or {}))
            edges_walls.configure(ds1, contexts[-1])
    if walls:
        edges_walls.check_walls(walls, contexts)


def void_pass(on_d2r: list, tiles: "Tiles", pd2_tiles: "Tiles", out_data: Path, d2r_data: Path) -> None:
    """Give the void passes (#783) what only the merge has: each DS1's empty cells, read from the DS1 named by its own
    DT1s, and the stock black plane."""
    void.STOCK[:] = [void.black(d2r_data)]
    drawn = void.Drawn(out_data / "global" / "tiles", d2r_data / "global" / "tiles")
    for _, _, files, ds1s in on_d2r:
        for ds1 in ds1s:
            legacy = layout((out_data / "global" / "tiles" / ds1).read_bytes(),
                            {p: pd2_tiles[p] or tiles[p] for p in files})
            void.configure(ds1, void.empty(legacy, drawn))


@dataclass
class Plan:
    """What apply() builds, from the tables alone (cheap, no DS1 read): the inputs, the rule, and the DS1 by tile set.
    Picklable: a build worker (parallel.py) gets it and runs setup() itself."""
    rule: Path
    pd2_data: Path
    d2r_data: Path
    out_data: Path
    sets: dict[str, str]
    standins: dict[str, str | None]
    water_rule: dict[str, str]
    walls_rule: dict[str, str]
    presets: list
    on_d2r: list
    groups: dict[str, list[str]]  # tile set -> its DS1, in the order the presets list them
    terrain: dict


class DS1Built(NamedTuple):
    """What build_ds1 reports for one DS1 (its preset JSON is already written): (tiles, tiles with a key no stock preset
    draws, entities, painted tree lines, water planes), and the waypoint objects with no stock waypoint."""
    row: tuple[int, int, int, bool, int]
    bare: list[str]


@dataclass
class State:
    """setup()'s result: what every build_ds1 reads and none writes (memos aside: they only save work)."""
    plan: Plan
    layouts: dict
    kit: Kit
    stock: list
    mod_classes: dict
    prefab: Callable
    water: dict
    learned: dict = field(default_factory=dict)  # painter.harvest's memo: lives for this state only
    trees: dict = field(default_factory=dict)  # tile set -> (its stock, its tree kit once a DS1 of it draws trees)
    inside: Kit | None = None  # indoor(kit), made when a DS1 draws no trees


def plan(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> Plan | None:
    """Read the rule and the tables and check them; None (with a line) when D2R's presets or DT1 are not there."""
    sets, standins, water_rule, walls_rule = read_rule_all(rule)
    tiles_dir = d2r_data / "global" / "tiles"
    if not (d2r_data / PRESETS).is_dir() or not any(tiles_dir.rglob("*.dt1")):
        print(f"  presets-hd: skipped, no D2R presets or DT1 under {d2r_data} (dev builds run without; `{FETCH}`)")
        return None
    presets = pd2_only_presets(out_data, len(load(d2r_data, "lvlprest").records()))
    # a preset on PD2 tiles is built once every PD2 DT1 it loads has a stand-in line (LP4/LP5); the others wait
    on_d2r = [p for p in presets if all(f in standins for f in p[2] if f.startswith("pd2assets/"))]
    if unruled := sorted({s for _, s, _, _ in on_d2r if s.lower() not in sets}):
        raise ValueError(f"PD2-only presets on D2R tiles or stand-ins in tile sets with no line: {', '.join(unruled)}")
    if stale := sorted(set(sets) - {s.lower() for _, s, _, _ in on_d2r}):
        raise ValueError(f"lines for tile sets no PD2-only preset on D2R tiles or stand-ins loads: {', '.join(stale)}")
    if stale := sorted(set(standins) - {f for p in presets for f in p[2]}):
        raise ValueError(f"stand-in lines for DT1 no PD2-only preset loads: {', '.join(stale)}")
    terrain_src = d2r_data / PRESETS / f"{TERRAIN}.json"
    if not terrain_src.is_file():
        raise ValueError(f"no {terrain_src}: run `{FETCH}`")
    terrain = json.loads(terrain_src.read_text("utf-8-sig"))["terrain"]
    groups: dict[str, list[str]] = {}
    for _, tile_set, _, ds1s in on_d2r:
        groups.setdefault(tile_set, []).extend(ds1s)
    return Plan(rule, pd2_data, d2r_data, out_data, sets, standins, water_rule, walls_rule, presets, on_d2r, groups, terrain)


def setup(pl: Plan) -> State:
    """The slow part before any DS1: read the stock presets, name every DS1's tiles, harvest the kit, set up the wall
    pass. Deterministic, so a worker process that runs it gets the same State."""
    d2r_data, out_data, on_d2r, standins = pl.d2r_data, pl.out_data, pl.on_d2r, pl.standins
    tiles_dir = d2r_data / "global" / "tiles"
    tiles, pd2_tiles = Tiles(tiles_dir), Tiles(out_data / "global" / "tiles")
    pd2_files = {f: out_data / "global" / "tiles" / f for _, _, files, _ in on_d2r for f in files
                 if f.startswith("pd2assets/")}
    copied = dt1_copies.copies({f: p.read_bytes() for f, p in sorted(pd2_files.items()) if p.is_file()},
                               dt1_copies.d2r_images(tiles_dir), standins) if pd2_files else {}

    def classes(data: Path) -> dict[tuple[int, int], str]:  # objpreset's (Act, Index) -> ObjectClass
        t = Table(excel(data) / "objpreset.txt")
        c = [col(t, n) for n in ("Act", "Index", "ObjectClass")]
        return {(int(r[c[0]]), int(r[c[1]])): r[c[2]] for r in t.rows if r[c[0]] and r[c[1]]}
    stock_classes, mod_classes = classes(d2r_data), classes(out_data)
    layouts = {ds1: layout((out_data / "global" / "tiles" / ds1).read_bytes(),
                           stand_in(files, tiles, pd2_tiles, standins, copied), copied)
               for _, _, files, ds1s in on_d2r for ds1 in ds1s}
    keep = set().union(*map(wanted, layouts.values()))
    loaded = {t[0] for k in keep for t in k[1]}
    if stale := sorted(set(pl.water_rule) - loaded):
        raise ValueError(f"water lines for DT1 no PD2-only preset draws from: {', '.join(stale)}")
    walls_rule = pl.walls_rule
    if stale := sorted(set(walls_rule) - loaded):
        raise ValueError(f"walls lines for DT1 no PD2-only preset draws from: {', '.join(stale)}")
    water = water_planes(d2r_data, pl.water_rule)
    kit, stock = Kit(far=far_off(d2r_data), top=model_top(d2r_data)), []
    for p in sorted((d2r_data / PRESETS).rglob("*.json")):
        ds1 = tiles_dir / p.relative_to(d2r_data / PRESETS).with_suffix(".ds1")
        if not ds1.is_file():  # a few stock presets have no DS1 (D2R's HD-only rooms)
            continue
        data = ds1.read_bytes()
        listed = header_dt1s(data)
        if loaded.isdisjoint(listed):
            continue
        preset = json.loads(p.read_text("utf-8-sig"))
        preset[GROUND] = terrain_ground.of_preset(preset, d2r_data)
        stock.append((layout(data, tiles.of(listed)), preset, set(listed)))
        kit.add(*stock[-1][:2], keep, waypoints(data, stock_classes), alike=frozenset(loaded))

    if gone := sorted(d for d in set(walls_rule.values()) if not tiles[d]):
        raise ValueError(f"walls lines name D2R DT1 with no tiles: {', '.join(gone)}")
    wall_pass(on_d2r, tiles, pd2_tiles, out_data, d2r_data, tiles_dir, [(lay, p) for lay, p, _ in stock], walls_rule)
    void_pass(on_d2r, tiles, pd2_tiles, out_data, d2r_data)

    @functools.cache
    def prefab(path: str) -> list[tuple[float, float]]:
        f = d2r_data / path.removeprefix("data/")
        ents = json.loads(f.read_text("utf-8-sig")).get("entities", []) if f.is_file() else []
        return [(t["position"]["x"], t["position"]["z"]) for t in map(_transform, ents) if t] or [(0.0, 0.0)]
    return State(pl, layouts, kit, stock, mod_classes, prefab, water)


def build_ds1(st: State, tile_set: str, ds1: str) -> DS1Built:
    """Build one DS1's HD preset and write it (out_data/hd/env/preset/<ds1>.json). The unit the work is farmed out in
    (parallel.py): inputs in, one DS1's report out. Builds share nothing they write, so any order and any split of the
    DS1 over processes gives the same files."""
    pl, lay = st.plan, st.layouts[ds1]
    outdoor = draws_trees(lay)
    if tile_set not in st.trees:
        # the tile set's stock: the presets that list a DT1 its DS1 draw from; the tree kit is harvested from them
        drawn = {t[0] for d in pl.groups[tile_set] for k in wanted(st.layouts[d]) for t in k[1]}
        st.trees[tile_set] = [(lay_, preset) for lay_, preset, listed in st.stock if not drawn.isdisjoint(listed)], None
    own, trees = st.trees[tile_set]
    if outdoor and trees is None:
        trees = painter.harvest(own, wet_dt1s(pl.water_rule), st.learned) or ()
        st.trees[tile_set] = own, trees
    objects = waypoints((pl.out_data / "global" / "tiles" / ds1).read_bytes(), st.mod_classes)
    bare = [f"{Path(ds1).stem} {o[0]}" for o in objects if not waypoint_options(lay, st.kit, *o)]
    if not outdoor and st.inside is None:
        st.inside = indoor(st.kit)
    preset, missing = build(lay, st.kit if outdoor else st.inside, Path(ds1).stem, ds1, pl.sets[tile_set.lower()],
                            pl.terrain, own, (trees or None) if outdoor else None, st.prefab, st.water, objects)
    dest = pl.out_data / PRESETS / Path(ds1).with_suffix(".json")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(preset, separators=(",", ":")), "utf-8")
    return DS1Built((len(lay.keys), missing, len(preset["entities"]), bool(outdoor and trees),
                     sum(e["name"] == STREAM for e in preset["entities"])), bare)


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/presets-hd.tsv and its folder presets-hd.d/ (see the module docstring). The DS1
    builds run in worker processes when merge slots are free (parallel.py); the output is the same for any worker count."""
    pl = plan(rule, pd2_data, d2r_data, out_data)
    if pl is None:
        return
    from . import parallel
    tasks = [(tile_set, ds1) for tile_set, ds1s in pl.groups.items() for ds1 in ds1s]
    built, sources = parallel.build_all(pl, tasks)
    report, bare = [b.row for b in built], [n for b in built for n in b.bare]
    print(f"  presets-hd: {len(report)} HD presets from {sources} stock presets, "
          f"{sum(r[2] for r in report)} entities, {sum(r[3] for r in report)} with painted tree lines, {sum(r[4] for r in report)} water planes; {sum(r[1] for r in report)} of {sum(r[0] for r in report)} tiles "
          f"have a key no stock preset draws; {len(pl.presets) - len(pl.on_d2r)} presets on PD2 tiles wait for stand-ins (LP4/LP5); "
          f"{len(bare)} waypoint objects have no stock waypoint ({', '.join(sorted(bare)) or 'none'})")
    from .passes import edges_walls
    edges_walls.CONTEXT.clear()  # the wall pass's contexts hold this run's layouts and presets: nothing outlives apply()
    void.CELLS.clear()
    void.STOCK.clear()
