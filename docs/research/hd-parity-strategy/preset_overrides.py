"""Prototype: per-map overrides of the generated HD presets, applied after generation (research/hd-parity-strategy).

A pd2_merge rule. Its anchor is merge/presets-overrides.tsv (no lines; it must sort AFTER presets-hd.tsv, because the
toolkit's merge driver runs the rule files in name order: `preset-overrides` would sort BEFORE `presets-hd`). The ops
live beside it, one JSON file per DS1 variant, in a folder per map family:

    merge/presets-overrides.d/<family>/<ds1 stem>.json

    {"ds1": "jungle1",                        # the generated preset's DS1 stem (must exist: a stale file fails)
     "ops": [
       {"op": "remove", "at": [212, 154], "model": "act3_jungle_rocks/*", "why": "rock on open floor"},
       {"op": "ground", "area": [180, 140, 240, 170], "model": "act3_ferns/*", "why": "ferns float 1-3 units"},
       {"op": "move", "at": [97, 101], "model": "*/stone_pillar*", "by": [5, 0, 0]},
       {"op": "swap", "at": [60, 40], "model": "*/cinchona*", "to": "data/hd/env/.../bush_fern01.model"},
       {"op": "add", "at": [120, 88], "from": "data/hd/env/preset/act3/jungle/...json", "pick": "*/act3_jungle_rocks/*",
        "offset": [5, 0, 5]}
     ]}

Selectors (every op): `at` [x, y] (one DS1 tile) or `area` [x0, y0, x1, y1] (inclusive tiles), the tile of an entity
being its top-level position // TILE; `model` an fnmatch glob on a model or prefab path of the entity; `name` a glob
on the entity name; `pos` [x, z] HD units (POS_EPS): one entity by its generated position, the stable identity an editor's output is
diffed by (an editor re-serialises the file and renumbers ids, so ids and array order are never selectors);
`expect` how many entities the op must match (default: at least one). Ops run in file order.

- remove: drop the matched entities.
- ground: set their y to the ground plane under them (coverage.plane_ground): the floating fix.
- move: `by` [dx, dy, dz] HD units, or `to` [x, y] tile (the entity keeps its offset inside the tile).
- swap: the matched entities' model (or prefab) path becomes `to`; the dependencies of the stock preset that holds
  `to` (`from`, optional) are merged in.
- add: copies the first entity of stock preset `from` whose model matches `pick`, places it at the centre of tile
  `at` (+ `offset`), y on the ground plane, a fresh id; that preset's dependencies are merged in.

Lint: an op that matches nothing, or not `expect`, is STALE: the merge prints it and pd2_merge lists it; `lint()` (and
the CLI below, for CI) fails on it. The generator moved or replaced what the op aimed at: re-aim the op or drop it.

CLI:
    py -3 preset_overrides.py lint                       # every override against generated/ (exit 1 on stale ops)
    py -3 preset_overrides.py diff <generated.json> <edited.json> [--ds1 stem]
        # ops that turn the generated preset into an edited copy (any editor's output, e.g. an external level
        # editor): removed, added (as inline `entity` ops) and moved entities, keyed by model and position
"""
from __future__ import annotations

import fnmatch
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

import pd2_coverage as coverage  # noqa: E402
from presets.consts import PRESETS, TILE  # noqa: E402
from presets.entity import _transform  # noqa: E402

ANCHOR = "presets-overrides.tsv"


def files(rule: Path) -> list[Path]:
    d = rule.with_name(rule.stem + ".d")
    return sorted(d.rglob("*.json")) if d.is_dir() else []


def paths(e: dict) -> list[str]:
    return [(c.get("filename") or c.get("prefab") or "").lower() for c in e["components"]
            if c["type"] in ("ModelDefinitionComponent", "PrefabPlacementDefinitionComponent")]


def tile(e: dict) -> tuple[int, int] | None:
    t = _transform(e)
    return None if t is None else (math.floor(t["position"]["x"] / TILE), math.floor(t["position"]["z"] / TILE))


def selects(op: dict, e: dict) -> bool:
    c = tile(e)
    if c is None or e.get("name") == "terrain":
        return False
    if "at" in op and list(c) != list(op["at"]):
        return False
    if "area" in op:
        x0, y0, x1, y1 = op["area"]
        if not (x0 <= c[0] <= x1 and y0 <= c[1] <= y1):
            return False
    if "model" in op and not any(fnmatch.fnmatch(p, op["model"].lower()) for p in paths(e)):
        return False
    if "name" in op and not fnmatch.fnmatch((e.get("name") or "").lower(), op["name"].lower()):
        return False
    if "pos" in op:  # stable identity: the generator's own x, z (HD units), so ids and array order never matter
        p = _transform(e)["position"]
        if abs(p["x"] - op["pos"][0]) > POS_EPS or abs(p["z"] - op["pos"][1]) > POS_EPS:
            return False
    return True


