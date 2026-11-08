"""Edge kinds of the border ring, per preset: each ring tile's kind from its DT1 names; covered = under a piece at
least as high as the kind needs (tree lines, cliffs, walls TALL; fences and shores LOWH). Also the shore: stream
tiles (playable blocked floor) under a water plane or a pasted room's mesh.
python kinds.py <scr> <preset dir or 'out'> name..."""
import collections, json, math, os, re, sys
from pathlib import Path
sys.path.insert(0, 'tools'); sys.path.insert(0, sys.argv[1])
import border_score as B, pd2_presets as P, pd2_painter as PA
from pd2_ids import load
KINDS = [('fence', r'fence|stonewall|palisade|picket|stake|spike'), ('shore', r'river|rivbank|pool|swamp|pond|puddle|water|shore|lake'),
         ('cliff', r'cliff|ascendor|rock|crevice|hellgate|lava|edge'), ('tree', r'tree|shrub|pine|jungle|bush|forest|plant|brush'),
         ('wall', r'wall|fort|build|town|ruin|tomb|kanemith|cave|sets|terrace|temple|dock|dungeon|hell')]
LOWH = 1.0
def kind(k):
    names = ' '.join((d[0] or '?').rsplit('/', 1)[-1] for d in k[1])
    for n, rx in KINDS:
        if re.search(rx, names): return n
    return 'other'
sets, standins, _ = P.read_rule(Path('merge/presets-hd.tsv'))
presets = P.pd2_only_presets(B.OUT, len(load(B.D, 'lvlprest').records()))
tiles, pd2_tiles = P.Tiles(B.D / 'global/tiles'), P.Tiles(B.OUT / 'global/tiles')
src = sys.argv[2]
for _, ts, files, ds1s in presets:
    for ds1 in ds1s:
        n = ds1.rsplit('/', 1)[-1][:-4]
        if sys.argv[3:] and n not in sys.argv[3:]: continue
        f = (B.OUT / P.PRESETS / Path(ds1).with_suffix('.json')) if src == 'out' else Path(src) / f'{n}.json'
        if not f.is_file(): continue
        lay = P.layout((B.OUT / 'global/tiles' / ds1).read_bytes(), P.stand_in(files, tiles, pd2_tiles, standins))
        if not P.draws_trees(lay): continue
        top = {}
        ents = json.loads(f.read_text('utf-8'))['entities']
        for x0, x1, z0, z1, h in B.boxes(ents):
            if x1 - x0 > 400 or z1 - z0 > 400: continue
            for tx in range(math.floor(x0/10), math.floor(x1/10)+1):
                for tz in range(math.floor(z0/10), math.floor(z1/10)+1): top[(tx, tz)] = max(top.get((tx, tz), -1e9), h)
        play = PA.playable(lay)
        ring = ({(x+dx, y+dy) for x, y in play for dx, dy in PA.STEPS} - play) & set(lay.keys)
        tot, cov = collections.Counter(), collections.Counter()
        for t in ring:
            kd = kind(lay.keys[t]); tot[kd] += 1
            if top.get(t, -1e9) >= (LOWH if kd in ('fence', 'shore') else B.TALL): cov[kd] += 1
        allc = sum(cov.values()) / max(sum(tot.values()), 1)
        print(f'{n:22} ring {sum(tot.values()):5} covered {100*allc:5.1f}%  ' + '  '.join(f'{k} {100*cov[k]/tot[k]:.0f}%/{tot[k]}' for k, _ in tot.most_common()))
