"""Top-down map of a generated PD2 preset: walkable ground, pasted rooms, water planes, trees (scale 4 px per tile)."""
import json, sys
from pathlib import Path
from PIL import Image, ImageDraw
sys.path.insert(0, 'tools')
import pd2_presets as P, pd2_painter as PA

name = sys.argv[1]  # e.g. jungle1
out = Path('out/data')
D = Path(r"C:/Users/micah/orca/workspaces/pd2-resurrected/d2rAI-toolkit/d2r-data/93847/data")
preset = json.loads((out / f'hd/env/preset/expansion/map/{name}.json').read_text('utf-8'))
sets, standins, _ = P.read_rule(Path('merge/presets-hd.tsv'))
from pd2_ids import load
pres = P.pd2_only_presets(out, len(load(D, 'lvlprest').records()))
files = next(f for _, _, f, ds1s in pres for d in ds1s if d.endswith(f'/{name}.ds1'))
lay = P.layout((out / f'global/tiles/expansion/map/{name}.ds1').read_bytes(),
               P.stand_in(files, P.Tiles(D / 'global/tiles'), P.Tiles(out / 'global/tiles'), standins))
S = 4
im = Image.new('RGB', (lay.width * S, lay.height * S), (20, 20, 20))
d = ImageDraw.Draw(im)
for (x, y), k in lay.keys.items():
    col = (60, 50, 40) if (x, y) in lay.unwalkable else (110, 100, 80)
    if k[0] == 'f' and PA.blocked_floor(lay.flags[(x, y)]):
        col = (30, 40, 90)
    d.rectangle([x * S, y * S, x * S + S - 1, y * S + S - 1], fill=col)
for e in preset['entities']:
    t = P._transform(e)
    if not t:
        continue
    m = json.dumps(e['components'])
    x, z = t['position']['x'] / 10, t['position']['z'] / 10
    if 'water_16x16_01' in m:
        w, h = t['scale']['x'] * 18, t['scale']['z'] * 18
        d.rectangle([(x - w / 2) * S, (z - h / 2) * S, (x + w / 2) * S - 1, (z + h / 2) * S - 1], fill=(60, 140, 255))
    elif 'water_flow' in m or 'bridge' in m:
        d.ellipse([x * S - 3, z * S - 3, x * S + 3, z * S + 3], outline=(255, 200, 0))
im.save(sys.argv[2])
print(im.size)