POS_EPS = 0.01  # HD units: a `pos` selector's tolerance (the generator writes 6 decimals; an editor may round)


def merge_deps(preset: dict, deps: dict) -> None:
    for kind, items in deps.items():
        if not items:  # never add an empty kind the generated preset does not have
            continue
        have = {i["path"].lower() for i in preset["dependencies"].setdefault(kind, [])}
        preset["dependencies"][kind] += [i for i in items if i["path"].lower() not in have]


def stock(d2r: Path, path: str) -> dict:
    return json.loads((d2r / path.removeprefix("data/")).read_text("utf-8-sig"))


def apply_ops(preset: dict, ops: list[dict], d2r: Path) -> list[str]:
    """Apply `ops` to `preset` in place; the stale ops' descriptions."""
    ground = coverage.plane_ground(preset)
    stale, ents = [], preset["entities"]
    next_id = max((e.get("id", 0) for e in ents), default=0) + 1
    for i, op in enumerate(ops):
        kind = op["op"]
        if kind == "add":
            src = stock(d2r, op["from"])
            pick = next((e for e in src["entities"] if any(fnmatch.fnmatch(p, op["pick"].lower()) for p in paths(e))), None)
            if pick is None:
                stale.append(f"op {i} add: no {op['pick']} in {op['from']}")
                continue
            e = json.loads(json.dumps(pick))
            t, (x, y), off = _transform(e), op["at"], op.get("offset", [TILE / 2, 0, TILE / 2])
            px, pz = x * TILE + off[0], y * TILE + off[2]
            t["position"].update(x=px, y=ground(px, pz) + off[1], z=pz)
            e["id"], e["name"] = next_id, f"pd2r_override:{e.get('name', '')}"
            next_id += 1
            ents.append(e)
            merge_deps(preset, src["dependencies"])
            continue
        if kind == "entity":  # an inline entity (diff of an editor's output): kept as is, a fresh id
            e = json.loads(json.dumps(op["entity"]))
            e["id"] = next_id
            next_id += 1
            ents.append(e)
            continue
        hit = [e for e in ents if selects(op, e)]
        want = op.get("expect")
        if not hit or (want is not None and len(hit) != want):
            stale.append(f"op {i} {kind} {json.dumps({k: v for k, v in op.items() if k != 'why'})}: "
                         f"matched {len(hit)}" + (f", expected {want}" if want is not None else ""))
            continue
        if kind == "remove":
            gone = {id(e) for e in hit}
            ents[:] = [e for e in ents if id(e) not in gone]
        for e in hit if kind != "remove" else ():
            t = _transform(e)["position"]
            if kind == "ground":
                t["y"] = ground(t["x"], t["z"])
            elif kind == "move":
                if "by" in op:
                    t["x"], t["y"], t["z"] = t["x"] + op["by"][0], t["y"] + op["by"][1], t["z"] + op["by"][2]
                else:
                    c = tile(e)
                    t["x"] += (op["to"][0] - c[0]) * TILE
                    t["z"] += (op["to"][1] - c[1]) * TILE
                    t["y"] = ground(t["x"], t["z"])
            elif kind == "swap":
                for c in e["components"]:
                    if c["type"] == "ModelDefinitionComponent":
                        c["filename"] = op["to"]
                    elif c["type"] == "PrefabPlacementDefinitionComponent":
                        c["prefab"] = op["to"]
                if "from" in op:
                    merge_deps(preset, stock(d2r, op["from"])["dependencies"])
                elif not any(m["path"].lower() == op["to"].lower() for m in preset["dependencies"].get("models", [])):
                    preset["dependencies"].setdefault("models", []).append({"path": op["to"]})
            elif kind != "remove":
                raise ValueError(f"unknown op {kind!r}")
    return stale


def generated(out_data: Path) -> dict[str, Path]:
    return {p.stem.lower(): p for p in (out_data / PRESETS).rglob("*.json")}


