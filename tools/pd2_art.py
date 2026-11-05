"""HD art from PD2's classic pictures, upscaled with Real-ESRGAN's realesrgan-x4plus model (4x): picked in #205 over a
bilinear upscale for a painted look closer to D2R's own art.

Its output is cached as merge/art/<name>-<hash of the classic frame>.png and committed, so the merge runs Real-ESRGAN
only for a frame with no cached art: once, for every such frame. REALESRGAN names realesrgan-ncnn-vulkan.exe (from
github.com/xinntao/Real-ESRGAN releases); the default is the folder realesrgan-ncnn-vulkan next to this repository.
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import tempfile
import warnings
from pathlib import Path

import pd2_strings

ROOT = Path(__file__).resolve().parent.parent
UPSCALER = Path(os.environ.get("REALESRGAN", ROOT.parent / "realesrgan-ncnn-vulkan" / "realesrgan-ncnn-vulkan.exe"))
SCALE = 4
# D2R's glyph edge, the pixel rings out from it as factors of the plate under them: a thin dark line, a copper rim, a
# faint rim (measured on every class sheet of build 93847: plate 46, then 36-50, 71-74, 51-53); the lit bevel is faint
EDGE, BEVEL = (0.45, 1.7, 1.12), 0.5
# PD2's glyph: what is GLYPH brighter than the plate (on the 4x upscale), cleaned of specks and pinholes under CLEAN
# 4x px; its greys darken the metal by up to SHADING
GLYPH, CHROMA, CLEAN, SHADING = 50, 0.25, 7, 0.15
MIN_SIZE = 32  # Real-ESRGAN (ncnn) returns noise for a whole run that holds a smaller image (PD2's 10 px frame)


class ArtError(ValueError):
    pass


def modkit():
    """modkit's dc6, hd and images modules (d2rAI-toolkit found as pd2_strings finds it)."""
    pd2_strings.modkit()
    from modkit import dc6, hd, images
    return dc6, hd, images


def esrgan(src: Path, dst: Path) -> None:
    """Every PNG in src upscaled into dst, under the same name."""
    if not UPSCALER.is_file():
        raise ArtError(f"art missing from merge/art and no Real-ESRGAN at {UPSCALER}: set REALESRGAN to "
                       "realesrgan-ncnn-vulkan.exe")
    subprocess.run([str(UPSCALER), "-i", str(src), "-o", str(dst), "-n", "realesrgan-x4plus", "-s", str(SCALE),
                    "-f", "png"], check=True, capture_output=True)


def upscale(art: Path, frames: dict, finish=None) -> dict:
    """{name: classic RGBA image} -> {name: the image at SCALE x, or finish(name, classic, upscaled) of it}, from art/
    or made into it. finish runs only for new art (icon_style needs numpy and Pillow).
    shortcut: art for a frame PD2 later changes, or made by an older finish, stays in art/; delete it by hand then."""
    _, _, images = modkit()
    path = {n: art / f"{n}-{hashlib.sha256(repr(img[:2]).encode() + img[2]).hexdigest()[:8]}.png"
            for n, img in frames.items()}
    missing = [n for n in frames if not path[n].is_file()]
    if missing:
        with tempfile.TemporaryDirectory() as tmp:
            src, dst = Path(tmp) / "in", Path(tmp) / "out"
            src.mkdir()
            dst.mkdir()
            for k, n in enumerate(missing):
                w, h, _ = frames[n]
                padded = images.paste(images.blank(max(w, MIN_SIZE), max(h, MIN_SIZE)), frames[n], 0, 0)
                images.write_png(src / f"{k}.png", padded)
            esrgan(src, dst)
            for k, n in enumerate(missing):
                w, h, _ = frames[n]
                big = images.read_png(dst / f"{k}.png")
                if big[:2] != (max(w, MIN_SIZE) * SCALE, max(h, MIN_SIZE) * SCALE):
                    raise ArtError(f"{n}: Real-ESRGAN gave the wrong size")
                big = images.crop(big, 0, 0, w * SCALE, h * SCALE)
                path[n].parent.mkdir(parents=True, exist_ok=True)
                images.write_png(path[n], finish(n, frames[n], big) if finish else big)
        print(f"  art: upscaled {len(missing)} frames")
    return {n: images.read_png(path[n]) for n in frames}


