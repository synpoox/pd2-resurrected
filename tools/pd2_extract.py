"""Extract the PD2 baseline's data files into git-ignored pd2-data/ (ADR 0002).

    python tools/pd2_extract.py [--install DIR]

d2rAI-toolkit's shared/classic_import extract does the work: it refuses an install whose archives differ from
pd2-baseline.json, then writes the files the baseline's "extract" patterns name (every file of pd2data.mpq, the skill
icon sheets, the item pictures, the missile DCC, the automap cels, animdata.d2, the DT1 tiles and the WAVs of
pd2assets.mpq, and the DS1 maps of pd2maps.mpq; ADR 0002 amendment) to pd2-data/data/... (lower-case paths, forward
slashes), then pd2-data/baseline.json last, which pd2_merge reads to refuse a stale extract. With a pd2_cache root
and no --out, pd2-data/ is a junction to the cache's extract for this stamp, so worktrees share one; a run holds the
cache's lock, so extracts never run at once (#440).
"""
from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import sys
from pathlib import Path

import pd2_cache
import pd2_ids

ROOT = Path(__file__).resolve().parent.parent


def classic(name: str):
    """d2rAI-toolkit's shared/classic_import.<name> module (PYTHONPATH, else the nearest toolkit beside a parent
    folder). Imported on use, not at the top, so the unit tests run on CI, which has no toolkit."""
    try:
        return importlib.import_module(f"shared.classic_import.{name}")
    except ImportError:
        sys.path.insert(0, str(pd2_ids.toolkit(ROOT)))
        try:
            return importlib.import_module(f"shared.classic_import.{name}")
        except ImportError:
            raise ValueError(f"d2rAI-toolkit's shared/classic_import/{name}.py not found: put the toolkit at "
                             "README.md's commit on PYTHONPATH") from None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--install", type=Path, help="PD2 folder (default: the one named in pd2-baseline.json)")
    ap.add_argument("--out", type=Path, help="output folder (default: pd2-data/, a junction to the shared cache's "
                                                  "extract when pd2_cache has a cache root)")
    ap.add_argument("--baseline", type=Path, default=ROOT / "pd2-baseline.json")
    a = ap.parse_args(argv)
    ex = classic("extract")
    install = a.install or Path(json.loads(a.baseline.read_text("utf-8"))["install"])
    out, cache = a.out or ROOT / "pd2-data", pd2_cache.root()
    with pd2_cache.lock((cache or out.parent) / "pd2_extract.lock"):
        if a.out or not cache or (out.exists() and not pd2_cache.is_link(out)):
            if not a.out and cache:
                print(f"{out} is a folder, not the shared extract: delete it to use {cache / 'extract'}")
            return ex.extract(install, out, a.baseline)
        shared = cache / "extract" / hashlib.sha256(ex.stamp(a.baseline.read_bytes())).hexdigest()[:16]
        rc = ex.extract(install, shared, a.baseline)
        if rc == 0:
            pd2_cache.point(out, shared)
            print(f"{out} -> {shared}")
        return rc


if __name__ == "__main__":
    sys.exit(main())
