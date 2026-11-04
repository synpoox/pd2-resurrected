"""Reads the client's units from a running D2R 93847 game (read-only, no plugin): counts nova sub-missiles and pets.

The client keeps missiles in a 128-bucket hash table (unit guid & 0x7f), lists linked through unit +0x158. Found in the
client owner lookup at RVA 0x9a7b0 of the 93847 image (see docs/research/nova-orb-hits.md). The client's players,
monsters, objects and items are in tables of the same shape at 0x2a23910 + type * 0x400 (read from a running game).
"""
import ctypes
import ctypes.wintypes as wt
import struct
import time

IMAGE_STAMP = 0x6ab3782c  # rvas_93847.h ImageStamp
CLIENT_MISSILES = 0x2a25d10  # 128 x Unit*
CLIENT_UNITS = 0x2a23910  # + type * 0x400: 128 x Unit* of players, monsters, objects, items
UNIT_TYPE, UNIT_CLASS, UNIT_GUID, UNIT_MODE, UNIT_NEXT = 0x00, 0x04, 0x08, 0x0c, 0x158
MONSTER_GONE = (0, 12)  # monmode death, dead: the client keeps corpses in the table
MONSTER, MISSILE = 1, 3

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

    def units(self, kind: int) -> dict[int, int]:
        """guid -> class row (missiles.txt, monstats.txt, ...) of every client unit of this type now alive."""
        out = {}
        table = self.base + (CLIENT_MISSILES if kind == MISSILE else CLIENT_UNITS + kind * 8 * 128)
        for unit in struct.unpack("<128Q", self.read(table, 8 * 128)):
            for _ in range(4096):  # a broken list must not hang the check
                if not unit:
                    break
                try:
                    head = self.read(unit, UNIT_NEXT + 8)
                except OSError:
                    break  # freed while we walked it
                found, row, guid, mode = struct.unpack_from("<IIII", head, 0)
                if found == kind and not (kind == MONSTER and mode in MONSTER_GONE):
                    out[guid] = row
                unit = struct.unpack_from("<Q", head, UNIT_NEXT)[0]
        return out

    def missiles(self) -> dict[int, int]:
        """guid -> missiles.txt row of every client missile now alive."""
        return self.units(MISSILE)


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
