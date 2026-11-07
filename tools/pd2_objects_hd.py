"""HD looks of the objects PD2 adds. D2R draws an object from data/hd/objects/objects.json (key: the objects.txt
`Class` in snake case, value: its folder) and data/hd/objects/<folder>/<key>.json; a row with neither spawns and
works but draws nothing (#431: the xact portals).

merge/objects-hd.tsv has one line per drawn PD2 object: `class <tab> like`, the D2R objects.json key whose json it
copies as a stand-in. The class is the exact key (PD2's are lower case with `_`, as D2R's keys are). A line for a class
that is not a PD2-only objects row, or a like D2R lacks, fails the merge. Runs after ids-objects.tsv (by name).
"""
from __future__ import annotations

import json
from pathlib import Path

from pd2_ids import Table, excel

HD = Path("hd") / "objects"


class ObjectHdError(ValueError):
    pass


def read_lines(path: Path) -> dict[str, str]:
    likes = {}
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if line and not line.startswith("#"):
            parts = line.split("\t")
            if len(parts) != 2 or not all(parts):
                raise ObjectHdError(f"line {n}: want 'class<tab>like'")
            likes[parts[0]] = parts[1]
    return likes


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/objects-hd.tsv: objects.json plus a stand-in json per listed class."""
    likes = read_lines(rule)
    folders = json.loads((d2r_data / HD / "objects.json").read_text("utf-8-sig"))
    d2r = {r[0] for r in Table(excel(d2r_data) / "objects.txt").rows}
    new = {r[0] for r in Table(excel(out_data) / "objects.txt").rows} - d2r
    for cls, like in likes.items():
        if cls not in new:
            raise ObjectHdError(f"'{cls}' is not a PD2-only objects Class")
        src = d2r_data / HD / folders.get(like, {}).get("asset_path", "") / f"{like}.json"
        if like not in folders or not src.is_file():
            raise ObjectHdError(f"'{cls}': '{like}' has no D2R object json")
        folder = folders[like]["asset_path"]
        (out_data / HD / folder).mkdir(parents=True, exist_ok=True)
        (out_data / HD / folder / f"{cls}.json").write_bytes(src.read_bytes())
        folders[cls] = {"asset_path": folder}
    (out_data / HD / "objects.json").write_text(json.dumps(folders, indent=1), "utf-8")
    print(f"  objects-hd: {len(likes)} new objects drawn")
