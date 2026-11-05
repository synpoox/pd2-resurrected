"""Skill tree backgrounds (https://github.com/synpoox/pd2-resurrected/issues/84): every class's tree art redrawn so its boxes and prerequisite arrows follow PD2's
skill positions. Each D2R tree sprite (one frame per tab: the tab strip on top, the boxes and arrows below it on a
clear background) keeps its tab strip; below it the art is cleared and redrawn: a box at each merged skilldesc
SkillPage / SkillRow / SkillColumn (SkillPage 0 gets none) and an arrow from each skills reqskill on the same page to
its skill. The box and arrow pictures are cut from D2R's own Amazon tree, so no PD2 art is used. The Warlock keeps
D2R's art: alpha 0.1.0 ships D2R's Warlock as it is.

merge/tree-backgrounds.json: per sprite kind (HD, lowend, controller and its lowend), the sprite file and where
D2R's art draws its grid, measured on build 93847 (the art is hand-placed, so it differs from the layout JSON's
skillRow / skillColumn by a pixel or two):
    {"<kind>": {"sprite": "hd/.../{}skilltree.sprite", "rows": [top y of each SkillRow's box, ...],
                "columns": [left x of each SkillColumn's box in a frame, ...], "box": side, "clear_below": y}, ...}
"""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path

from pd2_ids import Table, excel

CLASSES = {"ama": "am", "sor": "so", "nec": "ne", "pal": "pa", "bar": "ba", "dru": "dr", "ass": "as"}
# where the pictures are cut from in D2R's Amazon tree, 1-based like skilldesc
# page, row, column: the top half of Inner Sight's box (no arrow comes in), the bottom half of Lightning Fury's
# (no arrow goes out)
BOX_FROM = (2, 1, 1), (3, 6, 3)
ARROW_FROM = (3, 1, 1, 3)  # page, column, from row, to row: Jab -> Impale, a straight arrow over an empty row
HEAD = struct.Struct("<4sHHIIIIIIII")  # SpA1 header, then RGBA rows


class Image:
    def __init__(self, w: int, h: int, px: bytearray | None = None):
        self.w, self.h = w, h
        self.px = px if px is not None else bytearray(w * h * 4)

    def crop(self, x: int, y: int, w: int, h: int) -> Image:
        out = Image(w, h)
        for j in range(min(h, self.h - y)):  # rows past the bottom stay clear: the last row's boxes reach it
            s = ((y + j) * self.w + x) * 4
            out.px[j * w * 4:(j + 1) * w * 4] = self.px[s:s + w * 4]
        return out

    def paste(self, src: Image, x: int, y: int) -> None:
        """Copy src in, transparent pixels included (boxes never overlap, and go in before the arrows)."""
        for j in range(min(src.h, self.h - y)):
            d = ((y + j) * self.w + x) * 4
            self.px[d:d + src.w * 4] = src.px[j * src.w * 4:(j + 1) * src.w * 4]


def read_sprite(path: Path) -> tuple[bytes, Image, int]:
    """The header, the whole strip, and the frame stride."""
    b = path.read_bytes()
    magic, version, _, w, h, _, frames, *_ = HEAD.unpack_from(b)
    if magic != b"SpA1" or version != 31 or len(b) < HEAD.size + w * h * 4:
        raise ValueError(f"{path}: not a SpA1 v31 sprite")
    return b[:HEAD.size], Image(w, h, bytearray(b[HEAD.size:HEAD.size + w * h * 4])), w // frames


