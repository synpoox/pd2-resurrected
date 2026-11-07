"""Border painter (prototype): trees on the ground a player can't reach, placed the way stock jungle rooms place them.

Subtile grid: 5x5 per tile, blocked = DT1 subtile flag bit 0 (walk.py). Stock rooms teach, per tree instance, its
distance (subtiles) to the nearest blocked subtile and the direction to it (8 buckets); a PD2 map gets trees on
unreachable subtiles, nearest-to-the-border first, at least SPACING apart, each copied from a stock tree with the same
direction and distance."""
import collections, copy, functools, json, math, re, zlib

import walk

TREE = re.compile(r'tree_wall|treewall|treeline|small_cluster')
SPACING = 9        # subtiles between tree origins (stock median nearest-neighbour, measured below)
DEPTH = 30         # paint unreachable ground up to this many subtiles from the playable area
CAP = 15           # distances to the blocked line are capped here


def transform(e):
    return next((c for c in e['components'] if c['type'] == 'TransformDefinitionComponent'), None)


def subtile_grid(keys, width, height, roots):
    """blocked subtiles {(sx, sy)} and the set of all subtiles inside the layout."""
    fl = walk.cell_flags(keys, roots)
    blocked = set()
    for (x, y), f in fl.items():
        for k in range(25):
            if f[k] & 1:
                blocked.add((x * 5 + k % 5, y * 5 + k // 5))
    return blocked, width * 5, height * 5


def nearest_blocked(blocked, W, H):
    """BFS from the blocked set: distance (capped) and the blocked subtile reached, for every subtile."""
    dist, src = {}, {}
    q = collections.deque()
    for b in blocked:
        dist[b], src[b] = 0, b
        q.append(b)
    while q:
        p = q.popleft()
        if dist[p] >= CAP:
            continue
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (p[0] + dx, p[1] + dy)
            if 0 <= n[0] < W and 0 <= n[1] < H and n not in dist:
                dist[n], src[n] = dist[p] + 1, src[p]
                q.append(n)
    return dist, src


@functools.cache
def prefab_points(path):
    """(x, z) of every piece of a prefab, in its own frame."""
    from modkit.base import Base  # the toolkit is on sys.path by then
    try:
        j = json.loads(Base().text(path))
    except Exception:
        return ((0.0, 0.0),)
    return tuple((t['position']['x'], t['position']['z']) for t in map(transform, j.get('entities', [])) if t) or ((0.0, 0.0),)


def rotate(q, v):
    """vector v rotated by quaternion q (x, y, z, w)."""
    qx, qy, qz, qw = q['x'], q['y'], q['z'], q['w']
    tx, ty, tz = 2 * (qy * v[2] - qz * v[1]), 2 * (qz * v[0] - qx * v[2]), 2 * (qx * v[1] - qy * v[0])
    return (v[0] + qw * tx + (qy * tz - qz * ty), v[1] + qw * ty + (qz * tx - qx * tz), v[2] + qw * tz + (qx * ty - qy * tx))


_foot = {}


def footprint(e):
    """subtile offsets from the entity's origin subtile that its pieces cover (a prefab spreads over tiles)."""
    if id(e) not in _foot:
        t = transform(e)
        pf = next((c['prefab'] for c in e['components'] if c['type'] == 'PrefabPlacementDefinitionComponent'), None)
        sc = t.get('scale') or {'x': 1, 'y': 1, 'z': 1}
        out = set()
        for x, z in (prefab_points(pf) if pf else ((0.0, 0.0),)):
            v = rotate(t['orientation'], (x * sc['x'], 0.0, z * sc['z']))
            out.add((math.floor(v[0] / 2), math.floor(v[2] / 2)))
        _foot[id(e)] = frozenset(out)
    return _foot[id(e)]


def spread(start, W, H, depth):
    """BFS distance (subtiles) from `start`, up to depth."""
    reach, q = {p: 0 for p in start}, collections.deque(start)
    while q:
        p = q.popleft()
        if reach[p] >= depth:
            continue
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (p[0] + dx, p[1] + dy)
            if 0 <= n[0] < W and 0 <= n[1] < H and n not in reach:
                reach[n] = reach[p] + 1
                q.append(n)
    return reach


def bucket(p, s):
    if s is None or s == p:
        return -1
    return int(round(math.atan2(s[1] - p[1], s[0] - p[0]) / (math.pi / 4))) % 8


def harvest(rooms, roots):
    """{(bucket, distance): [entity]} from stock rooms [(layout, preset json)], and the stock tree spacing."""
    out, gaps = collections.defaultdict(list), []
    for lay, j in rooms:
        blocked, W, H = subtile_grid(lay.keys, lay.width, lay.height, roots)
        dist, src = nearest_blocked(blocked, W, H)
        fl = walk.cell_flags(lay.keys, roots)
        wall = {c for c, k in lay.keys.items() if k[0] == 'w'}
        thick = wall | {(x + i, y + j) for x, y in wall for i in (-1, 0, 1) for j in (-1, 0, 1)}
        open_ = {c for c, k in lay.keys.items() if c not in thick and not (k[0] == 'f' and walk.blocked(fl[c]))}
        reach = spread({(x * 5 + i, y * 5 + j) for x, y in open_ for i in range(5) for j in range(5)}, W, H, 10 ** 6)
        pts = []
        for e in j['entities']:
            if not TREE.search(json.dumps(e['components'])):
                continue
            t = transform(e)
            p = (int(t['position']['x'] / 2), int(t['position']['z'] / 2))  # 10 units per tile, 5 subtiles
            if not (0 <= p[0] < W and 0 <= p[1] < H):
                continue
            pts.append(p)
            d = dist.get(p, CAP)
            out[(bucket(p, src.get(p)), d)].append((e, t['position']['x'] - p[0] * 2, t['position']['z'] - p[1] * 2,
                                                  reach.get(p, 10 ** 6)))
        for a in pts:
            near = min((math.dist(a, b) for b in pts if b != a), default=None)
            if near is not None:
                gaps.append(near)
    gaps.sort()
    return out, gaps[len(gaps) // 2] if gaps else SPACING


WALK = 10  # subtiles
LINE = 10  # wall tiles in an 8-connected group this big are a tree line; smaller groups are trees standing in a clearing


def lines(wall):
    """the wall tiles that belong to lines (big 8-connected groups)."""
    out, seen = set(), set()
    for s0 in wall:
        if s0 in seen:
            continue
        comp, q = {s0}, collections.deque([s0])
        seen.add(s0)
        while q:
            p = q.popleft()
            for i in (-1, 0, 1):
                for j in (-1, 0, 1):
                    m = (p[0] + i, p[1] + j)
                    if m in wall and m not in seen:
                        seen.add(m)
                        comp.add(m)
                        q.append(m)
        if len(comp) >= LINE:
            out |= comp
    return out


def dilate(tiles):
    return tiles | {(x + i, y + j) for x, y in tiles for i in (-1, 0, 1) for j in (-1, 0, 1)}


def components(tiles):
    """4-connected groups of `tiles`."""
    seen = set()
    for s0 in tiles:
        if s0 in seen:
            continue
        comp, q = {s0}, collections.deque([s0])
        seen.add(s0)
        while q:
            p = q.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                m = (p[0] + dx, p[1] + dy)
                if m in tiles and m not in seen:
                    seen.add(m)
                    comp.add(m)
                    q.append(m)
        yield comp


def playable(lay, roots, seeds=frozenset()):
    """The tiles a player reaches, at tile level. Every wall tile widened by one tile seals PD2's thin tree lines; the
    sealed regions that hold a DS1 object (`seeds`), or that streams (blocked floor) run through and that don't touch
    the map's edge, are the paths; the
    others are outside. The paths then grow over lone trees (small wall groups) up to the lines and the outside, and
    take a one-tile margin and the streams."""
    fl = walk.cell_flags(lay.keys, roots)
    walls = {c for c, k in lay.keys.items() if k[0] == 'w'}
    line = lines(walls)
    water = {c for c, k in lay.keys.items() if k[0] == 'f' and walk.blocked(fl[c])}
    open_ = set(lay.keys) - dilate(walls) - water
    paths, outside = set(), set()
    for comp in components(open_):
        near = sum(1 for p in comp for i, j in ((1, 0), (-1, 0), (0, 1), (0, -1)) if (p[0] + i, p[1] + j) in water)
        edge = any((p[0] + i, p[1] + j) not in lay.keys for p in comp for i, j in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        (paths if comp & seeds or (near >= 20 and not edge) else outside).update(comp)
    free = set(lay.keys) - dilate(line) - water - outside
    grown = set().union(*(c for c in components(free | paths) if c & paths)) if paths else set()
    margin = dilate(grown) - line
    play = grown | (margin & set(lay.keys)) | water
    # then the ground a player really walks to from there (DT1 subtile flags), up to WALK subtiles out: the tile rule
    # misses open ground among lone trees next to a line, and an unbounded walk leaks through gaps in the lines
    free = {(x * 5 + k % 5, y * 5 + k // 5) for (x, y), f in fl.items() if (x, y) not in line for k in range(25)
            if not f[k] & 1}  # never through a line tile: an 8-connected line then seals a 4-connected walk
    start = {(x * 5 + i, y * 5 + j) for x, y in play for i in range(5) for j in range(5)} & free
    reach, q = {p: 0 for p in start}, collections.deque(start)
    while q:
        p = q.popleft()
        if reach[p] >= WALK:
            continue
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (p[0] + dx, p[1] + dy)
            if n in free and n not in reach:
                reach[n] = reach[p] + 1
                q.append(n)
    walked = collections.Counter((x // 5, y // 5) for x, y in reach)
    return (play | {c for c, n in walked.items() if n >= 5}) - line


def paint(lay, roots, kit, spacing, seed, skip=frozenset(), seeds=frozenset()):
    """Tree entities for a PD2 layout; `skip` = tiles left alone (pasted rooms)."""
    blocked, W, H = subtile_grid(lay.keys, lay.width, lay.height, roots)
    play_tiles = playable(lay, roots, seeds)
    best = {(x * 5 + i, y * 5 + j) for x, y in play_tiles for i in range(5) for j in range(5)}
    # distance from the playable area, for everything else
    reach = spread(best, W, H, DEPTH)
    keep_off = best | {(x * 5 + i, y * 5 + j) for x, y in skip for i in range(5) for j in range(5)}  # paths and pasted rooms
    dist, src = nearest_blocked(blocked, W, H)
    cand = [p for p, r in reach.items() if r > 0 and (p[0] // 5, p[1] // 5) in lay.keys
            and (p[0] // 5, p[1] // 5) not in skip]
    cand.sort(key=lambda p: (reach[p], zlib.crc32(f'{seed}:{p}'.encode())))
    singles = collections.defaultdict(list)  # per direction, the stock trees that are one model, not a prefab
    for (b, _), opts in kit.items():
        singles[b] += [o for o in opts if 'canopy' not in json.dumps(o[0]['components']) and not any(c['type'] == 'PrefabPlacementDefinitionComponent' for c in o[0]['components'])]
    canopy = lambda o: 'canopy' in json.dumps(o[0]['components'])

    def run(sub, allow_single, grid):
        placed, ents = [], []
        cell = max(1, int(spacing))
        for p in cand:
            g = (p[0] // cell, p[1] // cell)
            if any(math.dist(p, o) < spacing for gx in (-1, 0, 1) for gy in (-1, 0, 1) for o in grid[(g[0] + gx, g[1] + gy)]):
                continue
            b, d = bucket(p, src.get(p)), dist.get(p, CAP)
            ok = lambda o: o[3] <= reach[p]  # no closer to the playable area than the stock tree stood
            options = [o for o in sub.get((b, d), ()) if ok(o)] or next(
                (oo for dd in sorted(range(CAP + 1), key=lambda dd: abs(dd - d)) if (oo := [o for o in sub.get((b, dd), ()) if ok(o)])), None)
            if not options:
                continue
            k0 = zlib.crc32(f'{seed}:{p}:t'.encode())
            pick = None
            for i in range(len(options)):  # the first option (from a hashed start) whose pieces all stay off paths and pasted rooms
                o = options[(k0 + i) % len(options)]
                if not any((p[0] + dx, p[1] + dz) in keep_off for dx, dz in footprint(o[0])):
                    pick = o
                    break
            if pick is None:  # a band too thin for a tree cluster: a single tree model from the same direction
                small = [o for o in (singles.get(b, ()) if allow_single else ()) if o[3] <= reach[p] and not (set(footprint(o[0])) and
                         any((p[0] + dx, p[1] + dz) in keep_off for dx, dz in footprint(o[0])))]
                if not small:
                    continue
                pick = small[k0 % len(small)]
            e, ox, oz, _ = pick
            e = copy.deepcopy(e)
            t = transform(e)
            t['position']['x'], t['position']['z'] = p[0] * 2 + ox, p[1] * 2 + oz
            for c in e['components']:  # a wall's transparency names its tile
                if c['type'] == 'WallTransparencyComponent' and c['wallTileLocalCoord']['x'] >= 0:
                    c['wallTileLocalCoord'] = {'x': p[0] // 5, 'y': p[1] // 5}
            ents.append(e)
            grid[g].append(p)
            placed.append(p)
        return ents

    # tree clusters first, then single tree models in the gaps (same spacing), then canopy on its own spacing
    pf = lambda o: any(c['type'] == 'PrefabPlacementDefinitionComponent' for c in o[0]['components'])
    walls = collections.defaultdict(list)
    trees = run({k: [o for o in v if pf(o)] for k, v in kit.items()}, False, walls)
    trees += run({k: [o for o in v if not pf(o) and not canopy(o)] for k, v in kit.items()}, True, walls)
    tops = run({k: [o for o in v if canopy(o)] for k, v in kit.items()}, False, collections.defaultdict(list))
    return trees + tops, play_tiles

