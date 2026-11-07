"""Spike (not repo code): Jungle Map presets from pasted stock rooms (>= THRESH tile match, with their
sculpted terrain) plus per-tile wall pieces elsewhere, on the flat plane. Writes jungle1-3.json to OUT.
  python spikes/lp3-border-painter/build_jungle.py OUT [THRESH]   (D2RAI_TOOLKIT: the toolkit, default ../d2rAI-toolkit)"""
import copy, functools, json, os, re, sys, zlib
import painter
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]  # the repo
TOOLKIT = Path(os.environ.get('D2RAI_TOOLKIT', ROOT.parent / 'd2rAI-toolkit'))
sys.path[:0] = [str(ROOT / 'tools'), str(TOOLKIT)]
import pd2_presets as P
from pd2_ids import load
from modkit.base import Base

D = TOOLKIT / 'd2r-data' / '93847' / 'data'
G = ROOT / 'generated' / 'data'
OUT, THRESH = Path(sys.argv[1]), float(sys.argv[2]) if len(sys.argv) > 2 else 0.6
BUDGET_EXPANDED, BUDGET_PHYSICS = 10**9, 10**9  # pool grown by core-maps
base = Base()


@functools.cache
def prefab_size(path):
    try:
        return weight(json.loads(base.text(path)).get('entities', []))
    except Exception:
        return 1


def weight(ents):
    return sum(sum(prefab_size(c['prefab']) for c in e['components'] if c['type'] == 'PrefabPlacementDefinitionComponent')
               or 1 for e in ents)


def physics(ents):
    return sum(1 for e in ents for c in e['components'] if c['type'] == 'PhysicsBodyDefinitionComponent')


tiles = P.Tiles(D / 'global/tiles')
name, ts, files, ds1s = next(p for p in P.pd2_only_presets(G, len(load(D, 'lvlprest').records())) if p[0] == 'Jungle Map')
stock = []
for p in sorted((D / P.PRESETS).rglob('*.json')):
    ds1 = D / 'global/tiles' / p.relative_to(D / P.PRESETS).with_suffix('.ds1')
    if ds1.is_file() and str(p.relative_to(D / P.PRESETS)).lower().startswith('act3'):
        data = ds1.read_bytes()
        lay = P.layout(data, tiles.of(P.header_dt1s(data)))
        if len(lay.keys) >= 16:
            stock.append((p, lay))
@functools.cache
def stock_json(p):
    return json.loads(p.read_text('utf-8-sig'))


terrain = json.loads((D / P.PRESETS / f'{P.TERRAIN}.json').read_text('utf-8-sig'))['terrain']

