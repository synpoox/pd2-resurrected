"""core-maps game checks for d2rAI-toolkit's testing/ framework (game_checks.py).

uber-scaling (#347, docs/research/pd2-uber-bosses-dll.md section 4) reads <slot>/d2rloader/logs/core-maps.log.
Setup: tools/install.ps1 on the merged data, a level 90+ character in Hell with every quest done. In game:
`debug_mode`, `cheat spawn dcma,inv 0` (a tier-0 Diablo Clone key; `cheat stat` cannot set an item's tier), stand on
the Blood Fountain in Harrogath and use the key; in level 138 spawn fight monsters, e.g. `cheat mspawn uberdiablonew`
and `cheat mspawn dcloneskele,3`, and a monster outside the fight set, e.g. `cheat mspawn zombie1`. Then:
  py testing/game_checks.py run core-maps-ub2 --slot N --path <repo>/tests/game --module core_maps_checks
"""
import os
import re

from game_checks import check

PLUGIN = "core-maps"
BOSSES = {806, 950, 951, 952, 953, 1129}
FIGHT = BOSSES | set(range(807, 812)) | set(range(903, 909)) | set(range(954, 957)) | {983, 985, 986, 1123, 1124,
                                                                                      1130, 1131, 1132, 1157, 1158}
SCALED = re.compile(r"uber scaling: monster (\d+) \(unit (\d+)\) in level (\d+), tier (\d+), entries (\d+)(?: \(boss\))?: "
                    r"maxhp (\d+) -> (\d+), life (\d+) -> (\d+), skills \+(\d+), absorbs (-?\d+)/(-?\d+), dr (-?\d+), "
                    r"plr (-?\d+)")


def _log(ctx) -> str:
    path = os.path.join(ctx.cfg.slot_dir(ctx.slot), "d2rloader", "logs", f"{PLUGIN}.log")
    if not os.path.isfile(path):
        ctx.skip(f"no plugin log yet: {path}")
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    starts = [m.start() for m in re.finditer(rf"{PLUGIN}: \S+ loaded \(", text)]
    return text[starts[-1]:] if starts else ""


def expected_maxhp(maxhp: int, tier: int, boss: bool) -> int:
    """PD2's 0x10381160 in whole life points (the log's), so within 3 of the 1/256 result."""
    p = maxhp * 3 // 2 if tier < 1 else maxhp * (4 if boss else 3)
    return p + tier * (p * 20 // 100)


@check("uber-scaling", tags=["core-maps-ub2"])
def uber_scaling(ctx):
    """Each scaled fight monster has PD2's maxhp and +skills for its tier, a boss its absorbs, none scaled twice."""
    rows = [tuple(int(v) for v in m.groups()) for m in SCALED.finditer(_log(ctx))]
    if not rows:
        ctx.skip("no 'uber scaling:' line yet (open an uber area and spawn a fight monster)")
    units = [r[1] for r in rows]
    assert len(units) == len(set(units)), f"a unit was scaled twice: {sorted(units)}"
    for cls, unit, level, tier, entries, old, new, _, _, skills, absorb, magic, dr, plr in rows:
        boss = cls in BOSSES
        assert cls in FIGHT, f"monster {cls} is not an uber fight monster but was scaled"
        assert abs(new - expected_maxhp(old, tier, boss)) <= 3, f"monster {cls}: maxhp {old} -> {new} at tier {tier}"
        assert skills >= 4 * tier, f"monster {cls}: skills +{skills} at tier {tier}"
        e = max(entries - 1, 0)
        if boss:
            assert absorb >= 7 * tier + 2 * e and magic >= 3 * tier + 2 * e, f"boss {cls}: absorbs {absorb}/{magic}"
    return f"{len(rows)} monsters scaled: " + ", ".join(f"{r[0]} t{r[3]} {r[5]}->{r[6]}" for r in rows[:6])
