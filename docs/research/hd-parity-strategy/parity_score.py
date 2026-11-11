"""Prototype: a per-map HD:legacy parity score that matches the eye (research/hd-parity-strategy).

    py -3 docs/research/hd-parity-strategy/parity_score.py [--maps jungle1,tomb1] [--out <dir>] [--vote <json>]

Run from the repo root after `tools/pd2_merge.py`. Reuses pd2_coverage (model boxes, prefab walk, ground planes) and
pd2_fidelity (legacy edge classes) and scores every visible tile of each generated preset against its legacy DS1.
One tile can carry several defects; the map's parity counts each tile once (see PARITY in the doc).

Checks, per DS1 tile (TILE = 10 HD units; a piece's tiles are coverage.core_footprint, so a box only grazing a tile
does not count):

  clutter      walkable legacy floor with no legacy edge on or beside it, under an HD prop LOW..TALL high standing
               on the ground (not ground cover)
  solid        the same floor under a piece >= TALL standing on the ground: a wall/rock/house you can walk through
  occlusion    the same floor under a piece >= TALL whose base is raised (canopy, overhangs): it hides the floor
  floating     a piece whose base is >= FLOAT above the ground plane under it (not a tree, light, chain, banner or
               water surface;
               pasted stock rooms are skipped: their own terrain is not the plane)
  invisible    legacy draws a wall cell there (not inv_wall, not liquid) and HD draws nothing >= LOW: you bump into
               air (blocked floor legacy draws as plain ground is left out: it looks walkable in legacy too)
  void         legacy draws nothing there (no floor, no wall), HD draws ground or a piece
  liquid       legacy liquid floor (pd2_fidelity.classify "liquid") under no water or lava model, any size
  edge         pd2_fidelity's missing / wrong type / wrong height per legacy edge tile (folder-name typing, or
               stock-vote typing with --vote, the #822 proposal)

Writes <out>/<map>_parity.png (each tile coloured by its worst defect) and prints a table; --tsv writes one line per
map for ranking.
"""
from __future__ import annotations

import argparse
import functools
import json
import math
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

import pd2_coverage as coverage  # noqa: E402
import pd2_fidelity as fidelity  # noqa: E402
import pd2_presets as presets  # noqa: E402
from pd2_ids import load  # noqa: E402

VOTE_SHARE, VOTE_MIN = 0.6, 50  # the stock vote types a folder only with this share of its tiles, over this many
LOW, TALL = coverage.LOW, coverage.TALL  # 1 and 4 HD units, as the gate
SIGHT = 7  # tiles from walkable floor the camera shows (D2R HD at 1080p: about 7 tiles each way from the player)
FLOAT = 1.0  # HD units of air under a piece's base that the eye reads as floating (a tile is 10 units)
NOT_FLOATING = ("tree", "palm", "canopy", "light", "lamp", "candle", "torch", "chain", "banner", "flag", "beam",
                "bird", "bat", "fx", "vfx", "particle", "cloud", "fog", "ray", "god", "dust", "smoke", "hanging",
                "vine", "web", "roof", "ceiling")
WATERY = ("water", "river", "lava", "pond", "lake", "swamp", "oasis", "puddle", "stream", "canal", "flow", "pool")
# the weight of a tile in the map's parity: an edge tile is what the eye follows (walls, cliffs, shores); a defect on
# walkable floor misleads play (you walk through it or cannot see it); the rest is scenery
WEIGHT = {"edge": 3.0, "walk": 2.0, "other": 1.0}
SKIP: set = set()  # --skip: checks left out of the parity number (a what-if: the parity once a generator fix lands)
CHECKS = ("edge", "solid", "invisible", "void", "floating", "occlusion", "clutter", "liquid")
COLOUR = {"ok": (70, 160, 70), "edge": (230, 40, 40), "solid": (240, 120, 20), "invisible": (40, 200, 230),
          "void": (150, 60, 220), "floating": (250, 230, 40), "occlusion": (30, 70, 220), "clutter": (240, 140, 200),
          "liquid": (20, 220, 180), "bg": (20, 20, 20), "plain": (55, 50, 42)}


