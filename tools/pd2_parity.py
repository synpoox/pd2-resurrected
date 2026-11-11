"""One HD:legacy parity number per map (#838, strategy: docs/research/hd-parity-strategy.md on research/hd-parity-strategy).

    py -3 tools/pd2_parity.py [--maps jungle1,tomb1] [--out coverage] [--baseline parity-baseline.tsv] [--check]

Run from the repo root after `tools/pd2_merge.py`. Scores every visible tile of each generated preset against its
legacy DS1 (the Palace is out: it is unplayable). It reuses pd2_coverage (model boxes, prefab walk, ground planes) and
pd2_fidelity (legacy edge classes and the HD typing). One tile can carry several defects; the map's parity counts each
tile once.

Checks, per DS1 tile (TILE = 10 HD units; a piece's tiles are coverage.core_footprint, so a box only grazing a tile
does not count). Only tiles within SIGHT tiles of walkable legacy floor are scored: what the camera can show.

  edge         pd2_fidelity's missing / wrong type / wrong height per legacy edge tile
  solid        walkable floor with no legacy edge on or beside it, under a piece >= TALL standing on the ground
               (a wall or rock you walk through)
  clutter      the same floor under a grounded prop LOW..TALL high (not ground cover)
  occlusion    the same floor under a piece >= TALL whose base is raised (canopy, overhangs): it hides the floor
  floating     a piece whose base is >= FLOAT above the ground plane under it (not a tree, light, chain, banner or
               water surface; pieces of pasted stock rooms are skipped: their own terrain is not the plane; a
               piece 6+ units tall from base to top is skipped too: a tall prop on a slope or a stack reads as
               standing, not floating)
  invisible    legacy draws a wall cell there (not inv_wall, not liquid) and HD draws nothing >= LOW: you bump into
               air (blocked floor legacy draws as plain ground is left out: it looks walkable in legacy too)
  void         legacy (the DS1 with PD2's own DT1s) draws nothing there (no floor, no wall), HD draws a ground
               plane or a piece (#783)
  liquid       legacy liquid floor under no water or lava model, any size

parity = 1 - sum(w(t) over bad tiles) / sum(w(t) over visible tiles). w is 3 on a legacy edge tile (what the eye
follows), 2 on walkable floor (a defect there misleads play), 1 elsewhere. There is no coverage term: drawing more
pieces cannot raise the number, drawing fewer shows up as edge, invisible or liquid. Floor and wall coverage
(pd2_coverage) stay an information report, not a gate.

Outputs in --out: parity.md (the step summary: mean, worst maps, a PR's changes against main, a table of every map),
parity-baseline.tsv (--write-baseline: main's scores, uploaded by the gate on every push to main) and <map>_parity.png (each tile coloured by its worst defect; green: right, dim: other drawn tiles;
red edge, orange solid, cyan invisible, purple void, yellow floating, blue occlusion, pink clutter, teal liquid).

`--check` exits 1 when a map's parity falls more than --tolerance points below the baseline, or a baselined map is
no longer scored. The gate passes a drop when the PR carries the label parity-drop-ok.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import pd2_coverage as coverage  # noqa: E402
import pd2_fidelity as fidelity  # noqa: E402
import pd2_presets as presets  # noqa: E402
from pd2_ids import load  # noqa: E402

LOW, TALL = coverage.LOW, coverage.TALL  # 1 and 4 HD units, as the coverage gate
SIGHT = 7  # tiles from walkable floor the camera shows (D2R HD at 1080p: about 7 tiles each way from the player)
FLOAT = 1.0  # HD units of air under a piece's base that the eye reads as floating (a tile is 10 units)
# points a map's parity may fall below main's and still pass --check. 2, not 1, until the weights are calibrated against
# the owner's grades (P10): one shared-pass fix often trades one defect for another, and on a small map 1 point is a
# handful of tiles. Tighten after P10.
TOLERANCE = 2.0
NOT_FLOATING = ("tree", "palm", "canopy", "light", "lamp", "candle", "torch", "chain", "banner", "flag", "beam",
                "bird", "bat", "fx", "vfx", "particle", "cloud", "fog", "ray", "god", "dust", "smoke", "hanging",
                "vine", "web", "roof", "ceiling")
WATERY = ("water", "river", "lava", "pond", "lake", "swamp", "oasis", "puddle", "stream", "canal", "flow", "pool")
WEIGHT = {"edge": 3.0, "walk": 2.0, "other": 1.0}
CHECKS = ("edge", "solid", "invisible", "void", "floating", "occlusion", "clutter", "liquid")
COLOUR = {"ok": (70, 160, 70), "edge": (230, 40, 40), "solid": (240, 120, 20), "invisible": (40, 200, 230),
          "void": (150, 60, 220), "floating": (250, 230, 40), "occlusion": (30, 70, 220), "clutter": (240, 140, 200),
          "liquid": (20, 220, 180), "bg": (20, 20, 20), "plain": (55, 50, 42)}
BASELINE_FILE = "parity-baseline.tsv"
MAIN_DIR = "main-parity"  # under --out: main's baseline artifact, downloaded for a PR


def score(name, lay, legacy, preset, box, prefab, heights, stock: dict | None = None) -> dict:
    """The parity of one map: `lay` is the layout with stand-in DT1s (what HD builds from), `legacy` the layout with
    PD2's own DT1s (what legacy draws). `stock` is fidelity.stock_types (the stock vote's folder types, #822)."""
    W, H = lay.width, lay.height
    grid = {(x, y) for x in range(W) for y in range(H)}
    ground = coverage.plane_ground(preset)
    edges_by_kind = fidelity.legacy_edges(legacy, heights)
    edge_tiles = {c for cs in edges_by_kind.values() for c in cs}
    walkable = presets.walkable(lay)
    near_edge = {(x + dx, y + dy) for x, y in edge_tiles for dx, dy in fidelity.STEPS8} | edge_tiles
    open_floor = walkable - near_edge  # walkable floor where legacy draws no edge on or beside the tile
    void = {c for c in grid if c not in legacy.keys or all(t[0] is None for t in legacy.keys[c][1])}
    liquid = {c for c, (k, ts) in legacy.keys.items() if k == "f" and any(
        fidelity.liquid_floor(t, legacy.flags.get(c, bytes(25)), heights) for t in ts)}
    invisible_dt1 = {c for c, (k, ts) in legacy.keys.items()
                     if any(t[0] and fidelity.INVISIBLE.search(t[0].lower()) for t in ts)}
    # what legacy DRAWS as an obstacle: a wall cell. Blocked floor that legacy draws as plain ground (past a tree line,
    # the dashed tree lines' gaps) looks walkable in legacy too, so HD plain ground there is no mismatch.
    blocked = {c for c, (k, _) in lay.keys.items() if k == "w"} - void - liquid - invisible_dt1

    d: dict[str, set] = {k: set() for k in CHECKS}
    drawn_any, watery, floats = set(), set(), Counter()
    hd = fidelity.Hd()
    for x0, x1, z0, z1, top, bottom, path, in_room in coverage.boxes(
            preset["entities"], box, prefab, ground=ground, meta=True):
        b = (x0, x1, z0, z1, top, bottom)
        big = x1 - x0 > coverage.ROOM or z1 - z0 > coverage.ROOM
        core = coverage.footprint([b]) if big else coverage.core_footprint(b)
        if any(w in path for w in WATERY):
            watery |= coverage.footprint([b])
        if big:
            continue
        if top >= LOW:
            drawn_any |= core
        cover = bool(fidelity.GROUND_COVER.search("/".join(path.split("/")[-2:])))
        types = fidelity.model_types(path, stock)
        hd.add(b, types, fidelity.model_type(path), fidelity.folder_of(path))  # the edge check's pieces
        if (not in_room and bottom >= FLOAT and top - bottom < 60
                and not any(w in path for w in NOT_FLOATING + WATERY) and not cover):
            d["floating"] |= core
            floats[fidelity.folder_of(path)] += 1
        on_ground = bottom < FLOAT
        if top >= TALL and on_ground and "treeline" not in types:
            d["solid"] |= core & open_floor
        elif top >= TALL:
            d["occlusion"] |= core & open_floor
        elif top >= LOW and not cover and on_ground:
            d["clutter"] |= core & open_floor
    d["occlusion"] -= d["solid"]
    planes = coverage.ground_tiles(preset, box, prefab)
    d["void"] = void & (planes | drawn_any)
    d["invisible"] = blocked - drawn_any - watery
    d["liquid"] = liquid - watery
    fid = {k: fidelity.score_type(k, cs, hd) for k, cs in edges_by_kind.items() if cs}
    for s in fid.values():
        d["edge"] |= s.missing | s.wrong_type | s.wrong_height

    visible = {(x + dx, y + dy) for x, y in walkable for dx in range(-SIGHT, SIGHT + 1)
               for dy in range(-SIGHT, SIGHT + 1)} & grid

    def weight(c):
        return WEIGHT["edge"] if c in edge_tiles else WEIGHT["walk"] if c in walkable else WEIGHT["other"]
    bad = set().union(*d.values()) & visible
    total = sum(map(weight, visible))
    parity = 1 - sum(map(weight, bad)) / total if total else 1.0
    return {"name": name, "parity": parity, "tiles": len(visible), "walkable": len(walkable), "edges": len(edge_tiles),
            "defects": {k: len(v & visible) for k, v in d.items()}, "sets": d,
            "floats": floats.most_common(6), "visible": visible, "edge_tiles": edge_tiles, "walkable_set": walkable}


