"""HD presets for the PD2-only presets on D2R tiles (LP3, ADR 0005): one generated preset JSON per DS1.

Two halves with one seam, the Layout. layout() reads a DS1 into a layout: per tile, a key for what lies there, its
wall tiles (one per wall layer) or, with no wall, its floor tiles. A tile is named (DT1 file, orientation, main
index, sub index), the DT1 being the first, by path, of the DS1's DT1 files that holds that tile: a PD2 preset's are
the ones its level type and Dt1Mask load, a stock DS1's the ones its header lists. build() makes the preset from a
layout and a kit, harvested from D2R's stock DS1 and presets: each stock entity belongs to the tile under its
position (one tile is TILE HD units, DS1 x along x, DS1 y along z), so a key collects every instance D2R drew on such
a tile, empty ones too. build() gives each tile one of its key's instances, picked by a hash of the DS1 path and the
tile, so props come at D2R's density. A key no stock preset has falls back to each of its tiles alone (a wall on two
layers), else the tile stays empty. The terrain is D2R's stock flat plane (TERRAIN) scaled over the level, textured
by the tile set's biome; the preset depends on what the stock presets it copies from depend on.

merge/presets-hd.tsv: `tile set <tab> biome`, one line per lvltypes Name the PD2-only presets on D2R tiles load. The
merge fails on a PD2-only preset on D2R tiles whose tile set has no line, and on a line no such preset needs; a
preset that loads a PD2 tile (pd2assets/) gets none, it waits for LP4/LP5's stand-in kits. Needs D2R's tiles and
presets in modkit's cache, else it makes none (dev builds run without, ADR 0005):
`python -m modkit fetch --prefix data/global/tiles/ --prefix data/hd/env/preset/`.
"""
from __future__ import annotations

import copy
import json
import re
import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path

from pd2_ids import Table, excel, load
from pd2_levels import SLOTS, col, ds1_header, norm

TILE = 10  # HD units per DS1 tile
TERRAIN = "act2/palace/celse3"  # a stock preset whose terrain is a flat TERRAIN_SIZE square (D2R Reimagined's plane)
TERRAIN_SIZE = 170
PRESETS = Path("hd") / "env" / "preset"
FETCH = "python -m modkit fetch --prefix data/global/tiles/ --prefix data/hd/env/preset/"

Tile = tuple  # (DT1 path or None, orientation, main, sub)
Key = tuple  # ("w" or "f", tiles)


def dt1_tiles(data: bytes) -> set[tuple[int, int, int]]:
    """The (orientation, main, sub) of every tile in a DT1."""
    count, at = struct.unpack_from("<2i", data, 268)
    return {struct.unpack_from("<3i", data, at + k * 96 + 20) for k in range(count)}


def header_dt1s(data: bytes) -> list[str]:
    """The DT1 paths (under global/tiles) a DS1's header lists: the editor's .tg1 names."""
    version, o = struct.unpack_from("<i", data)[0], 12 + 4 * (struct.unpack_from("<i", data)[0] >= 8)
    o += 4 * (version >= 10)
    out = []
    if version >= 3:
        n, o = struct.unpack_from("<i", data, o)[0], o + 4
        for _ in range(n):
            end = data.index(b"\0", o)
            name, o = norm(data[o:end].decode("latin-1")), end + 1
            if "/tiles/" in name:
                out.append(name.split("/tiles/", 1)[1].rsplit(".", 1)[0] + ".dt1")
    return out


@dataclass
class Layout:
    width: int  # tiles
    height: int
    keys: dict[tuple[int, int], Key]  # (x, y) -> key; tiles with nothing are left out


