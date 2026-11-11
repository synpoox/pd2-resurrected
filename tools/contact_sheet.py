"""A HD/legacy contact sheet and image metrics from pd2_edge_tour pairs (#840, from the research/hd-parity-strategy prototype).

    py -3 tools/contact_sheet.py <folder of pairs> <out.jpg> [--cols 2] [--width 1600]

Each pair PNG (pd2_edge_tour.shoot: HD left, legacy right, 1920x1080 each) becomes one row cell, labelled with its
file name and metrics. Measured on the playfield only: the bottom HUD band (HUD) is cut, and the legacy half is cut to
its 4:3 picture (D2R draws legacy pillarboxed: 1440 of 1920 px at 1080p), so black bars do not count as dark.

Metrics per pair (reliable ones only):
  luma      mean Rec.601 luma of HD and of legacy, and HD/legacy (a median sits in the dark majority of a night
            scene and says little). Lighting parity (#773/#804) wants about 0.85-1.15.
  black     share of near-black pixels (luma < 12) in each: legacy void reads black; HD drawing ground there shows as
            a lower black share (the void check's image twin).
Not measured: walkable-area overlap and edge masks. The two cameras do not frame the same world area (legacy's
view is narrower and the projection differs), so a pixel overlap needs a per-resolution world-to-screen fit first
(a harness ticket); until then those are judged by eye on the sheet.
"""
from __future__ import annotations

import argparse
import statistics
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageOps

HUD = 0.16  # bottom share of the frame that is HUD (orbs, belt, skill bar)
LEGACY_W = 1440 / 1920  # legacy's 4:3 picture inside the 16:9 frame
BLACK = 12  # a pixel below this luma is near-black


def halves(img: Image.Image) -> tuple[Image.Image, Image.Image]:
    w, h = img.size
    hd, legacy = img.crop((0, 0, w // 2, h)), img.crop((w // 2, 0, w, h))
    lw = int(legacy.width * LEGACY_W)
    x0 = (legacy.width - lw) // 2
    return hd.crop((0, 0, hd.width, int(h * (1 - HUD)))), legacy.crop((x0, 0, x0 + lw, int(h * (1 - HUD))))


def metrics(img: Image.Image) -> dict:
    out = {}
    for name, part in zip(("hd", "legacy"), halves(img)):
        g = list(ImageOps.grayscale(part).resize((part.width // 8, part.height // 8)).getdata())
        out[name] = (statistics.fmean(g), sum(v < BLACK for v in g) / len(g))
    out["ratio"] = out["hd"][0] / out["legacy"][0] if out["legacy"][0] else float("inf")
    return out


def sheet(folder: Path, dest: Path, cols: int = 2, width: int = 1600) -> list[tuple[str, dict]]:
    """Write the JPEG sheet of every pair PNG in `folder` to `dest`; returns (name, metrics) per pair."""
    pairs = sorted(folder.glob("*.png"))
    if not pairs:
        raise SystemExit(f"no pairs in {folder}")
    cell_w = width // cols
    rows, found = [], []
    for p in pairs:
        img = Image.open(p).convert("RGB")
        m = metrics(img)
        found.append((p.stem, m))
        t = img.resize((cell_w, int(img.height * cell_w / img.width)))
        canvas = Image.new("RGB", (cell_w, t.height + 18), (12, 12, 12))
        canvas.paste(t, (0, 18))
        ImageDraw.Draw(canvas).text((4, 3), f"{p.stem}  luma HD {m['hd'][0]:.0f} / legacy {m['legacy'][0]:.0f} "
                                             f"({m['ratio']:.2f})  black {m['hd'][1]:.0%} / {m['legacy'][1]:.0%}",
                                    fill=(230, 230, 230))
        rows.append(canvas)
    h = rows[0].height
    out = Image.new("RGB", (cell_w * cols, h * ((len(rows) + cols - 1) // cols)), (0, 0, 0))
    for i, c in enumerate(rows):
        out.paste(c, ((i % cols) * cell_w, (i // cols) * h))
    out.save(dest, quality=82)
    return found


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("folder", type=Path)
    ap.add_argument("dest", type=Path)
    ap.add_argument("--cols", type=int, default=2)
    ap.add_argument("--width", type=int, default=1600)
    a = ap.parse_args(argv)
    for name, m in sheet(a.folder, a.dest, a.cols, a.width):
        print(f"{name}\tluma {m['hd'][0]:.0f}/{m['legacy'][0]:.0f} = {m['ratio']:.2f}\tblack {m['hd'][1]:.0%}/{m['legacy'][1]:.0%}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
