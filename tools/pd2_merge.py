"""Merge the extracted PD2 data into git-ignored generated/data/ by the committed rules in merge/ (ADR 0002).

    python tools/pd2_extract.py && python tools/pd2_merge.py && modkit build .
    python tools/pd2_merge.py --seed-ids     append id-list lines for PD2 rows that have none (see pd2_ids)

`modkit build` layers generated/data/ after files/ and before patches/. Every run rebuilds generated/ from scratch.
d2rAI-toolkit's shared/classic_import merge driver applies each merge/ file by the rule kind its name maps to in RULES
(`*.md` files are notes); a file with no rule kind fails the run, so a rule is never silently skipped. Refuses a
pd2-data/ extracted from another baseline. A failed run leaves no generated/, so a build never picks up half a merge.
D2R's own tables come from modkit's cache (--d2r).
With a pd2_cache root, each rule whose code, stamps and inputs match a kept run replays it instead (#558), and the
rule cache is pruned to PD2R_CACHE_GB (default 20) GB (the old whole-merge folders too with PD2R_PRUNE_LEGACY=1).
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Callable

import pd2_animdata
import pd2_cache
import pd2_extract
import pd2_ids
import pd2_items
import pd2_levels
import pd2_missiles_hd
import pd2_monsters_hd
import pd2_objects_hd
import pd2_overlays_hd
import pd2_panels
import pd2_presets
import pd2_skill_icons
import pd2_skilltrees
import pd2_sounds
import pd2_standins
import pd2_strings
import pd2_tables

ROOT = Path(__file__).resolve().parent.parent
# the build the port targets, in modkit's cache (MODKIT_CACHE, as modkit reads it)
D2R_DATA = Path(os.environ.get("MODKIT_CACHE", pd2_ids.toolkit(ROOT) / "d2r-data")) / "93847" / "data"

# merge/ file name -> fn(rule file, PD2 data root, D2R data root, generated/data). Added by the work items that need them.
RULES: dict[str, Callable[[Path, Path, Path, Path], None]] = {
    "animdata.tsv": pd2_animdata.apply,
    **{f"ids-{t}.tsv": pd2_ids.apply for t in pd2_ids.TABLES},
    "ids-monpreset.tsv": pd2_levels.apply_monpreset,
    "item-tables.txt": pd2_items.apply,
    "level-files.tsv": pd2_levels.apply_files,  # after the id rules: lvltypes, objpreset, DS1 and DT1
    "missiles-hd.tsv": pd2_missiles_hd.apply,  # after ids-missiles.tsv
    "monsters-hd.tsv": pd2_monsters_hd.apply,
    "objects-hd.tsv": pd2_objects_hd.apply,  # after ids-objects.tsv
    "panel-grids.json": pd2_panels.apply,
    "presets-hd.tsv": pd2_presets.apply,  # after level-files.tsv: reads the merged levels, lvlprest, lvltypes and DS1
    "skill-icons.txt": pd2_skill_icons.apply,
    "sounds.tsv": pd2_sounds.apply,
    "standins.tsv": pd2_standins.apply,  # after the id and item rules (merge runs rule files by name)
    "strings.json": pd2_strings.rule,
    "tables.json": pd2_tables.rule,
    "tree-backgrounds.json": pd2_skilltrees.apply,  # after tables.json (skilldesc)
    "vfx-overlays.tsv": pd2_overlays_hd.apply,  # after tables.json (overlay)
}


class MergeError(Exception):
    pass


def merge(root: Path, d2r: Path = D2R_DATA, cache: Path | None = None) -> list[str]:
    """Rebuild root/generated/ and return the rule files applied (d2rAI-toolkit's shared/classic_import driver). With a
    cache folder, a rule whose code, stamps and the files it read match a run kept there replays it (pd2_cache.rule)."""
    mg = pd2_extract.classic("merge")
    rules, ran = RULES, []
    if cache:
        kit = pd2_ids.toolkit(root)
        # every rule's key: the stamps, the toolkit code rules and the driver use, this cache's code and D2R's path
        base = pd2_cache.key([root / "pd2-baseline.json", root / "pd2-data" / "baseline.json", kit / "modkit",
                              kit / "shared" / "classic_import", Path(pd2_cache.__file__)], str(d2r.resolve()))

        def cached(name: str, fn: Callable) -> Callable:
            def run(p: Path, data: Path, d2r_: Path, out: Path) -> None:
                k = pd2_cache.key(pd2_cache.sources(fn), base, name, fn.__qualname__)
                if not pd2_cache.rule(cache, k, {"p": root, "d": d2r_}, lambda: fn(p, data, d2r_, out)):
                    ran.append(name)
            return run
        rules = {n: cached(n, fn) for n, fn in RULES.items()}
    try:
        mg.check_extract(root / "pd2-data", root / "pd2-baseline.json")
        applied = mg.merge(rules, root / "merge", root / "pd2-data" / "data", d2r, root / "generated", (pd2_ids.IdError,))
    except mg.MergeError as e:
        raise MergeError(f"{e} (tools/pd2_extract.py extracts)" if "extract" in str(e) else str(e)) from e
    if cache:
        print(f"rule cache: {len(applied) - len(ran)} of {len(applied)} rules replayed; ran {', '.join(ran) or 'none'}")
    return applied


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=ROOT, help="project folder (default: this repo)")
    ap.add_argument("--d2r", type=Path, default=D2R_DATA, help=f"D2R data root (default: {D2R_DATA})")
    ap.add_argument("--seed-ids", action="store_true", help="append id-list lines for PD2 rows that have none")
    a = ap.parse_args(argv)
    if a.seed_ids:
        if not (a.d2r / "global" / "excel").is_dir():
            print(f"pd2_merge: no D2R tables under {a.d2r}: run `modkit fetch` or pass --d2r", file=sys.stderr)
            return 1
        added = pd2_ids.seed_lists(a.root / "merge", a.root / "pd2-data" / "data", a.d2r)
        added["monpreset"] = pd2_levels.seed_list(a.root / "merge", a.root / "pd2-data" / "data", a.d2r)
        print("id lines added: " + ", ".join(f"{t} {n}" for t, n in added.items()))
        return 0
    cache = pd2_cache.root()
    try:
        applied = merge(a.root, a.d2r, cache and cache / "rules")
    except (MergeError, ValueError) as e:  # ValueError: no toolkit
        print(f"pd2_merge: {e}", file=sys.stderr)
        return 1
    if cache:
        for e in pd2_cache.prune(cache, int(float(os.environ.get("PD2R_CACHE_GB", "20")) * 2**30),
                                  legacy=os.environ.get("PD2R_PRUNE_LEGACY") == "1"):
            print(f"pruned {e}")
    print(f"generated/data rebuilt; rules applied: {', '.join(applied) or 'none'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
