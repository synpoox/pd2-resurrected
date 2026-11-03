"""Read files out of a classic (v1) MPQ archive, as PD2 ships them.

Covers what PD2's archives use: encrypted hash/block tables, per-file encryption (with and without the fixed key),
single-unit and sectored files, and zlib, bzip2 and PKWARE DCL implode compression. Read-only, no dependencies.
"""
from __future__ import annotations

import bz2
import struct
import zlib
from pathlib import Path

IMPLODE, COMPRESS, ENCRYPTED, FIX_KEY, SINGLE_UNIT, EXISTS = 0x100, 0x200, 0x10000, 0x20000, 0x1000000, 0x80000000
MASK = 0xFFFFFFFF


def _crypt_table() -> list[int]:
    t = [0] * 0x500
    seed = 0x00100001
    for i in range(0x100):
        for j in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            hi = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            t[i + j * 0x100] = hi | (seed & 0xFFFF)
    return t


CRYPT = _crypt_table()


def hash_string(s: str, kind: int) -> int:
    """kind: 0 = hash table index, 1 = name hash A, 2 = name hash B, 3 = encryption key."""
    s1, s2 = 0x7FED7FED, 0xEEEEEEEE
    for c in s.upper().encode("latin-1"):
        s1 = CRYPT[kind * 0x100 + c] ^ ((s1 + s2) & MASK)
        s2 = (c + s1 + s2 + (s2 << 5) + 3) & MASK
    return s1


def decrypt(data: bytes, key: int) -> bytes:
    n = len(data) // 4
    out = []
    s2 = 0xEEEEEEEE
    for v in struct.unpack(f"<{n}I", data[:n * 4]):
        s2 = (s2 + CRYPT[0x400 + (key & 0xFF)]) & MASK
        v ^= (key + s2) & MASK
        out.append(v)
        key = ((((~key) << 0x15) + 0x11111111) & MASK) | (key >> 0x0B)
        s2 = (v + s2 + (s2 << 5) + 3) & MASK
    return struct.pack(f"<{n}I", *out) + data[n * 4:]


# ---- PKWARE DCL explode (a port of zlib's contrib/blast)
_LITLEN = [11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8, 9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24,
           12, 11, 7, 9, 11, 12, 6, 7, 22, 5, 7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12, 8, 12,
           5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27, 44, 253, 253, 253, 252, 252, 252, 13, 12, 45,
           12, 45, 12, 61, 12, 45, 44, 173]
_LENLEN = [2, 35, 36, 53, 38, 23]
_DISTLEN = [2, 20, 53, 230, 247, 151, 248]
_BASE = [3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264]
_EXTRA = [0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8]


def _huffman(rep: list[int]) -> tuple[list[int], list[int]]:
    lengths = []
    for r in rep:
        lengths += [r & 15] * ((r >> 4) + 1)
    count = [0] * 16
    for n in lengths:
        count[n] += 1
    offs = [0] * 16
    for i in range(1, 15):
        offs[i + 1] = offs[i] + count[i]
    sym = [0] * len(lengths)
    for i, n in enumerate(lengths):
        sym[offs[n]] = i
        offs[n] += 1
    return count, sym


_LIT, _LEN, _DIST = _huffman(_LITLEN), _huffman(_LENLEN), _huffman(_DISTLEN)


def explode(d: bytes) -> bytes:
    pos = buf = cnt = 0

    def bits(n: int) -> int:
        nonlocal pos, buf, cnt
        while cnt < n:
            if pos >= len(d):
                raise ValueError("implode: input ended early")
            buf |= d[pos] << cnt
            pos += 1
            cnt += 8
        v = buf & ((1 << n) - 1)
        buf >>= n
        cnt -= n
        return v

    def decode(h: tuple[list[int], list[int]]) -> int:
        count, sym = h
        code = first = index = 0
        for n in range(1, 16):
            code |= bits(1) ^ 1  # codes are stored bit-inverted
            c = count[n]
            if code - c < first:
                return sym[index + (code - first)]
            index += c
            first = (first + c) << 1
            code <<= 1
        raise ValueError("implode: bad code")

    coded_lit, dict_bits = bits(8), bits(8)
    out = bytearray()
    while True:
        if bits(1):
            s = decode(_LEN)
            length = _BASE[s] + bits(_EXTRA[s])
            if length == 519:  # end marker
                return bytes(out)
            shift = 2 if length == 2 else dict_bits
            dist = (decode(_DIST) << shift) + bits(shift) + 1
            for _ in range(length):
                out.append(out[-dist])
        else:
            out.append(decode(_LIT) if coded_lit else bits(8))


