"""The table-diff gate: compare the committed tables to the PD2 baseline by table, row and column name.

    python tools/pd2_diff.py --scope P2|P3|P4|M1|alpha [--root DIR] [--d2r DIR]

d2rAI-toolkit's shared/classic_import diff does the comparing; this file gives it the mod's paths, id lists, scopes and
fixes. Reads PD2's tables from pd2-data/ and the mod's from files/data/ (D2R's own table where files/data has none). A
table that pd2-import.txt lists is `committed` (PD2's, imported); any other is D2R's.
Exit 0: every deviation is on docs/deviations.md; 1: some are not (listed in the report); 2: the list or an id list is
broken.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import pd2_port as P
import pd2_table
from shared.classic_import import diff as D
from shared.classic_import import ids as I

ROOT = Path(__file__).resolve().parent.parent
STATUSES = ("not yet", "approximated", "cut")
# the roadmap's work items that merge tables (P2 the id-listed ones, P3 the item tables, P4 the rest); M1 and alpha both
ITEM_TABLES = {"armor", "weapons", "misc", "itemtypes", "magicprefix", "magicsuffix", "automagic", "rareprefix",
               "raresuffix", "uniqueitems", "sets", "setitems", "runes", "gems", "cubemain", "cubemod", "cubetype",
               "treasureclassex", "treasureclass", "qualityitems", "lowqualityitems", "itemratio", "gamble",
               "properties", "books", "belts", "bodylocs"}
# the tables whose rows the gate pairs by their id list, as the merge placed them (names repeat in them)
LIST_PAIRED = ITEM_TABLES | {"levels", "lvlprest", "lvlwarp", "objects", "objgroup"}
SCOPES = {"P2": lambda t: t in P.TABLES, "P3": lambda t: t in ITEM_TABLES,
          "P4": lambda t: t not in ITEM_TABLES and t not in P.TABLES, "M1": lambda t: True, "alpha": lambda t: True}
GateError = D.GateError


def gate(root: Path, d2r: Path, scope: str) -> D.Report:
    if scope not in SCOPES:
        raise GateError(f"scope {scope!r} is not one of {', '.join(SCOPES)}")
    pd2_data = root / "pd2-data" / "data"
    if not (root / "pd2-import.txt").is_file():
        raise GateError(f"no {root / 'pd2-import.txt'}: it says which tables the PD2 import owns")
    imported = {ln for ln in (x.strip() for x in (root / "pd2-import.txt").read_text("utf-8").splitlines())
                if ln and not ln.startswith("#")}
    lists = {t: I.read_list(I.list_path(root / "merge", t)) for t in P.TABLES}
    tables = root / "merge" / "tables.json"
    ctx: list[I.Context] = []  # built on the first table that reads ids

    def compare_args(table: str) -> dict:
        # property or hit parameters, level tables' numbers or map items' levels, name ids
        ids_read = table in I.PROPS or table in ("missiles", "misc", *P.NUM_REFS, *I.ID_COLS)
        if ids_read and not ctx:
            ctx.append(P.context_of(lists, pd2_data, d2r))
        fixes = [I.missile_funcs(P.MISSILE_FUNCS[table]) if table in P.MISSILE_FUNCS else None]
        args = {"fix_row": P.FIX_ROW, "spell": P.SPELL, "num_refs": P.NUM_REFS, "by_row": tuple(P._BY_ROW),
                "list_paired": tuple(LIST_PAIRED)}
        if ids_read:
            c = ctx[0]
            args["id_map"] = c.ids
            fixes.append(I.hit_params(P.HIT_PARAMS, c.ids) if table == "missiles"
                         else I.props_finish(c.ren, c.ids, c.props))
            if table == "misc":
                fixes.append(lambda h, r, s, o: P.map_level(h, r, s, o, c.ids))
        return {**args, "fix": D.fix_of(I.finishes(*fixes))}

    return D.gate(pd2_data / "global" / "excel", root / "files" / "data" / "global" / "excel", d2r / "global" / "excel",
                  root / "docs" / "deviations.md", STATUSES, SCOPES[scope], lists,
                  json.loads(tables.read_text("utf-8")) if tables.is_file() else {}, None, compare_args, "PD2",
                  "run `python tools/pd2_extract.py` (the merge's first step) or pass --root a checkout that has them",
                  lambda name: f"data/global/excel/{name}" in imported, "committed", strict_whole_table=True)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scope", required=True, choices=SCOPES)
    ap.add_argument("--root", type=Path, default=ROOT, help="repo root holding pd2-data/, files/data/, pd2-import.txt and docs/")
    ap.add_argument("--d2r", type=Path, default=pd2_table.D2R_DATA, help="D2R data root (default: %(default)s)")
    a = ap.parse_args(argv)
    try:
        rep = gate(a.root, a.d2r, a.scope)
    except (GateError, I.IdError) as e:
        print(f"pd2_diff: {e}", file=sys.stderr)
        return 2
    print(D.report(rep, a.scope, D.commit_of(a.root), "PD2"))
    return 0 if rep.ok else 1


if __name__ == "__main__":
    sys.exit(main())
