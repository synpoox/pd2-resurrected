"""Lint the stand-in lines (#786) in merge/presets-hd.d/ (apply.rule_files).

A stand-in line renames only the PD2 tiles its D2R DT1 also holds, with the same (orientation, main, sub) and subtile
flags (apply.stand_in), and picks the D2R home of a pixel copy (pd2_dt1_copies); the merge builds the preset whatever
the line does. So this checks each line:
- its share of tiles that stand in as a D2R tile (flag matches and pixel copies) is a quarter or more, or the line carries a reason: a third tab column `# reason`;
- its built output differs from `-`'s, renames and copies together, in some map that loads it; a line that changes
  nothing is `-`, or carries a `# reason`;
- both DT1 files exist (the lint reads them through Tiles(strict=True), which raises on a missing one);
and reports the tiles that differ from `-`'s build in every map that loads the line.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from pd2_ds1 import norm

from .apply import rule_files, stand_in
from .dt1 import Tiles

QUARTER = 0.25

# Lines on main whose output equals `-` and that nobody has given a reason (#786 review): reported, not failed, until
# the owner decides on each (the first lint run found them on main; no reason is known for any). Do not add to this list: give a new line a `# reason`, or write `-`.
KNOWN_NOOPS: frozenset[str] = frozenset({
    "pd2assets/a5_palm.dt1",
    "pd2assets/a5_prickly.dt1",
    "pd2assets/barflooralt.dt1",
    "pd2assets/dtprivate/rock.dt1",
    "pd2assets/dtprivate/segradient.dt1",
    "pd2assets/flip.dt1",
    "pd2assets/rotatedthronearch.dt1",
    "pd2assets/sanctuaryadds.dt1",
    "pd2assets/sanctuaryreindexed.dt1",
    "pd2assets/torturehalls/trees.dt1",
    "pd2assets/v/act4/fort/foregate.dt1",
    "pd2assets/v/expansion/town/waypt.dt1",
    "pd2assets/xxthrone.dt1",
})


def reasons(rule: Path) -> dict[str, str]:
    """PD2 DT1 -> the `# reason` third column of its stand-in line (lines without one are left out)."""
    out = {}
    for f in rule_files(rule):
        for line in f.read_text("utf-8").splitlines():
            cols = line.split("\t")
            if line and not line.startswith("#") and cols[0].lower().endswith(".dt1") and not cols[0].lower().startswith(("walls:", "water:")) and len(cols) > 2 \
                    and cols[2].startswith("#") and cols[2][1:].strip():
                out[norm(cols[0])] = cols[2][1:].strip()
    return out


@dataclass
class Line:
    pd2: str
    standin: str
    own: int = 0  # tiles in the PD2 DT1
    matched: int = 0  # of the tiles that are not copies, those the stand-in holds with the same flags
    copied: int = 0  # tiles that are pixel copies of a D2R tile (named by that tile whatever the line says)
    reason: str = ""
    missing: bool = False  # a DT1 file of the line does not exist
    problems: list[str] = field(default_factory=list)
    known: list[str] = field(default_factory=list)  # problems of a KNOWN_NOOPS line: reported, not failed
    maps: dict[str, int] = field(default_factory=dict)  # DS1 -> tiles whose key differs from the `-` build's

    @property
    def share(self) -> float:
        """The share of the PD2 DT1's tiles that stand in as a D2R tile: flag matches and pixel copies."""
        return (self.matched + self.copied) / self.own if self.own else 0.0


def check_line(pd2: str, standin: str, tiles: Tiles, pd2_tiles: Tiles, reason: str = "",
               copied: set | dict = frozenset()) -> Line:
    """One stand-in line (`standin` not `-`): its tile counts and what is wrong with it. `copied` is pd2_dt1_copies'
    copies with this line's stand-in."""
    line = Line(pd2, standin, reason=reason)
    try:
        own, theirs = pd2_tiles[pd2], tiles[standin]
    except FileNotFoundError as e:
        line.missing = True
        line.problems.append(f"{e}")
        return line
    line.own = len(own)
    line.copied = sum((pd2, *t) in copied for t in own)
    line.matched = sum(theirs.get(t) == v for t, v in own.items() if (pd2, *t) not in copied)
    if not line.matched and not line.copied:
        line.problems.append("renames no tile: write `-`")
    elif line.share < QUARTER and not reason:
        line.problems.append(f"matches {line.matched} and copies {line.copied} of {line.own} tiles ({line.share:.0%}), under a "
                             f"quarter: pick a better stand-in or add a `# reason` third column")
    return line


def renamed(lines: list[Line], tiles: Tiles, pd2_tiles: Tiles, presets: list[tuple[str, str, list[str], list[str]]],
            layout_of, copies_of) -> None:
    """Fill each line's `maps`: for every DS1 a preset loading its PD2 DT1 draws, the tiles whose key differs between
    the build with the line and the build with `-` in its place, copies applied in both (`copies_of(pd2, standin)`:
    pd2_dt1_copies' copies of that PD2 DT1 with that stand-in, None for `-`). `layout_of(ds1, dt1s, copies)` builds a
    layout (a callable, so tests need no real DS1). A line that changes no tile in any map that loads it, and has no
    `# reason`, is a `-`."""
    for line in lines:
        if line.missing:
            continue
        with_line, without = copies_of(line.pd2, line.standin), copies_of(line.pd2, None)
        for _, _, files, ds1s in presets:
            if line.pd2 not in files:
                continue
            none = {f: None for f in files if f.startswith("pd2assets/")}  # every PD2 DT1 on its own tiles
            for ds1 in ds1s:
                before = layout_of(ds1, stand_in(files, tiles, pd2_tiles, none, without), without)
                after = layout_of(ds1, stand_in(files, tiles, pd2_tiles, {**none, line.pd2: line.standin}, with_line),
                                  with_line)
                line.maps[ds1] = sum(after.keys.get(c) != k for c, k in before.keys.items())
        if line.maps and not any(line.maps.values()) and not line.reason:
            (line.known if line.pd2 in KNOWN_NOOPS else line.problems).append(
                "output equals `-` in every map that loads it: write `-` or add a `# reason` (say which D2R home "
                "its copies get)")


def lint(standins: dict[str, str | None], why: dict[str, str], tiles: Tiles, pd2_tiles: Tiles,
         copies_of=lambda pd2, standin: {}) -> list[Line]:
    return [check_line(p, s, tiles, pd2_tiles, why.get(p, ""), copies_of(p, s))
            for p, s in sorted(standins.items()) if s]


def report(lines: list[Line]) -> str:
    out = []
    for ln in lines:
        out.append(f"{ln.pd2} -> {ln.standin}: {ln.matched} flag-matched, {ln.copied} copied "
                   f"of {ln.own} tiles" + (f"  [reason: {ln.reason}]" if ln.reason else ""))
        out += [f"    {ds1}: {n} tiles differ from `-`" for ds1, n in sorted(ln.maps.items())]
        out += [f"    PROBLEM: {p}" for p in ln.problems]
        out += [f"    KNOWN (allow-listed): {p}" for p in ln.known]
    return "\n".join(out)