def layout(data: bytes, dt1s: dict[str, set]) -> Layout:
    """The DS1's tiles, named in the DT1 files `dt1s` (path -> dt1_tiles)."""
    version, w, h, _act, _tag, o, walls, floors = ds1_header(data)
    if version < 4:
        raise ValueError(f"DS1 version {version}: only 4 and up keep the tile layers in the order layout() reads")
    n = w * h
    grids = [struct.unpack_from(f"<{n}I", data, o + k * n * 4) for k in range(2 * walls + floors)]
    order = sorted(dt1s)

    def tile(orientation: int, cell: int) -> Tile:
        t = (orientation, cell >> 20 & 0x3F, cell >> 8 & 0xFF)
        return (next((p for p in order if t in dt1s[p]), None), *t)
    keys = {}
    for i in range(n):
        wall = tuple(tile(grids[2 * k + 1][i] & 0xFF, grids[2 * k][i]) for k in range(walls) if grids[2 * k][i])
        floor = tuple(tile(0, g[i]) for g in grids[2 * walls:] if g[i])
        if wall or floor:
            keys[(i % w, i // w)] = ("w", wall) if wall else ("f", floor)
    return Layout(w, h, keys)


def _transform(entity: dict) -> dict | None:
    return next((c for c in entity["components"] if c["type"] == "TransformDefinitionComponent"), None)


def wanted(lay: Layout) -> set[Key]:
    """The keys build() may look up for a layout: each key and, for its fallback, each of its tiles alone."""
    return {k for key in lay.keys.values() for k in (key, *((key[0], (t,)) for t in key[1]))}


@dataclass
class Kit:
    instances: dict[Key, list[tuple[int, list[tuple[dict, float, float]]]]] = field(default_factory=dict)
    sources: list[dict[str, list[dict]]] = field(default_factory=list)  # each stock preset's dependencies

    def add(self, lay: Layout, preset: dict, keep: set[Key]) -> None:
        """Harvest one stock (layout, preset) pair, only the keys in `keep`."""
        src = len(self.sources)
        self.sources.append(preset["dependencies"])
        per: dict[tuple[int, int], list] = {c: [] for c, k in lay.keys.items() if k in keep}
        for e in preset["entities"]:
            t = _transform(e)
            if t is None:
                continue
            x, z = t["position"]["x"], t["position"]["z"]
            c = (min(max(int(x // TILE), 0), lay.width - 1), min(max(int(z // TILE), 0), lay.height - 1))
            if c in per:
                per[c].append((e, x - c[0] * TILE, z - c[1] * TILE))
        for c, ents in per.items():
            self.instances.setdefault(lay.keys[c], []).append((src, ents))


def build(lay: Layout, kit: Kit, name: str, seed: str, biome: str, terrain: dict) -> tuple[dict, int]:
    """The preset for a layout, and how many of its tiles stayed empty for a key the kit lacks."""
    entities, used, missing = [], set(), 0

    def place(x: int, y: int, key: Key, salt: str) -> bool:
        options = kit.instances.get(key)
        if not options:
            return False
        src, ents = options[zlib.crc32(f"{seed}:{x}:{y}{salt}".encode()) % len(options)]
        used.add(src)
        for e, dx, dz in ents:
            e = copy.deepcopy(e)
            e["id"] = len(entities) + 1
            t = _transform(e)
            t["position"]["x"], t["position"]["z"] = x * TILE + dx, y * TILE + dz
            for c in e["components"]:  # a wall's transparency names its tile
                if c["type"] == "WallTransparencyComponent" and c["wallTileLocalCoord"]["x"] >= 0:
                    c["wallTileLocalCoord"] = {"x": x, "y": y}
            entities.append(e)
        return True
    for (x, y), key in sorted(lay.keys.items()):
        if place(x, y, key, ""):
            continue
        alone = [(k, (key[0], (t,))) for k, t in enumerate(key[1]) if (key[0], (t,)) in kit.instances]
        for k, single in alone:
            place(x, y, single, f":{k}")
        missing += not alone
    terrain = copy.deepcopy(terrain)
    terrain["id"] = len(entities) + 1
    scale = _transform(terrain)["scale"]
    scale["x"], scale["z"] = lay.width * TILE / TERRAIN_SIZE, lay.height * TILE / TERRAIN_SIZE
    # shortcut: every dependency of each stock preset copied from, as a texture can't be traced to its model; trim it
    # if the frame-rate or load-time check in Jungle Map fails
    deps: dict[str, dict[str, dict]] = {}
    for src in sorted(used):
        for kind, items in kit.sources[src].items():
            for d in items:
                deps.setdefault(kind, {}).setdefault(d["path"].lower(), d)
    for kind, path in [("json", biome)] + [("models" if p.endswith(".model") else "physics", p)
                                          for p in re.findall(r'"filename"\s*:\s*"([^"]+)"', json.dumps(terrain))]:
        deps.setdefault(kind, {}).setdefault(path.lower(), {"path": path})
    return {"dependencies": {k: list(v.values()) for k, v in deps.items()}, "type": "Preset", "name": name,
            "entities": entities, "terrain": terrain, "biomeFilename": biome, "perTileBiomeOverrides": [],
            "specialTiles": {}}, missing


def read_rule(rule: Path) -> dict[str, str]:
    sets = {}
    for line in rule.read_text("utf-8").splitlines():
        if line and not line.startswith("#"):
            name, biome = line.split("\t")
            sets[name.lower()] = biome
    return sets


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


class Tiles(dict):
    """DT1 path -> dt1_tiles, read from D2R's tiles on first use (an absent file holds nothing)."""
    def __init__(self, root: Path):
        super().__init__()
        self.root = root

    def __missing__(self, path: str) -> set:
        f = self.root / path
        self[path] = dt1_tiles(f.read_bytes()) if f.is_file() else set()
        return self[path]

    def of(self, paths: list[str]) -> dict[str, set]:
        return {p: self[p] for p in paths}


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/presets-hd.tsv (see the module docstring)."""
    sets = read_rule(rule)
    tiles_dir = d2r_data / "global" / "tiles"
    if not (d2r_data / PRESETS).is_dir() or not any(tiles_dir.rglob("*.dt1")):
        print(f"  presets-hd: skipped, no D2R presets or DT1 under {d2r_data} (dev builds run without; `{FETCH}`)")
        return
    presets = pd2_only_presets(out_data, len(load(d2r_data, "lvlprest").records()))
    on_d2r = [p for p in presets if not any(f.startswith("pd2assets/") for f in p[2])]
    if unruled := sorted({s for _, s, _, _ in on_d2r if s.lower() not in sets}):
        raise ValueError(f"PD2-only presets on D2R tiles in tile sets with no line: {', '.join(unruled)}")
    if stale := sorted(set(sets) - {s.lower() for _, s, _, _ in on_d2r}):
        raise ValueError(f"lines for tile sets no PD2-only preset on D2R tiles loads: {', '.join(stale)}")
    terrain_src = d2r_data / PRESETS / f"{TERRAIN}.json"
    if not terrain_src.is_file():
        raise ValueError(f"no {terrain_src}: run `{FETCH}`")
    terrain = json.loads(terrain_src.read_text("utf-8-sig"))["terrain"]

    tiles = Tiles(tiles_dir)
    layouts = {ds1: layout((out_data / "global" / "tiles" / ds1).read_bytes(), tiles.of(files))
               for _, _, files, ds1s in on_d2r for ds1 in ds1s}
    keep = set().union(*map(wanted, layouts.values()))
    loaded = {t[0] for k in keep for t in k[1]}
    kit = Kit()
    for p in sorted((d2r_data / PRESETS).rglob("*.json")):
        ds1 = tiles_dir / p.relative_to(d2r_data / PRESETS).with_suffix(".ds1")
        if not ds1.is_file():  # a few stock presets have no DS1 (D2R's HD-only rooms)
            continue
        data = ds1.read_bytes()
        listed = header_dt1s(data)
        if loaded.isdisjoint(listed):
            continue
        kit.add(layout(data, tiles.of(listed)), json.loads(p.read_text("utf-8-sig")), keep)

    report = []
    for _, tile_set, _, ds1s in on_d2r:
        for ds1 in ds1s:
            lay = layouts[ds1]
            preset, missing = build(lay, kit, Path(ds1).stem, ds1, sets[tile_set.lower()], terrain)
            dest = out_data / PRESETS / Path(ds1).with_suffix(".json")
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(json.dumps(preset, separators=(",", ":")), "utf-8")
            report.append((len(lay.keys), missing, len(preset["entities"])))
    print(f"  presets-hd: {len(report)} HD presets from {len(kit.sources)} stock presets, "
          f"{sum(r[2] for r in report)} entities; {sum(r[1] for r in report)} of {sum(r[0] for r in report)} tiles "
          f"have a key no stock preset draws; {len(presets) - len(on_d2r)} presets on PD2 tiles have none (LP4/LP5)")
