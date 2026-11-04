"""skilldesc's description lines (#73), applied by pd2_tables after the merge.

A line is a function number plus text a/b and calc a/b (descline1 + desctexta1, ...). PD2's rows keep classic
functions that print vanilla's text and then the value ("Duration: " 40 " seconds"); D2R's strings carry the value's
format code instead ("Duration: %d seconds"), so the merged line printed both. A line clashes when a text names a D2R
string with a format code in a column, under a function, that D2R's own lines never use it in; or when it is PD2's
function 1 (the built-in Mana Cost line, which D2R dropped: it prints D2R's "an evil force"). Each clashing line is
rewritten onto D2R's functions by LINES, the owner-approved mapping on #73; strings stay D2R's. A clash left after
the rewrite fails the merge.
"""
from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

from pd2_ids import Table

LINE = re.compile(r"^(desc|dsc2|dsc3)line(\d+)$")
# PD2 function -> D2R function, and how text a/b and calc a/b are rebuilt (key: the line's D2R string with a format
# code, after KEYS). "d2r": text a/b as a D2R line of that function lays the key out; "swap": PD2's text b, then
# text a (synergy: format, then skill name); "b": PD2's text b alone; "copy": the skill's own D2R line naming the key,
# calcs included
LINES = {
    **{f: ("74", "d2r", lambda a, b: (a, "")) for f in ("2", "3", "4", "5", "7", "51", "66")},
    "73": ("74", "d2r", lambda a, b: (f"({a})*100/({b})", "")),
    **{f: ("75", "d2r", lambda a, b: (a, b)) for f in ("38", "35", "43", "47", "52", "59")},
    "62": ("75", "b", lambda a, b: (a, b)),
    "63": ("76", "swap", lambda a, b: (a, "")), "67": ("76", "swap", lambda a, b: (a, "")),
    "12": ("36", "d2r", lambda a, b: (a, "25")),  # frames -> seconds
    "19": ("36", "d2r", lambda a, b: (f"({a})*2", "3")),  # PD2 prints a*2/3 yards
    "37": ("36", "d2r", lambda a, b: (a, "3")),
    **{f: (None, "copy", None) for f in ("23", "24", "30", "31")},  # no calc of their own (missile values)
}
# whole lines, by PD2 function and text a
FIXED = {("1", ""): ("36", "StrSkill3", "", "usmc", "256"),
         ("5", "StrSkill3"): ("36", "StrSkill3", "", "usmc*2", "256"),  # PD2: usmc/128
         ("61", "StrSkill3"): ("36", "EskillPerBlade", "", "usmc", "256"),
         ("70", "StrSkeleAll"): ("74", "StrSkill96", "StrSkill42", None, ""),  # None: PD2's calc a
         ("70", "StrSkeleAllPhys"): ("74", "StrSkillSleketonMasterySkeletonDamage", "", None, "")}
# D2R reused these keys with a range ("Damage: %d-%d") or another suffix; by PD2 function and text a
KEYS = {"2": {"StrSkill4": "StrSkillDamagePercent", "StrSkill5": "StrSkillFireDamagePercent",
              "StrSkill6": "StrSkillColdDamagePercent", "StrSkill7": "StrSkillLightningDamagePercent",
              "StrSkill39": "StrSkillMagicDamagePercent", "StrSkill8": "StrSkillPoisonDamagePercent"},
        "3": {"StrSkill3": "StrSkillManaCostPerSecond", "OpenWoundsDamage": "OpenWoundsDamagePerSecond",
              "PartyOpenWoundsDamage": "PartyOpenWoundsDamagePerSecond",
              "ThornsOpenWoundsDamage": "ThornsOpenWoundsDamagePerSecond"},
        "4": {"StrSkill4": "StrSkillDamageFlat", "StrSkill50": "StrSkillHealFlat"},
        "52": {"StrSkill89": "StrSkillRunWalkRange", "StrSkill106": "StrSkillAttackSpeedRange",
               "EskillLifeSteal": "StrSkillLifeStealRange", "StrSkill4": "StrSkillDamagePercentRange"}}


