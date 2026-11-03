"""Reads the client's missile units from a running D2R 93847 game (read-only, no plugin): counts nova sub-missiles.

The client keeps missiles in a 128-bucket hash table (unit guid & 0x7f), lists linked through unit +0x158. Found in the
client owner lookup at RVA 0x9a7b0 of the 93847 image (see docs/research/nova-orb-hits.md).
"""
import ctypes
import ctypes.wintypes as wt
import struct
import time

IMAGE_STAMP = 0x6ab3782c  # rvas_93847.h ImageStamp
CLIENT_MISSILES = 0x2a25d10  # 128 x Unit*
UNIT_TYPE, UNIT_CLASS, UNIT_GUID, UNIT_NEXT = 0x00, 0x04, 0x08, 0x158
MISSILE = 3

_k32 = ctypes.WinDLL("kernel32", use_last_error=True)
_psapi = ctypes.WinDLL("psapi", use_last_error=True)
_k32.OpenProcess.restype = wt.HANDLE
_k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_size_t)]


class Game:
    def __init__(self, pid: int):
        self.h = _k32.OpenProcess(0x0410, False, pid)  # QUERY_INFORMATION | VM_READ
        if not self.h:
            raise OSError(f"OpenProcess({pid}) failed: {ctypes.get_last_error()}")
        mods, need = (ctypes.c_void_p * 1)(), wt.DWORD()
        if not _psapi.EnumProcessModulesEx(self.h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3):  # LIST_MODULES_ALL
            raise OSError(f"EnumProcessModulesEx failed: {ctypes.get_last_error()}")
        self.base = mods[0]
        pe = self.u32(self.base + 0x3c)
        stamp = self.u32(self.base + pe + 8)
        if stamp != IMAGE_STAMP:
            raise RuntimeError(f"image stamp {stamp:#x}, the probe knows {IMAGE_STAMP:#x} (93847) only")

    def read(self, addr: int, n: int) -> bytes:
        buf, got = ctypes.create_string_buffer(n), ctypes.c_size_t()
        if not _k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) or got.value != n:
            raise OSError(f"read {addr:#x} failed")
        return buf.raw

    def u32(self, addr: int) -> int:
        return struct.unpack("<I", self.read(addr, 4))[0]

    def missiles(self) -> dict[int, int]:
        """guid -> missiles.txt row of every client missile now alive."""
        out = {}
        for unit in struct.unpack("<128Q", self.read(self.base + CLIENT_MISSILES, 8 * 128)):
            for _ in range(4096):  # a broken list must not hang the check
                if not unit:
                    break
                try:
                    head = self.read(unit, UNIT_NEXT + 8)
                except OSError:
                    break  # freed while we walked it
                kind, row, guid = struct.unpack_from("<III", head, 0)
                if kind == MISSILE:
                    out[guid] = row
                unit = struct.unpack_from("<Q", head, UNIT_NEXT)[0]
        return out


def bursts(game: Game, row: int, seconds: float, gap: float = 0.25) -> list[int]:
    """Watch for `seconds`; missiles of `row` first seen within `gap` of each other form one burst. Burst sizes."""
    seen: dict[int, float] = {}
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        now = time.monotonic()
        for guid, r in game.missiles().items():
            if r == row and guid not in seen:
                seen[guid] = now
        time.sleep(0.005)
    sizes, last = [], None
    for t in sorted(seen.values()):
        if last is None or t - last > gap:
            sizes.append(0)
        sizes[-1] += 1
        last = t
    return sizes
