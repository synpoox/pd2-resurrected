"""Write docs/agents/warp-locations.md: `cheat goto` lines for every PD2-specific map the presets-hd rule makes.

    python tools/pd2_merge.py && python tools/pd2_warps.py          # rewrites the doc

A map is PD2-specific when it has an appended lvlprest row (past D2R's) whose generated HD preset exists under
generated/data/hd/env/preset/. The tables and DS1s come from generated/ (what the merge wrote). `cheat goto` takes
absolute subtile coordinates (cheat-commands.md): see pd2_edge_tour.subtile() and hd_subtile().
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import pd2_presets as presets  # noqa: E402
from pd2_edge_tour import offsets, subtile  # noqa: E402
from pd2_ids import load  # noqa: E402
from pd2_levels import col, ds1_header, norm, object_list  # noqa: E402

DOC = ROOT / "docs" / "agents" / "warp-locations.md"
ACTS = {0: "Act 1", 1: "Act 2", 2: "Act 3", 3: "Act 4", 4: "Act 5"}


def hd_subtile(offset: int, hd: float) -> int:
    """Absolute subtile coordinate of an HD preset position (10 units per tile, 2 per subtile)."""
    return int(offset * 5 + hd / 2)


def read_objects(data: bytes) -> list[tuple[int, int, int, int]]:
    """A DS1's objects: (type, id, x, y), x and y in subtiles of the DS1."""
    _act, at, count, size = object_list(data)
    return [struct.unpack_from("<4i", data, at + 4 + k * size) for k in range(count)]


def walkable_near(lay: presets.Layout, cx: int, cy: int) -> tuple[int, int]:
    """The walkable floor tile (presets.walkable) nearest (cx, cy); (cx, cy) if the DS1 has none."""
    tiles = presets.walkable(lay)
    return min(tiles, key=lambda t: ((t[0] - cx) ** 2 + (t[1] - cy) ** 2, t)) if tiles else (cx, cy)


def object_classes(gen: Path) -> dict[tuple[int, int], str]:
    """objpreset.txt: (act, object id) -> class name, as the waypoint lookup reads it."""
    op = load(gen, "objpreset")
    return {(int(r[1]), int(r[0])): r[2] for r in op.rows if r[0].isdigit() and r[1].isdigit()}