ROOTS = [D / 'global/tiles', G / 'global/tiles']
jungle_rooms = [(sp, s) for sp, s in stock if 'jungle' in str(sp).lower() and 'dungeon' not in str(sp).lower()]
tree_kit, spacing = painter.harvest([(s, stock_json(sp)) for sp, s in jungle_rooms], ROOTS)
print(f'tree kit: {sum(map(len, tree_kit.values()))} stock trees in {len(tree_kit)} classes, spacing {spacing:.1f}')
import pd2_levels, struct
def objects(d):
    raw = (G / 'global/tiles' / d).read_bytes()
    _, at, count, size = pd2_levels.object_list(raw)
    return frozenset((x // 5, y // 5) for _, _, x, y in (struct.unpack_from('<4i', raw, at + 4 + k * size) for k in range(count)))


ROOMLOG = []
for d in ds1s:
    lay = P.layout((G / 'global/tiles' / d).read_bytes(), tiles.of(files))
    at = {}
    for c, k in lay.keys.items():
        at.setdefault(k, []).append(c)
    found = []
    for sp, s in stock:
        items = list(s.keys.items())
        ac, ak = min(items, key=lambda it: len(at.get(it[1], ())) or 10 ** 9)
        need = THRESH * len(items)
        for px, py in at.get(ak, ()):
            ox, oy = px - ac[0], py - ac[1]
            if ox < 0 or oy < 0 or ox + s.width > lay.width or oy + s.height > lay.height:
                continue
            hit = 0
            for n, ((x, y), key) in enumerate(items):
                hit += lay.keys.get((x + ox, y + oy)) == key
                if n + 1 - hit > len(items) - need:
                    break
            if hit >= need:
                found.append((hit, sp, ox, oy, s.width, s.height))
    found.sort(key=lambda f: -f[0])
    ROOMLOG.append(found)
    taken, rooms = set(), []
    for hit, sp, ox, oy, w, h in found:
        cells = {(x, y) for x in range(ox, ox + w) for y in range(oy, oy + h)}
        if not cells & taken:
            taken |= cells
            rooms.append((sp, ox, oy))

    # pasted rooms: every entity and the room's own terrain, shifted
    entities, deps = [], {}
    def add_deps(dd):
        for kind, items in dd.items():
            for x in items:
                deps.setdefault(kind, {}).setdefault(x['path'].lower(), x)
    room_ents = []
    for sp, ox, oy in rooms:
        j = json.loads(sp.read_text('utf-8-sig'))
        add_deps(j['dependencies'])
        for e in j['entities'] + [j['terrain']]:
            e = copy.deepcopy(e)
            t = P._transform(e)
            if t is None:
                continue
            t['position']['x'] += ox * P.TILE
            t['position']['z'] += oy * P.TILE
            for c in e['components']:
                if c['type'] == 'WallTransparencyComponent' and c['wallTileLocalCoord']['x'] >= 0:
                    c['wallTileLocalCoord'] = {'x': c['wallTileLocalCoord']['x'] + ox, 'y': c['wallTileLocalCoord']['y'] + oy}
            room_ents.append(e)

    # the rest: wall pieces only, from the per-tile kit
    rest = P.Layout(lay.width, lay.height, {c: k for c, k in lay.keys.items() if c not in taken})
    keep = P.wanted(rest)
    kit = P.Kit()
    for sp, s in stock:
        kit.add(s, stock_json(sp), keep)
    # patch lookup: a tile whose 3x3 (else 4-neighbour cross) neighbourhood appears in a stock preset copies what the
    # stock preset placed on that centre tile, empty included; the generator below fills only the rest
    SQ = [(i, j) for j in (-1, 0, 1) for i in (-1, 0, 1)]
    CROSS = [(0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)]
    def patch(keys, x, y, cells):
        return tuple(keys.get((x + i, y + j)) for i, j in cells)
    patch_ents, patched = [], set()
    for name, cells in (('sq', SQ), ('cross', CROSS)):
        want = {c: (name, patch(lay.keys, *c, cells)) for c in rest.keys if c not in patched}
        pkit = P.Kit()
        for sp, s in stock:
            plays = P.Layout(s.width, s.height, {c: (name, patch(s.keys, *c, cells)) for c in s.keys})
            pkit.add(plays, stock_json(sp), set(want.values()))
        found = {c: k for c, k in want.items() if k in pkit.instances}
        patched |= set(found)
        built, _ = P.build(P.Layout(lay.width, lay.height, found), pkit, 'x', d,
                           'data/hd/env/biome/act3_jungle.json', terrain)
        patch_ents += built['entities']
        add_deps(built['dependencies'])
    near = set()
    for (x, y) in patched:
        if lay.keys[(x, y)][0] == 'w':
            near |= {(x + i, y + j) for i in (-1, 0, 1) for j in (-1, 0, 1)}
    print(f'{d}: rooms', [(Path(str(sp)).stem, ox, oy, w, h, round(hit / (w * h), 2)) for hit, sp, ox, oy, w, h in ROOMLOG[-1] if (sp, ox, oy) in rooms])
    print(f'{d}: patch lookup covers {100 * len(patched) / len(lay.keys):.1f}% of tiles')
    rest = P.Layout(rest.width, rest.height, {c: k for c, k in rest.keys.items() if c not in patched})
    # trees (prefabs) on wall tiles, one per neighbourhood as stock spaces them; every other tile, and wall tiles
    # skipped for spacing, gets a non-empty copy's non-prefab entities (ground, water, rocks, decals)
    pf = lambda e: any(c['type'] == 'PrefabPlacementDefinitionComponent' for c in e['components'])
    TALL = re.compile(r'treeline|trees?_|canopy|hanging|ivy|vines')
    def tall(e):  # trees, canopy and hanging plants: overhead or solid, they belong to wall tiles only
        return pf(e) or any(c['type'] == 'PhysicsBodyDefinitionComponent' for c in e['components']) or             bool(TALL.search(json.dumps(e['components'])))
    trees, fill = P.Kit(sources=kit.sources), P.Kit(sources=kit.sources)
    for key, opts in kit.instances.items():
        t = [o for o in opts if any(pf(e[0]) for e in o[1])]
        trees.instances[key] = t or opts
        f = [(src, [e for e in ents if not tall(e[0])]) for src, ents in opts]
        fill.instances[key] = [o for o in f if o[1]] or f
    tree_lay, fill_lay = {}, {}
    for (x, y), key in sorted(rest.keys.items(), key=lambda it: zlib.crc32(f"{d}:{it[0]}".encode())):
        if key[0] == 'w' and (x, y) not in near:
            tree_lay[(x, y)] = key
            near |= {(x + i, y + j) for i in (-1, 0, 1) for j in (-1, 0, 1)}
        else:
            fill_lay[(x, y)] = key
    walls, missing = P.build(P.Layout(rest.width, rest.height, tree_lay), trees, 'x', d,
                             'data/hd/env/biome/act3_jungle.json', terrain)
    filled, _ = P.build(P.Layout(rest.width, rest.height, fill_lay), fill, 'x', d,
                        'data/hd/env/biome/act3_jungle.json', terrain)
    walls['entities'] += filled['entities'] + patch_ents
    add_deps(filled['dependencies'])
    # border painter: drop every tree the patch lookup and the fallback placed, paint them from the playable area out
    is_tree = lambda e: bool(painter.TREE.search(json.dumps(e['components'])))
    stock_trees = [e for e in walls['entities'] if is_tree(e)]
    walls['entities'] = [e for e in walls['entities'] if not is_tree(e)]
    painted, playable = painter.paint(lay, ROOTS, tree_kit, spacing, d, frozenset(taken), objects(d))
    walls["entities"] += [] if os.environ.get("NOPAINT") else painted
    # a line with playable ground on both sides (a path one side, a stream the other) leaves the painter no room: keep
    # the stock trees the patch lookup put on its tiles, where no painted tree stands within two tiles
    line = painter.lines({c for c, k in lay.keys.items() if k[0] == 'w'})
    tile_of = lambda e: (int(P._transform(e)['position']['x'] // P.TILE), int(P._transform(e)['position']['z'] // P.TILE))
    near_painted = painter.dilate(painter.dilate({tile_of(e) for e in painted}))
    kept = [e for e in stock_trees if tile_of(e) in line and tile_of(e) not in near_painted]
    walls['entities'] += kept
    print(f'{d}: kept {len(kept)} stock trees on bare line tiles')
    for sp, _ in jungle_rooms:
        add_deps(stock_json(sp)['dependencies'])
    print(f'{d}: painted {len(painted)} trees around a playable area of {len(playable)} tiles')
    wall_ents = walls['entities']
    add_deps(walls['dependencies'])

    # budget: thin wall pieces first (by hash), then pasted rooms' prefabs
    def over(ents):
        return weight(ents) > BUDGET_EXPANDED or physics(ents) > BUDGET_PHYSICS
    wall_ents.sort(key=lambda e: zlib.crc32(json.dumps(P._transform(e)['position']).encode()))
    while wall_ents and over(room_ents + wall_ents):
        del wall_ents[-max(1, len(wall_ents) // 20):]
    dropped_room = 0
    if over(room_ents):
        room_ents.sort(key=lambda e: any(c['type'] == 'PrefabPlacementDefinitionComponent' for c in e['components']))
        while over(room_ents):
            room_ents.pop(); dropped_room += 1
    # ground: copies of the stock flat plane, one per 17x17-tile block not mostly under pasted rooms (a scaled plane
    # does not draw)
    span = P.TERRAIN_SIZE // P.TILE
    ground = []
    for bx in range(0, lay.width, span):
        for by in range(0, lay.height, span):
            block = [(x, y) for x in range(bx, min(bx + span, lay.width)) for y in range(by, min(by + span, lay.height))]
            if sum(c in taken for c in block) * 2 < len(block):
                g = copy.deepcopy(terrain)
                P._transform(g)['position'].update({'x': bx * P.TILE, 'z': by * P.TILE})
                ground.append(g)
    entities = ground + room_ents + wall_ents
    for i, e in enumerate(entities):
        e['id'] = i + 1
    t = copy.deepcopy(walls['terrain']); t['id'] = len(entities) + 1
    preset = {'dependencies': {k: list(v.values()) for k, v in deps.items()}, 'type': 'Preset', 'name': Path(d).stem,
              'entities': entities, 'terrain': t, 'biomeFilename': 'data/hd/env/biome/act3_jungle.json',
              'perTileBiomeOverrides': [], 'specialTiles': {}}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / f'{Path(d).stem}.json').write_text(json.dumps(preset, separators=(',', ':')))
    cov = sum(1 for c in lay.keys if c in taken)
    print(f'{d}: {len(rooms)} rooms cover {100 * cov / len(lay.keys):.1f}% of tiles; {len(room_ents)} room + '
          f'{len(wall_ents)} wall entities (room entities dropped {dropped_room}); expanded {weight(entities)}, '
          f'physics {physics(entities)}')
