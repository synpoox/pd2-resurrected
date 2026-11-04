"""Class skill icon sheets from PD2's art: skilldesc IconCel picks a frame of the class's sheet, and PD2 points its new
skills past D2R's 60-frame HD strips (blank icons in the skill tree).

merge/skill-icons.txt lists the classes (charclass codes) whose sheet PD2 ships in pd2assets.mpq (pd2_extract pulls
data/global/ui/spells/<xx>skillicon.dc6). For each, the classic DC6 is PD2's file as it is, and the HD strip
(data/hd/global/ui/spells/<class>/<xx>skillicon.sprite, with its .lowend twin) has a frame per DC6 frame: D2R's own HD
frame where PD2's classic frame is D2R's, else PD2's frame upscaled (normal and pressed frames alike, PD2's DC6 holds
both). Trailing vanilla frames past D2R's strip are left off (the barbarian's DC6 has 156, D2R's strip 60). A listed
class with no PD2 sheet, or an extracted sheet whose class is not listed, fails the merge.
"""
from __future__ import annotations

from pathlib import Path

import pd2_strings


class SkillIconError(ValueError):
    pass


def modkit():
    """modkit's dc6, hd and images modules (d2rAI-toolkit found as pd2_strings finds it)."""
    pd2_strings.modkit()
    from modkit import dc6, hd, images
    return dc6, hd, images


def read_lines(path: Path) -> list[str]:
    return [line.strip() for line in path.read_text("utf-8").splitlines() if line.strip() and not line.startswith("#")]


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/skill-icons.txt."""
    dc6, hd, images = modkit()
    classes = read_lines(rule)
    unknown = [c for c in classes if c not in hd.CLASSES]
    if unknown:
        raise SkillIconError(f"not a class: {', '.join(unknown)}")
    paths = {c: [Path(p).relative_to("data") for p in hd.icon_paths(c)] for c in classes}
    listed = {classic.name for _, classic in paths.values()}
    extra = sorted(p.name for p in (pd2_data / "global" / "ui" / "spells").glob("??skillicon.dc6") if p.name not in listed)
    if extra:
        raise SkillIconError(f"PD2 sheets not listed: {', '.join(extra)}")
    palette = dc6.palette((d2r_data / "global" / "palette" / "units" / "pal.dat").read_bytes())
    for c in classes:
        sprite, classic = paths[c]
        if not (pd2_data / classic).is_file():
            raise SkillIconError(f"{c}: no {classic.as_posix()} in pd2-data: run tools/pd2_extract.py")
        pd2, d2r = dc6.read(pd2_data / classic), dc6.read(d2r_data / classic)
        strip, fw, n_hd = images.read_sprite(d2r_data / sprite)
        stride = strip[0] // n_hd
        n = len(pd2)
        while n > n_hd and n <= len(d2r) and pd2[n - 1] == d2r[n - 1]:
            n -= 1
        if n * stride > hd.MAX_STRIP:
            raise SkillIconError(f"{c}: {n} frames is past D2R's {hd.MAX_STRIP} px strip")
        keep = [k < n_hd and k < len(d2r) and pd2[k] == d2r[k] for k in range(n)]
        frames = [images.crop(strip, k * stride, 0, stride, strip[1]) if keep[k]
                  else images.scale(dc6.decode(pd2[k], palette), fw, strip[1]) for k in range(n)]
        images.write_sprite(out_data / sprite, images.hstack(frames, stride, strip[1]), fw, n)
        (out_data / classic).parent.mkdir(parents=True, exist_ok=True)
        (out_data / classic).write_bytes((pd2_data / classic).read_bytes())
        print(f"  skill-icons: {c} {n} HD frames ({keep.count(False)} from PD2)")
