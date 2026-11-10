"""Lint merge/presets-hd.tsv's stand-in lines (#786): `py -3 tools/pd2_presets_lint.py [--data D] [--d2r D]`.

Prints, per stand-in line, the share of the PD2 DT1's tiles its D2R DT1 holds with the same flags and the tiles it
renames in each map that loads it; exits 1 on a line under a quarter with no `# reason` third column, a line that
renames no tile (it is `-`) or a missing DT1 file. Needs the merged data (pd2_merge) and D2R's tiles in modkit's
cache, so it runs in the gate job, not the unit job; the logic's tests are tests/test_presets_lint.py. See
presets/lint.py.
"""
from __future__ import annotations

import argparse
import functools
import sys
from pathlib import Path

import pd2_coverage as coverage
import pd2_dt1_copies as dt1_copies
import pd2_presets as presets
from pd2_ids import load
from presets import lint

ROOT = Path(__file__).resolve().parent.parent


def run(rule: Path, data: Path, d2r: Path) -> tuple[list[lint.Line], str]:
    _, standins, _ = presets.read_rule(rule)
    tiles_dir = d2r / "global" / "tiles"
    tiles, pd2_tiles = presets.Tiles(tiles_dir, strict=True), presets.Tiles(data / "global" / "tiles", strict=True)
    stock, pd2_stock = presets.Tiles(tiles_dir), presets.Tiles(data / "global" / "tiles")  # lenient: layouts as built
    pd2_only = presets.pd2_only_presets(data, len(load(d2r, "lvlprest").records()))
    images = dt1_copies.d2r_images(tiles_dir)

    @functools.cache
    def copies_of(pd2: str, standin: str | None) -> dict:
        """pd2_dt1_copies' copies of one PD2 DT1 with `standin` as its line (None: the `-` line)."""
        f = data / "global" / "tiles" / pd2
        return dt1_copies.copies({pd2: f.read_bytes()}, images, {pd2: standin}) if f.is_file() else {}
    lines = lint.lint(standins, lint.reasons(rule), tiles, pd2_tiles, copies_of)

    def layout_of(ds1: str, dt1s: dict, copies: dict):
        return presets.layout((data / "global" / "tiles" / ds1).read_bytes(), dt1s, copies)
    lint.renamed(lines, stock, pd2_stock, pd2_only, layout_of, copies_of)
    return lines, lint.report(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rule", type=Path, default=ROOT / "merge" / "presets-hd.tsv")
    ap.add_argument("--data", type=Path, default=ROOT / "generated" / "data", help="merged data (pd2_merge's generated/data)")
    ap.add_argument("--d2r", type=Path, default=coverage.D2R_DATA, help="D2R data root")
    a = ap.parse_args(argv)
    lines, text = run(a.rule, a.data, a.d2r)
    print(text)
    bad = [ln for ln in lines if ln.problems]
    print(f"pd2_presets_lint: {len(lines)} stand-in lines, {len(bad)} with problems")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
