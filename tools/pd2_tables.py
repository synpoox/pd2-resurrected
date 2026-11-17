"""The tables with no id list (P3, P4): merge/tables.json names each table the merge takes over from D2R.

d2rAI-toolkit's shared/classic_import tables does the merge (the options are in its docstring; "source_only_rows" is
what this repo's rule file calls PD2-only rows). What stays here is the skilldesc step: its description lines then move
onto D2R's line functions (pd2_skilldesc.MAPPING).
"""
from __future__ import annotations

from pathlib import Path

import pd2_port as P
import pd2_skilldesc
from shared.classic_import import skilldesc, tables
from shared.classic_import.ids import Table, excel


def skilldesc_hook(d2r_data: Path, out_data: Path):
    """tables.rule's hook for skilldesc: D2R's line functions for PD2's, then hide the rows off the tree."""
    def hook(merged: Table, d2r: Table) -> None:
        fmt = skilldesc.formatted(d2r_data)
        skills = excel(out_data) / "skills.txt"  # merged by ids-skills.tsv, which runs first
        skills = Table(skills if skills.is_file() else excel(d2r_data) / "skills.txt")
        col = {c.lower(): j for j, c in enumerate(skills.header)}
        elements = {r[col["skilldesc"]].lower(): r[col["etype"]] for r in skills.rows}
        texts = skilldesc.texts(d2r_data, out_data)  # strings.json runs first
        skilldesc.rewrite(merged, d2r, fmt, pd2_skilldesc.MAPPING, elements, texts)
        skilldesc.hide_off_tree(merged)
        if bad := skilldesc.clashes(merged, d2r, fmt, texts):
            raise ValueError(f"skilldesc lines D2R would print wrongly: {'; '.join(bad)}")
    return hook


def rule(rule_file: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/tables.json."""
    tables.rule(rule_file, pd2_data, d2r_data, out_data, tables=P.TABLES,
                hooks={"skilldesc": skilldesc_hook(d2r_data, out_data)}, spell=P.SPELL)