def wanted(stem: str, only: set[str] = frozenset()) -> bool:
    """Whether a DS1 is scored: not the Palace (palace1-6), and in `only` when that is given."""
    return stem not in coverage.UNPLAYABLE and (not only or stem in only)


def worst_check(r: dict) -> str:
    """The check that flags the most visible tiles of a map ('-' when none does)."""
    k = max(CHECKS, key=lambda k: r["defects"][k])
    return k if r["defects"][k] else "-"


def image(lay, r: dict) -> bytes:
    px = {c: COLOUR["plain"] for c in r["visible"]}
    order = ("clutter", "occlusion", "liquid", "floating", "void", "invisible", "solid", "edge")  # worst drawn last
    for c in r["edge_tiles"] | r["walkable_set"]:
        px[c] = COLOUR["ok"]
    for k in order:
        for c in r["sets"][k] & r["visible"]:
            px[c] = COLOUR[k]
    return coverage.png(lay.width, lay.height, px, COLOUR["bg"], zoom=3)


# --- baseline, changes, report -------------------------------------------------------------------------------------

def mean(rows: list[dict]) -> float:
    return sum(r["parity"] for r in rows) / len(rows) if rows else 0.0


def baseline_text(rows: list[dict]) -> str:
    lines = ["# HD:legacy parity on main (tools/pd2_parity.py --write-baseline): map, parity %, defect tiles per check "
             + ", ".join(CHECKS)]
    lines += [f"{r['name']}\t{100 * r['parity']:.1f}\t" + "\t".join(str(r["defects"][k]) for k in CHECKS)
              for r in sorted(rows, key=lambda r: r["name"])]
    return "\n".join(lines) + "\n"


