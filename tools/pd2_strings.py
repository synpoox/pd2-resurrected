"""PD2's strings (decision #29): the merge/strings.json rule and the command that assigns new string IDs.

    python tools/pd2_strings.py --assign     # append newly referenced PD2 keys to merge/strings.json

The rule writes complete copies of each touched D2R string file (strings/ and its strings-legacy/ twin) into
generated/data/local/lng/. merge/strings.json holds key names and numbers only, never PD2 text:
  overrides  vanilla keys PD2 changed that take PD2's text (enUS only; D2R's placeholders must stay the same)
  literal_percent  overrides whose "%" is plain text the placeholder check misreads ("50% chance")
  renames    our key -> the PD2 key whose text it carries (Runeword171 means another runeword in D2R)
  ids        key -> [string ID, D2R file], append-only. New keys copy PD2's English into all 13 languages.
Text going into skills.json has its lines flipped (D2R stacks multi-line skill text top-down). The rule fails if D2R
starts using a listed key or the low 16 bits of a listed ID.

D2R's string files come from the D2R data root pd2_merge passes (modkit's cache); modkit's Strings does the editing.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TBL = "local/lng/eng/patchstring.tbl"  # under PD2's data root
FIRST_ID = 50000  # leaves 28,106-49,999 to Blizzard (decision #29)


def read_tbl(data: bytes) -> dict[str, str]:
    """Key -> text of a classic .tbl. A repeated key resolves as the game's lookup does: the entry its hash probe
    meets first."""
    n, size = struct.unpack_from("<HI", data, 2)
    out: dict[str, tuple[int, str]] = {}
    for slot in struct.unpack_from(f"<{n}H", data, 21):
        _, _, hv, ko, vo, _ = struct.unpack_from("<BHIIIH", data, 21 + 2 * n + 17 * slot)
        key, text = (data[o:data.index(0, o)].decode("utf-8") for o in (ko, vo))
        dist = (slot - hv % size) % size
        if key not in out or dist < out[key][0]:
            out[key] = (dist, text)
    return {k: t for k, (_, t) in out.items()}


def modkit():
    """modkit.base and modkit.strings, from PYTHONPATH or the nearest d2rAI-toolkit beside a parent folder."""
    try:
        from modkit import base, strings
    except ImportError:
        kit = next((p / "d2rAI-toolkit" for p in ROOT.parents if (p / "d2rAI-toolkit" / "modkit").is_dir()), None)
        if kit is None:
            raise ValueError("modkit not found: put d2rAI-toolkit on PYTHONPATH")
        sys.path.insert(0, str(kit))
        from modkit import base, strings
    return base, strings


class DataRoot:
    """The slice of modkit's Base that Strings uses, over a folder holding data/... (D2R's files)."""

    def __init__(self, data: Path):
        self.root = data.parent

    def list(self, prefix: str):
        raise OSError("no CASC listing: Strings globs the folder")

    def text(self, rel: str) -> str:
        return (self.root / rel).read_text("utf-8-sig")

    def exists(self, rel: str) -> bool:
        return (self.root / rel).is_file()


def flip(file: str, text: str) -> str:
    return "\n".join(reversed(text.split("\n"))) if file == "skills.json" else text


def build(cfg: dict, pd2: dict[str, str], s, out_root: Path) -> list[str]:
    """Applies cfg to modkit Strings `s` and writes the touched files under out_root (the generated/ folder)."""
    from_key = {k: cfg.get("renames", {}).get(k, k) for k in cfg["ids"]}
    index = s.index()
    d2r_keys = {e["Key"] for hits in index.values() for _, e in hits}
    used16 = {int(e["id"]) & 0xFFFF for hits in index.values() for _, e in hits}
    mk = modkit()[1]
    for key in cfg["overrides"]:
        hits = [(rel, e) for rel, e in index.get(key.lower(), []) if e["Key"] == key]
        if not hits or key not in pd2:
            raise ValueError(f"override {key!r}: not in {'D2R' if not hits else 'PD2'}")
        for rel, e in hits:
            new = flip(rel.rsplit("/", 1)[1], pd2[key])
            if key not in cfg.get("literal_percent", []) and                     sorted(mk.PLACEHOLDER.findall(e["enUS"])) != sorted(mk.PLACEHOLDER.findall(new)):
                raise ValueError(f"override {key!r} changes D2R's placeholders (crash risk)")
            e["enUS"] = new
            s.changed.add(rel)
    seen: set[int] = set()
    for key, (sid, file) in sorted(cfg["ids"].items(), key=lambda kv: kv[1][0]):
        if key in d2r_keys:
            raise ValueError(f"D2R now has key {key!r}: rename ours (decision #29)")
        if sid & 0xFFFF in used16 or sid > 0xFFFF or sid in seen:
            raise ValueError(f"string ID {sid} ({key}) is used by D2R, repeated or above 65535")
        seen.add(sid)
        if from_key[key] not in pd2:
            raise ValueError(f"{key!r}: PD2 has no key {from_key[key]!r}")
        text = flip(file, pd2[from_key[key]])
        for folder in mk.FOLDERS:
            rel = f"{folder}/{file}"
            if s.base.exists(rel):
                s.load(rel).append({"id": sid, "Key": key, **{la: text for la in mk.LANGS}})
                s.changed.add(rel)
    return s.write(out_root)


def rule(rule_file: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/strings.json."""
    if not (d2r_data / "local" / "lng" / "strings").is_dir():
        raise ValueError(f"no D2R strings under {d2r_data}: run `modkit fetch` or pass --d2r")
    cfg = json.loads(rule_file.read_text("utf-8"))
    build(cfg, read_tbl((pd2_data / TBL).read_bytes()), modkit()[1].Strings(DataRoot(d2r_data)), out_data.parent)