def layout(skills: Table, skilldesc: Table, rows: int, columns: int) -> dict[str, tuple[list, list]]:
    """class code -> (boxes [(page, row, column)], arrows [(from box, to box)]) from the merged tables."""
    sc = {c.lower(): j for j, c in enumerate(skills.header)}
    dc = {c.lower(): j for j, c in enumerate(skilldesc.header)}
    desc = {r[0].lower(): r for r in skilldesc.rows}
    at, cls = {}, {}  # skill name (lower case) -> its box, its class
    out: dict[str, tuple[list, list]] = {c: ([], []) for c in CLASSES}
    for r in skills.rows:
        d = desc.get(r[sc["skilldesc"]].lower())
        if r[sc["charclass"]] not in CLASSES or d is None or d[dc["skillpage"]] in ("", "0"):
            continue
        box = tuple(int(d[dc[c]]) for c in ("skillpage", "skillrow", "skillcolumn"))
        if not (1 <= box[0] <= 3 and 1 <= box[1] <= rows and 1 <= box[2] <= columns):
            raise ValueError(f"'{r[0]}': page {box[0]} row {box[1]} column {box[2]} is off the {rows}x{columns} tree")
        boxes = out[r[sc["charclass"]]][0]
        if box in boxes:
            raise ValueError(f"'{r[0]}': another {r[sc['charclass']]} skill has page {box[0]} row {box[1]} "
                             f"column {box[2]}")
        boxes.append(box)
        at[r[0].lower()], cls[r[0].lower()] = box, r[sc["charclass"]]
    names = {r[0].lower() for r in skills.rows}
    for r in skills.rows:
        for c in ("reqskill1", "reqskill2", "reqskill3"):
            req = r[sc[c]].lower() if c in sc else ""
            if req and req not in names:
                raise ValueError(f"'{r[0]}': {c} '{r[sc[c]]}' is no skill")
            # a prerequisite off the tree or on another page has no arrow to draw
            if r[0].lower() in at and req in at and at[req][0] == at[r[0].lower()][0]:
                out[cls[req]][1].append((at[req], at[r[0].lower()]))
    return out


