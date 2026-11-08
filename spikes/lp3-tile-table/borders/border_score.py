"""Border check of generated outdoor presets: does a tall piece stand on every tile of the ring just outside the
playable area (where the automap draws the map's edge), and does the DS1 block it?

Ring = tiles with DS1 content, outside pd2_painter.playable(), 4-adjacent to it. Per ring tile:
  covered  - under the footprint of a piece at least TALL units high (model boxes from model_lod_desc.bin, prefabs
             followed); else a visual gap
  leak     - every subtile walkable and no unwalkable cell bit: the player can walk onto it
Also counts tall-piece footprints on playable tiles that are walkable ground (misplaced: hides the path).
Writes <name>_border.png: play grey, covered ring green, gap red, leak magenta, misplaced yellow."""
import functools, json, math, struct, sys
from pathlib import Path
from PIL import Image
sys.path.insert(0, 'tools')
import pd2_presets as P, pd2_painter as PA
from pd2_ids import load

D = Path(r"C:/Users/micah/orca/workspaces/pd2-resurrected/d2rAI-toolkit/d2r-data/93847/data")
OUT = Path('out/data')
SCR = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
TALL = 4.0
raw = (D / 'hd/model_lod_desc.bin').read_bytes()
count, off = struct.unpack_from('<Ii', raw, 0)
BOX = {struct.unpack_from('<I', raw, 4 + off + 44 * k)[0]: struct.unpack_from('<6f', raw, 8 + off + 44 * k) for k in range(count)}


def fnv(path):
    h = 0x811c9dc5
    for b in path.lower().encode():
        h = ((h ^ b) * 0x1000193) & 0xFFFFFFFF
    return h


@functools.cache
def prefab_entities(path):
    f = D / path.removeprefix('data/')
    return json.loads(f.read_text('utf-8-sig')).get('entities', []) if f.is_file() else []


I = {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}


def mul(a, b):
    ax, ay, az, aw = a['x'], a['y'], a['z'], a['w']
    bx, by, bz, bw = b['x'], b['y'], b['z'], b['w']
    return {'w': aw * bw - ax * bx - ay * by - az * bz, 'x': aw * bx + ax * bw + ay * bz - az * by,
            'y': aw * by - ax * bz + ay * bw + az * bx, 'z': aw * bz + ax * by - ay * bx + az * bw}


def boxes(entities, base=None, depth=0):
    """(x0, x1, z0, z1, top) per model, prefabs followed (their pieces placed by the parent's transform)."""
    for e in entities:
        t = P._transform(e)
        if t is None or e.get('name') == 'terrain':
            continue
        p = (t['position']['x'], t['position']['y'], t['position']['z'])
        q, s = t.get('orientation') or I, t.get('scale') or {'x': 1, 'y': 1, 'z': 1}
        s = (s['x'], s['y'], s['z'])
        if base:
            bp, bq, bs = base
            v = PA.rotate(bq, (p[0] * bs[0], p[1] * bs[1], p[2] * bs[2]))
            p, q, s = (bp[0] + v[0], bp[1] + v[1], bp[2] + v[2]), mul(bq, q), (s[0] * bs[0], s[1] * bs[1], s[2] * bs[2])
        for c in e['components']:
            if c['type'] == 'ModelDefinitionComponent' and (bb := BOX.get(fnv(c.get('filename', '')))):
                xs, ys, zs = [], [], []
                for cx in (bb[0], bb[3]):
                    for cy in (bb[1], bb[4]):
                        for cz in (bb[2], bb[5]):
                            v = PA.rotate(q, (cx * s[0], cy * s[1], cz * s[2]))
                            xs.append(p[0] + v[0]); ys.append(p[1] + v[1]); zs.append(p[2] + v[2])
                yield min(xs), max(xs), min(zs), max(zs), max(ys)
            elif c['type'] == 'PrefabPlacementDefinitionComponent' and c.get('prefab') and depth < 2:
                yield from boxes(prefab_entities(c['prefab']), (p, q, s), depth + 1)


def check(lay, entities, name):
    tall = set()
    for x0, x1, z0, z1, h in boxes(entities):
        if h < TALL or x1 - x0 > 400 or z1 - z0 > 400:
            continue
        for tx in range(math.floor(x0 / 10), math.floor(x1 / 10) + 1):
            for tz in range(math.floor(z0 / 10), math.floor(z1 / 10) + 1):
                tall.add((tx, tz))
    play = PA.playable(lay)
    ring = {(x + dx, y + dy) for x, y in play for dx, dy in PA.STEPS} - play
    ring &= set(lay.keys)
    walkable = {c for c in ring if c not in lay.unwalkable and not any(b & 1 for b in lay.flags.get(c, bytes(25)))}
    ground = {c for c in play - lay.unwalkable if lay.keys[c][0] != 'w' and not PA.blocked_floor(lay.flags[c])}
    gaps = ring - tall
    misplaced = ground & tall
    im = Image.new('RGB', (lay.width, lay.height), (20, 20, 20))
    px = im.load()
    for c in lay.keys:
        px[c] = (45, 40, 32)
    for c in play:
        px[c] = (120, 112, 95)
    for c in misplaced:
        px[c] = (230, 200, 40)
    for c in ring:
        px[c] = (230, 40, 200) if c in walkable else (50, 200, 60) if c in tall else (230, 40, 40)
    im.resize((lay.width * 4, lay.height * 4), Image.NEAREST).save(SCR / f'{name}_border.png')
    return len(ring), 1 - len(gaps) / max(len(ring), 1), len(gaps), len(walkable), len(misplaced), len(ground)


if __name__ == '__main__':
    sets, standins, _ = P.read_rule(Path('merge/presets-hd.tsv'))
    presets = P.pd2_only_presets(OUT, len(load(D, 'lvlprest').records()))
    tiles, pd2_tiles = P.Tiles(D / 'global/tiles'), P.Tiles(OUT / 'global/tiles')
    for _, ts, files, ds1s in presets:
        for ds1 in ds1s:
            n = ds1.rsplit('/', 1)[-1][:-4]
            if sys.argv[2:] and n not in sys.argv[2:]:
                continue
            f = OUT / P.PRESETS / Path(ds1).with_suffix('.json')
            if not f.is_file():
                continue
            lay = P.layout((OUT / 'global/tiles' / ds1).read_bytes(), P.stand_in(files, tiles, pd2_tiles, standins))
            if not P.draws_trees(lay):
                continue
            r = check(lay, json.loads(f.read_text('utf-8'))['entities'], n)
            print(f'{n:22} ring {r[0]:6}  covered {100 * r[1]:5.1f}%  gaps {r[2]:5}  leaks {r[3]:4}  '
                  f'misplaced {r[4]:5} of {r[5]} ground tiles')
