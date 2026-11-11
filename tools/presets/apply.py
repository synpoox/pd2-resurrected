"""pd2_merge's presets-hd rule: read the rule, harvest the stock, build and write every PD2-only preset."""
from __future__ import annotations

import functools
import json
from pathlib import Path

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
from .hooks.structure import structure_off
from .kit import Kit, indoor
from .layout import draws_trees, layout, wanted
from .pillars import model_top
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
    import pd2_coverage as coverage
    import pd2_fidelity as fidelity
    from .passes import edges_walls
    models = d2r_data / coverage.MODELS
    if not models.is_file():
        return
    found = coverage.model_boxes(models.read_bytes())

    @functools.cache
    def prefab(path: str) -> list[dict]:
        f = d2r_data / path.removeprefix("data/")
        return json.loads(f.read_text("utf-8-sig")).get("entities", []) if f.is_file() else []
    heights = fidelity.Heights(out_data / "global" / "tiles", tiles_dir)
    far = far_off(d2r_data)
    contexts = []
    for _, _, files, ds1s in on_d2r:
        for ds1 in ds1s:
            legacy = layout((out_data / "global" / "tiles" / ds1).read_bytes(),
                            {p: pd2_tiles[p] or tiles[p] for p in files})
            contexts.append(edges_walls.Context(legacy, heights, lambda m: found.get(coverage.fnv(m)),
                                                prefab, stock, far, walls or {}))
            edges_walls.configure(ds1, contexts[-1])
    if walls:
        edges_walls.check_walls(walls, contexts)


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/presets-hd.tsv and its folder presets-hd.d/ (see the module docstring)."""
    sets, standins, water_rule, walls_rule = read_rule_all(rule)
    tiles_dir = d2r_data / "global" / "tiles"
    if not (d2r_data / PRESETS).is_dir() or not any(tiles_dir.rglob("*.dt1")):
        print(f"  presets-hd: skipped, no D2R presets or DT1 under {d2r_data} (dev builds run without; `{FETCH}`)")
        return
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
    if stale := sorted(set(water_rule) - loaded):
        raise ValueError(f"water lines for DT1 no PD2-only preset draws from: {', '.join(stale)}")
    if stale := sorted(set(walls_rule) - loaded):
        raise ValueError(f"walls lines for DT1 no PD2-only preset draws from: {', '.join(stale)}")
    water = water_planes(d2r_data, water_rule)
    kit, stock = Kit(far=far_off(d2r_data), structure=structure_off(d2r_data), top=model_top(d2r_data)), []
    inside = None  # indoor(kit), made once the kit is harvested
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

    @functools.cache
    def prefab(path: str) -> list[tuple[float, float]]:
        f = d2r_data / path.removeprefix("data/")
        ents = json.loads(f.read_text("utf-8-sig")).get("entities", []) if f.is_file() else []
        return [(t["position"]["x"], t["position"]["z"]) for t in map(_transform, ents) if t] or [(0.0, 0.0)]
    report, groups, bare = [], {}, []
    for _, tile_set, _, ds1s in on_d2r:
        groups.setdefault(tile_set, []).extend(ds1s)
    learned: dict = {}  # painter.harvest's memo: lives for this apply() only
    for tile_set, ds1s in groups.items():
        # the tile set's stock: the presets that list a DT1 its DS1 draw from; the tree kit is harvested from them
        drawn = {t[0] for ds1 in ds1s for k in wanted(layouts[ds1]) for t in k[1]}
        own = [(lay, preset) for lay, preset, listed in stock if not drawn.isdisjoint(listed)]
        trees = painter.harvest(own, wet_dt1s(water_rule), learned)
        for ds1 in ds1s:
            lay = layouts[ds1]
            outdoor = draws_trees(lay)
            objects = waypoints((out_data / "global" / "tiles" / ds1).read_bytes(), mod_classes)
            bare += [f"{Path(ds1).stem} {o[0]}" for o in objects if not waypoint_options(lay, kit, *o)]
            if not outdoor and inside is None:
                inside = indoor(kit)
            preset, missing = build(lay, kit if outdoor else inside, Path(ds1).stem, ds1, sets[tile_set.lower()], terrain, own,
                                    trees if outdoor else None, prefab, water, objects)
            dest = out_data / PRESETS / Path(ds1).with_suffix(".json")
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(json.dumps(preset, separators=(",", ":")), "utf-8")
            report.append((len(lay.keys), missing, len(preset["entities"]), bool(outdoor and trees),
                           sum(e["name"] == STREAM for e in preset["entities"])))
    print(f"  presets-hd: {len(report)} HD presets from {len(kit.sources)} stock presets, "
          f"{sum(r[2] for r in report)} entities, {sum(r[3] for r in report)} with painted tree lines, {sum(r[4] for r in report)} water planes; {sum(r[1] for r in report)} of {sum(r[0] for r in report)} tiles "
          f"have a key no stock preset draws; {len(presets) - len(on_d2r)} presets on PD2 tiles wait for stand-ins (LP4/LP5); "
          f"{len(bare)} waypoint objects have no stock waypoint ({', '.join(sorted(bare)) or 'none'})")
    from .passes import edges_walls
    edges_walls.CONTEXT.clear()  # the wall pass's contexts hold this run's layouts and presets: nothing outlives apply()
