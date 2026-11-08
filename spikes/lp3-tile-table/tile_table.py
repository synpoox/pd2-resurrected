"""Feasibility: a classic tile -> HD piece table learned from D2R's stock (DS1, HD preset) pairs.

Per single tile (DT1, orientation, main, sub): the instances D2R drew on it across every stock preset (models of the
entities whose position falls on that tile), their distinct signatures, and the coverage of the tiles PD2 maps use.
"""
import collections, hashlib, json, pickle, re, struct, sys
from pathlib import Path
sys.path.insert(0, 'tools')
import pd2_presets as P, pd2_painter as PA
from pd2_ids import load

D = Path(r"C:/Users/micah/orca/workspaces/pd2-resurrected/d2rAI-toolkit/d2r-data/93847/data")
OUT = Path('out/data')
SCR = Path(sys.argv[1])
tiles = P.Tiles(D / 'global/tiles')

def models(e):
    return tuple(sorted({c['filename'].rsplit('/', 1)[-1] for c in e['components']
                         if c['type'] in ('ModelDefinitionComponent', 'PrefabPlacementDefinitionComponent') and c.get('filename')}))

ROOMSIZED = re.compile(r'/preset/|water_flow|terrain', re.I)

# 1. the table: single tile -> Counter(signature)
cache = SCR / 'table.pkl'
if cache.exists():
    table, kinds, stats = pickle.loads(cache.read_bytes())
else:
    table = collections.defaultdict(collections.Counter)
    kinds = {}
    stats = collections.Counter()
    for p in sorted((D / P.PRESETS).rglob('*.json')):
        ds1 = D / 'global/tiles' / p.relative_to(D / P.PRESETS).with_suffix('.ds1')
        if not ds1.is_file():
            stats['preset no ds1'] += 1
            continue
        data = ds1.read_bytes()
        try:
            lay = P.layout(data, tiles.of(P.header_dt1s(data)))
        except Exception:
            stats['ds1 unreadable'] += 1
            continue
        preset = json.loads(p.read_text('utf-8-sig'))
        stats['stock presets'] += 1
        per = collections.defaultdict(list)
        for e in preset['entities']:
            t = P._transform(e)
            if t is None:
                continue
            m = models(e)
            if not m:
                continue
            stats['entities'] += 1
            if ROOMSIZED.search(' '.join(m)) or any(c['type'] == 'TerrainStampDefinitionComponent' for c in e['components']):
                stats['room-sized entities'] += 1
            c = (min(max(int(t['position']['x'] // 10), 0), lay.width - 1), min(max(int(t['position']['z'] // 10), 0), lay.height - 1))
            if c not in lay.keys:
                stats['entities on empty cells'] += 1
            per[c].extend(m)
        for c, key in lay.keys.items():
            sig = tuple(sorted(per.get(c, ())))
            stats['stock tiles'] += 1
            for t in key[1]:
                if t[0] is None:
                    continue
                table[t][sig] += 1
                kinds[t] = key[0]
    cache.write_bytes(pickle.dumps((dict(table), kinds, stats)))
print('stock:', dict(stats))

# 2. ambiguity of the table
def amb(t):
    c = table[t]
    n = sum(c.values())
    nonempty = {s: k for s, k in c.items() if s}
    top = max(nonempty.values()) / sum(nonempty.values()) if nonempty else 0
    return n, len(nonempty), sum(nonempty.values()) / n, top

walls = [t for t in table if kinds[t] == 'w']
floors = [t for t in table if kinds[t] == 'f']
for name, ts in (('wall', walls), ('floor', floors)):
    a = [amb(t) for t in ts]
    drawn = [x for x in a if x[1]]
    print(f'{name} tiles in table: {len(ts)}; with any piece {len(drawn)}; '
          f'median distinct signatures {sorted(x[1] for x in drawn)[len(drawn)//2] if drawn else 0}; '
          f'one signature only {sum(x[1] == 1 for x in drawn)}; top signature >= 50% of drawn instances {sum(x[3] >= .5 for x in drawn)}')

# 3. coverage of PD2's map tiles (all PD2-only presets, those on PD2 tiles through any stand-in lines)
sets, standins, _ = P.read_rule(Path('merge/presets-hd.tsv'))
presets = P.pd2_only_presets(OUT, len(load(D, 'lvlprest').records()))
pd2_tiles = P.Tiles(OUT / 'global/tiles')
cov = collections.Counter()
permap = {}
for name, ts, files, ds1s in presets:
    on_d2r = all(f in standins for f in files if f.startswith('pd2assets/'))
    for ds1 in ds1s:
        lay = P.layout((OUT / 'global/tiles' / ds1).read_bytes(), P.stand_in(files, tiles, pd2_tiles, standins))
        m = collections.Counter()
        for c, key in lay.keys.items():
            for t in key[1]:
                kind = key[0]
                if t[0] is None:
                    r = 'unnamed'
                elif t[0].startswith('pd2assets/'):
                    r = 'pd2 dt1 (no stand-in)'
                elif t not in table:
                    r = 'unseen in stock'
                elif amb(t)[1] == 0:
                    r = 'seen, D2R draws nothing'
                else:
                    r = 'seen, drawn'
                m[(kind, r)] += 1
                cov[(kind, r, on_d2r)] += 1
        permap[ds1] = m
for kind in 'wf':
    for on in (True, False):
        tot = sum(v for (k, r, o), v in cov.items() if k == kind and o == on)
        if tot:
            print(f"{'wall' if kind == 'w' else 'floor'} tiles, presets {'on D2R tiles' if on else 'waiting on stand-ins'}: {tot}")
            for (k, r, o), v in sorted(cov.items(), key=lambda kv: -kv[1]):
                if k == kind and o == on:
                    print(f'   {r:28} {v:8} {100 * v / tot:5.1f}%')
(SCR / 'permap.json').write_text(json.dumps({k: {f'{a}|{b}': v for (a, b), v in m.items()} for k, m in permap.items()}, indent=0))
