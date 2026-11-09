"""Why each border-ring gap is a gap, per built outdoor preset (out/). python attrib.py <scr>"""
import collections, json, math, sys
from pathlib import Path
sys.path.insert(0, 'tools'); sys.path.insert(0, sys.argv[1])
import border_score as B, pd2_presets as P, pd2_painter as PA
from pd2_ids import load
D, OUT = B.D, B.OUT
sets, standins, water_rule = P.read_rule(Path('merge/presets-hd.tsv'))
wet = frozenset(water_rule)
presets = P.pd2_only_presets(OUT, len(load(D, 'lvlprest').records()))
on_d2r = [p for p in presets if all(f in standins for f in p[2] if f.startswith('pd2assets/'))]
tiles, pd2_tiles = P.Tiles(D / 'global/tiles'), P.Tiles(OUT / 'global/tiles')
layouts = {ds1: P.layout((OUT / 'global/tiles' / ds1).read_bytes(), P.stand_in(files, tiles, pd2_tiles, standins))
           for _, _, files, ds1s in on_d2r for ds1 in ds1s}
layouts = {d: l for d, l in layouts.items() if P.draws_trees(l)}
keep = set().union(*(P.wanted(l) for l in layouts.values())); loaded = {t[0] for k in keep for t in k[1]}
stock = []
for p in sorted((D / P.PRESETS).rglob('*.json')):
    ds1 = D / 'global/tiles' / p.relative_to(D / P.PRESETS).with_suffix('.ds1')
    if not ds1.is_file(): continue
    data = ds1.read_bytes(); listed = P.header_dt1s(data)
    if loaded.isdisjoint(listed): continue
    stock.append((P.layout(data, tiles.of(listed)), json.loads(p.read_text('utf-8-sig')), set(listed)))
groups = {}
for _, ts, _, ds1s in on_d2r:
    for d in ds1s:
        if d in layouts: groups.setdefault(ts, []).append(d)
for ts, ds1s in groups.items():
    drawn = {t[0] for d in ds1s for k in P.wanted(layouts[d]) for t in k[1]}
    own = [(l, pr) for l, pr, listed in stock if not drawn.isdisjoint(listed)]
    for ds1 in sorted(set(ds1s)):
        lay = layouts[ds1]; n = ds1.rsplit('/', 1)[-1][:-4]
        _, taken = P.rooms(lay, own)
        ents = json.loads((OUT / P.PRESETS / Path(ds1).with_suffix('.json')).read_text('utf-8'))['entities']
        top = {}
        for x0, x1, z0, z1, h in B.boxes(ents):
            if x1 - x0 > 400 or z1 - z0 > 400: continue
            for tx in range(math.floor(x0/10), math.floor(x1/10)+1):
                for tz in range(math.floor(z0/10), math.floor(z1/10)+1): top[(tx, tz)] = max(top.get((tx, tz), -1e9), h)
        play = PA.playable(lay, wet)
        ring = ({(x+dx, y+dy) for x, y in play for dx, dy in PA.STEPS} - play) & set(lay.keys)
        gaps = {t for t in ring if top.get(t, -1e9) < B.TALL}
        why = collections.Counter()
        for t in gaps:
            k = lay.keys[t]
            near = any((t[0]-dx, t[1]-dy) in play for dx, dy in ((1, 0), (0, 1)))
            if t in taken: why['room'] += 1
            elif any(d[0] is None or d[0].startswith('pd2assets') for d in k[1]): why['pd2art'] += 1
            elif t[0] in (0, lay.width - 1) or t[1] in (0, lay.height - 1): why['metric'] += 1
            elif top.get(t, -1e9) > 0.5: why['low piece'] += 1
            elif k[0] == 'w': why['kit wall'] += 1
            elif near: why['near ground'] += 1
            else: why['far ground'] += 1
        cov = 1 - len(gaps) / max(len(ring), 1)
        need = max(0, math.ceil(0.9 * len(ring)) - (len(ring) - len(gaps)))
        print(f'{n:22} {100*cov:5.1f}%  ring {len(ring):5}  gaps {len(gaps):4}  to 90%: {need:4}  ' + '  '.join(f'{k} {v}' for k, v in why.most_common()), flush=True)