def referenced(excel: Path) -> dict[str, set[tuple[str, str]]]:
    """Every excel cell value -> the (table, column)s it appears in, comment columns left out."""
    refs: dict[str, set[tuple[str, str]]] = defaultdict(set)
    for t in sorted(excel.glob("*.txt")):
        rows = t.read_bytes().decode("latin-1").splitlines()
        cols = rows[0].split("\t")
        for row in rows[1:]:
            for c, v in zip(cols, row.split("\t")):
                if v and not c.startswith("*") and c.lower() != "description":  # comment columns
                    refs[v].add((t.stem, c))
    return refs


def assign(cfg: dict, pd2: dict[str, str], excel: Path, s) -> list[str]:
    """Appends to cfg["ids"] each renamed key and each PD2 key a table names that D2R lacks (exact case), with the
    D2R file the column's other keys mostly sit in, numbered from FIRST_ID skipping D2R's low-16 values."""
    index = s.index()
    d2r = {e["Key"]: rel.rsplit("/", 1)[1] for hits in index.values() for rel, e in hits if "-legacy" not in rel}
    refs = referenced(excel)
    votes: dict[object, Counter] = defaultdict(Counter)  # (table, col), col and table -> D2R files of its keys
    for v, cols in refs.items():
        if v in d2r:
            for t, c in cols:
                for k in ((t, c), c, t):
                    votes[k][d2r[v]] += 1
    renames = cfg.setdefault("renames", {})
    want = [(k, sorted(refs[src])) for k, src in renames.items()]
    want += [(k, sorted(refs[k])) for k in sorted(pd2) if k not in d2r and refs.get(k)]
    used16 = {int(e["id"]) & 0xFFFF for hits in index.values() for _, e in hits}
    used16 |= {sid & 0xFFFF for sid, _ in cfg["ids"].values()}
    nxt = max([FIRST_ID - 1] + [sid for sid, _ in cfg["ids"].values()]) + 1
    added = []
    for key, cols in want:
        if key in cfg["ids"]:
            continue
        tally = next((sum((votes[k] for k in ks), Counter()) for ks in (cols, {c for _, c in cols}, {t for t, _ in cols})
                      if any(votes[k] for k in ks)), None)
        while nxt & 0xFFFF in used16:
            nxt += 1
        if nxt > 0xFFFF:
            raise ValueError("no free string ID below 65536")
        cfg["ids"][key] = [nxt, tally.most_common(1)[0][0] if tally else "ui.json"]
        used16.add(nxt & 0xFFFF)
        added.append(key)
    return added


def dump(cfg: dict) -> str:
    """strings.json with one id row per line, so appends diff as one line each."""
    rows = ",\n".join(f"    {json.dumps(k, ensure_ascii=False)}: {json.dumps(v)}" for k, v in cfg["ids"].items())
    head = {k: v for k, v in cfg.items() if k != "ids"}
    return json.dumps(head, indent=2, ensure_ascii=False)[:-2] + ',\n  "ids": {\n' + rows + "\n  }\n}\n"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--assign", action="store_true", required=True)
    ap.add_argument("--root", type=Path, default=ROOT)
    ap.add_argument("--d2r", type=Path, help="D2R data root (default: pd2_merge's)")
    a = ap.parse_args(argv)
    import pd2_merge
    pd2_data = a.root / "pd2-data" / "data"
    cfg_file = a.root / "merge" / "strings.json"
    cfg = json.loads(cfg_file.read_text("utf-8"))
    strings = modkit()[1].Strings(DataRoot(a.d2r or pd2_merge.D2R_DATA))
    added = assign(cfg, read_tbl((pd2_data / TBL).read_bytes()), pd2_data / "global" / "excel", strings)
    cfg_file.write_text(dump(cfg), "utf-8", newline="\n")
    print(f"assigned {len(added)} new string IDs ({len(cfg['ids'])} listed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
