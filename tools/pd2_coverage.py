"""No-game coverage report for the generated HD presets of PD2-only areas (LP3c, #672).

Run after pd2_merge.py. Each entity's model box (D2R's data/hd/model_lod_desc.bin, prefabs followed TWO levels, their
transforms composed) is placed on the DS1's tiles (pd2_presets.TILE HD units each), giving the tiles under a piece at
least TALL units high. Per generated preset:

- wall score: the share of DS1 wall tiles (what the automap draws as walls) under a tall piece;
- border score, for a DS1 that draws trees (pd2_presets.draws_trees): the ring of DS1 tiles 4-adjacent to and
  outside pd2_painter.playable() (the map's edge on the automap). A ring tile is covered (under a tall piece), a gap
  (bare), and a leak if the DS1 lets the player walk on it.

Writes <out>/report.md, worst first (border score, then wall score), and per preset <name>_walls.png (walls green
where covered, red where bare) and, for a border, <name>_border.png (ring green, gap red, leak magenta). Known bias:
a turned piece's footprint is its axis-aligned box, which spills onto neighbour tiles, so both scores run a little
generous. TALL 4 reproduced the LP3 slice 1 game check (research/lp3-tile-table, FINDINGS.md). The report cannot judge
looks, frame rate or collision.

Changes against main: BASELINE (docs/coverage-baseline.tsv, `preset <tab> wall % <tab> border % or -`) holds main's
scores. The report and <out>/changes.md list each preset whose score moved, a new one, and one no longer scored.
`--check` fails (exit 1) when a preset's wall or border score drops more than TOLERANCE points below the baseline, or
a baselined preset is no longer scored. To accept an intended drop (or record a gain), run with `--write-baseline`
after the merge and commit docs/coverage-baseline.tsv with the PR: the drop then shows in its diff and review.
Needs model_lod_desc.bin in modkit's cache, else it skips:
`python -m modkit fetch --prefix data/hd/model_lod_desc.bin`.
"""
from __future__ import annotations

import argparse
import functools
import json
import math
import os
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterator

import pd2_painter as painter
import pd2_presets as presets
from pd2_ids import load, toolkit

ROOT = Path(__file__).resolve().parent.parent
D2R_DATA = Path(os.environ.get("MODKIT_CACHE", toolkit(ROOT) / "d2r-data")) / "93847" / "data"
MODELS = Path("hd") / "model_lod_desc.bin"
TALL = 4.0  # HD units: a piece this high hides a wall in game
ROOM = 400  # HD units: a box wider than this is a room-sized mesh (water, sculpted terrain), not a wall piece
DEPTH = 2  # prefab levels followed
BASELINE = ROOT / "docs" / "coverage-baseline.tsv"
TOLERANCE = 1.0  # percentage points a score may drop below the baseline
IDENTITY = {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}

Box = tuple[float, float, float, float, float]  # x0, x1, z0, z1, top


def fnv(path: str) -> int:
    """model_lod_desc.bin's key for a model path: 32-bit FNV-1a of the lower-cased path."""
    h = 0x811C9DC5
    for b in path.lower().encode():
        h = ((h ^ b) * 0x1000193) & 0xFFFFFFFF
    return h


def model_boxes(raw: bytes) -> dict[int, tuple[float, ...]]:
    """model_lod_desc.bin: `u32 count | i32 offset`, then from 4 + offset, 44-byte records
    `u32 key | f32 bbMin[3] | f32 bbMax[3] | ...`. key -> (min x, y, z, max x, y, z)."""
    count, off = struct.unpack_from("<Ii", raw, 0)
    return {r[0]: r[1:] for r in (struct.unpack_from("<I6f", raw, 4 + off + 44 * i) for i in range(count))}


def qmul(a: dict, b: dict) -> dict:
    """The quaternion a * b (b turned, then a)."""
    ax, ay, az, aw = a["x"], a["y"], a["z"], a["w"]
    bx, by, bz, bw = b["x"], b["y"], b["z"], b["w"]
    return {"w": aw * bw - ax * bx - ay * by - az * bz, "x": aw * bx + ax * bw + ay * bz - az * by,
            "y": aw * by - ax * bz + ay * bw + az * bx, "z": aw * bz + ax * by - ay * bx + az * bw}


