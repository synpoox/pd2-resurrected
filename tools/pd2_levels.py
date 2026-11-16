"""The PD2-only areas' monster presets (LP1, #151); the DS1/DT1/lvltypes/objpreset/automap conversion is retired (#965).

merge/ids-monpreset.tsv: monpreset's append-only list (ADR 0003), one line per PD2 row, `<act>/<Place> <tab> index
<tab> Place`: the row's index within its act. Below VANILLA the index is the same row in both games (D2R's act 5 rows
51+ are D2R's own); higher PD2 rows append to their act. Runs after pd2_ids has merged the other id lists.
"""
from __future__ import annotations

from pathlib import Path

import pd2_ids
from pd2_ids import IdError, Table, ident, load, read_list

MONPRESET_VANILLA = {"1": 47, "2": 59, "3": 39, "4": 28, "5": 51}


def acts(t: Table) -> dict[str, list[str]]:
    """monpreset's places by act, in file order (a DS1's monster id is the index in its act)."""
    out: dict[str, list[str]] = {}
    for r in t.rows:
        out.setdefault(r[0], []).append(r[1])
    return out


def seed_monpreset(pd2: dict[str, list[str]], d2r: dict[str, list[str]],
                   existing: list[tuple[str, int, str]]) -> list[tuple[str, int, str]]:
    listed = {p.lower() for p, _, _ in existing}
    new = []
    for act, places in pd2.items():
        nxt = max([len(d2r.get(act, [])) - 1, *(i for p, i, _ in existing if p.split("/")[0] == act)]) + 1
        for i, name in enumerate(ident(places)):
            key = f"{act}/{name}"
            if key.lower() in listed:
                continue
            if i < MONPRESET_VANILLA.get(act, 0) and i < len(d2r.get(act, [])):
                new.append((key, i, d2r[act][i]))
            else:
                new.append((key, nxt, places[i]))
                nxt += 1
    return new


def check_monpreset(lines: list[tuple[str, int, str]], pd2: dict[str, list[str]], d2r: dict[str, list[str]]) -> None:
    want = {f"{act}/{n}".lower() for act, places in pd2.items() for n in ident(places)}
    seen, ids = set(), {}
    for p, i, n in lines:
        act = p.split("/")[0]
        if p.lower() in seen or p.lower() not in want or i < 0:
            raise IdError(f"ids-monpreset.tsv: '{p}' is listed twice, is not a PD2 row or has no index")
        seen.add(p.lower())
        if i < len(d2r.get(act, [])) and d2r[act][i] != n:
            raise IdError(f"ids-monpreset.tsv: act {act} index {i} is D2R's '{d2r[act][i]}', the list says '{n}'")
        ids.setdefault(act, []).append(i)
    if want - seen:
        raise IdError(f"ids-monpreset.tsv: PD2 rows with no index ({', '.join(sorted(want - seen)[:5])}): "
                      "run tools/pd2_merge.py --seed-ids")
    for act, got in ids.items():
        added = sorted(i for i in got if i >= len(d2r.get(act, [])))
        if len(set(got)) != len(got) or added != list(range(len(d2r.get(act, [])), len(d2r.get(act, [])) + len(added))):
            raise IdError(f"ids-monpreset.tsv: act {act}'s indices repeat or append with gaps")


def merge_monpreset(d2r: Table, lines: list[tuple[str, int, str]]) -> Table:
    """D2R's monpreset with the appended PD2 rows after each act's last row."""
    per = acts(d2r)
    for p, i, n in sorted(lines, key=lambda line: line[1]):
        act = p.split("/")[0]
        if i >= len(per.setdefault(act, [])):
            per[act].append(n)
    t = Table.__new__(Table)
    t.header, t.newline, t.key = d2r.header, d2r.newline, 0
    t.rows = [[act, n] + [""] * (len(d2r.header) - 2) for act, places in per.items() for n in places]
    return t


def monpreset_lines(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> list[tuple[str, int, str]]:
    lines = read_list(merge_dir / "ids-monpreset.tsv")
    check_monpreset(lines, acts(load(pd2_data, "monpreset")), acts(load(d2r_data, "monpreset")))
    return lines


def apply_monpreset(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/ids-monpreset.tsv."""
    pd2_ids.write("monpreset", merge_monpreset(load(d2r_data, "monpreset"), monpreset_lines(rule.parent, pd2_data, d2r_data)),
                  [], out_data)


def seed_list(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> int:
    path = merge_dir / "ids-monpreset.tsv"
    existing = read_list(path)
    new = seed_monpreset(acts(load(pd2_data, "monpreset")), acts(load(d2r_data, "monpreset")), existing)
    if new:
        text = path.read_text("utf-8") if path.exists() else "# act/PD2 place\tindex in act\tplace (append-only, ADR 0003)\n"
        path.write_text(text + "".join(f"{p}\t{i}\t{n}\n" for p, i, n in new), "utf-8")
    return len(new)
