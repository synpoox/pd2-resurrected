"""data/global/animdata.d2, the classic frame timing D2R's server still runs on: a unit's mode (attack, cast, death)
ends after the frames its record gives. PD2's new monster and object codes (`$7` Lucion, `!3` Na-Krul, `!8` Iskatu, ...)
have no record in D2R's file, so their attacks and casts never ended and their AI was never called again (#502).

The merged file is D2R's with every PD2 record D2R lacks added, and PD2's change to a record both have, except the
records merge/animdata.tsv names (one name per line, such as `AMA1XBW`), which keep D2R's (docs/deviations.md). A line
naming a record PD2 does not change fails the merge, so the list never goes stale.

The format (1.10 to D2R): 256 buckets, each a u32 count and that many 160-byte records: the name (COF name: code, mode,
weapon class) in 8 bytes, u32 frames per direction, u32 speed, 144 frame-event bytes. A record is in the bucket of
its upper-cased name's byte sum, mod 256.
"""
from __future__ import annotations

import struct
from pathlib import Path

FILE = Path("global") / "animdata.d2"
SIZE = 160


class AnimDataError(ValueError):
    pass


def name_of(rec: bytes) -> str:
    return rec[:8].split(b"\0")[0].decode("latin-1").upper()


def bucket(name: str) -> int:
    return sum(name.encode("latin-1")) & 0xFF


def read(data: bytes) -> list[list[bytes]]:
    """animdata.d2 -> its 256 buckets of records."""
    buckets, o = [], 0
    for _ in range(256):
        (n,) = struct.unpack_from("<I", data, o)
        buckets.append([data[o + 4 + k * SIZE:o + 4 + (k + 1) * SIZE] for k in range(n)])
        o += 4 + n * SIZE
    if o != len(data):
        raise AnimDataError(f"animdata.d2: {len(data) - o} bytes after the last bucket")
    return buckets


def write(buckets: list[list[bytes]]) -> bytes:
    return b"".join(struct.pack("<I", len(b)) + b"".join(b) for b in buckets)


def merge(d2r: bytes, pd2: bytes, keep: set[str]) -> tuple[bytes, int, int]:
    """D2R's file with PD2's records merged in -> (file, records added, records changed). keep: upper-case names."""
    out = read(d2r)
    at = {}  # name -> (bucket, index) of D2R's records; D2R repeats some names, and every copy takes the change
    for b, recs in enumerate(out):
        for k, rec in enumerate(recs):
            at.setdefault(name_of(rec), []).append((b, k))
    added = changed = 0
    stale = set(keep)
    for rec in (r for recs in read(pd2) for r in recs):
        name = name_of(rec)
        if name not in at:
            out[bucket(name)].append(rec)
            added += 1
        elif any(out[b][k][8:] != rec[8:] for b, k in at[name]):
            stale.discard(name)
            if name not in keep:
                for b, k in at[name]:
                    out[b][k] = out[b][k][:8] + rec[8:]  # D2R's name bytes, PD2's timing
                changed += 1
    if stale:
        raise AnimDataError(f"lines for records PD2 does not change: {', '.join(sorted(stale))}")
    return write(out), added, changed


def read_lines(path: Path) -> set[str]:
    return {s.strip().upper() for s in path.read_text("utf-8").splitlines() if s.strip() and not s.startswith("#")}


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/animdata.tsv: write the merged animdata.d2."""
    if not (d2r_data / FILE).is_file():
        raise AnimDataError(f"no {d2r_data / FILE}: run `modkit fetch --prefix data/global/animdata.d2`")
    data, added, changed = merge((d2r_data / FILE).read_bytes(), (pd2_data / FILE).read_bytes(), read_lines(rule))
    (out_data / FILE).parent.mkdir(parents=True, exist_ok=True)
    (out_data / FILE).write_bytes(data)
    print(f"  animdata: {added} PD2 records added, {changed} changed")