def run(rule: Path, d2r: Path, out_data: Path) -> dict[str, list[str]]:
    have, report = generated(out_data), {}
    for f in files(rule):
        spec = json.loads(f.read_text("utf-8"))
        ds1 = spec["ds1"].lower()
        if ds1 != f.stem.lower():
            raise ValueError(f"{f}: ds1 {ds1!r} is not the file's name")
        if ds1 not in have:
            raise ValueError(f"{f}: no generated preset {ds1}.json (a stale override file)")
        preset = json.loads(have[ds1].read_text("utf-8"))
        n0 = len(preset["entities"])
        report[ds1] = apply_ops(preset, spec["ops"], d2r)
        have[ds1].write_text(json.dumps(preset, separators=(",", ":")), "utf-8")
        print(f"  presets-overrides: {ds1}: {len(spec['ops'])} ops, entities {n0} -> {len(preset['entities'])}, "
              f"{len(report[ds1])} stale")
        for s in report[ds1]:
            print(f"    STALE {s}")
    return report


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/presets-overrides.tsv (RULES["presets-overrides.tsv"])."""
    if not (out_data / PRESETS).is_dir():
        print("  presets-overrides: skipped, no generated presets")
        return
    report = run(rule, d2r_data, out_data)
    (out_data.parent / REPORT).write_text("# ds1 <tab> stale op\n" + "".join(
        f"{d}\t{s}\n" for d, ss in sorted(report.items()) for s in ss), "utf-8")


def key(e: dict) -> tuple:
    t = _transform(e)
    p = t["position"] if t else {"x": 0, "y": 0, "z": 0}
    return (tuple(paths(e)), round(p["x"], 2), round(p["y"], 2), round(p["z"], 2))


def diff(gen: dict, edited: dict, ds1: str) -> dict:
    """Override ops that turn `gen` into `edited` (an editor's output): entities only in gen are removed (by tile and
    model, `expect` the count), entities only in edited are added inline, and a removed/added pair of one model within
    two tiles is a move."""
    a, b = {}, {}
    for e in gen["entities"]:
        a.setdefault(key(e), []).append(e)
    for e in edited["entities"]:
        b.setdefault(key(e), []).append(e)
    gone = [e for k, es in a.items() for e in es[len(b.get(k, [])):]]
    new = [e for k, es in b.items() for e in es[len(a.get(k, [])):]]
    ops = []
    for e in list(gone):
        twin = next((n for n in new if paths(n) == paths(e) and max(abs(tile(n)[0] - tile(e)[0]), abs(tile(n)[1] - tile(e)[1])) <= 2), None)
        if twin:
            p, q = _transform(e)["position"], _transform(twin)["position"]
            ops.append({"op": "move", "at": list(tile(e)), "model": paths(e)[0] if paths(e) else "*", "expect": 1,
                        "pos": [round(p["x"], 3), round(p["z"], 3)],
                        "by": [round(q["x"] - p["x"], 3), round(q["y"] - p["y"], 3), round(q["z"] - p["z"], 3)]})
            gone.remove(e)
            new.remove(twin)
    for e in gone:
        ops.append({"op": "remove", "at": list(tile(e)), "model": paths(e)[0] if paths(e) else "*", "expect": 1,
                    "pos": [round(_transform(e)["position"]["x"], 3), round(_transform(e)["position"]["z"], 3)]})
    for e in new:
        ops.append({"op": "entity", "entity": e})  # an inline add: the editor's own entity, kept as is
    return {"ds1": ds1, "ops": ops}


REPORT = "presets-overrides-report.tsv"  # beside generated/data (under generated/, so the rule cache keeps it; not shipped)


def lint(out_data: Path) -> int:
    """The stale ops of the last merge (the rule writes them to generated/REPORT, run on presets-hd's fresh output): exit 1
    when there are any. The applied presets in generated/ cannot be re-checked: their ops already ran."""
    f = out_data.parent / REPORT
    if not f.is_file():
        print(f"no {f}: run pd2_merge.py")
        return 1
    stale = [line for line in f.read_text("utf-8").splitlines() if line and not line.startswith("#")]
    for line in stale:
        print("STALE", line)
    return 1 if stale else 0

if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("lint")
    d = sub.add_parser("diff")
    d.add_argument("generated", type=Path)
    d.add_argument("edited", type=Path)
    d.add_argument("--ds1", default="")
    r = sub.add_parser("roundtrip", help="diff two presets, replay the ops on the first, check it equals the second")
    r.add_argument("generated", type=Path)
    r.add_argument("edited", type=Path)
    a = ap.parse_args()
    if a.cmd == "roundtrip":
        g, e = json.loads(a.generated.read_text("utf-8-sig")), json.loads(a.edited.read_text("utf-8-sig"))
        spec = diff(g, e, a.generated.stem)
        stale = apply_ops(g, spec["ops"], coverage.D2R_DATA)

        def r2(o):
            return ({k: r2(v) for k, v in o.items()} if isinstance(o, dict) else [r2(v) for v in o]
                    if isinstance(o, list) else round(o, 2) + 0.0 if isinstance(o, float) else o)

        def key_(p):
            return sorted(json.dumps(r2(x["components"]), sort_keys=True) for x in p["entities"])
        same = key_(g) == key_(e)
        print(f"{len(spec['ops'])} ops, {len(stale)} stale, replay equals the edited preset: {same}")
        sys.exit(0 if same and not stale else 1)
    if a.cmd == "lint":
        sys.exit(lint(ROOT / "generated" / "data"))
    g, e = json.loads(a.generated.read_text("utf-8-sig")), json.loads(a.edited.read_text("utf-8-sig"))
    print(json.dumps(diff(g, e, a.ds1 or a.generated.stem), indent=1))
