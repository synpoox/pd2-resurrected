"""A no-game check per generated preset: of the DS1's wall tiles (what the automap draws as walls), the share with an
HD piece on the tile or its 8 neighbours, or inside a pasted stock room; and the same for water tiles (blocked floor).
Also writes a top-down preview PNG per preset: wall tiles red where bare, green where drawn."""
import collections, json, sys
from pathlib import Path
from PIL import Image
sys.path.insert(0, 'tools')
import pd2_presets as P, pd2_painter as PA
from pd2_ids import load

D = Path(r"C:/Users/micah/orca/workspaces/pd2-resurrected/d2rAI-toolkit/d2r-data/93847/data")
OUT = Path('out/data')
SCR = Path(sys.argv[1])
(SCR / 'previews').mkdir(exist_ok=True)
sets, standins, _ = P.read_rule(Path('merge/presets-hd.tsv'))
presets = P.pd2_only_presets(OUT, len(load(D, 'lvlprest').records()))
on_d2r = [p for p in presets if all(f in standins for f in p[2] if f.startswith('pd2assets/'))]
tiles, pd2_tiles = P.Tiles(D / 'global/tiles'), P.Tiles(OUT / 'global/tiles')
layouts = {ds1: P.layout((OUT / 'global/tiles' / ds1).read_bytes(), P.stand_in(files, tiles, pd2_tiles, standins))
           for _, _, files, ds1s in on_d2r for ds1 in ds1s}
keep = set().union(*map(P.wanted, layouts.values()))
loaded = {t[0] for k in keep for t in k[1]}
stock = []
for p in sorted((D / P.PRESETS).rglob('*.json')):
    ds1 = D / 'global/tiles' / p.relative_to(D / P.PRESETS).with_suffix('.ds1')
    if not ds1.is_file():
        continue
    data = ds1.read_bytes()
    listed = P.header_dt1s(data)
    if loaded.isdisjoint(listed):
        continue
    stock.append((P.layout(data, tiles.of(listed)), json.loads(p.read_text('utf-8-sig')), set(listed)))
rows = []
for _, ts, _, ds1s in on_d2r:
    for ds1 in ds1s:
        lay = layouts[ds1]
        drawn = {t[0] for k in P.wanted(lay) for t in k[1]}
        own = [(l, pr) for l, pr, listed in stock if not drawn.isdisjoint(listed)]
        _, taken = P.rooms(lay, own)
        preset = json.loads((OUT / P.PRESETS / Path(ds1).with_suffix('.json')).read_text('utf-8'))
        has = set()
        for e in preset['entities']:
            t = P._transform(e)
            if t and e.get('name') != 'terrain':
                has.add((int(t['position']['x'] // 10), int(t['position']['z'] // 10)))
        near = PA.dilate(has)
        walls = [c for c, k in lay.keys.items() if k[0] == 'w']
        water = [c for c, k in lay.keys.items() if k[0] == 'f' and PA.blocked_floor(lay.flags[c])]
        ok_w = [c for c in walls if c in near or c in taken]
        ok_f = [c for c in water if c in has or c in taken]
        rows.append((ds1.rsplit('/', 1)[-1], ts, len(walls), len(ok_w) / max(len(walls), 1), len(water), len(ok_f) / max(len(water), 1)))
        im = Image.new('RGB', (lay.width, lay.height), (25, 25, 25))
        px = im.load()
        for c, k in lay.keys.items():
            px[c] = (90, 85, 70)
        for c in water:
            px[c] = (60, 120, 230) if c in has or c in taken else (20, 30, 80)
        for c in walls:
            px[c] = (60, 200, 60) if c in near or c in taken else (230, 40, 40)
        im.resize((lay.width * 3, lay.height * 3), Image.NEAREST).save(SCR / 'previews' / (ds1.rsplit('/', 1)[-1][:-4] + '.png'))
rows.sort(key=lambda r: r[3])
print(f"{'preset':28} {'tile set':24} {'walls':>7} {'drawn':>6} {'water':>6} {'drawn':>6}")
for r in rows:
    print(f'{r[0]:28} {r[1]:24} {r[2]:7} {100 * r[3]:5.0f}% {r[4]:6} {100 * r[5]:5.0f}%')
