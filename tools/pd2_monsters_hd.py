"""HD looks of the monsters PD2 adds: every PD2-only monstats row gets a data/hd/character/monsters.json entry, or D2R's
HD renderer draws nothing for it (the monster is invisible but fights). Stand-in art (pd2-asset-inventory.md, Monster
DCC); most are PD2's map monsters, clones of D2R monsters under a new Id.

IMPORTANT: D2R reads JSON keys case-sensitively. A key must be the exact monstats `Id`: a lower-cased "hellbovinemap"
left PD2's hellbovineMap invisible in game.

A new monster takes the model of its `BaseId` when that is a D2R monster with the same `Code` (PD2's clones name their
original there), else the model most D2R rows with its `Code` (the classic art) use. merge/monsters-hd.tsv has one line
per monster with PD2-only art (no D2R `Code` with a model) or a wrong pick (a `Code` D2R draws with several models):
`monster <tab> like`, the D2R monster whose model it takes. A new monster with no model fails the merge, so none is
skipped silently; so does a line for a monster that is not new or that picks the model it gets anyway, so the list
never goes stale.
"""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from pd2_ids import Table, excel

HD = Path("hd") / "character" / "monsters.json"


class MonsterHdError(ValueError):
    pass


def entries(pd2: Table, d2r: Table, models: dict, likes: dict[str, str]) -> dict[str, str]:
    """monsters.json key -> model for every PD2 row not in D2R's table. likes: monster (lower case) -> D2R monster."""
    code_col, base_col, dc = pd2.header.index("Code"), pd2.header.index("BaseId"), d2r.header.index("Code")
    by_code: dict[str, Counter] = {}
    code_of = {}
    for r in d2r.rows:
        code_of[r[0].lower()] = r[dc].lower()
        if r[0].lower() in models:
            by_code.setdefault(r[dc].lower(), Counter())[models[r[0].lower()]] += 1
    out, missing, stale = {}, [], set(likes)
    for r in pd2.rows:
        name = r[0]
        key = name  # IMPORTANT: D2R looks keys up case-sensitively, so the exact Id ("hellbovineMap"), never lower case
        if not name or name.lower() in code_of:
            continue
        like, code, base = likes.get(name.lower()), r[code_col].lower(), r[base_col].lower()
        stale.discard(name.lower())
        if key in models or key in out:
            raise MonsterHdError(f"'{name}': monsters.json already has '{key}'")
        if base in models and code_of.get(base) == code:
            auto = models[base]  # the model it gets with no line
        else:
            auto = by_code[code].most_common(1)[0][0] if code in by_code else None
        if like is not None:
            if like.lower() not in models:
                raise MonsterHdError(f"'{name}': '{like}' has no monsters.json entry")
            if models[like.lower()] == auto:
                raise MonsterHdError(f"'{name}' gets {auto} anyway: delete its line")
            out[key] = models[like.lower()]
        elif auto is not None:
            out[key] = auto
        else:
            missing.append(f"{name} ({r[code_col] or 'no Code'})")
    if missing:
        raise MonsterHdError(f"no HD model for {len(missing)} new monsters, add lines: {', '.join(missing)}")
    if stale:
        raise MonsterHdError(f"lines for monsters that are not new: {', '.join(sorted(stale))}")
    return out


def read_lines(path: Path) -> dict[str, str]:
    likes = {}
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if line and not line.startswith("#"):
            parts = line.split("\t")
            if len(parts) != 2 or not all(parts):
                raise MonsterHdError(f"line {n}: want 'monster<tab>like'")
            likes[parts[0].lower()] = parts[1]
    return likes


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/monsters-hd.tsv: write monsters.json. Reads PD2's monstats, not the merged one
    (merge/tables.json writes that later and keeps every Id), so the rule needs no place in the run order."""
    models = json.loads((d2r_data / HD).read_text("utf-8-sig"))
    new = entries(Table(excel(pd2_data) / "monstats.txt"), Table(excel(d2r_data) / "monstats.txt"), models,
                  read_lines(rule))
    (out_data / HD).parent.mkdir(parents=True, exist_ok=True)
    (out_data / HD).write_text(json.dumps(models | new, separators=(",", ":")), "utf-8")
    print(f"  monsters-hd: {len(new)} new monsters mapped")
