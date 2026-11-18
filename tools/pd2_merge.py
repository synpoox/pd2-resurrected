"""Merge the extracted PD2 data into git-ignored generated/data/ by the committed rules in merge/ (ADR 0007).

Only tools/pd2_import.py runs this, when PD2 releases a version; the mod's data is the committed files/data/.

    python tools/pd2_import.py --out DIR     the only caller: extract, merge, snapshot
    python tools/pd2_merge.py --seed-ids     append id-list lines for PD2 rows that have none (see pd2_port)

Every run rebuilds generated/ from scratch.
d2rAI-toolkit's shared/classic_import merge driver applies each merge/ file by the rule kind its name maps to in RULES
(`*.md` files are notes); a file with no rule kind fails the run, so a rule is never silently skipped. Refuses a
pd2-data/ extracted from another baseline. A failed run leaves no generated/, so a build never picks up half a merge.
A second run in the same project waits for the first (merge.lock). Merges in different worktrees run at once: the
rule cache is safe to share, as entries are kept and deleted by rename (#745). The output is checked before it is
handed back (tables present, count); a bad tree is rebuilt once with no rule cache, then fails the run.
D2R's own tables come from modkit's cache (--d2r).
CI (the gate) replays rules the same way when it sets PD2R_CI_SCOPE (#820): a push to main ("main") replays only entries
main kept and keeps into the shared cache; a PR ("pr-<n>") replays main's entries and its own scratch folder and keeps
only there, so a PR's accidents stay out of the entries main replays (cache.Policy).
With a cache root, each rule whose code, stamps and inputs match a kept run replays it instead (#558), and the
rule cache is pruned to PD2R_CACHE_GB (default 20) GB.
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path
from typing import Callable

import pd2_animdata
import pd2_cutmaps
import pd2_extract  # noqa: F401  (the rule cache's key follows its imports)
import pd2_items
import pd2_levels
import pd2_missiles_hd
import pd2_monsters_hd
import pd2_objects_hd
import pd2_overlays_hd
import pd2_panels
import pd2_port
import pd2_skill_icons
import pd2_skilltrees
import pd2_sounds
import pd2_standins
import pd2_strings
import pd2_table
import pd2_tables
from pd2_table import D2R_DATA
from shared.classic_import import cache, merge as mg

ROOT = Path(__file__).resolve().parent.parent
# merge/ file name -> fn(rule file, PD2 data root, D2R data root, generated/data). Added by the work items that need them.
RULES: dict[str, Callable[[Path, Path, Path, Path], None]] = {
    "animdata.tsv": pd2_animdata.apply,
    **{f"ids-{t}.tsv": pd2_port.apply_ids for t in pd2_port.TABLES},
    "ids-monpreset.tsv": pd2_levels.apply_monpreset,
    "item-tables.txt": pd2_items.apply,
    "missiles-hd.tsv": pd2_missiles_hd.apply,  # after ids-missiles.tsv
    "monsters-hd.tsv": pd2_monsters_hd.apply,
    "objects-hd.tsv": pd2_objects_hd.apply,  # after ids-objects.tsv
    "panel-grids.json": pd2_panels.apply,
    "skill-icons.txt": pd2_skill_icons.apply,
    "sounds.tsv": pd2_sounds.apply,
    "standins.tsv": pd2_standins.apply,  # after the id and item rules (merge runs rule files by name)
    "strings.json": pd2_strings.rule,
    "tables.json": pd2_tables.rule,
    "tree-backgrounds.json": pd2_skilltrees.apply,  # after tables.json (skilldesc)
    "vfx-overlays.tsv": pd2_overlays_hd.apply,  # after tables.json (overlay)
    "z-maps-cut.txt": pd2_cutmaps.apply,  # named to run last: reads the merged monstats and superuniques (#964)
}


# What a whole merge must have written (#745): D2R's table set is ~80 files; the bug left 43 and no monstats.
REQUIRED_TABLES = ("monstats.txt", "skills.txt", "levels.txt", "itemstatcost.txt", "uniqueitems.txt", "treasureclassex.txt")
MIN_TABLES = 70


# What the retired PD2 DT1/DS1 converter wrote (#965): PD2's DS1 and DT1 under global/tiles, and the HD presets it
# generated. The mod ships none of them: its levels are D2R's own and Cartographer's.
LEGACY_OUTPUT = ("global/tiles", "hd/env/preset")


MergeError = mg.MergeError


def legacy_output(data: Path) -> list[str]:
    """The retired converter's output folders data (generated/data or out/data) still holds, empty when it is clean."""
    return [d for d in LEGACY_OUTPUT if (data / d).exists()]


def merge(root: Path, d2r: Path = D2R_DATA, rule_cache: Path | None = None, slots_home: Path | None = None,
          policy: cache.Policy = cache.Policy()) -> list[str]:
    """Rebuild root/generated/ and return the rule files applied (d2rAI-toolkit's shared/classic_import merge.run, which
    holds the rule cache, the machine-wide merge cap and the whole-output check). policy says whose cached entries may be
    replayed and where a miss is kept (CI, #820)."""
    try:
        applied = mg.run(RULES, root / "merge", root / "pd2-data" / "data", d2r, root / "generated",
                         root / "pd2-baseline.json", root / "pd2-data", (pd2_table.IdError,), rule_cache, slots_home,
                         policy, required=REQUIRED_TABLES, minimum=MIN_TABLES)
    except mg.MergeError as e:
        raise MergeError(f"{e} (tools/pd2_extract.py extracts)" if "extract" in str(e) else str(e)) from e
    if old := legacy_output(root / "generated" / "data"):
        shutil.rmtree(root / "generated")  # a failed run leaves no generated/
        raise MergeError(f"generated/data holds the retired converter's output {old}: a rule still writes it")
    cfg = root / "merge" / "strings.json"
    try:
        if cfg.is_file():
            pd2_strings.check_cut(json.loads(cfg.read_text("utf-8")), root / "generated" / "data" / "global" / "excel")
    except ValueError as e:
        shutil.rmtree(root / "generated")
        raise MergeError(f"merge/strings.json: {e}") from e
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
        added = pd2_port.seed_lists(a.root / "merge", a.root / "pd2-data" / "data", a.d2r)
        added["monpreset"] = pd2_levels.seed_list(a.root / "merge", a.root / "pd2-data" / "data", a.d2r)
        print("id lines added: " + ", ".join(f"{t} {n}" for t, n in added.items()))
        return 0
    root, policy = mg.cache_setup()
    try:
        applied = merge(a.root, a.d2r, root and root / "rules", cache.slot_home(), policy)
    except (MergeError, ValueError) as e:  # ValueError: no toolkit
        print(f"pd2_merge: {e}", file=sys.stderr)
        return 1
    if root:
        for e in mg.prune_cache(root):
            print(f"pruned {e}")
    print(f"generated/data rebuilt; rules applied: {', '.join(applied) or 'none'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
