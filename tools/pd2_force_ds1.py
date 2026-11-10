"""Test-only: pin a level to one DS1 variant in a game slot's INSTALLED lvlprest.txt, so a warp lands on the preset
under test (a level with several DS1 variants rolls one per game).

    python tools/pd2_force_ds1.py --slot 4 "Dark Temple Map=DarktempleV2.ds1" 200=darktemplev3.ds1
    python tools/pd2_force_ds1.py --slot 4 darktemplev2.ds1        # a DS1 name alone: the row that holds it
    python tools/pd2_force_ds1.py --slot 4 --reset

A selector matches lvlprest rows by Name (case-insensitive), by LevelId (a number), or, with no `=`, by DS1 file
name. The DS1 (path or bare file name, either slash) must be one of that row's variants (File1..File<Files>).
The row gets Files=1, File1=<ds1>, File2..File6=0. The first change saves the slot's original as
lvlprest.txt.pd2-force-ds1.orig next to it; --reset puts it back. Only the installed slot copy is touched, never
out/ or the merge cache, so built data and CI never see a forced variant. Reinstalling the slot also undoes it.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pd2_ids import Table  # noqa: E402

SLOTS_DIR = Path(r"C:\D2RTestSlots")
REL = Path("mods/PD2Resurrected/PD2Resurrected.mpq/data/global/excel/lvlprest.txt")
BACKUP_SUFFIX = ".pd2-force-ds1.orig"


class ForceError(Exception):
    pass


def norm(ds1: str) -> str:
    return ds1.replace("\\", "/").strip().lower()


def variants(header: list[str], row: list[str]) -> list[str]:
    """The DS1s the game can roll for a row: File1..File<Files> (at least File1), without the 0 placeholders."""
    n = min(max(int(row[header.index("Files")] or 0), 1), 6)
    out = [row[header.index(f"File{i}")] for i in range(1, n + 1)]
    return [v for v in out if v and v != "0"]


def _match(header: list[str], row: list[str], ds1: str) -> str | None:
    """The row's variant that `ds1` names (full path or bare file name), or None."""
    d = norm(ds1)
    return next((v for v in variants(header, row) if norm(v) == d or norm(v).rsplit("/", 1)[-1] == d), None)


def force(t: Table, selector: str) -> str:
    """Pin the row `selector` names to its DS1 in table t, in place; returns the report line."""
    key, eq, ds1 = selector.partition("=")
    key, ds1 = key.strip(), ds1.strip()
    if not eq:  # a DS1 name alone
        key, ds1 = "", key
    if not ds1:
        raise ForceError(f"'{selector}': want <name or LevelId>=<ds1 file>, or a ds1 file name alone")
    h = t.header
    for c in ["Name", "LevelId", "Files", *[f"File{i}" for i in range(1, 7)]]:
        if c not in h:
            raise ForceError(f"lvlprest.txt has no {c} column")
    name_i, id_i = h.index("Name"), h.index("LevelId")
    if not key:
        rows = [r for r in t.rows if _match(h, r, ds1)]
    elif key.isdigit():
        rows = [r for r in t.rows if r[id_i] == str(int(key))]
    else:
        rows = [r for r in t.rows if r[name_i].lower() == key.lower()]
    if not rows:
        raise ForceError(f"'{selector}': no lvlprest row matches '{key or ds1}'")
    hit = [r for r in rows if _match(h, r, ds1)]
    if not hit:
        have = "; ".join(f"{r[name_i]}: {', '.join(variants(h, r)) or 'none'}" for r in rows)
        raise ForceError(f"'{ds1}' is not a variant of '{key}'. Its variants: {have}")
    if len(hit) > 1:
        raise ForceError(f"'{selector}' matches {len(hit)} rows ({', '.join(r[name_i] for r in hit)}); name the row")
    r = hit[0]
    want, before = _match(h, r, ds1), variants(h, r)
    r[h.index("Files")] = "1"
    r[h.index("File1")] = want
    for i in range(2, 7):
        r[h.index(f"File{i}")] = "0"
    return f"{r[name_i]} (LevelId {r[id_i]}): {len(before)} variants [{', '.join(before)}] -> {want}"


def lvlprest_path(slot: int, slots_dir: Path = SLOTS_DIR) -> Path:
    return slots_dir / f"Slot{slot}" / REL


def apply(path: Path, selectors: list[str]) -> list[str]:
    if not path.is_file():
        raise ForceError(f"{path} not found: install the slot first")
    t = Table(path)
    report = [force(t, s) for s in selectors]  # raises before anything is written
    backup = path.with_name(path.name + BACKUP_SUFFIX)
    if not backup.exists():
        shutil.copyfile(path, backup)
    path.write_bytes(t.text().encode("latin-1"))
    return report


def reset(path: Path) -> str:
    backup = path.with_name(path.name + BACKUP_SUFFIX)
    if not backup.is_file():
        return f"nothing to reset: no {backup.name} beside {path.name}"
    shutil.copyfile(backup, path)
    backup.unlink()
    return f"restored {path}"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("selectors", nargs="*", help="<lvlprest Name or LevelId>=<ds1 file>, or a ds1 file name alone")
    ap.add_argument("--slot", type=int, required=True, help="game slot number (C:\\D2RTestSlots\\Slot<N>)")
    ap.add_argument("--slots-dir", type=Path, default=SLOTS_DIR)
    ap.add_argument("--reset", action="store_true", help="restore the slot's original lvlprest.txt")
    a = ap.parse_args(argv)
    path = lvlprest_path(a.slot, a.slots_dir)
    try:
        if a.reset:
            if a.selectors:
                raise ForceError("--reset takes no selectors")
            print(reset(path))
        elif not a.selectors:
            raise ForceError("give at least one selector, or --reset")
        else:
            print("\n".join(apply(path, a.selectors)))
            print(f"wrote {path}\nTest-only: run with --reset when the check is done.")
    except ForceError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
