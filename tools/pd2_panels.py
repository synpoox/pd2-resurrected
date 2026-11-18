"""Panel grids (#299): D2R's own panel art redrawn one cell column wider, for a grid PD2 makes wider (the cube's 3 x 4
to PD2's 4 x 4). Stand-in art, no PD2 art: the framed grid is cut at its second column, that column is drawn twice,
and the frame's two halves move apart by half a cell each. The layouts' grid rects move with it (the committed layout JSONs).

merge/panel-grids.json, measured on build 93847:
    {"<D2R art path under data/>": {"grid": [left x, top y of the first cell], "cell": px, "size": [columns, rows],
                                    "border": px of frame around the grid to move with it}, ...}
An HD .sprite is one frame; a classic .dc6 is a panel of 4 frames in a 2 x 2 tiling (only frames 0-3 are redrawn).
"""
from __future__ import annotations

import json
import struct
from pathlib import Path

import pd2_art
import pd2_table
from pd2_skilltrees import Image, read_sprite

ROOT = Path(__file__).resolve().parent.parent


def widen(img: Image, grid: list[int], cell: int, size: list[int], border: int) -> None:
    """Insert one cell column after the grid's first, in place: the frame grows by half a cell on each side."""
    gx, gy = grid
    left, a, b, right = gx - border, gx + cell, gx + 2 * cell, gx + size[0] * cell + border
    top, h = gy - border, size[1] * cell + 2 * border
    half = cell // 2
    parts = [(img.crop(left, top, a - left, h), left - half), (img.crop(a, top, b - a, h), a - half),
             (img.crop(a, top, right - a, h), a + cell - half)]
    for part, x in parts:
        img.paste(part, x, top)