def boxes(entities: list[dict], box: Callable[[str], tuple | None], prefab: Callable[[str], list[dict]],
          base: tuple | None = None, depth: int = 0) -> Iterator[Box]:
    """The world boxes of the entities' models; a prefab's entities are placed by the prefab's transform."""
    for e in entities:
        t = presets._transform(e)
        if t is None or e.get("name") == "terrain":
            continue
        p = (t["position"]["x"], t["position"]["y"], t["position"]["z"])
        q, sc = t.get("orientation") or IDENTITY, t.get("scale") or {"x": 1, "y": 1, "z": 1}
        s = (sc["x"], sc["y"], sc["z"])
        if base:
            bp, bq, bs = base
            v = painter.rotate(bq, (p[0] * bs[0], p[1] * bs[1], p[2] * bs[2]))
            p, q, s = (bp[0] + v[0], bp[1] + v[1], bp[2] + v[2]), qmul(bq, q), (s[0] * bs[0], s[1] * bs[1], s[2] * bs[2])
        for c in e["components"]:
            if c["type"] == "ModelDefinitionComponent" and (bb := box(c.get("filename", ""))):
                corners = [painter.rotate(q, (x * s[0], y * s[1], z * s[2]))
                           for x in (bb[0], bb[3]) for y in (bb[1], bb[4]) for z in (bb[2], bb[5])]
                xs, ys, zs = ([p[i] + v[i] for v in corners] for i in range(3))
                yield min(xs), max(xs), min(zs), max(zs), max(ys)
            elif c["type"] == "PrefabPlacementDefinitionComponent" and c.get("prefab") and depth < DEPTH:
                yield from boxes(prefab(c["prefab"]), box, prefab, (p, q, s), depth + 1)


def tall_tiles(found: Iterator[Box], tall: float = TALL) -> set[tuple[int, int]]:
    """The tiles under the footprint of a box at least `tall` high (room-sized boxes left out)."""
    out = set()
    T = presets.TILE
    for x0, x1, z0, z1, top in found:
        if top < tall or x1 - x0 > ROOM or z1 - z0 > ROOM:
            continue
        out.update((tx, tz) for tx in range(math.floor(x0 / T), math.floor(x1 / T) + 1)
                   for tz in range(math.floor(z0 / T), math.floor(z1 / T) + 1))
    return out


@dataclass
class Score:
    name: str
    area: str  # the lvlprest row (Name), e.g. Ashen Plains Map
    walls: int  # DS1 wall tiles
    bare: set  # wall tiles under no tall piece
    ring: set | None = None  # border ring tiles, None for a DS1 without trees
    gaps: set = frozenset()
    leaks: set = frozenset()

    @property
    def wall_score(self) -> float:
        return 1 - len(self.bare) / self.walls if self.walls else 1.0

    @property
    def border_score(self) -> float | None:
        return None if self.ring is None else 1 - len(self.gaps) / len(self.ring) if self.ring else 1.0


def score(lay: presets.Layout, tall: set, name: str = "", area: str = "") -> Score:
    walls = {c for c, k in lay.keys.items() if k[0] == "w"}
    s = Score(name, area, len(walls), walls - tall)
    if presets.draws_trees(lay):
        play = painter.playable(lay)
        ring = ({(x + dx, y + dy) for x, y in play for dx, dy in painter.STEPS} - play) & set(lay.keys)
        s.ring, s.gaps = ring, ring - tall
        s.leaks = {c for c in ring if c not in lay.unwalkable and not any(b & 1 for b in lay.flags.get(c, bytes(25)))}
    return s


