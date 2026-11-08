"""Scan PD2-only presets: blocked-floor (water) tiles by DT1, inside/outside pasted rooms."""
import sys, collections, json, functools
from pathlib import Path
sys.path.insert(0, 'tools')
import pd2_presets as P, pd2_painter as PA
from pd2_ids import load
D = Path(r"C:/Users/micah/orca/workspaces/pd2-resurrected/d2rAI-toolkit/d2r-data/93847/data")
out = Path('out/data')
sets, standins = P.read_rule(Path('merge/presets-hd.tsv'))
presets = P.pd2_only_presets(out, len(load(D, 'lvlprest').records()))
on_d2r = [p for p in presets if all(f in standins for f in p[2] if f.startswith('pd2assets/'))]
tiles, pd2_tiles = P.Tiles(D/'global'/'tiles'), P.Tiles(out/'global'/'tiles')
only = sys.argv[1:]
layouts = {ds1: (ts, P.layout((out/'global'/'tiles'/ds1).read_bytes(), P.stand_in(files, tiles, pd2_tiles, standins)))
           for _, ts, files, ds1s in on_d2r for ds1 in ds1s if not only or any(o in ds1 for o in only)}
keep = set().union(*(P.wanted(l) for _, l in layouts.values()))
loaded = {t[0] for k in keep for t in k[1]}
stock = []
for p in sorted((D/P.PRESETS).rglob('*.json')):
    ds1 = D/'global'/'tiles'/p.relative_to(D/P.PRESETS).with_suffix('.ds1')
    if not ds1.is_file(): continue
    data = ds1.read_bytes(); listed = P.header_dt1s(data)
    if loaded.isdisjoint(listed): continue
    stock.append((P.layout(data, tiles.of(listed)), json.loads(p.read_text('utf-8-sig')), set(listed), p.stem))
tot = collections.Counter()
for ds1, (ts, lay) in layouts.items():
    drawn = {t[0] for k in P.wanted(lay) for t in k[1]}
    own = [(l, pr) for l, pr, listed, _ in stock if not drawn.isdisjoint(listed)]
    names = [n for l, pr, listed, n in stock if not drawn.isdisjoint(listed)]
    pasted, taken = P.rooms(lay, own)
    water = {c for c, k in lay.keys.items() if k[0] == 'f' and PA.blocked_floor(lay.flags[c])}
    by = collections.Counter(); byin = collections.Counter()
    for c in water:
        dt = lay.keys[c][1][0][0]
        (byin if c in taken else by)[dt] += 1
    print(ds1, ts, lay.width, lay.height, 'water', len(water), 'outside', dict(by), 'inside', dict(byin))
    print('   pasted', [(names[n], x, y) for n, x, y in pasted][:12])