def reference_spots(data: bytes, lay: presets.Layout | None, classes: dict[tuple[int, int], str],
                    places: dict[int, list[str]] | None = None) -> list[tuple[str, int, int]]:
    """A DS1's reference spots as (label, tileX, tileY): the centre, the walkable tile nearest it (when `lay` is given:
    the centre itself can be rock or water), then in object order each waypoint and, when `places` (act -> monpreset
    names) is given, each boss-like monster preset. The sweep (pd2_edge_tour) calls this without `places`."""
    _v, w, h, dact, *_ = ds1_header(data)
    spots = [("centre", w // 2, h // 2)]
    if lay is not None:
        spots.append(("nearest walkable to centre", *walkable_near(lay, w // 2, h // 2)))
    for kind, oid, x, y in read_objects(data):
        if kind == 2 and "waypoint" in classes.get((dact + 1, oid), "").lower():
            spots.append(("waypoint", x // 5, y // 5))
        if places is not None and kind == 1 and oid < len(places.get(dact + 1, [])) and any(
                s in places[dact + 1][oid].lower() for s in ("boss", "unique", "diablo", "baal", "mephisto")):
            spots.append((f"monster preset {places[dact + 1][oid]}", x // 5, y // 5))
    return spots


def selector(ds1: str) -> str:
    """The `pd2_force_ds1.py` selector of a DS1: its file name."""
    return ds1.rsplit("/", 1)[-1].lower()


def main() -> None:
    from pd2_merge import D2R_DATA
    gen = ROOT / "generated" / "data"
    lp, lv = load(gen, "lvlprest"), load(gen, "levels")
    mp = load(gen, "monpreset")
    n_d2r = len(load(D2R_DATA, "lvlprest").records())
    lvl = {int(r[col(lv, "Id")]): r for r in lv.rows if r[col(lv, "Id")].isdigit()}
    origin = offsets(gen)
    classes = object_classes(gen)
    places: dict[int, list[str]] = {}
    for r in mp.rows:
        if r[0].isdigit():
            places.setdefault(int(r[0]), []).append(r[1])
    preset_dir = gen / "hd" / "env" / "preset"
    _, standins, _ = presets.read_rule(ROOT / "merge" / "presets-hd.tsv")
    tiles, pd2_tiles = presets.Tiles(D2R_DATA / "global" / "tiles"), presets.Tiles(gen / "global" / "tiles")
    dt1s = {name: files for name, _, files, _ in presets.pd2_only_presets(gen, n_d2r)}
    maps: dict[tuple[int, str], dict] = {}
    for k in lp.records()[n_d2r:]:
        r = lp.rows[k]
        lid = int(r[col(lp, "LevelId")])
        if lid not in lvl:
            continue
        files = [r[col(lp, f"File{n}")] for n in range(1, 7) if r[col(lp, f"File{n}")] not in ("", "0")]
        built = [f for f in files if (preset_dir / Path(norm(f)).with_suffix(".json")).is_file()]
        if built:
            maps[(lid, r[0])] = {"files": files, "built": built}
    by_act: dict[int, list] = {}
    for (lid, name), m in sorted(maps.items()):
        L = lvl[lid]
        act = int(L[col(lv, "Act")] or 0)
        by_act.setdefault(act, []).append((lid, name, L, m))
    total = sum(len(v) for v in by_act.values())
    variants = len({f.lower() for v in by_act.values() for *_, m in v for f in m["built"]})
    out = [HEADER.format(n=total, ds1s=variants)]
    for act in sorted(by_act):
        out.append(f"\n## {ACTS.get(act, f'Act {act + 1}')}\n")
        for lid, name, L, m in by_act[act]:
            _, ox, oy = origin[lid]
            fixed = ox >= 0 and oy >= 0
            multi = len(m["files"]) > 1
            out.append(f"### {L[col(lv, 'Name')]} (LevelId {lid}, lvlprest `{name}`)\n")
            if not fixed:
                out.append(f"Placed at random by the level generator (OffsetX/Y {ox},{oy}): no fixed origin. Tile "
                           f"coordinates below are in-map; find the origin by warping in with `cheat goto {lid}` and "
                           "reading your subtile position, then subtract the entry tile's offset.\n")
            for ds1 in m["files"]:
                path = gen / "global" / "tiles" / Path(norm(ds1))
                if not path.is_file():
                    out.append(f"- `{ds1}`: DS1 not in generated/, skipped\n")
                    continue
                data = path.read_bytes()
                _v, w, h, dact, *_ = ds1_header(data)
                sel = f" (pin: `pd2_force_ds1.py --slot <N> \"{selector(ds1)}\"`)" if multi else ""
                built = "" if ds1 in m["built"] else " (no HD preset generated for this variant)"
                out.append(f"- **{ds1.rsplit('/', 1)[-1]}**{sel}, {w}x{h} tiles{built}")
                lay = None
                try:
                    lay = presets.layout(data, presets.stand_in(dt1s.get(name, m["files"]), tiles, pd2_tiles, standins))
                except ValueError as e:  # a DS1 older than version 4: layout() cannot read its layers
                    out.append(f"  - (no walkable-tile line: {e})")
                spots = reference_spots(data, lay, classes, places)
                seen = set()
                for label, tx, ty in spots:
                    if (tx, ty) in seen:
                        continue
                    seen.add((tx, ty))
                    if fixed:
                        out.append(f"  - {label} (tile {tx},{ty}): "
                                   f"`cheat goto {lid},{subtile(ox, tx)},{subtile(oy, ty)}`")
                    else:
                        out.append(f"  - {label}: tile {tx},{ty} (in-map; origin unknown)")
                if fixed:
                    out.append(f"  - entry: `cheat goto {lid}`")
                out.append("")
    DOC.write_text("\n".join(out) + "\n", "utf-8")
    print(f"{total} maps, wrote {DOC}")


HEADER = """# Warp locations for PD2-specific HD maps

Ready-to-paste `cheat goto <levelId>,<x>,<y>` lines for the {n} PD2-specific maps (levels) whose HD presets the
`presets-hd` rule generates ({ds1s} DS1 variants). Generated by `python tools/pd2_warps.py` after
`python tools/pd2_merge.py` (it reads `generated/`); do not edit by hand.

- `cheat goto <levelId>` alone lands at the level's entry. The x,y lines are absolute subtile coordinates:
  `x = OffsetX*5 + tileX*5 + 2` (DS1 tile) or `x = OffsetX*5 + hdX/2` (HD preset position), the same for y with
  OffsetY, from the merged `levels.txt`. Unit-tested in `tools/tests/test_warps.py`.
- A map with several DS1 variants rolls one per game. Pin one first, game closed (`docs/agents/game-slots.md`,
  "Force a DS1 variant"), or the warp can land on another layout.
- "nearest walkable to centre" is the DS1 tile nearest the centre that is walkable floor (`presets.walkable`: a
  floor tile with no wall, not marked unwalkable and not mostly blocked), so a centre can be rock or water.
- Test in Hell on a level 90+ character (`AGENTS.md`).
"""

if __name__ == "__main__":
    main()
