"""HD looks of the overlays PD2 adds. D2R draws an overlay from data/hd/overlays/<Character>/<overlay>.json, reading
overlay.txt `Character` as that folder; a row with no json there is invisible in HD (classic mode still draws it).
merge/tables.json keeps D2R's `Character` on shared rows (PD2's are classic values: all, ?, Assasin, Bar) and leaves
PD2's appended rows blank, which this rule fills.

merge/vfx-overlays.tsv has one line per overlay PD2 adds: `overlay <tab> like`, the D2R overlay whose json it copies
as a stand-in (PD2's classic art names differ from D2R's, so none match by `Filename`). The new row takes the like's
folder. The copy's file name and `name` keep PD2's spelling (D2R's names and keys are case-sensitive) with D2R's `_`
between a letter and a digit. A new overlay with no line fails the merge, and so does a line for an overlay that is
not new. Runs after tables.json (by name).
"""
from __future__ import annotations

import json
import re
from pathlib import Path

from pd2_ids import Table, excel

HD = Path("hd") / "overlays"


class OverlayHdError(ValueError):
    pass


def read_lines(path: Path) -> dict[str, str]:
    likes = {}
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if line and not line.startswith("#"):
            parts = line.split("\t")
            if len(parts) != 2 or not all(parts):
                raise OverlayHdError(f"line {n}: want 'overlay<tab>like'")
            likes[parts[0].lower()] = parts[1]
    return likes


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/vfx-overlays.tsv: the new rows' `Character` and their stand-in jsons."""
    likes = read_lines(rule)
    d2r = Table(excel(d2r_data) / "overlay.txt")
    merged_path = excel(out_data) / "overlay.txt"
    merged = Table(merged_path)
    ch = merged.header.index("Character")
    folder = {r[0].lower(): r[d2r.header.index("Character")] for r in d2r.rows}
    missing, stale = [], set(likes)
    for r in merged.rows:
        name = r[0]
        if not name or name.lower() in folder:
            continue
        stale.discard(name.lower())
        like = likes.get(name.lower())
        if like is None:
            missing.append(name)
            continue
        src = d2r_data / HD / folder.get(like.lower(), "") / f"{like.lower()}.json"
        if like.lower() not in folder or not src.is_file():
            raise OverlayHdError(f"'{name}': '{like}' has no D2R overlay json")
        r[ch] = folder[like.lower()]
        file = re.sub(r"([A-Za-z])(\d)", r"\1_\2", name)  # D2R's own doubledamage1 is doubledamage_1.json
        copy = json.loads(src.read_text("utf-8-sig")) | {"name": file}
        dest = out_data / HD / r[ch] / f"{file}.json"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps(copy, separators=(",", ":")), "utf-8")
    if missing:
        raise OverlayHdError(f"no HD overlay for {len(missing)} new overlays, add lines: {', '.join(missing)}")
    if stale:
        raise OverlayHdError(f"lines for overlays that are not new: {', '.join(sorted(stale))}")
    merged_path.write_bytes(merged.text().encode("latin-1"))
    print(f"  vfx-overlays: {len(likes)} new overlays given a D2R stand-in")
