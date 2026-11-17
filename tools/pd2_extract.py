"""Extract the PD2 baseline's data files into git-ignored pd2-data/ (ADR 0007: the gate and tools/pd2_import.py read it).

    python tools/pd2_extract.py [--install DIR]

d2rAI-toolkit's shared/classic_import extract does the work: it refuses an install whose archives differ from
pd2-baseline.json, then writes the files the baseline's "extract" patterns name (every file of pd2data.mpq, the skill
icon sheets, the item pictures, the missile DCC, the automap cels, animdata.d2, the DT1 tiles and the WAVs of
pd2assets.mpq, and the DS1 maps of pd2maps.mpq; ADR 0002 amendment) to pd2-data/data/... (lower-case paths, forward
slashes), then pd2-data/baseline.json last, which pd2_merge reads to refuse a stale extract. With a cache root
and no --out, pd2-data/ is a junction to the cache's extract for this stamp, so worktrees share one; a run holds the
cache's lock, so extracts never run at once (#440).
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import pd2_table  # noqa: F401  (first: puts the toolkit on sys.path)
from shared.classic_import import cache, extract

ROOT = Path(__file__).resolve().parent.parent


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--install", type=Path, help="PD2 folder (default: the one named in pd2-baseline.json)")
    ap.add_argument("--out", type=Path, help="output folder (default: pd2-data/, a junction to the shared cache's "
                                                  "extract when there is a cache root)")
    ap.add_argument("--baseline", type=Path, default=ROOT / "pd2-baseline.json")
    a = ap.parse_args(argv)
    install = a.install or Path(json.loads(a.baseline.read_text("utf-8"))["install"])
    return extract.extract_shared(install, a.out or ROOT / "pd2-data", a.baseline, cache.root(), bool(a.out))


if __name__ == "__main__":
    sys.exit(main())