def read_baseline(text: str) -> dict[str, tuple[float, dict[str, int]]]:
    """map -> (parity in percent, defect tiles per check)."""
    out = {}
    for line in text.splitlines():
        if line.strip() and not line.startswith("#"):
            f = line.split("\t")
            out[f[0]] = (float(f[1]), {k: int(v) for k, v in zip(CHECKS, f[2:])})
    return out


@dataclass
class Change:
    name: str
    old: float | None  # percent; None: not in the baseline (new map)
    new: float | None  # percent; None: no longer scored
    drop: bool  # fell more than the tolerance, or no longer scored
    deltas: dict[str, int] = field(default_factory=dict)  # defect tiles per check, now minus main (only the ones that moved)


def changes(rows: list[dict], base: dict, tolerance: float = TOLERANCE) -> list[Change]:
    """The maps whose parity differs from the baseline by a displayed point or more, worst drop first."""
    now = {r["name"]: round(100 * r["parity"], 1) for r in rows}
    defects = {r["name"]: r["defects"] for r in rows}
    found = []
    for name in sorted(now.keys() | base.keys()):
        old, new = base.get(name, (None, {}))[0], now.get(name)
        if old is None or new is None:
            found.append(Change(name, old, new, new is None))
        elif new != old:
            was = base[name][1]
            found.append(Change(name, old, new, old - new > tolerance,
                                {k: defects[name].get(k, 0) - was.get(k, 0) for k in CHECKS
                                 if defects[name].get(k, 0) != was.get(k, 0)}))
    return sorted(found, key=lambda c: (c.new if c.new is not None else -1) - (c.old if c.old is not None else 100))


