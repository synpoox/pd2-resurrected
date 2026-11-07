"""Panel grids (#299): D2R's own panel art redrawn one cell column wider, for a grid PD2 makes wider (the cube's 3 x 4
to PD2's 4 x 4). Stand-in art, no PD2 art: the framed grid is cut at its second column, that column is drawn twice,
and the frame's two halves move apart by half a cell each. The layouts' grid rects move with it (patches/).

merge/panel-grids.json, measured on build 93847:
    {"<D2R art path under data/>": {"grid": [left x, top y of the first cell], "cell": px, "size": [columns, rows],
                                    "border": px of frame around the grid to move with it}, ...}
An HD .sprite is one frame; a classic .dc6 is a panel of 4 frames in a 2 x 2 tiling (only frames 0-3 are redrawn).
"""
from __future__ import annotations

import json
import struct
from pathlib import Path

from pd2_skilltrees import Image, read_sprite


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