def dc6_panel(data: bytes) -> tuple[list[tuple], Image]:
    """The frame headers (DC6 frames 0-3) and the 2 x 2 tiled panel as palette indices: pixel byte 0 is the index,
    byte 3 is 255 where the pixel is drawn."""
    dirs, fpd = struct.unpack_from("<2I", data, 16)
    frames = []
    for p in struct.unpack_from(f"<{dirs * fpd}I", data, 24)[:4]:
        _flip, w, h, ox, oy, _u, _n, length = struct.unpack_from("<8i", data, p)
        frames.append((w, h, ox, oy, data[p + 32:p + 32 + length]))
    img = Image(frames[0][0] + frames[1][0], frames[0][1] + frames[2][1])
    for k, (w, h, _ox, _oy, block) in enumerate(frames):
        fx, fy = (k % 2) * frames[0][0], (k // 2) * frames[0][1]
        x, y, i = 0, h - 1, 0  # rows run bottom-up
        while i < len(block) and y >= 0:
            c = block[i]
            i += 1
            if c == 0x80:
                x, y = 0, y - 1
            elif c & 0x80:
                x += c & 0x7F
            else:
                for idx in block[i:i + c]:
                    o = ((fy + y) * img.w + fx + x) * 4
                    img.px[o], img.px[o + 3] = idx, 255
                    x += 1
                i += c
    return frames, img


def dc6_frame(img: Image, x0: int, y0: int, w: int, h: int) -> bytes:
    """RLE block of one frame cut from a panel of palette indices."""
    out = bytearray()
    for y in range(h - 1, -1, -1):
        row = [img.px[((y0 + y) * img.w + x0 + x) * 4:((y0 + y) * img.w + x0 + x) * 4 + 4] for x in range(w)]
        x = 0
        while x < w:
            if row[x][3] == 0:
                run = 0
                while x < w and row[x][3] == 0 and run < 127:
                    run, x = run + 1, x + 1
                if x < w:  # trailing transparency needs no skip
                    out.append(0x80 | run)
                continue
            px = bytearray()
            while x < w and row[x][3] and len(px) < 127:
                px.append(row[x][0])
                x += 1
            out.append(len(px))
            out += px
        out.append(0x80)
    return bytes(out)


def redraw_dc6(data: bytes, g: dict) -> bytes:
    frames, img = dc6_panel(data)
    widen(img, g["grid"], g["cell"], g["size"], g["border"])
    dirs, fpd = struct.unpack_from("<2I", data, 16)
    ptrs = list(struct.unpack_from(f"<{dirs * fpd}I", data, 24))
    ends = ptrs[1:] + [len(data)]
    blobs = []
    for k, (w, h, ox, oy, _block) in enumerate(frames):
        block = dc6_frame(img, (k % 2) * frames[0][0], (k // 2) * frames[0][1], w, h)
        blobs.append(struct.pack("<8i", 0, w, h, ox, oy, 0, 0, len(block)) + block + b"\xee\xee\xee")
    blobs += [data[p:e] for p, e in zip(ptrs[4:], ends[4:])]  # frames past the panel (another resolution) unchanged
    offset, out = 24 + 4 * len(ptrs), []
    for blob in blobs:
        out.append(offset)
        offset += len(blob)
    return data[:24] + struct.pack(f"<{len(out)}I", *out) + b"".join(blobs)


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/panel-grids.json."""
    for rel, g in json.loads(rule.read_text("utf-8")).items():
        src = d2r_data / rel
        if not src.is_file():
            raise ValueError(f"panel-grids: no {src} (classic art: `python -m modkit fetch legacy-ui`)")
        dest = out_data / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if rel.endswith(".dc6"):
            dest.write_bytes(redraw_dc6(src.read_bytes(), g))
        else:
            head, img, _ = read_sprite(src)
            widen(img, g["grid"], g["cell"], g["size"], g["border"])
            dest.write_bytes(head + img.px)
    print(f"  panel-grids: {len(json.loads(rule.read_text('utf-8')))} panels widened")


# The Sacrifice Altar's panel art (#1398, #1413): the Horadric Cube's panel with its 4 x 4 opening filled with the
# inventory's stone and the inventory's amulet-slot frame set in the middle. The inputs are the repo's committed cube
# art and D2R's pristine inventory art (modkit's cache, as pd2_table.D2R_DATA finds it).
HD_ALTAR = "files/data/hd/global/ui/panel/pd2r/altar_bg.sprite"  # + the .lowend.sprite twin
SD_ALTAR = "files/data/global/ui/panel/pd2r_altar.dc6"


def altar_hd(cube, inv):
    """The HD panel from the cube art and D2R's inventory classic_background (modkit images, (w, h, rgba))."""
    _, _, im = pd2_art.modkit()
    iw, _ih, ic = inv
    ox, oy, n = 385, 540, 385  # the cube's 4x4 opening inside the gold frame
    sx, sy, sw = 322, 160, 143  # clean stone strip of the inventory, left of the torso slot
    ov, offs = 22, [0, 190, 95]
    starts = [k * (sw - ov) for k in range(3)]
    fill = bytearray(n * n * 4)
    for y in range(n):
        for x in range(n):
            acc, tw = [0.0, 0.0, 0.0], 0.0
            for k, s in enumerate(starts):
                lx = x - s
                if 0 <= lx < sw:
                    wgt = 1.0
                    if k > 0 and lx < ov:
                        wgt = (lx + 0.5) / ov
                    if k < 2 and lx >= sw - ov and x >= starts[k + 1]:
                        wgt = 1 - (lx - (sw - ov) + 0.5) / ov
                    o = ((sy + offs[k] + y) * iw + sx + lx) * 4
                    for i in range(3):
                        acc[i] += ic[o + i] * wgt
                    tw += wgt
            d = min(x, y, n - 1 - x, n - 1 - y)  # recessed edge: the stone sits under the gold frame
            shade = 0.55 + 0.45 * min(d, 10) / 10
            o = (y * n + x) * 4
            fill[o:o + 4] = bytes([min(255, round(a / tw * shade)) for a in acc] + [255])
    img = im.paste(cube, (n, n, bytes(fill)), ox, oy)
    fx, fy, fw, fh = 703, 336, 130, 135  # the inventory's amulet slot frame; its slot cell is at inventory (718, 353)
    frame = im.crop(inv, fx, fy, fw, fh)[2]
    px0, py0 = 529 - (718 - fx), 683 - (353 - fy)  # the layout rect of slot_bg / AltarSlot: centred in the opening
    w, h, base = img
    out = bytearray(base)
    for y in range(fh):
        for x in range(fw):
            a = min(min(x, y, fw - 1 - x, fh - 1 - y) + 1, 4) / 4  # feather the outer 3 px into the stone
            so, do = (y * fw + x) * 4, ((py0 + y) * w + px0 + x) * 4
            for i in range(3):
                out[do + i] = round(frame[so + i] * a + out[do + i] * (1 - a))
    return w, h, bytes(out)


def altar_sd(root: Path, d2r_data: Path) -> bytes:
    """The legacy panel (a DC6 of 4 frames, 2 x 2) from the committed supertransmogrifier.dc6 and pd2_invchar6.dc6."""
    dc6, _, im = pd2_art.modkit()
    pal = dc6.palette((d2r_data / "global/palette/act1/pal.dat").read_bytes())
    panel = root / "files/data/global/ui/panel"

    def compose(path, first):
        fr = [dc6.decode(f, pal) for f in dc6.read(path)[first:first + 4]]
        out = im.blank(320, fr[0][1] + fr[2][1])
        for i, f in enumerate(fr):
            out = im.paste(out, f, 256 * (i % 2), fr[0][1] * (i // 2))
        return out

    cube = compose(panel / "supertransmogrifier.dc6", 0)
    stats = compose(panel / "pd2_invchar6.dc6", 0)  # the character panel: the inventory's stone, with a clean patch
    inv = compose(panel / "pd2_invchar6.dc6", 4)
    ox, oy, n = 104, 139, 115  # the cube's 4x4 opening inside its gold frame
    src = im.crop(stats, 4, 328, 116, 100)
    greys = sorted({src[2][i:i + 3] for i in range(0, len(src[2]), 4)})  # only the stone's own colours
    sc = bytearray()
    for y in range(n):  # nearest-neighbour stretch keeps the DC6 colours exact
        for x in range(n):
            o = ((y * 100 // n) * 116 + x * 116 // n) * 4
            sc += src[2][o:o + 4]
    for y in range(n):
        for x in range(n):
            k = 0.55 + 0.45 * min(min(x, y, n - 1 - x, n - 1 - y), 3) / 3
            o = (y * n + x) * 4
            v = [sc[o + i] * k for i in range(3)]
            sc[o:o + 3] = min(greys, key=lambda g: sum((g[i] - v[i]) ** 2 for i in range(3)))
    img = im.paste(cube, (n, n, bytes(sc)), ox, oy)
    # the inventory's neck frame (gold box x 197-229, y 37-70 around slot rect (202, 43)); the slot rect maps to (150, 185)
    img = im.paste(img, im.crop(inv, 195, 35, 37, 38), 195 + 150 - 202, 35 + 185 - 43)
    parts = [im.crop(img, 0, 0, 256, 256), im.crop(img, 256, 0, 64, 256), im.crop(img, 0, 256, 256, 176),
             im.crop(img, 256, 256, 64, 176)]
    return _dc6_bytes(dc6, [dc6.encode(p, pal) for p in parts])


def _dc6_bytes(dc6, frames) -> bytes:
    """dc6.write only writes a path: write to a temp file and read it back."""
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        dc6.write(Path(tmp) / "a.dc6", frames)
        return (Path(tmp) / "a.dc6").read_bytes()


def build_altar(root: Path = ROOT, d2r_data: Path = pd2_table.D2R_DATA) -> None:
    """Rebuild the altar panel's three art files under root from root's cube art and D2R's inventory art."""
    _, _, im = pd2_art.modkit()
    cube = im.read_sprite(root / "files/data/hd/global/ui/panel/horadric_cube/horadriccube_bg.sprite")[0]
    inv = im.read_sprite(d2r_data / "hd/global/ui/panel/inventory/classic_background.sprite")[0]
    im.write_sprite(root / HD_ALTAR, altar_hd(cube, inv))
    (root / SD_ALTAR).write_bytes(altar_sd(root, d2r_data))


if __name__ == "__main__":
    build_altar()
    print("altar panel art rebuilt")
