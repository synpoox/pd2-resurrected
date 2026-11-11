"""Write docs/agents/warp-locations.md: `cheat goto` lines for every PD2-specific map the presets-hd rule generates.

    python tools/pd2_merge.py && python tools/pd2_warps.py          # rewrites the doc

A map is PD2-specific when it has an appended lvlprest row (past D2R's) whose generated HD preset exists under
generated/data/hd/env/preset/. The tables and DS1s come from generated/ (what the merge wrote). `cheat goto` takes
absolute subtile coordinates (cheat-commands.md): see subtile() and hd_subtile().
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from pd2_ids import Table, load  # noqa: E402
from pd2_levels import col, ds1_header, norm, object_list  # noqa: E402

DOC = ROOT / "docs" / "agents" / "warp-locations.md"
ACTS = {0: "Act 1", 1: "Act 2", 2: "Act 3", 3: "Act 4", 4: "Act 5"}


def subtile(offset: int, tile: int) -> int:
    """Absolute subtile coordinate of the middle of DS1 tile `tile` on a level at `offset` (OffsetX or OffsetY)."""
    return offset * 5 + tile * 5 + 2


def hd_subtile(offset: int, hd: float) -> int:
    """Absolute subtile coordinate of an HD preset position (10 units per tile, 2 per subtile)."""
    return int(offset * 5 + hd / 2)


def read_objects(data: bytes) -> list[tuple[int, int, int, int]]:
    """A DS1's objects: (type, id, x, y), x and y in subtiles of the DS1."""
    _act, at, count, size = object_list(data)
    return [struct.unpack_from("<4i", data, at + 4 + k * size) for k in range(count)]


def walkable_near(data: bytes, cx: int, cy: int) -> tuple[int, int]:
    """The DS1 tile nearest (cx, cy) that no wall or floor cell marks unwalkable and that has a floor tile."""
    version, w, h, _a, _t, o, walls, floors = ds1_header(data)
    n = w * h
    from presets.consts import UNWALKABLE
    grids = [struct.unpack_from(f"<{n}I", data, o + k * n * 4) for k in range(2 * walls + floors)]
    cells = [grids[2 * k] for k in range(walls)] + list(grids[2 * walls:])
    floor = grids[2 * walls:]
    best = None
    for i in range(n):
        if any(g[i] & UNWALKABLE for g in cells) or not any(g[i] for g in floor):
            continue
        x, y = i % w, i // w
        d = (x - cx) ** 2 + (y - cy) ** 2
        if best is None or d < best[0]:
            best = (d, x, y)
    return (best[1], best[2]) if best else (cx, cy)


def selector(ds1: str, variants: list[str]) -> str:
    return ds1.rsplit("/", 1)[-1].lower()


def main() -> None:
    from pd2_merge import D2R_DATA
    gen = ROOT / "generated" / "data"
    lp, lv = load(gen, "lvlprest"), load(gen, "levels")
    mp, op = load(gen, "monpreset"), load(gen, "objpreset")
    n_d2r = len(load(D2R_DATA, "lvlprest").records())
    lvl = {int(r[col(lv, "Id")]): r for r in lv.rows if r[col(lv, "Id")].isdigit()}
    classes = {(int(r[1]), int(r[0])): r[2] for r in op.rows if r[0].isdigit() and r[1].isdigit()}
    places: dict[int, list[str]] = {}
    for r in mp.rows:
        if r[0].isdigit():
            places.setdefault(int(r[0]), []).append(r[1])
    preset_dir = gen / "hd" / "env" / "preset"
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
    out = [HEADER.format(n=total, ds1s=len({f.lower() for v in by_act.values() for *_, m in v for f in m['built']}))]
    for act in sorted(by_act):
        out.append(f"\n## {ACTS.get(act, f'Act {act + 1}')}\n")
        for lid, name, L, m in by_act[act]:
            ox, oy = int(L[col(lv, "OffsetX")]), int(L[col(lv, "OffsetY")])
            fixed = ox >= 0 and oy >= 0
            multi = len(m["files"]) > 1
            out.append(f"### {L[col(lv, 'Name')]} (LevelId {lid}, lvlprest `{name}`)\n")
            if not fixed:
                out.append(f"Placed at random by the level generator (OffsetX/Y {ox},{oy}): no fixed origin. Tile coordinates below are "
                           "in-map; find the origin by warping in with `cheat goto "
                           f"{lid}` and reading your subtile position, then subtract the entry tile's offset.\n")
            for ds1 in m["files"]:
                data = (gen / "global" / "tiles" / Path(norm(ds1))).read_bytes() if (gen / "global" / "tiles" / Path(norm(ds1))).is_file() else None
                if data is None:
                    out.append(f"- `{ds1}`: DS1 not in generated/, skipped\n")
                    continue
                _v, w, h, dact, *_ = ds1_header(data)
                sel = f" (pin: `pd2_force_ds1.py --slot <N> \"{selector(ds1, m['files'])}\"`)" if multi else ""
                built = "" if ds1 in m["built"] else " (no HD preset generated for this variant)"
                out.append(f"- **{ds1.rsplit('/', 1)[-1]}**{sel}, {w}x{h} tiles{built}")
                tx, ty = walkable_near(data, w // 2, h // 2)
                spots = [("centre", w // 2, h // 2), ("nearest walkable to centre", tx, ty)]
                for kind, oid, x, y in read_objects(data):
                    if kind == 2 and "waypoint" in classes.get((dact + 1, oid), "").lower():
                        spots.append(("waypoint", x // 5, y // 5))
                    if kind == 1 and oid < len(places.get(dact + 1, [])) and any(
                            s in places[dact + 1][oid].lower() for s in ("boss", "unique", "diablo", "baal", "mephisto")):
                        spots.append((f"monster preset {places[dact + 1][oid]}", x // 5, y // 5))
                seen = set()
                for label, tx, ty in spots:
                    if (tx, ty) in seen:
                        continue
                    seen.add((tx, ty))
                    if fixed:
                        out.append(f"  - {label} (tile {tx},{ty}): `cheat goto {lid},{subtile(ox, tx)},{subtile(oy, ty)}`")
                    else:
                        out.append(f"  - {label}: tile {tx},{ty} (in-map; origin unknown)")
                if fixed:
                    out.append(f"  - entry: `cheat goto {lid}`")
                out.append("")
    DOC.write_text("\n".join(out) + "\n", "utf-8")
    print(f"{total} maps, wrote {DOC}")


HEADER = """# Warp locations for PD2-specific HD maps

Ready-to-paste `cheat goto <levelId>,<x>,<y>` lines for the {n} PD2-specific maps (levels) whose HD presets the
`presets-hd` rule generates ({ds1s} DS1 variants). Generated by `python tools/pd2_warps.py` after `python tools/pd2_merge.py`
(it reads `generated/`); do not edit by hand.

- `cheat goto <levelId>` alone lands at the level's entry. The x,y lines are absolute subtile coordinates:
  `x = OffsetX*5 + tileX*5 + 2` (DS1 tile) or `x = OffsetX*5 + hdX/2` (HD preset position), the same for y with
  OffsetY, from the merged `levels.txt`. Unit-tested in `tools/tests/test_warps.py`.
- A map with several DS1 variants rolls one per game. Pin one first, game closed (`docs/agents/game-slots.md`,
  "Force a DS1 variant"), or the warp can land on another layout.
- "nearest walkable to centre" is the DS1 tile nearest the centre with a floor tile that no wall or floor cell marks
  unwalkable: a centre can be rock.
- Test in Hell on a level 90+ character (`AGENTS.md`).
"""

if __name__ == "__main__":
    main()
