"""Every `col=value` a port line (`+<id> col=value ...`, merge/README.md) sets matches the committed table's cell (#1376).

The committed tables are the source (ADR 0007), but pd2_port rebuilds each such row from its line on the next PD2
update: a table edit that skips the line is lost silently."""
import shlex
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import data_tables  # noqa: E402

MERGE = data_tables.ROOT / "merge"
ID_COLS = {"itemstatcost": "*ID", "levels": "Id"}  # as the toolkit's ids.merge_table: `*ID` or ID_COLS, else the row number
# A blank LevelEntry in a levels line is filled after the line (toolkit ids.fix_levels: "Entering <LevelName>").
DERIVED = {("levels", "LevelEntry")}
# Real mismatches today, kept visible: the table says 6300, the line 4200 (Dclone Arena OffsetY). Part of #1379; delete
# the entry when fixed (the test then demands it).
KNOWN_1379 = {"levels id 145 OffsetY"}


def port_lines(path):
    """(new row id, {col: value}) of each port line, parsed like the toolkit's ids.port_row, but quoted (posix shlex)
    so a column name with a space can be written "Send Bits=11"."""
    for ln in path.read_text("utf-8").splitlines():
        if ln.startswith("+"):
            first, new_id, _name = ln.split("\t")
            like, *sets = shlex.split(first[1:])
            assert like.isdigit() and all("=" in kv for kv in sets), f"{path.name}: bad port line {first!r}"
            yield int(new_id), dict(kv.split("=", 1) for kv in sets)


class PortRowsMatchTables(unittest.TestCase):
    def test_port_line_values_match_committed_rows(self):
        data_tables.need(self)
        bad, seen = [], 0
        for path in sorted(MERGE.glob("ids-*.tsv")):
            lines = list(port_lines(path))
            if not lines:
                continue
            tab = path.stem[len("ids-"):]
            rows = [r for r in data_tables.rows(tab) if next(iter(r.values())) != "Expansion"]  # not a numbered record
            idc = next((c for c in (ID_COLS.get(tab), "*ID") if c and c in rows[0]), None)
            for new_id, sets in lines:
                seen += 1
                row = next((r for r in rows if r[idc] == str(new_id)), None) if idc else rows[new_id] if new_id < len(rows) else None
                if row is None:
                    bad.append((f"{tab} id {new_id}", f"{tab}: no row with id {new_id}"))
                    continue
                for col, v in sets.items():
                    if col not in row:
                        bad.append((f"{tab} id {new_id} {col}", f"{tab} id {new_id}: no column {col!r}"))
                    elif row[col] != v and not (v == "" and (tab, col) in DERIVED):
                        bad.append((f"{tab} id {new_id} {col}", f"{tab} id {new_id} ({row.get(next(iter(row)))}): {col} line={v!r} table={row[col]!r}"))
        self.assertGreater(seen, 0)
        self.assertEqual({k for k, _ in bad if k in KNOWN_1379}, KNOWN_1379, "a known mismatch is fixed: drop it from KNOWN_1379")
        bad = [m for k, m in bad if k not in KNOWN_1379]
        self.maxDiff = None
        self.assertEqual(bad, [], f"{len(bad)} port-line values differ from the committed tables; fix the line in "
                                  "merge/ids-<table>.tsv (it wins on the next PD2 update) and the table")


if __name__ == "__main__":
    unittest.main()