def formatted(d2r_data: Path) -> set[str]:
    """D2R's string keys whose English text has a format code."""
    keys = set()
    for f in sorted((d2r_data / "local" / "lng" / "strings").glob("*.json")):
        keys |= {e["Key"] for e in json.loads(f.read_text("utf-8-sig")) if "%" in e.get("enUS", "")}
    return keys


def lines(t: Table) -> list[tuple[int, int, list[int]]]:
    """(row, line column, [text a, text b, calc a, calc b] columns) for every non-empty line."""
    col = {c: j for j, c in enumerate(t.header)}
    out = []
    for j, c in enumerate(t.header):
        if m := LINE.match(c):
            p, n = m.groups()
            parts = [col[f"{p}{s}{n}"] for s in ("texta", "textb", "calca", "calcb")]
            out += [(i, j, parts) for i, r in enumerate(t.rows) if r[j]]
    return out


def layouts(d2r: Table, fmt: set[str]) -> Counter:
    """(key, function, column 0/1) -> how often D2R's lines use it, and (function, key) -> D2R's text a/b for it."""
    seen: Counter = Counter()
    for i, j, (a, b, _, _) in lines(d2r):
        r = d2r.rows[i]
        for k, col in ((r[a], 0), (r[b], 1)):
            if k in fmt:
                seen[(k, r[j], col)] += 1
                seen[(r[j], k, r[a], r[b])] += 1
    return seen


def clashes(t: Table, d2r: Table, fmt: set[str]) -> list[str]:
    """The lines D2R's game would print wrongly: "skill column function text a text b"."""
    seen = layouts(d2r, fmt)
    used = {k[0] for k in seen if len(k) == 3}
    out = []
    for i, j, (a, b, _, _) in lines(t):
        r = t.rows[i]
        if (r[j] == "1" and not r[a]) or any(k in used and (k, r[j], col) not in seen
                                              for k, col in ((r[a], 0), (r[b], 1))):
            out.append(f"{r[0]} {t.header[j]} {r[j]} {r[a]} {r[b]}")
    return out


def rewrite(t: Table, d2r: Table, fmt: set[str]) -> None:
    """Rewrites t's clashing lines in place by LINES; raises ValueError naming lines LINES can't place."""
    seen = layouts(d2r, fmt)
    used = {k[0] for k in seen if len(k) == 3}
    bad = set(clashes(t, d2r, fmt))
    d2r_lines = {}
    for i, j, parts in lines(d2r):
        d2r_lines.setdefault(d2r.rows[i][0].lower(), []).append([d2r.rows[i][j]] + [d2r.rows[i][p] for p in parts])
    missing = []
    for i, j, parts in lines(t):
        r = t.rows[i]
        f, a, b, ca, cb = r[j], *(r[p] for p in parts)
        if f"{r[0]} {t.header[j]} {f} {a} {b}" not in bad:
            continue
        key = KEYS.get(f, {}).get(a) or next((k for k in (a, b) if k in used), a)
        new = None
        if (f, a) in FIXED:
            new = list(FIXED[(f, a)])
            new[3] = ca if new[3] is None else new[3]
        elif f in LINES:
            to, how, calc = LINES[f]
            if how == "copy":  # a PD2-only skill takes D2R's most common line for the key (lesser hydra: hydra's)
                common = Counter(tuple(x) for ls in d2r_lines.values() for x in ls if key in x[1:3])
                new = next((x for x in d2r_lines.get(r[0].lower(), []) if key in x[1:3]),
                           common and list(common.most_common(1)[0][0]))
            elif how == "swap":
                new = [to, b, a, *calc(ca, cb)]
            elif how == "b":
                new = [to, b, "", *calc(ca, cb)]
            else:
                # D2R's most used text a/b for the key under this function; a key only the mod has stands alone
                ab = max(((k[2], k[3]) for k in seen if len(k) == 4 and k[:2] == (to, key)),
                         key=lambda x: seen[(to, key, *x)], default=None if key in used else (key, ""))
                new = ab and [to, *ab, *calc(ca, cb)]
        if new is None:
            missing.append(f"{r[0]} {t.header[j]} {f} {a} {b}")
            continue
        r[j], *vals = new
        for p, v in zip(parts, vals):
            r[p] = v
    if missing:
        raise ValueError(f"skilldesc lines with no D2R layout (pd2_skilldesc.LINES): {'; '.join(missing)}")
