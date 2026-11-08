"""How many PD2 DT1 tiles (weighted by use in PD2-only DS1) are pixel-identical copies of a D2R DT1 tile."""
import collections, hashlib, struct, sys
from pathlib import Path
sys.path.insert(0, 'tools')
import pd2_presets as P
from pd2_ids import load

D = Path(r"C:/Users/micah/orca/workspaces/pd2-resurrected/d2rAI-toolkit/d2r-data/93847/data")
OUT = Path('out/data')


def tile_hashes(data):
    """(orientation, main, sub) -> hash of the tile's block headers and pixel data, per tile in the DT1."""
    count, at = struct.unpack_from('<2i', data, 268)
    out = {}
    for k in range(count):
        h = at + k * 96
        o, m, s = struct.unpack_from('<3i', data, h + 20)
        ptr, length, blocks = struct.unpack_from('<3i', data, h + 72)
        # block headers carry a file offset; hash per block its (x, y, format, length) and pixels
        parts = []
        for b in range(blocks):
            bx, by, _, gx, gy, fmt, ln, _, off = struct.unpack_from('<hhhbbhihi', data, ptr + b * 20)
            parts.append(struct.pack('<hhbbhi', bx, by, gx, gy, fmt, ln) + data[ptr + off:ptr + off + ln])
        out.setdefault((o, m, s), hashlib.sha1(b''.join(parts) + data[h + 40:h + 65]).hexdigest())
    return out


d2r = {}
for f in (D / 'global/tiles').rglob('*.dt1'):
    try:
        for k, hsh in tile_hashes(f.read_bytes()).items():
            d2r.setdefault(hsh, (str(f.relative_to(D / 'global/tiles')).replace('\\', '/'), k))
    except struct.error:
        pass
print('D2R tile images:', len(d2r))

presets = P.pd2_only_presets(OUT, len(load(D, 'lvlprest').records()))
sets, standins, _ = P.read_rule(Path('merge/presets-hd.tsv'))
use = collections.Counter()
for name, ts, files, ds1s in presets:
    pd2 = [f for f in files if f.startswith('pd2assets/')]
    if not pd2 or all(f in standins for f in pd2):
        continue
    dt1s = {f: P.dt1_tiles((OUT / 'global/tiles' / f).read_bytes()) for f in pd2 if (OUT / 'global/tiles' / f).is_file()}
    for ds1 in ds1s:
        lay = P.layout((OUT / 'global/tiles' / ds1).read_bytes(), dt1s)
        for key in lay.keys.values():
            for t in key[1]:
                if t[0]:
                    use[t] += 1
hashes = {}
for f in {t[0] for t in use}:
    hashes[f] = tile_hashes((OUT / 'global/tiles' / f).read_bytes())
tot = sum(use.values())
same = sum(v for t, v in use.items() if hashes[t[0]].get(t[1:]) in d2r)
print(f'tile uses on PD2 DT1 in presets waiting for stand-ins: {tot}; pixel-identical to a D2R tile: {same} ({100 * same / tot:.1f}%)')
per = collections.defaultdict(lambda: [0, 0])
for t, v in use.items():
    per[t[0]][0] += v
    per[t[0]][1] += v * (hashes[t[0]].get(t[1:]) in d2r)
for f, (a, b) in sorted(per.items(), key=lambda kv: -kv[1][0])[:25]:
    print(f'   {f:50} {a:7} {100 * b / a:5.1f}%')
