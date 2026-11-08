"""Build the named PD2-only outdoor presets as apply() does, and border-score them (no merge). Usage:
python quick.py <scratch> <outdir> name..."""
import functools, json, sys, time
from pathlib import Path
sys.path.insert(0, 'tools'); sys.path.insert(0, sys.argv[1])
import border_score as B, pd2_presets as P
import os
if os.environ.get("V"): __import__(os.environ["V"])
from pd2_presets import painter
from pd2_ids import load
t0 = time.time()
D, OUT, out = B.D, B.OUT, Path(sys.argv[2]); out.mkdir(exist_ok=True, parents=True)
names = set(sys.argv[3:])
sets, standins, water_rule = P.read_rule(Path('merge/presets-hd.tsv'))
presets = P.pd2_only_presets(OUT, len(load(D, 'lvlprest').records()))
on_d2r = [p for p in presets if all(f in standins for f in p[2] if f.startswith('pd2assets/'))]
terrain = json.loads((D / P.PRESETS / f'{P.TERRAIN}.json').read_text('utf-8-sig'))['terrain']
tiles, pd2_tiles = P.Tiles(D / 'global/tiles'), P.Tiles(OUT / 'global/tiles')
layouts = {ds1: P.layout((OUT / 'global/tiles' / ds1).read_bytes(), P.stand_in(files, tiles, pd2_tiles, standins))
           for _, _, files, ds1s in on_d2r for ds1 in ds1s}
groups = {}
for _, ts, _, ds1s in on_d2r:
    for d in ds1s:
        if d.rsplit('/', 1)[-1][:-4] in names:
            groups.setdefault(ts, []).append(d)
keep = set().union(*(P.wanted(layouts[d]) for v in groups.values() for d in v))
loaded = {t[0] for k in keep for t in k[1]}
water = P.water_planes(D, water_rule)
kit, stock = P.Kit(), []
for p in sorted((D / P.PRESETS).rglob('*.json')):
    ds1 = D / 'global/tiles' / p.relative_to(D / P.PRESETS).with_suffix('.ds1')
    if not ds1.is_file(): continue
    data = ds1.read_bytes(); listed = P.header_dt1s(data)
    if loaded.isdisjoint(listed): continue
    stock.append((P.layout(data, tiles.of(listed)), json.loads(p.read_text('utf-8-sig')), set(listed)))
    kit.add(*stock[-1][:2], keep)
@functools.cache
def prefab(path):
    f = D / path.removeprefix('data/')
    ents = json.loads(f.read_text('utf-8-sig')).get('entities', []) if f.is_file() else []
    return [(t['position']['x'], t['position']['z']) for t in map(P._transform, ents) if t] or [(0.0, 0.0)]
B.SCR = out
for ts, ds1s in groups.items():
    drawn = {t[0] for ds1 in ds1s for k in P.wanted(layouts[ds1]) for t in k[1]}
    own = [(l, pr) for l, pr, listed in stock if not drawn.isdisjoint(listed)]
    trees = painter.harvest(own)
    for ds1 in ds1s:
        lay = layouts[ds1]; n = ds1.rsplit('/', 1)[-1][:-4]
        preset, _ = P.build(lay, kit, Path(ds1).stem, ds1, sets[ts.lower()], terrain, own,
                            trees if P.draws_trees(lay) else None, prefab, water)
        (out / f'{n}.json').write_text(json.dumps(preset, separators=(',', ':')), 'utf-8')
        r = B.check(lay, preset['entities'], n)
        print(f'{n:22} ring {r[0]:6}  covered {100 * r[1]:5.1f}%  gaps {r[2]:5}  leaks {r[3]:4}  misplaced {r[4]:5} of {r[5]}', flush=True)
print(f'{time.time() - t0:.0f}s')