def walk(entities, box, prefab, ground, base=None, depth=0, room=False):
    """(model path, x0, x1, z0, z1, top, bottom, in a pasted room) of every model, as coverage.boxes places them;
    heights from the ground plane under the entity (a pasted room's pieces from 0)."""
    for e in entities:
        t = presets._transform(e)
        if t is None or e.get("name") == "terrain":
            continue
        p = (t["position"]["x"], t["position"]["y"], t["position"]["z"])
        in_room = room or (e.get("name") or "").startswith(presets.ROOM_PIECE)
        g = 0.0 if in_room or ground is None else ground(p[0], p[2])
        q, sc = t.get("orientation") or coverage.IDENTITY, t.get("scale") or {"x": 1, "y": 1, "z": 1}
        s = (sc["x"], sc["y"], sc["z"])
        if base:
            bp, bq, bs = base
            v = coverage.painter.rotate(bq, (p[0] * bs[0], p[1] * bs[1], p[2] * bs[2]))
            p, q, s = (bp[0] + v[0], bp[1] + v[1], bp[2] + v[2]), coverage.qmul(bq, q), (s[0] * bs[0], s[1] * bs[1], s[2] * bs[2])
        for c in e["components"]:
            if c["type"] == "ModelDefinitionComponent" and (bb := box(c.get("filename", ""))):
                corners = [coverage.painter.rotate(q, (x * s[0], y * s[1], z * s[2]))
                           for x in (bb[0], bb[3]) for y in (bb[1], bb[4]) for z in (bb[2], bb[5])]
                xs, ys, zs = ([p[i] + v[i] for v in corners] for i in range(3))
                yield c["filename"].lower(), min(xs), max(xs), min(zs), max(zs), max(ys) - g, min(ys) - g, in_room
            elif c["type"] == "PrefabPlacementDefinitionComponent" and c.get("prefab") and depth < coverage.DEPTH:
                yield from walk(prefab(c["prefab"]), box, prefab, (lambda x, z: g) if ground else None,
                                (p, q, s), depth + 1, in_room)


def folder(path: str) -> str:
    return path.rsplit("/", 2)[-2] if path.count("/") else ""


class Typer:
    """HD model -> edge type: pd2_fidelity.model_type, or the stock vote (a folder typed by what legacy edge its pieces
    stand on in D2R's own presets, #822) when `vote` maps folders to types."""
    def __init__(self, vote: dict | None = None):
        self.vote = vote

    def __call__(self, path: str) -> str | None:
        if self.vote is not None:
            # hybrid: an explicit folder name (tree, cliff, pillar, fence, wall) wins; the vote types only what the
            # name leaves untyped, and only with a clear majority (VOTE_SHARE of the folder's tiles, VOTE_MIN tiles).
            # A pure vote retyped 499 of 712 folders, most of them wrongly: tomb vases and travincal gore stand on
            # wall tiles in stock (props hug walls), dead trees and act2 big cliffs on floor tiles.
            named = fidelity.model_type(path)
            if named is not None:
                return named
            v = self.vote.get(folder(path))
            return v[0] if v and v[0] and v[1] >= VOTE_SHARE and v[2] >= VOTE_MIN else None
        return fidelity.model_type(path)


def stock_vote(d2r: Path, box, prefab, out: Path) -> dict:
    """For each HD model folder: the legacy edge type its pieces stand on in D2R's stock presets (majority of the
    tiles its pieces' cores cover that are edge tiles), or "none" when most of its tiles are plain floor. Cached."""
    if out.is_file():
        return json.loads(out.read_text("utf-8"))
    tiles = presets.Tiles(d2r / "global" / "tiles")
    heights = fidelity.Heights(d2r / "global" / "tiles")
    votes: dict[str, Counter] = {}
    root = d2r / presets.PRESETS
    for p in sorted(root.rglob("*.json")):
        ds1 = d2r / "global" / "tiles" / p.relative_to(root).with_suffix(".ds1")
        if not ds1.is_file():
            continue
        data = ds1.read_bytes()
        try:
            lay = presets.layout(data, tiles.of(presets.header_dt1s(data)))
        except Exception:  # noqa: BLE001  (old DS1 versions)
            continue
        edges = {c: k for k, cs in fidelity.legacy_edges(lay, heights).items() for c in cs}
        try:
            ents = json.loads(p.read_text("utf-8-sig")).get("entities", [])
        except Exception:  # noqa: BLE001
            continue
        for m in walk(ents, box, prefab, None):
            b = m[1:7]
            if b[1] - b[0] > coverage.ROOM or b[3] - b[2] > coverage.ROOM or b[4] < LOW:
                continue
            v = votes.setdefault(folder(m[0]), Counter())
            for c in coverage.core_footprint(b):
                if c in lay.keys:
                    v[edges.get(c, "none")] += 1
    result = {}
    for f, v in votes.items():  # folder -> [its most-voted type (None: plain floor), that type's share, tiles]
        k = max(v, key=v.get)
        result[f] = [None if k == "none" else k, v[k] / sum(v.values()), sum(v.values())]
    out.write_text(json.dumps(result, indent=0, sort_keys=True), "utf-8")
    return result


