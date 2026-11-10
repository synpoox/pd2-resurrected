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
Like testing/slot_install.py it needs MODKIT_TESTING_CONFIG, the slot lock held by MODKIT_RUN, and the slot's game
stopped (D2R reads lvlprest at launch). Forcing a level again checks the DS1 against the saved original.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pd2_ids import Table  # noqa: E402

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


def force(t: Table, selector: str, orig: Table | None = None) -> str:
    """Pin the row `selector` names to its DS1 in table t, in place; returns the report line. `orig` is the slot's
    original table: a DS1 is checked against the row's original variants, so a forced level can be forced again."""
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
    if orig is not None and len(orig.rows) != len(t.rows):
        orig = None
    src = {id(r): (orig.rows[i] if orig else r) for i, r in enumerate(t.rows)}  # the row whose variants count
    if not key:
        rows = [r for r in t.rows if _match(h, src[id(r)], ds1)]
    elif key.isdigit():
        rows = [r for r in t.rows if r[id_i] == str(int(key))]
    else:
        rows = [r for r in t.rows if r[name_i].lower() == key.lower()]
    if not rows:
        raise ForceError(f"'{selector}': no lvlprest row matches '{key or ds1}'")
    hit = [r for r in rows if _match(h, src[id(r)], ds1)]
    if not hit:
        have = "; ".join(f"{r[name_i]}: {', '.join(variants(h, src[id(r)])) or 'none'}" for r in rows)
        raise ForceError(f"'{ds1}' is not a variant of '{key}'. Its variants: {have}")
    if len(hit) > 1:
        raise ForceError(f"'{selector}' matches {len(hit)} rows ({', '.join(r[name_i] for r in hit)}); name the row")
    r = hit[0]
    want, before = _match(h, src[id(r)], ds1), variants(h, src[id(r)])
    r[h.index("Files")] = "1"
    r[h.index("File1")] = want
    for i in range(2, 7):
        r[h.index(f"File{i}")] = "0"
    return f"{r[name_i]} (LevelId {r[id_i]}): {len(before)} variants [{', '.join(before)}] -> {want}"


def _toolkit():
    """The toolkit's testing_config and release_install modules (the nearest d2rAI-toolkit, as pd2_edge_tour finds it)."""
    from pd2_ids import toolkit
    sys.path.insert(0, str(toolkit(Path(__file__).resolve().parent.parent) / "testing"))
    import release_install
    import testing_config
    return testing_config, release_install


def lvlprest_path(slot: int, slots_dir: Path | None = None) -> Path:
    """The slot's installed lvlprest.txt. With no slots_dir (the real use) the toolkit's testing config names the slot
    folder, and the guard of testing/slot_install.py runs: not slot 1, the lock held by MODKIT_RUN, no game running
    in the slot (D2R reads lvlprest at launch)."""
    if slots_dir is not None:
        return slots_dir / f"Slot{slot}" / REL
    try:
        tc, ri = _toolkit()
        cfg = tc.load(None)
        return Path(cfg.slot_dir(ri.check_slot(cfg, slot))) / REL
    except Exception as e:  # the toolkit's ReleaseError and config errors
        raise ForceError(str(e)) from e


def apply(path: Path, selectors: list[str]) -> list[str]:
    if not path.is_file():
        raise ForceError(f"{path} not found: install the slot first")
    t = Table(path)
    backup = path.with_name(path.name + BACKUP_SUFFIX)
    orig = Table(backup) if backup.is_file() else None  # already forced: the original row says what may be forced
    report = [force(t, s, orig) for s in selectors]  # raises before anything is written
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
    ap.add_argument("--slot", type=int, required=True, help="game slot number (folder from the toolkit testing config)")
    ap.add_argument("--slots-dir", type=Path, help="test only: <dir>/Slot<N>, no slot guard")
    ap.add_argument("--reset", action="store_true", help="restore the slot's original lvlprest.txt")
    a = ap.parse_args(argv)
    try:
        path = lvlprest_path(a.slot, a.slots_dir)
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
