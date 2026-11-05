"""PD2's own automap cels (#274): PD2's MaxiMap.dc6 has frames past D2R's automap tiles (cels 1499 and up, drawn by
its new level types). D2R's HD tiles are strips of 100 frames, AUTOMAP/Default00-14/Tiles in automaphd.json; the
last one has 99. The step fills the last strip and adds strips after it, each frame PD2's classic one upscaled, lists
the new strips in automaphd.json, and ships PD2's MaxiMap.dc6 and MaxiMapS.dc6 as they are for classic graphics, whose
layout (automap.json) loads those DC6 as its tiles: with D2R's 1499 frames, the limit below would stay 1499 there.

The tiles' frame count is also the map memory's limit: D2R's automap loader (0xd4570) sums the strips' frames
(every strip but the last must have the first's count), and the map memory loader (0xd5710) rejects a stored cel at
or past that sum (the next save then loops for ever). More frames raise both; no engine patch is needed.
"""
from __future__ import annotations

import re
from pathlib import Path

import pd2_strings

LAYOUT = Path("global") / "ui" / "layouts" / "automaphd.json"
CLASSIC = Path("global") / "ui" / "automap"  # PD2's maximap.dc6 (HD art source) and maximaps.dc6
HD = Path("hd") / "global" / "ui" / "automap"
TILE = 16  # a classic automap cel's width; D2R's HD frame is 4x (64), its lowend 2x (32)
# shortcut: edits the layout as text, one strip per line as D2R writes it; parse it if D2R reformats the file
STRIP = re.compile(r'^([ \t]*)"AUTOMAP/Default(\d\d)/Tiles",', re.M)


def modkit():
    pd2_strings.modkit()
    from modkit import dc6, images
    return dc6, images


def sheets(n: int) -> list[Path]:
    d = HD / f"default{n:02d}"
    return [d / "tiles.sprite", d / "tiles.lowend.sprite"]


def apply(pd2_data: Path, d2r_data: Path, out_data: Path) -> int:
    """Writes the tiles past D2R's, the layout and PD2's classic DC6; returns the cel count (the cels drawn)."""
    dc6, images = modkit()
    for name in ("maximap.dc6", "maximaps.dc6"):
        if not (pd2_data / CLASSIC / name).is_file():
            raise ValueError(f"no {(CLASSIC / name).as_posix()} in pd2-data: run tools/pd2_extract.py")
        (out_data / CLASSIC).mkdir(parents=True, exist_ok=True)
        (out_data / CLASSIC / name).write_bytes((pd2_data / CLASSIC / name).read_bytes())
    pd2 = dc6.read(pd2_data / CLASSIC / "maximap.dc6")
    layout = (d2r_data / LAYOUT).read_bytes().decode("utf-8-sig")  # not read_text: it keeps D2R's CRLF
    listed = [int(m.group(2)) for m in STRIP.finditer(layout)]
    if not listed or listed != list(range(len(listed))):
        raise ValueError(f"{LAYOUT.as_posix()}: tilesDefaultFilename is not Default00..NN in order")
    per = images.read_sprite(d2r_data / sheets(0)[0])[2]
    last = listed[-1]
    d2r_cels = last * per + images.read_sprite(d2r_data / sheets(last)[0])[2]
    if len(pd2) <= d2r_cels:
        return d2r_cels
    palette = dc6.palette((d2r_data / "global" / "palette" / "units" / "pal.dat").read_bytes())
    art = {c: (pd2[c], dc6.decode(pd2[c], palette)) for c in range(d2r_cels, len(pd2))}
    strips = -(-len(pd2) // per)
    for quality in (0, 1):  # sheets(n)[0] is the HD strip, [1] its lowend twin
        geometry, fw, _ = images.read_sprite(d2r_data / sheets(0)[quality])
        stride, h = geometry[0] // per, geometry[1]
        k = fw // TILE
        d2r_last = images.read_sprite(d2r_data / sheets(last)[quality])[0]
        for n in range(last, strips):
            frames = []
            for c in range(n * per, min(len(pd2), (n + 1) * per)):
                if c < d2r_cels:
                    frames.append(images.crop(d2r_last, (c - n * per) * stride, 0, stride, h))
                    continue
                (w, fh, ox, oy, _), img = art[c]
                big = images.scale(img, w * k, fh * k)
                # a DC6 frame's bottom-left sits at its offset; shortcut: art past the cell is clipped (6 PD2 cels are
                # bigger than 16x32), upgrade if a level shows them cut
                frames.append(images.paste(images.blank(stride, h), big, ox * k, h - fh * k + oy * k))
            images.write_sprite(out_data / sheets(n)[quality], images.hstack(frames, stride, h), fw, len(frames),
                                lowend=False)
    m = list(STRIP.finditer(layout))[-1]
    eol = "\r\n" if "\r\n" in layout else "\n"
    added = "".join(f'{eol}{m.group(1)}"AUTOMAP/Default{n:02d}/Tiles",' for n in range(last + 1, strips))
    (out_data / LAYOUT).parent.mkdir(parents=True, exist_ok=True)
    (out_data / LAYOUT).write_bytes((layout[:m.end()] + added + layout[m.end():]).encode("utf-8"))
    print(f"  automap: cels {d2r_cels}-{len(pd2) - 1} from PD2's MaxiMap.dc6, strips {last:02d}-{strips - 1:02d}")
    return len(pd2)
