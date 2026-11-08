"""Top-down HD preview from model bounding boxes (model_lod_desc.bin) vs the DS1's walls (what the automap draws).

Per preset: each entity's model box, rotated, scaled and placed, marks the tiles its footprint covers with its top
height. Score: the share of DS1 wall tiles under a piece at least TALL units high. Writes a PNG per preset."""
import functools, json, math, re, struct, sys
from pathlib import Path
from PIL import Image
sys.path.insert(0, 'tools')
import pd2_presets as P

D = Path(r"C:/Users/micah/orca/workspaces/pd2-resurrected/d2rAI-toolkit/d2r-data/93847/data")
SCR = Path(sys.argv[1])
TALL = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0

raw = (D / 'hd/model_lod_desc.bin').read_bytes()
count, off = struct.unpack_from('<Ii', raw, 0)
BOX = {}
for k in range(count):
    key, *bb = struct.unpack_from('<I6f', raw, 4 + off + 44 * k)
    BOX[key] = bb


def fnv(path: str) -> int:
    h = 0x811c9dc5
    for b in path.lower().encode():
        h = ((h ^ b) * 0x1000193) & 0xFFFFFFFF
    return h


def rot(q, v):
    x, y, z, w = q['x'], q['y'], q['z'], q['w']
    vx, vy, vz = v
    # v' = q v q*
    tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))


@functools.cache
def prefab(path):
    f = D / path.removeprefix('data/')
    return json.loads(f.read_text('utf-8-sig')).get('entities', []) if f.is_file() else []


def boxes(entities, parent=None, depth=0):
    """World boxes (x0, x1, z0, z1, top) of the entities' models, prefabs followed one level."""
    for e in entities:
        t = P._transform(e)
        if t is None:
            continue
        p, q, s = t['position'], t.get('orientation', {'x': 0, 'y': 0, 'z': 0, 'w': 1}), t.get('scale', {'x': 1, 'y': 1, 'z': 1})
        if parent:
            pp, pq = parent
            r = rot(pq, (p['x'], p['y'], p['z']))
            p = {'x': pp['x'] + r[0], 'y': pp['y'] + r[1], 'z': pp['z'] + r[2]}
            q = {'x': 0, 'y': 0, 'z': 0, 'w': 1} if q == {'x': 0, 'y': 0, 'z': 0, 'w': 1} else q
            q = pq if q == {'x': 0, 'y': 0, 'z': 0, 'w': 1} else q
        for c in e['components']:
            if c['type'] == 'ModelDefinitionComponent' and c.get('filename') in () or 'terrain' in e.get('name', ''):
                continue
            if c['type'] == 'ModelDefinitionComponent':
                bb = BOX.get(fnv(c['filename']))
                if not bb:
                    continue
                xs, zs, ys = [], [], []
                for cx in (bb[0], bb[3]):
                    for cy in (bb[1], bb[4]):
                        for cz in (bb[2], bb[5]):
                            v = rot(q, (cx * s['x'], cy * s['y'], cz * s['z']))
                            xs.append(p['x'] + v[0]); ys.append(p['y'] + v[1]); zs.append(p['z'] + v[2])
                yield min(xs), max(xs), min(zs), max(zs), max(ys)
            elif c['type'] == 'PrefabPlacementDefinitionComponent' and depth == 0 and c.get('prefab'):
                yield from boxes(prefab(c['prefab']), (p, q), 1)


def score(lay, entities, name=None):
    top = {}
    for x0, x1, z0, z1, h in boxes(entities):
        if x1 - x0 > 400 or z1 - z0 > 400:  # room-sized meshes (water, sculpted terrain): not a wall piece
            continue
        for tx in range(int(math.floor(x0 / 10)), int(math.floor(x1 / 10)) + 1):
            for tz in range(int(math.floor(z0 / 10)), int(math.floor(z1 / 10)) + 1):
                top[(tx, tz)] = max(top.get((tx, tz), -1e9), h)
    walls = [c for c, k in lay.keys.items() if k[0] == 'w']
    ok = [c for c in walls if top.get(c, -1e9) >= TALL]
    if name:
        im = Image.new('RGB', (lay.width, lay.height), (25, 25, 25))
        px = im.load()
        for c in lay.keys:
            px[c] = (80, 75, 60)
        for c, h in top.items():
            if c in lay.keys and h >= TALL:
                px[c] = (40, 90, 160)
        for c in walls:
            px[c] = (60, 210, 60) if c in ok else (230, 40, 40)
        im.resize((lay.width * 3, lay.height * 3), Image.NEAREST).save(SCR / 'previews' / f'{name}.png')
    return len(walls), len(ok) / max(len(walls), 1)


tiles = P.Tiles(D / 'global/tiles')
(SCR / 'previews').mkdir(exist_ok=True)
print(f'TALL = {TALL}')
for sub in ('act2/tomb', 'act3/jungle', 'act1/outdoors', 'act1/catacomb', 'act2/palace', 'expansion/siege'):
    a = b = 0
    for p in sorted((D / P.PRESETS / sub).glob('*.json'))[:80]:
        ds1 = D / 'global/tiles' / p.relative_to(D / P.PRESETS).with_suffix('.ds1')
        if not ds1.is_file():
            continue
        data = ds1.read_bytes()
        lay = P.layout(data, tiles.of(P.header_dt1s(data)))
        n, s = score(lay, json.loads(p.read_text('utf-8-sig'))['entities'])
        a += n; b += n * s
    print(f'stock     {sub:18} wall tiles {a:7}  under a tall piece {100 * b / max(a, 1):4.0f}%')
for n in sys.argv[3:]:
    f = next(Path('out/data/global/tiles').rglob(n + '.ds1'))
    j = json.loads(next(Path('out/data/hd/env/preset').rglob(n + '.json')).read_text('utf-8'))
    k, s = score(P.layout(f.read_bytes(), {}), j['entities'], n)
    print(f'generated {n:18} wall tiles {k:7}  under a tall piece {100 * s:4.0f}%')