def _decompress(data: bytes) -> bytes:
    """One COMPRESS-flagged chunk: a mask byte naming the method, then the data."""
    m = data[0]
    if m == 0x02:
        return zlib.decompress(data[1:])
    if m == 0x08:
        return explode(data[1:])
    if m == 0x10:
        return bz2.decompress(data[1:])
    raise ValueError(f"unsupported compression mask 0x{m:02x}")


class Archive:
    def __init__(self, path: Path | str):
        self.data = Path(path).read_bytes()
        self.base = next((o for o in range(0, len(self.data), 512) if self.data[o:o + 4] == b"MPQ\x1a"), None)
        if self.base is None:
            raise ValueError(f"{path}: no MPQ header")
        (_, _, _, _, shift, hash_off, block_off, hash_n, block_n) = struct.unpack_from("<4sIIHHIIII", self.data, self.base)
        self.sector = 512 << shift
        self.hashes = list(struct.iter_unpack("<IIHHI", self._table(hash_off, hash_n, "(hash table)")))
        self.blocks = list(struct.iter_unpack("<IIII", self._table(block_off, block_n, "(block table)")))

    def _table(self, off: int, n: int, name: str) -> bytes:
        start = self.base + off
        return decrypt(self.data[start:start + n * 16], hash_string(name, 3))

    def _block(self, name: str):
        n = len(self.hashes)
        a, b = hash_string(name, 1), hash_string(name, 2)
        i = hash_string(name, 0) & (n - 1)
        for _ in range(n):
            ha, hb, _, _, block = self.hashes[i]
            if block == 0xFFFFFFFF:
                return None
            if ha == a and hb == b and block != 0xFFFFFFFE:
                return self.blocks[block]
            i = (i + 1) & (n - 1)
        return None

    def has(self, name: str) -> bool:
        blk = self._block(name)
        return blk is not None and bool(blk[3] & EXISTS)

    def read(self, name: str) -> bytes:
        blk = self._block(name)
        if blk is None or not blk[3] & EXISTS:
            raise KeyError(name)
        off, packed, size, flags = blk
        raw = self.data[self.base + off:self.base + off + packed]
        key = hash_string(name.replace("/", "\\").split("\\")[-1], 3)
        if flags & FIX_KEY:
            key = ((key + off) ^ size) & MASK
        if flags & SINGLE_UNIT:
            if flags & ENCRYPTED:
                raw = decrypt(raw, key)
            return self._unpack(raw, size, flags)
        n = (size + self.sector - 1) // self.sector
        head = raw[:4 * (n + 1)]
        if flags & ENCRYPTED:
            head = decrypt(head, (key - 1) & MASK)
        pos = struct.unpack(f"<{n + 1}I", head)
        out = bytearray()
        for i in range(n):
            chunk = raw[pos[i]:pos[i + 1]]
            if flags & ENCRYPTED:
                chunk = decrypt(chunk, (key + i) & MASK)
            out += self._unpack(chunk, min(self.sector, size - i * self.sector), flags)
        if len(out) != size:
            raise ValueError(f"{name}: read {len(out)} bytes, expected {size}")
        return bytes(out)

    @staticmethod
    def _unpack(chunk: bytes, size: int, flags: int) -> bytes:
        if len(chunk) >= size:  # stored as is when packing would not shrink it
            return chunk[:size]
        if flags & COMPRESS:
            return _decompress(chunk)
        if flags & IMPLODE:
            return explode(chunk)
        return chunk

    def names(self) -> list[str]:
        """Names from the archive's own (listfile)."""
        text = self.read("(listfile)").decode("latin-1")
        return [n.strip() for n in text.replace(";", "\n").splitlines() if n.strip()]
