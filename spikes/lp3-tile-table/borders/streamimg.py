"""Jungle streams past the tree line: rivbank/pool blocked floor (blue), with a tree box over it (red), the
playable area grey, tree boxes elsewhere dark green. python streamimg.py <scr> <outdir> <tag> name..."""
import json, math, sys
from pathlib import Path
from PIL import Image
sys.path.insert(0, 'tools'); sys.path.insert(0, sys.argv[1])
import border_score as B, pd2_presets as P, pd2_painter as PA
from pd2_ids import load
WET = frozenset({'act3/river/rivbank.dt1', 'act3/river/pool.dt1'})
sets, standins, _ = P.read_rule(Path('merge/presets-hd.tsv'))
presets = P.pd2_only_presets(B.OUT, len(load(B.D, 'lvlprest').records()))
tiles, pd2_tiles = P.Tiles(B.D / 'global/tiles'), P.Tiles(B.OUT / 'global/tiles')
for _, ts, files, ds1s in presets:
    for ds1 in ds1s:
        n = ds1.rsplit('/', 1)[-1][:-4]
        if n not in sys.argv[4:]: continue
        lay = P.layout((B.OUT / 'global/tiles' / ds1).read_bytes(), P.stand_in(files, tiles, pd2_tiles, standins))
        ents = json.loads((B.OUT / P.PRESETS / Path(ds1).with_suffix('.json')).read_text('utf-8'))['entities']
        trees = set()
        for e in ents:
            if not PA.is_tree(e): continue
            for x0, x1, z0, z1, h in B.boxes([e]):
                if h < B.TALL: continue
                # the trunk tile: the box centre
                trees.add((math.floor((x0 + x1) / 20), math.floor((z0 + z1) / 20)))
        wet = {c for c, k in lay.keys.items() if k[0] == 'f' and PA.blocked_floor(lay.flags[c]) and any(t[0] in WET for t in k[1])}
        play = PA.playable(lay, WET)
        im = Image.new('RGB', (lay.width, lay.height), (20, 20, 20)); px = im.load()
        for c in lay.keys: px[c] = (45, 40, 32)
        for c in play: px[c] = (120, 112, 95)
        for c in trees:
            if c in lay.keys: px[c] = (30, 110, 40)
        for c in wet: px[c] = (230, 40, 40) if c in trees else (60, 120, 230)
        im.resize((lay.width * 4, lay.height * 4), Image.NEAREST).save(Path(sys.argv[2]) / f'{n}_streams_{sys.argv[3]}.png')
        print(n, sys.argv[3], 'stream tiles', len(wet), 'with a tree trunk on them', len(wet & trees))