def png(w: int, h: int, px: dict[tuple[int, int], tuple[int, int, int]], back=(20, 20, 20), zoom: int = 3) -> bytes:
    """An RGB PNG of w x h cells, `zoom` pixels each (no Pillow: the unit job has none)."""
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + b"".join(bytes(px.get((x // zoom, y // zoom), back)) for x in range(w * zoom))
                    for y in range(h * zoom))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", w * zoom, h * zoom, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def images(lay: presets.Layout, s: Score, tall: set) -> dict[str, bytes]:
    """<name>_walls.png and, with a border, <name>_border.png."""
    base = {c: (80, 75, 60) for c in lay.keys} | {c: (40, 90, 160) for c in tall if c in lay.keys}
    walls = {c: (230, 40, 40) if c in s.bare else (60, 210, 60) for c, k in lay.keys.items() if k[0] == "w"}
    out = {f"{s.name}_walls.png": png(lay.width, lay.height, base | walls)}
    if s.ring is not None:
        px = {c: (45, 40, 32) for c in lay.keys} | {c: (120, 112, 95) for c in painter.playable(lay)}
        px |= {c: (230, 40, 200) if c in s.leaks else (230, 40, 40) if c in s.gaps else (50, 200, 60) for c in s.ring}
        out[f"{s.name}_border.png"] = png(lay.width, lay.height, px, zoom=4)
    return out


def _key(s: Score) -> tuple:
    return (s.border_score is None, s.border_score or 0.0, s.wall_score, s.name)


def pct1(v: float | None) -> float | None:
    """A score as the baseline keeps it: a percentage to one decimal."""
    return None if v is None else round(100 * v, 1)


def baseline_text(scores: list[Score]) -> str:
    lines = ["# HD preset coverage on main (tools/pd2_coverage.py --write-baseline): preset, wall %, border % or -"]
    for s in sorted(scores, key=lambda s: s.name):
        b = pct1(s.border_score)
        lines.append(f"{s.name}\t{pct1(s.wall_score)}\t{'-' if b is None else b}")
    return "\n".join(lines) + "\n"


def read_baseline(text: str) -> dict[str, tuple[float, float | None]]:
    out = {}
    for line in text.splitlines():
        if line and not line.startswith("#"):
            name, wall, border = line.split("\t")
            out[name] = (float(wall), None if border == "-" else float(border))
    return out


@dataclass
class Change:
    name: str
    wall: tuple[float | None, float | None]  # (baseline, now); None: absent
    border: tuple[float | None, float | None]
    drop: bool  # beyond TOLERANCE, or no longer scored


def changes(scores: list[Score], base: dict, tolerance: float = TOLERANCE) -> list[Change]:
    """The presets whose scores differ from the baseline, worst drop first."""
    now = {s.name: (pct1(s.wall_score), pct1(s.border_score)) for s in scores}
    out = []
    for name in sorted(set(now) | set(base)):
        (w0, b0), (w1, b1) = base.get(name, (None, None)), now.get(name, (None, None))
        if name in base and name in now and (w0, b0) == (w1, b1):
            continue
        gone = name not in now
        drop = gone or any(old is not None and new is not None and old - new > tolerance
                           for old, new in ((w0, w1), (b0, b1)))
        out.append(Change(name, (w0, w1), (b0, b1), drop))
    def worst(c: Change) -> float:
        return min((n - o for o, n in (c.wall, c.border) if o is not None and n is not None), default=0.0)
    return sorted(out, key=lambda c: (not c.drop, worst(c), c.name))


def changes_md(found: list[Change], tolerance: float = TOLERANCE) -> str:
    def cell(old, new) -> str:
        if old is None and new is None:
            return "-"
        if old is None:
            return f"new {new}%"
        if new is None:
            return f"{old}% -> gone"
        return f"{old}% -> {new}% ({new - old:+.1f})" if old != new else f"{new}%"
    drops = sum(c.drop for c in found)
    lines = ["## Changes against main", ""]
    if not found:
        return "\n".join(lines + ["No preset's coverage changed.", ""])
    lines += [f"{len(found)} presets changed; {drops} dropped by more than the {tolerance:g}-point tolerance or are no longer scored"
              + (" (accept an intended drop: `--write-baseline`, commit docs/coverage-baseline.tsv)." if drops else "."),
              "", "| Preset | Wall | Border | |", "|---|---|---|---|"]
    lines += [f"| {c.name} | {cell(*c.wall)} | {cell(*c.border)} | {'**drop**' if c.drop else ''} |" for c in found]
    return "\n".join(lines) + "\n"


def report(scores: list[Score], commit: str = "", found: list[Change] | None = None) -> str:
    """The markdown report, worst first: presets with a border by border score, then the rest by wall score."""
    pct = lambda v: "-" if v is None else f"{100 * v:.1f}%"  # noqa: E731
    lines = ["# HD preset coverage" + (f" ({commit})" if commit else ""), "",
             f"{len(scores)} generated presets. Wall: DS1 wall tiles under a piece >= {TALL:g} units. Border (DS1s "
             "with trees): the ring outside the playable area; gaps bare, leaks walkable in the DS1. "
             "Images: red where bare.", "",
             "| Preset | Area | Border | Gaps | Leaks | Wall | Walls | Images |",
             "|---|---|---|---|---|---|---|---|"]
    if found is not None:
        lines[2:2] = [changes_md(found), ""]
    for s in sorted(scores, key=_key):
        img = f"[walls]({s.name}_walls.png)" + (f" [border]({s.name}_border.png)" if s.ring is not None else "")
        border = s.ring is not None
        lines.append(f"| {s.name} | {s.area} | {pct(s.border_score)} | {len(s.gaps) if border else '-'} | "
                     f"{len(s.leaks) if border else '-'} | "
                     f"{pct(s.wall_score)} | {s.walls} | {img} |")
    if scores:
        n = sum(s.walls for s in scores)
        lines += ["", f"All walls: {pct(1 - sum(len(s.bare) for s in scores) / n if n else 1.0)} of {n}."]
        rings = [s for s in scores if s.ring is not None]
        if r := sum(len(s.ring) for s in rings):
            lines.append(f"All borders: {pct(1 - sum(len(s.gaps) for s in rings) / r)} of {r} ring tiles.")
    return "\n".join(lines) + "\n"


def run(data: Path, d2r: Path, out: Path, commit: str = "", base: dict | None = None) -> list[Score] | None:
    """Score every generated preset under data (generated/data) and write the report to out, with the changes
    against `base` (read_baseline) when given. None: skipped."""
    if not (d2r / MODELS).is_file():
        print(f"pd2_coverage: skipped, no {d2r / MODELS} (`python -m modkit fetch --prefix data/hd/model_lod_desc.bin`)")
        return None
    rule = ROOT / "merge" / "presets-hd.tsv"
    standins = presets.read_rule(rule)[1]
    found = model_boxes((d2r / MODELS).read_bytes())

    @functools.cache
    def prefab(path: str) -> list[dict]:
        f = d2r / path.removeprefix("data/")
        return json.loads(f.read_text("utf-8-sig")).get("entities", []) if f.is_file() else []

    def box(path: str):
        return found.get(fnv(path))
    tiles, pd2_tiles = presets.Tiles(d2r / "global" / "tiles"), presets.Tiles(data / "global" / "tiles")
    out.mkdir(parents=True, exist_ok=True)
    scores, seen = [], set()
    for area, _, files, ds1s in presets.pd2_only_presets(data, len(load(d2r, "lvlprest").records())):
        for ds1 in ds1s:
            f = data / presets.PRESETS / Path(ds1).with_suffix(".json")
            if not f.is_file() or ds1 in seen:  # waiting on stand-ins (LP4/LP5), or a DS1 two presets load
                continue
            seen.add(ds1)
            lay = presets.layout((data / "global" / "tiles" / ds1).read_bytes(),
                                 presets.stand_in(files, tiles, pd2_tiles, standins))
            tall = tall_tiles(boxes(json.loads(f.read_text("utf-8"))["entities"], box, prefab))
            s = score(lay, tall, Path(ds1).stem, area)
            scores.append(s)
            for name, image in images(lay, s, tall).items():
                (out / name).write_bytes(image)
    found = None if base is None else changes(scores, base)
    if found is not None:
        (out / "changes.md").write_text(changes_md(found), "utf-8")
    (out / "report.md").write_text(report(scores, commit, found), "utf-8")
    print(f"pd2_coverage: {len(scores)} presets scored, {out / 'report.md'}")
    return scores


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, default=ROOT / "generated" / "data", help="merged data (pd2_merge's output)")
    ap.add_argument("--d2r", type=Path, default=D2R_DATA, help=f"D2R data root (default: {D2R_DATA})")
    ap.add_argument("--out", type=Path, default=ROOT / "out" / "coverage", help="report folder")
    ap.add_argument("--commit", default="", help="commit named in the report's title")
    ap.add_argument("--baseline", type=Path, default=BASELINE, help=f"main's scores (default: {BASELINE})")
    ap.add_argument("--check", action="store_true", help=f"exit 1 on a drop of more than {TOLERANCE:g} points")
    ap.add_argument("--write-baseline", action="store_true", help="write the scores to --baseline")
    a = ap.parse_args(argv)
    base = read_baseline(a.baseline.read_text("utf-8")) if a.baseline.is_file() else None
    scores = run(a.data, a.d2r, a.out, a.commit, base)
    if scores is None:
        return 0
    if a.write_baseline:
        a.baseline.write_text(baseline_text(scores), "utf-8", newline="\n")
        print(f"pd2_coverage: baseline written, {a.baseline}")
        return 0
    if base is None:
        print(f"pd2_coverage: no baseline at {a.baseline}" + ("" if not a.check else "; nothing to check"))
        return 1 if a.check else 0
    found = changes(scores, base)
    drops = [c.name for c in found if c.drop]
    print(f"pd2_coverage: {len(found)} presets changed against the baseline, {len(drops)} dropped"
          + (f": {', '.join(drops)}" if drops else ""))
    return 1 if a.check and drops else 0


if __name__ == "__main__":
    sys.exit(main())
