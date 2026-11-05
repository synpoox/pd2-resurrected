"""Skill icon sheets from PD2's art: skilldesc IconCel picks a frame of the skill's sheet, and PD2 points its new
skills past D2R's HD strips (blank icons in the skill tree).

merge/skill-icons.txt lists the classes (charclass codes) whose sheet PD2 ships in pd2assets.mpq (pd2_extract pulls
data/global/ui/spells/<xx>skillicon.dc6); the shared sheet of class-less skills, item charges and oskills
(skillicon.dc6, HD strip spells/submenu/skillicon) is rebuilt whenever PD2 ships it. For each, the classic DC6 is PD2's
file as it is, and the HD strip (with its .lowend twin) has a frame per DC6 frame: D2R's own HD frame where PD2's
classic frame is D2R's or PD2 has none, else PD2's frame upscaled and redrawn in D2R's style (pd2_art.icon_style;
normal and pressed frames alike, PD2's DC6 holds both) at the HD strip's size per classic pixel; the .lowend twin
likewise from D2R's .lowend frames, at its own stride (frame + 2, not half the HD's). Trailing vanilla frames past
D2R's strip are left off (the barbarian's DC6 has 156, D2R's strip 60). A listed class with no PD2 sheet, or an
extracted sheet whose class is not listed, fails the merge.
"""
from __future__ import annotations

from pathlib import Path

import pd2_art
from pd2_art import modkit

class SkillIconError(ValueError):
    pass


def read_lines(path: Path) -> list[str]:
    return [line.strip() for line in path.read_text("utf-8").splitlines() if line.strip() and not line.startswith("#")]


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/skill-icons.txt."""
    dc6, hd, images = modkit()
    art_dir = rule.parent / "art"
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
    sheets = [(c, *paths[c]) for c in classes]
    generic = Path("global") / "ui" / "spells" / "skillicon.dc6"
    if (pd2_data / generic).is_file():
        sheets.append(("shared", Path("hd") / "global" / "ui" / "spells" / "submenu" / "skillicon.sprite", generic))
    for c, sprite, classic in sheets:
        if not (pd2_data / classic).is_file():
            raise SkillIconError(f"{c}: no {classic.as_posix()} in pd2-data: run tools/pd2_extract.py")
        pd2, d2r = dc6.read(pd2_data / classic), dc6.read(d2r_data / classic)
        n_hd = images.read_sprite(d2r_data / sprite)[2]
        n = len(pd2)
        while n > n_hd and n <= len(d2r) and pd2[n - 1] == d2r[n - 1]:
            n -= 1
        n = max(n, n_hd)  # D2R's HD frames past PD2's sheet stay
        keep = [k >= len(pd2) or k < n_hd and k < len(d2r) and pd2[k] == d2r[k] for k in range(n)]
        styles = {}

        def finish(name, frame, up):
            k = int(name.rsplit("/", 1)[1])  # PD2's shared sheet stops pairing normal and pressed frames at 24
            kind = int(k % 2 == 1 and pd2_art.darker_copy(frame, dc6.decode(pd2[k - 1], palette)))
            if kind not in styles:
                strip, fw, _ = images.read_sprite(d2r_data / sprite)
                stride = strip[0] // n_hd
                styles[kind] = pd2_art.icon_style(
                    [images.crop(strip, j * stride, 0, fw, strip[1]) for j in range(kind, n_hd, 2)],
                    [dc6.decode(d2r[j], palette) for j in range(kind, min(len(d2r), n_hd), 2)])
            return styles[kind](frame, up)
        art = pd2_art.upscale(art_dir, {f"spells/{classic.stem}/{k}": dc6.decode(pd2[k], palette)
                                        for k in range(n) if not keep[k]}, finish)
        for sheet in (sprite, sprite.with_name(sprite.name.replace(".sprite", ".lowend.sprite"))):
            strip, fw, _ = images.read_sprite(d2r_data / sheet)
            stride, per_px = strip[0] // n_hd, fw / d2r[0][0]  # HD pixels per classic pixel
            if n * stride > hd.MAX_STRIP:
                raise SkillIconError(f"{c}: {n} frames is past D2R's {hd.MAX_STRIP} px strip")
            frames = [images.crop(strip, k * stride, 0, stride, strip[1]) if keep[k] else
                      images.scale(art[f"spells/{classic.stem}/{k}"], round(pd2[k][0] * per_px),
                                   round(pd2[k][1] * per_px)) for k in range(n)]
            images.write_sprite(out_data / sheet, images.hstack(frames, stride, strip[1]), fw, n, lowend=False)
        (out_data / classic).parent.mkdir(parents=True, exist_ok=True)
        (out_data / classic).write_bytes((pd2_data / classic).read_bytes())
        print(f"  skill-icons: {c} {n} HD frames ({keep.count(False)} from PD2)")