def cut(am: Image, stride: int, g: dict) -> tuple[Image, Image, Image]:
    """The box picture (its counter box included) and an arrow's shaft row and head, from D2R's Amazon tree."""
    b, m = g["box"], g["box"] // 24
    top, bottom = (am.crop(frame(page) * stride + g["columns"][col - 1] - m, g["rows"][row - 1] - m,
                           b * 3 // 2 + 2 * m, b + 2 * m) for page, row, col in BOX_FROM)
    box = Image(top.w, top.h, top.px[:top.w * 4 * (m + b // 2)] + bottom.px[top.w * 4 * (m + b // 2):])
    page, col, r1, r2 = ARROW_FROM
    w, x = b // 2, frame(page) * stride + g["columns"][col - 1] + b // 4
    end = g["rows"][r2 - 1] - gap(b)
    head_h = b * 3 // 10
    return box, am.crop(x, (g["rows"][r1 - 1] + b + end) // 2, w, 1), am.crop(x, end - head_h, w, head_h)


def frame(page: int) -> int:
    return 3 - page  # D2R's frames run from SkillPage 3 to 1


def gap(box: int) -> int:
    return max(1, box // 60)  # between an arrow's tip and the box it points at


def arrow(shaft: Image, head: Image, length: int) -> Image:
    n = max(length, head.h)  # an arrow between neighbouring rows is all head and overlaps the box above, as D2R's
    return Image(head.w, n, shaft.px * (n - head.h) + head.px)


def draw(dst: Image, a: Image, tip: tuple[float, float], d: tuple[float, float],
         clip: tuple[float, float] = (-math.inf, math.inf), replace: bool = False) -> None:
    """Lay arrow a over dst, turned to unit direction d (tail to tip), its bottom middle on tip, only where clip[0] <=
    x < clip[1]. Bilinear. With replace, a takes the place of what is under it instead of going over it."""
    (tx, ty), (dx, dy), hw = tip, d, a.w / 2
    xs = [tx + u * dy + v * dx for u in (-hw, hw) for v in (0, -a.h)]
    ys = [ty - u * dx + v * dy for u in (-hw, hw) for v in (0, -a.h)]
    src, px = a.px, dst.px
    for y in range(max(0, math.floor(min(ys))), min(dst.h, math.ceil(max(ys)) + 1)):
        for x in range(max(0, math.floor(min(xs)), math.ceil(max(clip[0], -1))),
                       min(dst.w, math.ceil(max(xs)) + 1, math.ceil(min(clip[1], dst.w)))):
            wx, wy = x + 0.5 - tx, y + 0.5 - ty
            sx, sy = wx * dy - wy * dx + hw - 0.5, wx * dx + wy * dy + a.h - 0.5
            x0, y0 = math.floor(sx), math.floor(sy)
            fx, fy = sx - x0, sy - y0
            acc, cov = [0.0, 0.0, 0.0, 0.0], 0.0  # premultiplied; how much of the pixel a covers
            for xx, yy, k in ((x0, y0, (1 - fx) * (1 - fy)), (x0 + 1, y0, fx * (1 - fy)),
                              (x0, y0 + 1, (1 - fx) * fy), (x0 + 1, y0 + 1, fx * fy)):
                if k > 0 and 0 <= xx < a.w and 0 <= yy < a.h:
                    cov += k
                    i = (yy * a.w + xx) * 4
                    al = src[i + 3] * k
                    if al:
                        acc[0] += src[i] * al
                        acc[1] += src[i + 1] * al
                        acc[2] += src[i + 2] * al
                        acc[3] += al
            if acc[3] < 0.5:
                continue
            i, sa = (y * dst.w + x) * 4, acc[3] / 255
            da = px[i + 3] / 255 * (1 - (cov if replace else sa))  # source over (or in place of) destination
            oa = sa + da
            for c in range(3):
                px[i + c] = round((acc[c] / 255 + px[i + c] * da) / oa)
            px[i + 3] = round(oa * 255)


def ends(s: tuple[int, int], t: tuple[int, int], b: int, tail: tuple[int, int] | None = None):
    """Tip, unit direction and length of the arrow from the box at s to the box at t (top left corners), or from
    tail on the straight arrow below s."""
    if t[0] != s[0] and t[1] > s[1]:  # off the box's side (or tail) to the near top corner; high on the right side,
        right = t[0] > s[0]  # where the counter box below cuts the arrow
        tail, head = tail or (s[0] + b * right, s[1] + b * (1 if right else 3) / 10), (t[0] + b * (not right), t[1])
    else:  # straight down, or a way D2R's trees never point: centre to centre, cut at the boxes' edges
        dx, dy = t[0] - s[0], t[1] - s[1]
        k = b / 2 / max(abs(dx), abs(dy))
        tail, head = (s[0] + b / 2 + dx * k, s[1] + b / 2 + dy * k), (t[0] + b / 2 - dx * k, t[1] + b / 2 - dy * k)
    dx, dy = head[0] - tail[0], head[1] - tail[1]
    n = math.hypot(dx, dy)
    ux, uy = dx / n, dy / n
    return (head[0] - ux * gap(b), head[1] - uy * gap(b)), (ux, uy), round(n - gap(b))


def counter(box: Image, right: int) -> tuple[int, int, int]:
    """The counter box's top, right and bottom in the box picture: what is right of the skill box and its shadow."""
    xs, ys = [], []
    for y in range(box.h):
        for x in range(right, box.w):
            if box.px[(y * box.w + x) * 4 + 3] > 32:  # D2R's art has faint specks around its boxes
                xs.append(x)
                ys.append(y)
    return min(ys), max(xs) + 1, max(ys) + 1


def redraw(strip: Image, stride: int, g: dict, pics: tuple[Image, Image, Image], boxes: list, arrows: list) -> None:
    box, shaft, head = pics
    b, m = g["box"], g["box"] // 24
    cx0, (cy0, cx1, cy1) = m + b, counter(box, m + b + b // 10)
    pad = b // 12
    strip.px[g["clear_below"] * strip.w * 4:] = bytes((strip.h - g["clear_below"]) * strip.w * 4)

    def corner(p: tuple[int, int, int]) -> tuple[int, int]:
        return frame(p[0]) * stride + g["columns"][p[2] - 1], g["rows"][p[1] - 1]
    for p in boxes:
        x, y = corner(p)
        strip.paste(box, x - m, y - m)
    alpha = [shaft.px[i * 4 + 3] for i in range(shaft.w)]
    left, right = alpha.index(max(alpha[:shaft.w // 2])), alpha.index(max(alpha[shaft.w // 2:]), shaft.w // 2)
    core = shaft.crop(left + 1, 0, right - left - 1, 1)  # inside the shaft's dark edge lines

    def branch(s, t):  # as PD2's: a diagonal that skips rows leaves the straight arrow
        return s[2] != t[2] and t[1] - s[1] > 1
    for s, t in sorted(arrows, key=lambda a: branch(*a)):  # straight arrows first: branches join them
        if not branch(s, t):
            tip, d, length = ends(corner(s), corner(t), b)
            draw(strip, arrow(shaft, head, length), tip, d)
            continue
        # shortcut: a box in the source column on the row above the target would sit on the branch; none does today
        x, y = corner(s)
        tail = (x + b // 2, g["rows"][t[1] - 2] + b // 2)
        bottom = min(tail[1] + b // 2, g["rows"][t[1] - 1] - gap(b) - head.h)  # above the straight arrow's head
        if not any(a == s and u[2] == s[2] and u[1] >= t[1] for a, u in arrows):
            n = tail[1] - y - b
            bottom = tail[1]
            draw(strip, Image(shaft.w, n, shaft.px * n), tail, (0, 1))
        tip, d, length = ends(corner(s), corner(t), b, tail)
        near = (-math.inf, tail[0]) if d[0] < 0 else (tail[0], math.inf)
        # its end stops inside the straight arrow, then each arrow's inside covers the other's edge lines: one shape
        draw(strip, arrow(shaft, head, length), tip, d, (near[0] - core.w / 2, near[1] + core.w / 2))
        n = bottom - tail[1] + b // 2
        draw(strip, Image(core.w, n, core.px * n), (tail[0], bottom), (0, 1), replace=True)
        n = b // 2
        draw(strip, Image(core.w, n, core.px * n), (tail[0] + d[0] * n, tail[1] + d[1] * n), d, near, True)
    for p in boxes:  # as PD2's: arrows keep clear of the counter boxes
        x, y = corner(p)
        x, y = x - m + cx0, y - m + cy0
        for j in range(max(0, y - pad), min(strip.h, y + cy1 - cy0 + pad)):  # from the skill box's edge
            strip.px[(j * strip.w + x) * 4:(j * strip.w + x + cx1 - cx0 + pad) * 4] = bytes((cx1 - cx0 + pad) * 4)
        strip.paste(box.crop(cx0, cy0, cx1 - cx0, cy1 - cy0), x, y)


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/tree-backgrounds.json: write every class's tree sprites for the merged tables."""
    kinds = json.loads(rule.read_text("utf-8"))
    tables = [excel(out_data) / f"{t}.txt" for t in ("skills", "skilldesc")]
    if not all(p.is_file() for p in tables):
        raise ValueError("skills.txt and skilldesc.txt are not merged: the trees follow the merged tables")
    for kind, g in kinds.items():
        trees = layout(Table(tables[0]), Table(tables[1]), len(g["rows"]), len(g["columns"]))
        _, am, stride = read_sprite(d2r_data / g["sprite"].format("am"))
        pics = cut(am, stride, g)
        for code, prefix in CLASSES.items():
            head, strip, stride = read_sprite(d2r_data / g["sprite"].format(prefix))
            redraw(strip, stride, g, pics, *trees[code])
            dest = out_data / g["sprite"].format(prefix)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(head + strip.px)
        print(f"  tree-backgrounds: {kind}: {len(CLASSES)} classes redrawn")
