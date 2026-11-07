"""HD looks of the objects PD2 adds. D2R draws an object from data/hd/objects/objects.json (key: the objects.txt
`Class` in snake case, value: its folder) and data/hd/objects/<folder>/<key>.json; a row with neither spawns and
works but draws nothing (#431: the xact portals).

merge/objects-hd.tsv has one line per drawn PD2 object: `class <tab> like [<tab> particles]`, the D2R objects.json key
whose json it copies as a stand-in, and optionally a D2R .particles path that replaces the like's one particle effect
(its colour, #461). The class is the exact key (PD2's are lower case with `_`, as D2R's keys are). A line for a class
that is not a PD2-only objects row, or a like D2R lacks, fails the merge. Runs after ids-objects.tsv (by name).
"""
from __future__ import annotations

import json
from pathlib import Path

from pd2_ids import Table, excel

HD = Path("hd") / "objects"


class ObjectHdError(ValueError):
    pass


def read_lines(path: Path) -> dict[str, tuple[str, str]]:
    """class -> (like, particles path or "")."""
    likes = {}
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if line and not line.startswith("#"):
            parts = line.split("\t")
            if len(parts) not in (2, 3) or not all(parts):
                raise ObjectHdError(f"line {n}: want 'class<tab>like[<tab>particles]'")
            likes[parts[0]] = (parts[1], parts[2] if len(parts) == 3 else "")
    return likes


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/objects-hd.tsv: objects.json plus a stand-in json per listed class."""
    likes = read_lines(rule)
    folders = json.loads((d2r_data / HD / "objects.json").read_text("utf-8-sig"))
    d2r = {r[0] for r in Table(excel(d2r_data) / "objects.txt").rows}
    new = {r[0] for r in Table(excel(out_data) / "objects.txt").rows} - d2r
    for cls, (like, particles) in likes.items():
        if cls not in new:
            raise ObjectHdError(f"'{cls}' is not a PD2-only objects Class")
        src = d2r_data / HD / folders.get(like, {}).get("asset_path", "") / f"{like}.json"
        if like not in folders or not src.is_file():
            raise ObjectHdError(f"'{cls}': '{like}' has no D2R object json")
        folder = folders[like]["asset_path"]
        (out_data / HD / folder).mkdir(parents=True, exist_ok=True)
        data = src.read_bytes()
        if particles:
            old = [d["path"] for d in json.loads(data)["dependencies"]["particles"]]
            if len(old) != 1:
                raise ObjectHdError(f"'{cls}': '{like}' has {len(old)} particle effects, want 1 to replace")
            data = data.replace(old[0].encode(), particles.encode())
        (out_data / HD / folder / f"{cls}.json").write_bytes(data)
        folders[cls] = {"asset_path": folder}
    (out_data / HD / "objects.json").write_text(json.dumps(folders, indent=1), "utf-8")
    print(f"  objects-hd: {len(likes)} new objects drawn")
