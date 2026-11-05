"""HD looks of the missiles PD2 adds: every appended missiles.txt row gets a data/hd/missiles/missiles.json entry, or
D2R's HD renderer draws nothing for it (the missile still works). Stand-in art (pd2-asset-inventory.md, Missile DCC).

A new missile takes the model of the D2R missile with the same `CelFile` (the classic art; the model most D2R rows
with it use). merge/missiles-hd.tsv has one line per missile whose `CelFile` no D2R row has (PD2's own art):
`missile <tab> like`, the D2R missile whose model it takes. A new missile with neither fails the merge, so none is
skipped silently; so does a line for a missile that is not new or has a `CelFile` match, so the list never goes stale.
"""
from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

from pd2_ids import Table, excel

HD = Path("hd") / "missiles" / "missiles.json"


class MissileHdError(ValueError):
    pass


def hd_key(name: str, lower: bool = True) -> str:
    """D2R's spelling of a missile name in missiles.json: bighead1 -> bighead_1, bomb in air -> bomb_in_air."""
    return re.sub(r"([A-Za-z])(\d)", r"\1_\2", (name.lower() if lower else name).replace(" ", "_"))


def cel(row: list[str], col: int) -> str:
    return row[col].lower().rsplit("\\", 1)[-1]  # PD2 and D2R differ on the expansion\ folder prefix


def entries(mod: Table, d2r: Table, models: dict, likes: dict[str, str]) -> dict[str, str]:
    """missiles.json key -> model for every mod row not in D2R's table. likes: missile (lower case) -> D2R missile."""
    c, dc = mod.header.index("CelFile"), d2r.header.index("CelFile")
    by_cel: dict[str, Counter] = {}
    for r in d2r.rows:
        if r[0] and hd_key(r[0]) in models:
            by_cel.setdefault(cel(r, dc), Counter())[models[hd_key(r[0])]] += 1
    vanilla = {r[0].lower() for r in d2r.rows}
    out, missing, stale = {}, [], set(likes)
    for r in mod.rows:
        name = r[0]
        if not name or name.lower() in vanilla:
            continue
        key, like = hd_key(name), likes.get(name.lower())
        # IMPORTANT: JSON keys are case-sensitive, and no vanilla missile name has a capital, so it is unproven whether
        # D2R lower-cases one before the lookup: a name with capitals (ZharBolts) gets its model under both spellings
        own = hd_key(name, lower=False)
        stale.discard(name.lower())
        for k in {key, own}:
            if k in models or k in out:
                raise MissileHdError(f"'{name}': missiles.json already has '{k}'")
        if like is not None:
            if cel(r, c) in by_cel:
                raise MissileHdError(f"'{name}' has a CelFile match ({by_cel[cel(r, c)].most_common(1)[0][0]}): "
                                     "delete its line")
            if hd_key(like) not in models:
                raise MissileHdError(f"'{name}': '{like}' has no missiles.json entry")
            out[key] = models[hd_key(like)]
        elif cel(r, c) in by_cel:
            out[key] = by_cel[cel(r, c)].most_common(1)[0][0]
        else:
            missing.append(f"{name} ({r[c] or 'no CelFile'})")
            continue
        out[own] = out[key]
    if missing:
        raise MissileHdError(f"no HD model for {len(missing)} new missiles, add lines: {', '.join(missing)}")
    if stale:
        raise MissileHdError(f"lines for missiles that are not new: {', '.join(sorted(stale))}")
    return out


def read_lines(path: Path) -> dict[str, str]:
    likes = {}
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if line and not line.startswith("#"):
            parts = line.split("\t")
            if len(parts) != 2 or not all(parts):
                raise MissileHdError(f"line {n}: want 'missile<tab>like'")
            likes[parts[0].lower()] = parts[1]
    return likes


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/missiles-hd.tsv: write missiles.json for the missiles.txt the id rule wrote."""
    models = json.loads((d2r_data / HD).read_text("utf-8-sig"))
    new = entries(Table(excel(out_data) / "missiles.txt"), Table(excel(d2r_data) / "missiles.txt"), models,
                  read_lines(rule))
    (out_data / HD).parent.mkdir(parents=True, exist_ok=True)
    (out_data / HD).write_text(json.dumps(models | new, separators=(",", ":")), "utf-8")
    print(f"  missiles-hd: {len(new)} entries for new missiles")