def report_md(rows: list[dict], commit: str = "", found: list[Change] | None = None,
              tolerance: float = TOLERANCE, note: str = "") -> str:
    worst = sorted(rows, key=lambda r: r["parity"])
    title = f"## HD:legacy parity{f' at {commit[:9]}' if commit else ''}"
    lines = [title, "",
             f"Mean parity **{100 * mean(rows):.1f}%** over {len(rows)} variants (Palace excluded). The weakest: "
             + ", ".join(f"{r['name']} {100 * r['parity']:.1f}%" for r in worst[:5]) + ".", ""]
    if note:
        lines += [f"**{note}**", ""]
    if found is not None:
        drops = [c for c in found if c.drop]
        lines += [f"Against main's baseline: {len(found)} maps changed, {len(drops)} dropped more than "
                  f"{tolerance:g} point{'s' if tolerance != 1 else ''} (a drop fails the gate; to accept an intended "
                  "one, label the PR `parity-drop-ok`).", ""]
        if found:
            lines += ["| map | main | now | change | defect tiles vs main |", "|---|--:|--:|--:|---|"]
            for c in found:
                o = "new" if c.old is None else f"{c.old:.1f}%"
                n = "gone" if c.new is None else f"{c.new:.1f}%"
                dl = "" if c.old is None or c.new is None else f"{c.new - c.old:+.1f}"
                dt = ", ".join(f"{k} {v:+d}" for k, v in c.deltas.items())
                lines.append(f"| {c.name} | {o} | {n} | {dl}{' FAIL' if c.drop else ''} | {dt} |")
            lines.append("")
    lines += ["Defect tiles are visible tiles flagged by each check (a tile can be flagged by several; it counts once "
              "in the parity). Floor and wall coverage stay in report.md, information only.", "",
              "| map | parity | worst | " + " | ".join(CHECKS) + " |", "|---|--:|---|" + "--:|" * len(CHECKS)]
    for r in worst:
        lines.append(f"| {r['name']} | {100 * r['parity']:.1f}% | {worst_check(r)} | "
                     + " | ".join(str(r["defects"][k]) for k in CHECKS) + " |")
    return "\n".join(lines) + "\n"


# --- run ------------------------------------------------------------------------------------------------------------

def run(data: Path, d2r: Path, out: Path | None, only: set[str] = frozenset(), images: bool = True) -> list[dict] | None:
    """Score every generated preset under data (generated/data) except the unplayable Palace; None: skipped (no model boxes)."""
    if not (d2r / coverage.MODELS).is_file():
        print(f"::warning::pd2_parity: skipped, no {d2r / coverage.MODELS} (`python -m modkit fetch --prefix data/hd/model_lod_desc.bin`)")
        return None
    box, prefab = coverage.model_lookup(d2r)
    votes = fidelity.VOTES_FILE.read_text("utf-8")
    if warn := fidelity.votes_stale(votes, fidelity.votes_input_hash(d2r)):
        print(f"::warning::pd2_parity: {warn}", file=sys.stderr)
    stock = fidelity.stock_types(fidelity.read_votes(votes))
    _, standins, _ = presets.read_rule(ROOT / "merge" / "presets-hd.tsv")
    tiles, pd2_tiles = presets.Tiles(d2r / "global" / "tiles"), presets.Tiles(data / "global" / "tiles")
    heights = fidelity.Heights(data / "global" / "tiles", d2r / "global" / "tiles")
    rows, seen = [], set()
    for area, _, files, ds1s in presets.pd2_only_presets(data, len(load(d2r, "lvlprest").records())):
        for ds1 in ds1s:
            stem = Path(ds1).stem.lower()
            f = data / presets.PRESETS / Path(ds1).with_suffix(".json")
            if ds1 in seen or not f.is_file() or not wanted(stem, only):
                continue
            seen.add(ds1)
            raw = (data / "global" / "tiles" / ds1).read_bytes()
            lay = presets.layout(raw, presets.stand_in(files, tiles, pd2_tiles, standins))
            legacy = presets.layout(raw, {p: pd2_tiles[p] or tiles[p] for p in files})
            r = score(stem, lay, legacy, json.loads(f.read_text("utf-8")), box, prefab, heights, stock)
            r["area"] = area
            if images and out is not None:
                (out / f"{stem}_parity.png").write_bytes(image(lay, r))
            del r["sets"], r["visible"], r["edge_tiles"], r["walkable_set"]  # keep a row small: 128 maps in memory
            rows.append(r)
            print(f"{stem:<24} parity {100 * r['parity']:5.1f}%  tiles {r['tiles']:6}  "
                  + "  ".join(f"{k} {r['defects'][k]}" for k in CHECKS), flush=True)
    return rows