def score(name, lay, legacy, preset, box, prefab, heights, typer):
    W, H = lay.width, lay.height
    grid = {(x, y) for x in range(W) for y in range(H)}
    ground = coverage.plane_ground(preset)
    edges_by_kind = fidelity.legacy_edges(legacy, heights)
    edge_tiles = {c for cs in edges_by_kind.values() for c in cs}
    walkable = presets.walkable(lay)
    near_edge = {(x + dx, y + dy) for x, y in edge_tiles for dx, dy in fidelity.STEPS8} | edge_tiles
    open_floor = walkable - near_edge  # walkable floor where legacy draws no edge on or beside the tile
    void = {c for c in grid if c not in lay.keys or all(t[0] is None for t in lay.keys[c][1])}
    liquid = {c for c, (k, ts) in legacy.keys.items() if k == "f" and any(
        fidelity.classify(t[0], 0, "floor", coverage.painter.blocked_floor(legacy.flags.get(c, bytes(25)))) == "liquid"
        for t in ts)}
    invisible_dt1 = {c for c, (k, ts) in legacy.keys.items() if any(t[0] and fidelity.INVISIBLE.search(t[0].lower()) for t in ts)}
    # what legacy DRAWS as an obstacle: a wall cell. Blocked floor that legacy draws as plain ground (past a tree line,
    # the dashed tree lines' gaps) looks walkable in legacy too, so HD plain ground there is no mismatch.
    blocked = {c for c, (k, _) in lay.keys.items() if k == "w"} - void - liquid - invisible_dt1

    d = {k: set() for k in CHECKS}
    drawn_any, watery, floats = set(), set(), Counter()
    hd = fidelity.Hd()
    for path, x0, x1, z0, z1, top, bottom, in_room in walk(preset["entities"], box, prefab, ground):
        b = (x0, x1, z0, z1, top, bottom)
        big = x1 - x0 > coverage.ROOM or z1 - z0 > coverage.ROOM
        core = coverage.footprint([b]) if big else coverage.core_footprint(b)
        low = path.rsplit("/", 1)[-1]
        if any(w in path for w in WATERY):
            watery |= coverage.footprint([b])
        if big:
            continue
        if top >= LOW:
            drawn_any |= core
        cover = bool(fidelity.GROUND_COVER.search("/".join(path.split("/")[-2:])))
        kind = typer(path)
        # edge pieces, for the edge check (fidelity.hd_pieces with this typing)
        if kind is not None or top >= TALL:
            for c in coverage.footprint([b]):
                if kind is None:
                    hd.other[c] = max(top, hd.other.get(c, top))
                else:
                    hd.tops[kind][c] = max(top, hd.tops[kind].get(c, top))
            if kind is not None:
                hd.core[kind] |= coverage.core_footprint(b)
                hd.centres[kind].append((int((x0 + x1) / 2 // presets.TILE), int((z0 + z1) / 2 // presets.TILE)))
        if (not in_room and bottom >= FLOAT and top - bottom < 60
                and not any(w in path for w in NOT_FLOATING + WATERY) and not cover):
            d["floating"] |= core
            floats[folder(path)] += 1
        on_ground = bottom < FLOAT
        if top >= TALL and on_ground and kind != "treeline":
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

    visible = {(x + dx, y + dy) for x, y in walkable for dx in range(-SIGHT, SIGHT + 1) for dy in range(-SIGHT, SIGHT + 1)} & grid
    def weight(c):
        return WEIGHT["edge"] if c in edge_tiles else WEIGHT["walk"] if c in walkable else WEIGHT["other"]
    bad = set().union(*(v for k, v in d.items() if k not in SKIP)) & visible
    total = sum(map(weight, visible))
    parity = 1 - sum(map(weight, bad)) / total if total else 1.0
    old_fid = {k: s.score for k, s in fid.items()}
    return {"name": name, "parity": parity, "tiles": len(visible), "walkable": len(walkable), "edges": len(edge_tiles),
            "defects": {k: len(v & visible) for k, v in d.items()}, "sets": d, "edge_fid": old_fid,
            "floats": floats.most_common(6), "visible": visible, "edge_tiles": edge_tiles, "walkable_set": walkable, "void_set": void}


def image(lay, r, out: Path):
    px = {c: COLOUR["plain"] for c in r["visible"]}
    order = ("clutter", "occlusion", "liquid", "floating", "void", "invisible", "solid", "edge")  # worst drawn last
    for c in r["edge_tiles"] | r["walkable_set"]:
        px[c] = COLOUR["ok"]
    for k in order:
        for c in r["sets"][k] & r["visible"]:
            px[c] = COLOUR[k]
    (out / f"{r['name']}_parity.png").write_bytes(coverage.png(lay.width, lay.height, px, COLOUR["bg"], zoom=3))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps", default="", help="comma-separated DS1 stems (default: every generated preset)")
    ap.add_argument("--data", type=Path, default=ROOT / "generated" / "data")
    ap.add_argument("--d2r", type=Path, default=coverage.D2R_DATA)
    ap.add_argument("--out", type=Path, default=ROOT / "coverage" / "parity")
    ap.add_argument("--vote", type=Path, default=None, help="stock-vote cache (built on first use)")
    ap.add_argument("--tsv", type=Path, default=None)
    ap.add_argument("--no-images", action="store_true")
    ap.add_argument("--skip", default="", help="comma-separated checks to leave out of the parity (what-if)")
    a = ap.parse_args(argv)
    SKIP.update(k for k in a.skip.split(",") if k)
    a.out.mkdir(parents=True, exist_ok=True)
    data, d2r = a.data, a.d2r
    found = coverage.model_boxes((d2r / coverage.MODELS).read_bytes())

    @functools.cache
    def prefab(path):
        f = d2r / path.removeprefix("data/")
        return json.loads(f.read_text("utf-8-sig")).get("entities", []) if f.is_file() else []

    def box(path):
        return found.get(coverage.fnv(path))
    typer = Typer(stock_vote(d2r, box, prefab, a.vote) if a.vote else None)
    _, standins, _ = presets.read_rule(ROOT / "merge" / "presets-hd.tsv")
    tiles, pd2_tiles = presets.Tiles(d2r / "global" / "tiles"), presets.Tiles(data / "global" / "tiles")
    heights = fidelity.Heights(data / "global" / "tiles", d2r / "global" / "tiles")
    want = {m.strip().lower() for m in a.maps.split(",") if m.strip()}
    rows, seen = [], set()
    for area, _, files, ds1s in presets.pd2_only_presets(data, len(load(d2r, "lvlprest").records())):
        for ds1 in ds1s:
            stem = Path(ds1).stem.lower()
            f = data / presets.PRESETS / Path(ds1).with_suffix(".json")
            if ds1 in seen or not f.is_file() or (want and stem not in want) or stem.startswith("palace"):
                continue
            seen.add(ds1)
            raw = (data / "global" / "tiles" / ds1).read_bytes()
            lay = presets.layout(raw, presets.stand_in(files, tiles, pd2_tiles, standins))
            legacy = presets.layout(raw, {p: pd2_tiles[p] or tiles[p] for p in files})
            r = score(stem, lay, legacy, json.loads(f.read_text("utf-8")), box, prefab, heights, typer)
            r["area"] = area
            rows.append(r)
            if not a.no_images:
                image(lay, r, a.out)
            print(f"{stem:<24} parity {100 * r['parity']:5.1f}%  tiles {r['tiles']:6}  "
                  + "  ".join(f"{k} {r['defects'][k]}" for k in CHECKS), flush=True)
    if a.tsv:
        lines = ["map\tarea\tparity\ttiles\twalkable\tedges\t" + "\t".join(CHECKS)]
        lines += [f"{r['name']}\t{r['area']}\t{100 * r['parity']:.1f}\t{r['tiles']}\t{r['walkable']}\t{r['edges']}\t"
                  + "\t".join(str(r["defects"][k]) for k in CHECKS) for r in rows]
        a.tsv.write_text("\n".join(lines) + "\n", "utf-8")
    for r in rows:
        if want:
            print(f"\n{r['name']} ({r['area']}): parity {100 * r['parity']:.1f}% over {r['tiles']} visible tiles "
                  f"({r['walkable']} walkable, {r['edges']} legacy edge tiles)")
            print("  old fidelity per edge kind: " + ", ".join(f"{k} {100 * v:.1f}%" for k, v in r["edge_fid"].items() if v is not None))
            print("  floating folders: " + ", ".join(f"{f} {n}" for f, n in r["floats"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
