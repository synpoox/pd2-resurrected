"""Give the Classic/LoD table bank our data: copy out/data's RotW tables into excel/base/ (#1853). Run after `modkit build`.

    python tools/mirror_base_bank.py [--root DIR]

D2R reads data/global/excel/ for Reign of the Warlock and data/global/excel/base/ for Classic and Lord of Destruction.
Every bank must play the same PD2 tables, so each excel/*.txt is copied to excel/base/, except a table committed in
files/data/global/excel/base/: that one is the base bank's own (inventory.txt, the Classic/LoD panel geometry, #1548).
"""
import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def mirror(root: Path) -> list[str]:
    excel = root / "out" / "data" / "global" / "excel"
    if not excel.is_dir():
        raise FileNotFoundError(f"{excel} missing: run modkit build first")
    own = {p.name.lower() for p in (root / "files" / "data" / "global" / "excel" / "base").glob("*.txt")}
    base = excel / "base"
    base.mkdir(exist_ok=True)
    copied = []
    for table in sorted(excel.glob("*.txt")):
        if table.name.lower() not in own:
            shutil.copy2(table, base / table.name)
            copied.append(table.name)
    return copied


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=ROOT)
    a = ap.parse_args(argv)
    try:
        copied = mirror(a.root)
    except FileNotFoundError as e:
        print(f"mirror_base_bank: {e}", file=sys.stderr)
        return 1
    print(f"mirror_base_bank: {len(copied)} tables -> excel/base/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