def icon_style(hd_frames: list, classic_frames: list, plate_frames: list | None = None):
    """D2R's look for PD2's skill icons, from a D2R sheet's HD frames and their classic frames (one kind: normal or
    pressed). D2R repainted each icon as a silver glyph on its class's plate and frame, edged by a thin dark line and a
    copper rim outside it; an upscale alone keeps classic art's look. So the icon is rebuilt: D2R's plate and frame
    (each pixel's median over plate_frames, glyph pixels left out: every class's sheet, as one class's icons share
    motifs a median keeps, the Paladin's aura ring; the sheet itself by default) and PD2's glyph (what is grey and
    brighter than the plate, on the crisp upscale), painted with the edge profile measured on D2R's glyphs: each ring
    of pixels out from a glyph's edge as a factor of the plate under it, each ring in as a colour. Returns
    finish(classic, up) -> the icon at the HD frame size; a classic frame not the sheet's size (PD2's 10 px
    placeholder) is returned upscaled as it is.
    Needs numpy and Pillow, only while new art is made: the merge reads the finished icons from merge/art."""
    import numpy as np
    from PIL import Image as Pil, ImageFilter

    def arr(img):
        return np.frombuffer(img[2], np.uint8).reshape(img[1], img[0], 4).astype(float)

    def lum(a):
        return a[..., :3] @ [0.299, 0.587, 0.114]

    def soft(a, radius: float):
        im = Pil.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8))
        return np.asarray(im.filter(ImageFilter.GaussianBlur(radius))) / 255

    def rings(glyph, n: int):
        """Rings of pixels 1..n out from the glyph's edge, then 1..n in (square steps)."""
        out, inner = [], []
        for step, rows in ((ImageFilter.MaxFilter, out), (ImageFilter.MinFilter, inner)):
            prev, im = glyph, Pil.fromarray(glyph.astype(np.uint8) * 255)
            for _ in range(n):
                im = im.filter(step(3))
                cur = np.asarray(im) > 127
                rows.append(cur ^ prev)
                prev = cur
        return out, inner, prev  # prev: the glyph n px in

    def band(shape):
        """The band just inside an icon's frame, which glyphs leave alone, and the area inside the frame."""
        h, w = shape
        lo, hi = round(w * 0.09), round(w * 0.16)
        inside = np.zeros((h, w), bool)
        inside[lo:-lo, lo:-lo] = True
        ring = inside.copy()
        ring[hi:-hi, hi:-hi] = False
        return ring, inside

    def median_of(st, leave_out):
        """Each pixel's median over the frames, leaving out the marked pixels; a pixel all of them mark takes its
        neighbours' value."""
        st = st.copy()
        st[leave_out] = np.nan
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", RuntimeWarning)  # a pixel every frame marks: filled below
            out = np.nanmedian(st, 0)
        hole = np.isnan(out[..., 0])
        while hole.any():
            known = (~hole).astype(float)
            near = [(dy, dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1)]
            total = sum(np.roll(np.where(hole[..., None], 0, out), d, (0, 1)) for d in near)
            count = sum(np.roll(known, d, (0, 1)) for d in near)
            fill = hole & (count > 0)
            out[fill] = total[fill] / count[fill][:, None]
            hole &= ~fill
        return out

    def plate(frames):
        """D2R's bare plate and frame: each pixel's median over the frames whose glyph doesn't cover it."""
        st = np.stack([arr(f) for f in frames])
        return median_of(st, np.abs(st[..., :3] - np.percentile(st, 25, axis=0)[None, ..., :3]).sum(-1) > 60)

    depth, fine = 5, 4  # rings 5 px out and in from the glyph's edge; the glyph found at 4x
    hd_plate, classic_size = plate(plate_frames or hd_frames), classic_frames[0][:2]
    size = hd_plate.shape[1], hd_plate.shape[0]
    inside, metal = [[] for _ in range(depth + 1)], []
    for f in hd_frames:
        a = arr(f)
        glyph = lum(a) - lum(hd_plate) > 100  # the glyph's solid metal: its edge pixels fall in ring 1
        _, inner, core = rings(glyph, depth)
        for d in range(depth):
            inside[d].append(a[..., :3][inner[d]])
        inside[depth].append(a[..., :3][core])
        metal.append(np.where(glyph[..., None], a[..., :3], np.nan))
    silver = [np.median(np.concatenate(r), 0) if sum(map(len, r)) else np.full(3, 180.0) for r in inside]
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", RuntimeWarning)  # a pixel no D2R glyph covers: plain metal
        grain = np.nanmedian(np.stack(metal), 0)
    grain = np.nan_to_num(lum(grain) / np.nanmean(lum(grain)), nan=1.0)
    # D2R's brushed metal: only its fine texture (the grain over its own blur), not the patches where few glyphs reach
    grain = 1 + 0.8 * (soft(grain / 2, 0.6) / np.maximum(soft(grain / 2, 4), 0.05) - 1)
    big = size[0] * fine, size[1] * fine

    def coverage(m):
        """A 4x mask -> its share of each HD pixel."""
        return np.asarray(Pil.fromarray(m.astype(np.uint8) * 255).resize(size, Pil.BOX)) / 255

    def finish(classic, up):
        _, _, images = modkit()
        if classic[:2] != classic_size:
            return images.scale(up, round(classic[0] * size[0] / classic_size[0]),
                                round(classic[1] * size[1] / classic_size[1]))
        a = np.asarray(Pil.frombytes("RGBA", up[:2], up[2]).resize(big, Pil.BICUBIC)).astype(float)
        # The plate's brightness, from the band just inside the icon's frame, which classic glyphs leave alone (a
        # median over the sheet is no good there: in the middle nearly every icon has its glyph)
        edge, inside = band(big[::-1])
        # PD2's glyphs hold greys (folds, shading): all of it brighter than the plate is metal, and the specks and
        # pinholes a threshold leaves in a grey are cleaned away, else the plate shows through in fragments; the plate's
        # light specks next to a glyph are tan, the glyph grey
        grey = a[..., :3].max(-1) - a[..., :3].min(-1) < CHROMA * lum(a)
        glyph = Pil.fromarray(((lum(a) > np.median(lum(a)[edge]) + GLYPH) & grey & inside).astype(np.uint8) * 255)
        for step in (ImageFilter.MinFilter, ImageFilter.MaxFilter, ImageFilter.MaxFilter, ImageFilter.MinFilter):
            glyph = glyph.filter(step(CLEAN))
        glyph = soft(np.asarray(glyph) / 255, 3) > 0.5  # smooth the outline before ringing it
        cover = coverage(glyph)  # the glyph's share of each HD pixel
        solid = cover > 0.5
        out, inner, core = rings(solid, depth)  # D2R's rings are whole pixels: drawn at 1x
        small = np.asarray(Pil.frombytes("RGBA", up[:2], up[2]).resize(size, Pil.LANCZOS)).astype(float)
        detail = soft(lum(small) / 255, 2.0)  # PD2's shading inside the glyph, softened
        top = np.percentile(detail[solid], 90) if solid.any() else 1
        floor = np.median(lum(a)[edge]) / 255
        tone = np.clip((detail - floor) / max(top - floor, 0.05), 0, 1)
        shade = (1 - SHADING + SHADING * tone) * np.linspace(1.04, 0.96, size[1])[:, None] * grain
        shade = 0.95 * shade / shade[solid].mean() if solid.any() else shade  # D2R's median silver
        height = soft(cover, 0.5)
        lit = np.roll(height, (-1, -1), (0, 1)) - np.roll(height, (1, 1), (0, 1))  # edges facing the top-left light
        shade = (shade * np.clip(1 + BEVEL * lit, 0.8, 1.2))[..., None]
        metal = np.broadcast_to(silver[0], (*solid.shape, 3)).copy()
        for d in range(depth):
            metal[inner[d]] = silver[d]
        metal[core] = silver[depth]
        img = hd_plate[..., :3].copy()
        for d, f in enumerate(EDGE):
            img[out[d]] *= f
        img[solid] *= EDGE[0]  # under the glyph's edge pixels: the dark line it fades into
        img = img * (1 - cover[..., None]) + metal * shade * cover[..., None]
        rgba = np.concatenate([img, hd_plate[..., 3:]], -1)
        return size[0], size[1], np.clip(rgba, 0, 255).astype(np.uint8).tobytes()
    return finish


def darker_copy(frame, previous) -> bool:
    """A pressed frame: previous darkened (same shape, its brightness pattern), as classic sheets pair them."""
    import numpy as np
    if frame[:2] != previous[:2]:
        return False
    a, b = (np.frombuffer(f[2], np.uint8).reshape(-1, 4)[:, :3].astype(float).sum(1) for f in (frame, previous))
    return a.sum() < b.sum() and np.corrcoef(a, b)[0, 1] > 0.9
