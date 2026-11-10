"""Lint merge/presets-hd.tsv's stand-in lines (#786).

A stand-in line renames only the PD2 tiles its D2R DT1 also holds, with the same (orientation, main, sub) and subtile
flags (apply.stand_in); the merge builds the preset whatever the line does. So this checks each line:
- its share of matching tiles is a quarter or more of the PD2 DT1's, or the line carries a reason: a third tab column
  `# reason`;
- it renames at least one tile (a line that renames none is `-`);
- both DT1 files exist (Tiles raises on a missing one);
and reports the tiles each line renames in every map that loads it.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from pd2_levels import norm

from .apply import stand_in
from .dt1 import Tiles

QUARTER = 0.25


def reasons(rule: Path) -> dict[str, str]:
    """PD2 DT1 -> the `# reason` third column of its stand-in line (lines without one are left out)."""
    out = {}
    for line in rule.read_text("utf-8").splitlines():
        cols = line.split("\t")
        if line and not line.startswith("#") and cols[0].lower().endswith(".dt1") and len(cols) > 2 \
                and cols[2].startswith("#") and cols[2][1:].strip():
            out[norm(cols[0])] = cols[2][1:].strip()
    return out


@dataclass
class Line:
    pd2: str
    standin: str
    own: int = 0  # tiles in the PD2 DT1
    matched: int = 0  # of them the stand-in holds with the same flags
    copied: int = 0  # of them pixel copies of a D2R tile (named by that tile whatever the line says)
    reason: str = ""
    problems: list[str] = field(default_factory=list)
    maps: dict[str, int] = field(default_factory=dict)  # DS1 -> tiles the line renames in it

    @property
    def share(self) -> float:
        return self.matched / self.own if self.own else 0.0


def check_line(pd2: str, standin: str, tiles: Tiles, pd2_tiles: Tiles, reason: str = "",
               copied: set | dict = frozenset()) -> Line:
    """One stand-in line (`standin` not `-`): its tile counts and what is wrong with it."""
    line = Line(pd2, standin, reason=reason)
    try:
        own, theirs = pd2_tiles[pd2], tiles[standin]
    except FileNotFoundError as e:
        line.problems.append(f"{e}")
        return line
    line.own = len(own)
    line.matched = sum(theirs.get(t) == v for t, v in own.items())
    line.copied = sum((pd2, *t) in copied for t in own)
    if not line.matched and not line.copied:
        line.problems.append("renames no tile: write `-`")
    elif line.share < QUARTER and not reason:
        line.problems.append(f"matches {line.matched} of {line.own} tiles ({line.share:.0%}), under a quarter: "
                             f"pick a better stand-in or add a `# reason` third column")
    return line


def renamed(lines: list[Line], standins: dict[str, str | None], tiles: Tiles, pd2_tiles: Tiles, copied,
            presets: list[tuple[str, str, list[str], list[str]]], layout_of) -> None:
    """Fill each line's `maps`: for every DS1 a preset loading its PD2 DT1 draws, the tiles whose key the line
    changes. `layout_of(ds1, dt1s, copies)` builds layouts built from `stand_in`'s output (a callable, so tests need no DS1)."""
    for line in lines:
        mine = {k: v for k, v in copied.items() if k[0] == line.pd2}  # this line's copies only
        for _, _, files, ds1s in presets:
            if line.pd2 not in files:
                continue
            none = {f: None for f in files if f.startswith("pd2assets/")}  # every PD2 DT1 on its own tiles
            for ds1 in ds1s:
                before = layout_of(ds1, stand_in(files, tiles, pd2_tiles, none), {})
                after = layout_of(ds1, stand_in(files, tiles, pd2_tiles, {**none, line.pd2: line.standin}, mine), mine)
                line.maps[ds1] = sum(after.keys.get(c) != k for c, k in before.keys.items())


def lint(standins: dict[str, str | None], why: dict[str, str], tiles: Tiles, pd2_tiles: Tiles,
         copied=frozenset()) -> list[Line]:
    return [check_line(p, s, tiles, pd2_tiles, why.get(p, ""), copied) for p, s in sorted(standins.items()) if s]


def report(lines: list[Line]) -> str:
    out = []
    for ln in lines:
        out.append(f"{ln.pd2} -> {ln.standin}: {ln.matched}/{ln.own} tiles match ({ln.share:.0%}), {ln.copied} copied"
                   + (f"  [reason: {ln.reason}]" if ln.reason else ""))
        out += [f"    {ds1}: {n} tiles renamed" for ds1, n in sorted(ln.maps.items())]
        out += [f"    PROBLEM: {p}" for p in ln.problems]
    return "\n".join(out)
