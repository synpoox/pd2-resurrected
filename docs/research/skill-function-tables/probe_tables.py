"""Read D2R 93847 function tables from the memory-layout dump (file offset == RVA)."""
import struct, sys
D = open(r"C:\ghidra-work\D2RLoader_6ab3782c_dump.exe", "rb").read()
u32 = lambda r: struct.unpack_from("<I", D, r)[0]
u64 = lambda r: struct.unpack_from("<Q", D, r)[0]
# runtime base: srvdofunc slot 11 is 0x5562b0 (registry)
base = u64(0x238ea00 + 11 * 8) - 0x5562b0
assert base & 0xffff == 0, hex(base)
print("base", hex(base))
for name, rva in [("prg bound 0x1d1945c", 0x1d1945c), ("cltst bound", 0x235f7c0), ("cltdo bound", 0x235f7c4),
                  ("srvhit bound", 0x2391404), ("clthit bound", 0x2358a64)]:
    print(name, u32(rva))
def table(name, rva, n):
    vals = [u64(rva + i * 8) for i in range(n)]
    rv = [v - base if v else 0 for v in vals]
    print(f"\n{name} @0x{rva:x} n={n}")
    print("  null:", [i for i, v in enumerate(rv) if not v])
    # slots sharing one target with another slot = default/stub
    from collections import Counter
    c = Counter(v for v in rv if v)
    print("  shared targets:", {hex(t): [i for i, v in enumerate(rv) if v == t] for t, k in c.items() if k > 1})
    return rv
T = {}
T["srvst"] = table("srvstfunc", 0x238e710, 91)
T["srvdo"] = table("srvdofunc", 0x238ea00, 191)
T["cltst"] = table("cltstfunc", 0x235f590, u32(0x235f7c0))
T["cltdo"] = table("cltdofunc", 0x235f7d0, u32(0x235f7c4))
T["msrvdo"] = table("missile srvdo", 0x2390e80, 60)
T["msrvhit"] = table("missile srvhit", 0x2391030, u32(0x2391404))
T["mcltdo"] = table("missile cltdo", 0x2358430, 60)
T["mclthit"] = table("missile clthit", 0x2358710, u32(0x2358a64))
for k in ("srvdo", "cltdo"):
    for s in ((41, 143, 163) if k == "srvdo" else (15, 22, 83)):
        print(k, s, hex(T[k][s]))
print("\nsrvdmg bound", u32(0x2391408), "prop bound", u32(0x2386bd8))
T["msrvdmg"] = table("missile srvdmg", 0x2391270, u32(0x2391408))
T["prop"] = table("property funcs", 0x2386ab0, u32(0x2386bd8))
T["mcltdo92"] = table("missile cltdo (to clthit start)", 0x2358430, (0x2358710 - 0x2358430) // 8)
T["msrvdo54"] = table("missile srvdo (to srvhit start)", 0x2390e80, (0x2391030 - 0x2390e80) // 8)
