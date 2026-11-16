"""Import a PD2 version's raw data into a folder for the kept `pd2-import` branch (ADR 0007). Run only when PD2 releases.

    python tools/pd2_import.py --out DIR      DIR is a worktree of the orphan branch `pd2-import`

Runs pd2_extract, `modkit fetch` (D2R's animdata and model table, which the merge reads), then pd2_merge, and moves the
merged generated/data to DIR/files/data with DIR/pd2-import.txt listing its files (the tables the gate calls
`committed`). generated/ is deleted, so install.ps1 never sees it. Never writes into this checkout's files/data.

New PD2 version: update pd2-baseline.json; commit the import on `pd2-import` in its worktree; then on a ticket branch from
main run `git diff --binary pd2-import~1 pd2-import | git apply --3way`. Our direct edits stay, a cell both sides changed
is a conflict to resolve by hand, and the PR diff is the import.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

import pd2_extract
import pd2_merge

ROOT = Path(__file__).resolve().parent.parent
HEADER = "# files the PD2 import writes (tools/pd2_import.py, ADR 0007); regenerate with it, never by hand"


def snapshot(gen: Path, out: Path) -> None:
    """Replace out/files/data with gen (generated/data), write out/pd2-import.txt, delete gen's generated/ folder."""
    shutil.rmtree(out / "files" / "data", ignore_errors=True)
    (out / "files").mkdir(parents=True, exist_ok=True)
    shutil.move(gen, out / "files" / "data")
    names = sorted(p.relative_to(out / "files").as_posix() for p in (out / "files" / "data").rglob("*") if p.is_file())
    (out / "pd2-import.txt").write_text("\n".join([HEADER, *names]) + "\n", "utf-8", newline="\n")
    shutil.rmtree(gen.parent, ignore_errors=True)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True, help="the pd2-import worktree to write files/data and pd2-import.txt into")
    a = ap.parse_args(argv)
    steps = [("pd2_extract", lambda: pd2_extract.main([])),
             ("modkit fetch", lambda: subprocess.call([sys.executable, "-m", "modkit", "fetch", "--prefix", "data/global/animdata.d2",
                                                       "--prefix", "data/hd/model_lod_desc.bin"], cwd=ROOT)),
             ("pd2_merge", lambda: pd2_merge.main([]))]
    for name, run in steps:
        if run():
            print(f"pd2_import: {name} failed", file=sys.stderr)
            return 1
    snapshot(ROOT / "generated" / "data", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
