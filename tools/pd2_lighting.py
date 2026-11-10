"""How bright each generated HD preset is (#773): the biome's ambient, the lights placed, and the lift of the lighting pass.

    py -3 tools/pd2_lighting.py report [--all]   one line per generated preset, darkest first, before and after the box
    py -3 tools/pd2_lighting.py check            the lighting pass's BIOME_IBL table against D2R's vis files

`report` reads generated/data/hd/env/preset (after `python tools/pd2_merge.py`) and D2R's biome and vis files from modkit's
cache (`python -m modkit fetch --prefix data/hd/env/biome/ --prefix data/hd/env/vis/`). Per preset:
- ibl: the biome's iblIntensity (the lowest of its time-of-day vis files), the map's ambient light;
- sun: the strongest global key light of the biome's vis files, nothing but the sun lights an outdoor map from above;
- lit: the share of the map's occupied tiles (a tile holding any entity) within a point or spot light's radius;
- score: ibl + sun / SUN_PER_IBL + lit * LIT_WEIGHT, a rough number to rank by, not a photometric one;
- after: the same with the pass's box (pd2r_ambient_visbox) taking the IBL up.
It lists the darkest PD2-only maps. Without D2R's files it says so and exits.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from presets.consts import PRESETS, TILE  # noqa: E402
from presets.passes import lighting  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
LIGHTS = ("PointLightDefinitionComponent", "SpotLightDefinitionComponent")
SUN_PER_IBL = 25.0  # a global key light of 25 (the usual day sun) counts as one unit of IBL
LIT_WEIGHT = 0.5  # a fully lit map counts as half a unit of IBL more


def transform(e: dict) -> dict | None:
    return next((c for c in e["components"] if c["type"] == "TransformDefinitionComponent"), None)


def lit_share(entities: list[dict]) -> tuple[int, float]:
    """(light count, share of the occupied tiles within a light's radius) of a preset's entities."""
    cells, lights = set(), []
    for e in entities:
        t = transform(e)
        if t is None:
            continue
        p = t["position"]
        for c in e["components"]:
            if c["type"] in LIGHTS:
                lights.append((p["x"], p["z"], c.get("radius", 0.0)))
                break
        else:
            cells.add((int(p["x"] // TILE), int(p["z"] // TILE)))
    if not cells:
        return len(lights), 0.0
    hit = 0
    for x, z in cells:
        cx, cz = x * TILE + TILE / 2, z * TILE + TILE / 2
        hit += any((cx - lx) ** 2 + (cz - lz) ** 2 <= r * r for lx, lz, r in lights)
    return len(lights), hit / len(cells)


def score(ibl: float, sun: float, lit: float) -> float:
    return ibl + sun / SUN_PER_IBL + lit * LIT_WEIGHT


def vis_of(d2r: Path, path: str) -> tuple[float, float]:
    """(iblIntensity, global key light power) of a vis file."""
    j = json.loads((d2r / path.removeprefix("data/")).read_text("utf-8-sig"))
    ibl, sun = 0.0, 0.0
    for e in j["entities"]:
        for c in e["components"]:
            if c["type"] == "VisualDataComponent":
                ibl = c["iblIntensity"]
            elif c["type"] == "DirectionalLightDefinitionComponent" and e["name"] == "global_key_light":
                sun = c["power"]
    return ibl, sun


def biome_light(d2r: Path, biome: str) -> tuple[float, float]:
    """(lowest iblIntensity, strongest global key light) over a biome's vis files."""
    files = json.loads((d2r / biome.removeprefix("data/")).read_text("utf-8-sig"))["visualDataFilenames"]
    vis = [vis_of(d2r, f["visualDataFilename"]) for f in files]
    return min(v[0] for v in vis), max(v[1] for v in vis)


def d2r_data() -> Path:
    import pd2_ids
    return Path(os.environ.get("MODKIT_CACHE", pd2_ids.toolkit(ROOT) / "d2r-data")) / "93847" / "data"


def rows(generated: Path, d2r: Path) -> list[dict]:
    out = []
    for p in sorted((generated / PRESETS).rglob("*.json")):
        j = json.loads(p.read_text("utf-8"))
        boxes = [c for e in j["entities"] for c in e["components"] if c["type"] == "VisualDataBoxDefinitionComponent"
                 and c["name"].startswith(lighting.BOX)]
        ibl, sun = biome_light(d2r, j["biomeFilename"])
        after = max(ibl, vis_of(d2r, boxes[0]["filename"])[0]) if boxes else ibl
        n, lit = lit_share(j["entities"])
        out.append({"name": p.stem, "biome": Path(j["biomeFilename"]).stem, "ibl": ibl, "after": after, "sun": sun,
                    "lights": n, "lit": lit, "before": score(ibl, sun, lit), "now": score(after, sun, lit)})
    return out


def check(d2r: Path) -> list[str]:
    """The BIOME_IBL lines that differ from D2R's files."""
    bad = []
    for biome, ibl in sorted(lighting.BIOME_IBL.items()):
        have = biome_light(d2r, biome)[0]
        if abs(have - ibl) > 1e-6:
            bad.append(f"{biome}: BIOME_IBL {ibl}, D2R {have}")
    return bad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("cmd", choices=("report", "check"))
    ap.add_argument("--all", action="store_true", help="list every preset, not the 25 darkest")
    ap.add_argument("--generated", type=Path, default=ROOT / "generated" / "data")
    args = ap.parse_args()
    d2r = d2r_data()
    if not (d2r / "hd" / "env" / "vis").is_dir() or not (d2r / "hd" / "env" / "biome").is_dir():
        print("pd2_lighting: skipped, no D2R biome or vis files (`python -m modkit fetch --prefix data/hd/env/biome/ "
              "--prefix data/hd/env/vis/`)")
        return 0
    if args.cmd == "check":
        bad = check(d2r)
        print("\n".join(bad) or "BIOME_IBL matches D2R's vis files")
        return 1 if bad else 0
    data = rows(args.generated, d2r)
    print(f"{'map':28} {'biome':22} {'ibl':>5} {'sun':>5} {'lights':>6} {'lit':>5} {'before':>6}   {'ibl+':>5} {'after':>6}")
    for r in sorted(data, key=lambda r: r["before"])[: None if args.all else 25]:
        print(f"{r['name']:28} {r['biome']:22} {r['ibl']:5.2f} {r['sun']:5.1f} {r['lights']:6d} {r['lit']:5.0%} "
              f"{r['before']:6.2f}   {r['after']:5.2f} {r['now']:6.2f}")
    dark = [r for r in data if r["before"] < lighting.AMBIENT]
    lifted = [r for r in data if r["now"] > r["before"]]
    print(f"{len(data)} presets, {len(lifted)} lifted by the box; below score {lighting.AMBIENT}: "
          f"{len(dark)} before, {len([r for r in data if r['now'] < lighting.AMBIENT])} after")
    return 0


if __name__ == "__main__":
    sys.exit(main())
