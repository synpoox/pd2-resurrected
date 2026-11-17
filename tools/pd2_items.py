"""Item tables with no id list (P3): the rule file is merge/item-tables.txt (see d2rAI-toolkit's shared/classic_import items):

    merge <tab> <table>                 merge PD2's table onto D2R's by key (pd2_port.TABLES / VANILLA)
    no-drop <tab> <type or code> ...    items of this type (or its descendants) or code never drop

The merge, the map items kept from dropping and the HD inventory pictures are the toolkit's. The mod's own cube fixes
live here: PD2's cube code does what these rows cannot say, so core-rules and core-events take over (the notes on each).
"""
from __future__ import annotations

from pathlib import Path

import pd2_art
import pd2_port as P
import pd2_table  # noqa: F401
from shared.classic_import import items
from shared.classic_import import ids as I

CORRUPTED_NEW = "100"  # `corrupt` value no PD2 outcome uses (1-92); the tooltip shows only "Corrupted" (descval 0)
CORRUPTOR_DONE = 3001  # PD2's own finished-map corruptor: above every outcome threshold (items 1000, maps 3000)
NEUTRAL_MODEL = {"misc": "scroll/identify_scroll"}
PD2_ORDER = {"treasureclassex"}  # kept in PD2's row order, not D2R's
block_drops = items.block_drops


def remake_rerolls(cube: I.Table) -> None:
    """PD2's op 31 rows (Demonic Cube + a unique or set item) have no outputs: PD2's cube code remakes the item and
    takes one Demonic Cube off the stack (IR6, #241). core-rules does that through an SDK item transaction. The rows get
    `usetype,reg` only so they are complete rows to D2R, and ship switched off (`enabled` 0): D2R never runs that
    output itself, and core-rules switches the rows on when its hook and services are in."""
    h = {c: j for j, c in enumerate(cube.header)}
    for r in cube.rows if "op" in h else []:
        if r[h["op"]] == "31":
            r[h["output"]] = "usetype,reg"
            if "enabled" in h:
                r[h["enabled"]] = "0"


def open_key_portal(cube: I.Table) -> None:
    """PD2's cube walk (0x10366f40) opens the mini-uber portal for the row with op 28, param 1 and min diff 2 (the three
    Pandemonium keys), through 0x10366740: a copy of vanilla's portal (a random one of levels 133-135 not opened yet in
    this game). Its output `useitem` is a placeholder; D2R's own `Pandemonium Portal` output does the same (#485)."""
    h = {c: j for j, c in enumerate(cube.header)}
    if not all(c in h for c in ("op", "param", "min diff", "output")):
        return
    for r in cube.rows:
        if (r[h["op"]], r[h["param"]], r[h["min diff"]], r[h["output"]]) == ("28", "1", "2", "useitem"):
            r[h["output"]] = "Pandemonium Portal"


def make_talisman(cube: I.Table) -> None:
    """PD2's organ row (output `useitem`, output b `ubtm`) uses up all three organs and makes the Pandemonium Talisman.
    In D2R `useitem` hands the first organ back, so the row's output is the Talisman itself (#593)."""
    h = {c: j for j, c in enumerate(cube.header)}
    if "output b" not in h:
        return
    for r in cube.rows:
        if r[h["output"]] == "useitem" and r[h["output b"]].strip('"').split(",")[0] == "ubtm":
            r[h["output"]], r[h["output b"]] = r[h["output b"]], ""


def close_finale_portal(cube: I.Table) -> None:
    """D2R's organ portal to its Uber Tristram (level 136) is off (docs/features/d2r-only-content.md, #153): PD2's organs make
    the Pandemonium Talisman (`ubtm`, level 185) instead. Every row with output `Pandemonium Finale Portal` is disabled,
    D2R's appended one too, so the organs reach only PD2's Talisman row (#485)."""
    h = {c: j for j, c in enumerate(cube.header)}
    for r in cube.rows if "enabled" in h and "output" in h else []:
        if r[h["output"]] == "Pandemonium Finale Portal":
            r[h["enabled"]] = "0"


def mark_new_corrupted(cube: I.Table, corruptor: str | None) -> None:
    """A corruption outcome that makes a new item ("destroyed -> rare") gets the corrupted mark (CORRUPTED_NEW) and
    corruptor CORRUPTOR_DONE, above every outcome's threshold, so no corruption outcome or roll row matches it again.
    PD2's cube code marks it (its rows have no mods for it); D2R only applies a row's mods."""
    h = {c: j for j, c in enumerate(cube.header)}
    outcomes = [r for r in cube.rows if corruptor and r[h["op"]] == "16" and r[h["param"]] == corruptor]
    if any(int(r[h["value"]]) >= CORRUPTOR_DONE for r in outcomes):
        raise I.IdError(f"cubemain: a corruption outcome's threshold reaches the finished corruptor {CORRUPTOR_DONE}")
    final = str(CORRUPTOR_DONE)
    for r in outcomes:
        mods = [r[h[f"mod {k}"]] for k in range(1, 6) if f"mod {k}" in h]
        if r[h["output"]] == "useitem" or "corruptnum" in mods:  # changes the item, or marks the new one itself
            continue
        free = [k for k, m in enumerate(mods, 1) if not m]
        for k, (mod, v) in zip(free[:2], (("corrupt", CORRUPTED_NEW), ("corruptnum", final))):
            r[h[f"mod {k}"]], r[h[f"mod {k} chance"]], r[h[f"mod {k} min"]], r[h[f"mod {k} max"]] = mod, "100", v, v


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/item-tables.txt."""
    ctx = P.context_of(P.lists_for(rule.parent, pd2_data, d2r_data), pd2_data, d2r_data)
    corruptor = next((str(i) for p, i, _ in ctx.lists.get("itemstatcost", []) if p == "corruptor"), None)
    items.apply(rule, pd2_data, d2r_data, out_data, ctx, P.VANILLA, P.SUFFIX, upscale=pd2_art.upscale,
                cube_fixes=[lambda cube: mark_new_corrupted(cube, corruptor),
                            lambda cube: items.count_each_input(cube, skip_ops=("30",)),
                            remake_rerolls, open_key_portal, make_talisman],
                cube_after=[close_finale_portal], by_source_order=PD2_ORDER, source="PD2",
                neutral_model=NEUTRAL_MODEL, merge_args=P.item_merge_args(ctx), picture_folder="pd2")
