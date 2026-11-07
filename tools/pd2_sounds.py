"""PD2's own sounds (UR8, #206): its WAVs as FLAC and their sounds.txt rows. D2R plays only FLAC, and its sounds.txt has
other columns than PD2's, so D2R's table is kept (docs/deviations.md) and the listed PD2 rows are appended to it.

merge/sounds.tsv has one line per PD2 sound to port: `sound <tab> like`, a D2R HD row (channel `*_hd`) whose D2R-only
columns (Channel, pitch, 3dSpread and the rest) the new row copies. The other columns D2R shares with PD2 take PD2's
values; PD2's `Volume` sets `Volume Min` and `Volume Max`. The WAV is written as FLAC at PD2's path under
data/hd/global/sfx, where the Pyre-Flies filter's `*_hd` drop sounds already play from. Rows append in PD2's order, so
a group (`Group Size`) stays together when all its rows are listed. A line for a sound D2R has or PD2 lacks fails the
merge, as does a missing WAV.
"""
from __future__ import annotations

import hashlib
import io
import wave
from pathlib import Path

from pd2_ids import Table, excel

SFX = Path("global") / "sfx"
HD_SFX = Path("hd") / "global" / "sfx"
BLOCK = 4096
SIZE_CODE = {8: 1, 12: 2, 16: 4, 20: 5, 24: 6}  # FLAC frame header sample-size codes


class SoundError(ValueError):
    pass


def _crc(data: bytes, poly: int, bits: int) -> int:
    crc, top, mask = 0, 1 << (bits - 1), (1 << bits) - 1
    for b in data:
        crc ^= b << (bits - 8)
        for _ in range(8):
            crc = ((crc << 1) ^ poly if crc & top else crc << 1) & mask
    return crc


def _utf8(n: int) -> bytes:
    """FLAC's UTF-8-style coding of a frame number."""
    if n < 0x80:
        return bytes([n])
    tail = []
    while True:
        tail.insert(0, 0x80 | (n & 0x3F))
        n >>= 6
        lead_bits = 6 - len(tail)  # payload bits left in the lead byte
        if n < (1 << lead_bits):
            return bytes([(0xFF00 >> (len(tail) + 1)) & 0xFF | n]) + bytes(tail)


def flac(wav: bytes) -> bytes:
    """A PCM WAV as FLAC, verbatim subframes (no compression: PD2's 30 files are 2.5 MB)."""
    with wave.open(io.BytesIO(wav)) as w:
        ch, width, rate, pcm = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.readframes(w.getnframes())
    bps = width * 8
    if bps not in SIZE_CODE:
        raise SoundError(f"{bps}-bit WAV")
    if width == 1:  # 8-bit WAV is unsigned, FLAC signed
        pcm = bytes((b - 128) & 0xFF for b in pcm)
    total = len(pcm) // (ch * width)
    block = min(BLOCK, total) or 1
    info = (block.to_bytes(2, "big") * 2 + bytes(6)
            + ((rate << 44) | ((ch - 1) << 41) | ((bps - 1) << 36) | total).to_bytes(8, "big")
            + hashlib.md5(pcm).digest())
    out = [b"fLaC", bytes([0x80, 0, 0, len(info)]), info]
    for n, start in enumerate(range(0, total, BLOCK)):
        count = min(BLOCK, total - start)
        head = bytes([0xFF, 0xF8, 0x70, ((ch - 1) << 4) | (SIZE_CODE[bps] << 1)]) + _utf8(n) + (count - 1).to_bytes(2, "big")
        frame = head + bytes([_crc(head, 0x07, 8)])
        for c in range(ch):
            frame += b"\x02" + b"".join(pcm[p:p + width][::-1]  # verbatim subframe, samples big-endian
                                        for p in range((start * ch + c) * width, (start + count) * ch * width, ch * width))
        out.append(frame + _crc(frame, 0x8005, 16).to_bytes(2, "big"))
    return b"".join(out)


def read_lines(path: Path) -> dict[str, str]:
    likes = {}
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if line and not line.startswith("#"):
            parts = line.split("\t")
            if len(parts) != 2 or not all(parts):
                raise SoundError(f"line {n}: want 'sound<tab>like'")
            likes[parts[0]] = parts[1]
    return likes


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/sounds.tsv: the listed PD2 sounds appended to D2R's sounds.txt, WAVs as FLAC."""
    likes = read_lines(rule)
    pd2 = Table(excel(pd2_data) / "sounds.txt")
    merged_path = excel(out_data) / "sounds.txt"
    merged = Table(merged_path if merged_path.is_file() else excel(d2r_data) / "sounds.txt")
    h, ph = merged.header, pd2.header
    have = {r[0]: r for r in merged.rows}
    shared = [c for c in h if c in ph and c not in ("Sound", "FileName")]
    left = dict(likes)
    for p in pd2.rows:
        name = p[0]
        if name not in left:
            continue
        like = have.get(left.pop(name))
        if name in have:
            raise SoundError(f"'{name}' is not new: D2R has it")
        if like is None:
            raise SoundError(f"'{name}': D2R has no row '{likes[name]}'")
        file = p[ph.index("FileName")]
        if not file.lower().endswith(".wav"):
            raise SoundError(f"'{name}': {file} is not a WAV")
        wav = pd2_data / SFX / file.replace("\\", "/").lower()
        if not wav.is_file():
            raise SoundError(f"'{name}': no {wav.relative_to(pd2_data)} in pd2-data (run tools/pd2_extract.py)")
        row = list(like)
        row[0], row[h.index("*Index")], row[h.index("Redirect")] = name, str(len(merged.rows)), ""
        row[h.index("FileName")] = file[:-4] + ".flac"
        for c in shared:
            row[h.index(c)] = p[ph.index(c)]
        row[h.index("Volume Min")] = row[h.index("Volume Max")] = p[ph.index("Volume")]
        merged.rows.append(row)
        have[name] = row
        dest = out_data / HD_SFX / (file[:-4] + ".flac").replace("\\", "/").lower()
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(flac(wav.read_bytes()))
    if left:
        raise SoundError(f"lines for sounds PD2 lacks: {', '.join(sorted(left))}")
    merged_path.parent.mkdir(parents=True, exist_ok=True)
    merged_path.write_bytes(merged.text().encode("latin-1"))
    print(f"  sounds: {len(likes)} PD2 sounds appended, WAVs as FLAC")
