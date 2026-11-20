"""Map immunities (#1552): each map's tooltip names the immunities its monsters have, like "Immunities: C120, F120".

    python tools/map_immunities.py            rewrite the committed map rows and strings
    python tools/map_immunities.py --check    exit 1 when they are out of date (the test runs this)

A map's monsters are the maps.txt Monsters column plus its Boss (via superuniques.txt); the value of an element is the highest of their Hell resists
(monstats ResCo(H), ResFi(H), ResLi(H), ResPo(H); every map monster's treasure classes are the (H) ones) and an element
shows when that is 100 or more. The line is static text: the map's misc.txt spelldescstr points at its own string
MapTier<code>, "Tier: N" plus the line (D2R's strings take "\\n"), in strings/ and its strings-legacy/ twin, with its
ID in merge/strings.json. A map with no such resist keeps the shared MapTier<N> string.
"""
from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "files/data"
EXCEL = DATA / "global/excel"
# letter, monstats column, D2R colour (cold blue, fire red, lightning yellow, poison green); \xff is the colour byte
ELEMENTS = (("C", "ResCo(H)", "3"), ("F", "ResFi(H)", "1"), ("L", "ResLi(H)", "9"), ("P", "ResPo(H)", "2"))
MINIMUM = 100
FIRST_ID = 51564  # after merge/strings.json's highest ID and open PR #1556's 51563; keys are append-only like every ID
STRING_FILES = [DATA / f"local/lng/{d}/item-modifiers.json" for d in ("strings", "strings-legacy")]


def _rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="latin-1", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def line(maps_monsters: str, monstats: dict[str, dict[str, str]]) -> str:
    """The "Immunities: ..." text for a comma-separated monster list, "" when no element reaches MINIMUM."""
    parts = []
    for letter, column, color in ELEMENTS:
        top = max(int(monstats[m][column] or 0) for m in maps_monsters.split(","))
        if top >= MINIMUM:
            parts.append(f"ÿc{color}{letter}{top}ÿc0")
    return "Immunities: " + ", ".join(parts) if parts else ""


def expected() -> dict[str, tuple[str, str]]:
    """Map item code -> (its tooltip string key, its text) for the maps that have a line."""
    monstats = {r["Id"]: r for r in _rows(EXCEL / "monstats.txt")}
    misc = {r["code"]: r for r in _rows(EXCEL / "misc.txt")}
    boss = {r["Superunique"]: r["Class"] for r in _rows(EXCEL / "superuniques.txt")}
    out = {}
    for m in _rows(EXCEL / "d2rloader/cartographer/maps.txt"):
        # the Boss column is a name; its superuniques row is cartoboss:<name with _20 for spaces>
        text = line(m["Monsters"] + "," + boss["cartoboss:" + m["Boss"].replace(" ", "_20")], monstats)
        if text:
            tier = misc[m["Item"]]["type"][1]  # t1m..t3m
            out[m["Item"]] = (f"MapTier{m['Item']}", f"Tier: {tier}\n{text}")
    return out


def _ids(strings: dict, keys: list[str]) -> dict[str, int]:
    """Each generated key's ID: the one merge/strings.json holds, else the next free one."""
    held = {k: v[0] for k, v in strings["ids"].items()}
    nxt = max(FIRST_ID, *(v + 1 for v in held.values()))
    out = {}
    for key in keys:
        out[key] = held.get(key, nxt)
        nxt += key not in held
    return out


def build() -> dict[Path, bytes]:
    """Every file this changes, as its new bytes."""
    want = expected()
    files: dict[Path, bytes] = {}

    path = EXCEL / "misc.txt"
    lines = path.read_bytes().decode("latin-1").split("\n")
    head = lines[0].rstrip("\r").split("\t")
    code_at, str_at = head.index("code"), head.index("spelldescstr")
    for i, ln in enumerate(lines[1:], 1):
        cells = ln.rstrip("\r").split("\t")
        if len(cells) > code_at and cells[code_at] in want:
            cells[str_at] = want[cells[code_at]][0]
            lines[i] = "\t".join(cells) + ("\r" if ln.endswith("\r") else "")
    files[path] = "\n".join(lines).encode("latin-1")

    path = ROOT / "merge/strings.json"
    raw = path.read_bytes().decode("utf-8")
    texts = dict(want.values())
    nl = "\r\n" if "\r\n" in raw else "\n"
    strings = json.loads(raw)
    ids = _ids(strings, list(texts))
    # text surgery, not a re-dump: the file keeps its own layout (inline [id, file] pairs)
    drop = tuple(f'    {json.dumps(k)}:' for k in texts)
    raw = nl.join(ln for ln in raw.split(nl) if not ln.startswith(drop))
    for block, end, fmt in (("texts", '\n  },\n  "entries"', lambda k: json.dumps(texts[k], ensure_ascii=False)),
                            ("ids", '\n  }\n}', lambda k: f'[{ids[k]}, "item-modifiers.json"]')):
        marker = end.replace("\n", nl)
        at = raw.rindex(marker) if block == "ids" else raw.index(marker)
        head = raw[:at].rstrip(", " + chr(13) + chr(10) + chr(9))  # the last line may have kept a comma from a dropped one
        add = ("," + nl).join(f"    {json.dumps(k)}: {fmt(k)}" for k in texts)
        raw = head + "," + nl + add + raw[at:]
    files[path] = raw.encode("utf-8")

    for path in STRING_FILES:
        entries = json.loads(path.read_bytes().decode("utf-8-sig"))
        entries = [e for e in entries if e["Key"] not in ids]
        template = next(e for e in entries if e["Key"] == "MapTier1")
        for key, text in texts.items():
            entry = {"id": ids[key], "Key": key}
            entry.update({lang: text for lang in template if lang not in ("id", "Key")})
            entries.append(entry)
        out = json.dumps(entries, indent=2, ensure_ascii=False).replace("\n", "\r\n")
        files[path] = b"\xef\xbb\xbf" + out.encode("utf-8")
    return files


def main() -> int:
    files = build()
    stale = [p for p, b in files.items() if p.read_bytes() != b]
    if "--check" in sys.argv:
        for p in stale:
            print("out of date:", p.relative_to(ROOT))
        return 1 if stale else 0
    for p in stale:
        p.write_bytes(files[p])
        print("wrote", p.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
