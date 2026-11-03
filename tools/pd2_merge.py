"""Merge the extracted PD2 data into git-ignored generated/data/ by the committed rules in merge/ (ADR 0002).

    python tools/pd2_extract.py && python tools/pd2_merge.py && modkit build .

`modkit build` layers generated/data/ after files/ and before patches/. Every run rebuilds generated/ from scratch.
Each merge/ file is handled by the rule kind its name maps to in RULES (`*.md` files are notes); a file with no rule
kind fails the run, so a rule is never silently skipped. Refuses a pd2-data/ extracted from another baseline.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path
from typing import Callable

import pd2_strings

ROOT = Path(__file__).resolve().parent.parent

# merge/ file name -> fn(rule file, pd2-data dir, generated/data dir). Added by the work items that need them.
RULES: dict[str, Callable[[Path, Path, Path], None]] = {
    "strings.json": pd2_strings.rule,
}


class MergeError(Exception):
    pass


def merge(root: Path) -> list[str]:
    """Rebuild root/generated/ and return the rule files applied."""
    pd2, rules_dir, out = root / "pd2-data", root / "merge", root / "generated" / "data"
    stamp = pd2 / "baseline.json"
    if not stamp.is_file():
        raise MergeError(f"{pd2} has no baseline stamp: run tools/pd2_extract.py")
    if stamp.read_bytes() != (root / "pd2-baseline.json").read_bytes():
        raise MergeError(f"{pd2} was extracted from another baseline: run tools/pd2_extract.py again")
    files = sorted(p for p in rules_dir.glob("*") if p.is_file() and p.suffix != ".md") if rules_dir.exists() else []
    unknown = [p.name for p in files if p.name not in RULES]
    if unknown:
        raise MergeError(f"merge/ files with no rule kind in pd2_merge.RULES: {', '.join(unknown)}")
    if out.parent.exists():
        shutil.rmtree(out.parent)
    out.mkdir(parents=True)
    for p in files:
        try:
            RULES[p.name](p, pd2, out)
        except ValueError as e:  # a rule's data error, reported by rule file
            raise MergeError(f"merge/{p.name}: {e}") from e
    return [p.name for p in files]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=ROOT, help="project folder (default: this repo)")
    a = ap.parse_args(argv)
    try:
        applied = merge(a.root)
    except MergeError as e:
        print(f"pd2_merge: {e}", file=sys.stderr)
        return 1
    print(f"generated/data rebuilt; rules applied: {', '.join(applied) or 'none'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