def main_baseline(into: Path) -> Path | None:
    """parity-baseline.tsv of the latest successful push run of gate.yml on main (its coverage-baseline artifact),
    downloaded into `into` with the gh CLI; None when there is none (the first runs, offline)."""
    def gh(*args: str) -> str:
        return subprocess.run(["gh", *args], capture_output=True, text=True, check=True, timeout=120).stdout.strip()
    try:
        run_id = gh("run", "list", "--workflow", "gate.yml", "--branch", "main", "--event", "push", "--status",
                    "success", "--limit", "1", "--json", "databaseId", "-q", ".[0].databaseId")
        if not run_id:
            raise ValueError("no successful gate run on main")
        shutil.rmtree(into, ignore_errors=True)
        gh("run", "download", run_id, "--name", "coverage-baseline", "--dir", str(into))
        got = next(into.rglob(BASELINE_FILE), None)
        if got is None:
            raise ValueError(f"run {run_id} has no {BASELINE_FILE}")
        print(f"pd2_parity: comparing with main's baseline from gate run {run_id}")
        return got
    except (OSError, subprocess.SubprocessError, ValueError) as e:
        print(f"::warning::pd2_parity: no main parity baseline ({e}); nothing to compare with", file=sys.stderr)
        return None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, default=ROOT / "generated" / "data", help="merged data (pd2_merge's output)")
    ap.add_argument("--d2r", type=Path, default=coverage.D2R_DATA, help=f"D2R data root (default: {coverage.D2R_DATA})")
    ap.add_argument("--out", type=Path, default=coverage.COVERAGE_OUT, help="report folder (outside out/, which modkit build wipes)")
    ap.add_argument("--maps", default="", help="comma-separated DS1 stems (default: every generated preset)")
    ap.add_argument("--commit", default="", help="commit named in the report's title")
    ap.add_argument("--baseline", type=Path, default=None, help="main's scores, a parity-baseline.tsv (default: the "
                    "latest coverage-baseline artifact of main's gate runs)")
    ap.add_argument("--check", action="store_true", help="exit 1 on a drop of more than --tolerance points")
    ap.add_argument("--tolerance", type=float, default=TOLERANCE, help=f"points a map may fall below main's and "
                    f"still pass --check (default {TOLERANCE:g})")
    ap.add_argument("--write-baseline", action="store_true", help=f"write the scores to --out/{BASELINE_FILE}")
    ap.add_argument("--no-images", action="store_true")
    a = ap.parse_args(argv)
    a.out.mkdir(parents=True, exist_ok=True)
    only = {m.strip().lower() for m in a.maps.split(",") if m.strip()}
    base = None
    if not a.write_baseline:
        path = a.baseline if a.baseline is not None else main_baseline(a.out / MAIN_DIR)
        if path is not None and path.is_file():
            base = read_baseline(path.read_text("utf-8"))
    rows = run(a.data, a.d2r, a.out, only, not a.no_images)
    if rows is None:
        (a.out / "parity.md").write_text(
            "## HD:legacy parity\n\n**Not scored: no model_lod_desc.bin in modkit's cache. Parity is not gated on this run.**\n",
            "utf-8")
        return 0
    found = changes(rows, base, a.tolerance) if base is not None and not only else None
    note = ""
    if base is None and not a.write_baseline and not only:
        note = "Not gated: no parity baseline from main (the first runs, or the download failed)."
        print(f"::warning::pd2_parity: {note}")
    (a.out / "parity.md").write_text(report_md(rows, a.commit, found, a.tolerance, note), "utf-8")
    if a.write_baseline:
        (a.out / BASELINE_FILE).write_text(baseline_text(rows), "utf-8", newline="\n")
        print(f"pd2_parity: baseline written, {a.out / BASELINE_FILE}")
    print(f"pd2_parity: mean {100 * mean(rows):.2f}% over {len(rows)} maps, {a.out / 'parity.md'}")
    if found is None:
        if a.check:
            print("pd2_parity: no baseline to check against")
        return 0
    drops = [c for c in found if c.drop]
    print(f"pd2_parity: {len(found)} maps changed against the baseline, {len(drops)} dropped"
          + (f": {', '.join(c.name for c in drops)}" if drops else ""))
    return 1 if a.check and drops else 0


if __name__ == "__main__":
    sys.exit(main())
