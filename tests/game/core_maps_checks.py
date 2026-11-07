"""core-maps game checks for d2rAI-toolkit's testing/ framework (game_checks.py).

uber-scaling (#347, docs/research/pd2-uber-bosses-dll.md section 4) reads <slot>/d2rloader/logs/core-maps.log.
Setup: tools/install.ps1 on the merged data, a level 90+ character in Hell. In game: `debug_mode`, `cheat goto 138`
(tier 0: no key opened, so the counters read 0; for tiers 1-2, `cheat spawn dcma,inv 0`, cube the key alone once per
tier, and right-click it at the fountain, `cheat goto 109,5074,5114`),
then `cheat mspawn uberdiablonew`, `cheat mspawn dcloneskele,3` and a monster outside the fight set, `cheat mspawn
zombie1`. Then:
  py testing/game_checks.py --path <repo>/tests/game --module core_maps_checks run core-maps-ub2 --slot N
Passed on Slot5 (#347, level 95 Amazon, Hell): 14 monsters x1.5 at full life, Dclone 1053000 -> 1579500, the zombie
not scaled.

Extra boss area stat checks (MS4c, #289). Each reads <slot>/d2rloader/logs/core-maps.log.

Setup (Slot4-8, game closed): merge, build and install (docs/agents/game-slots.md)
  py -3 tools/pd2_merge.py
  powershell -File plugin/build.ps1
  powershell -File tools/install.ps1 -Game C:/D2RTestSlots/SlotN -Data . -NoValidate
In game, Hell, a level 90+ character in Harrogath: `debug_mode`, `cheat god 1`, then a Sewers of Harrogath map with the
Duplicated prefix (map_glob_extra_boss): `cheat spawn t25,typ m,pre 843,inv 0`, `cheat idall`, right-click the map,
enter the red portal and go to the Sewer Boss (SewerBoss; sewermap1-3.ds1 place it at subtile 399,311, 419,109 or
54,334 of the level). Then:
  py testing/game_checks.py run core-maps --slot N --path <repo>/tests/game --module core_maps_checks

Multi-level area stats (MS7, #221): the same setup, then a rare dungeon (`cheat spawn t42,typ r,inv 0`, `cheat idall`)
right-clicked in Harrogath, and the same run (multi-level-area).

Added group check (MS4 slot 3, #219): the same setup, then a Sewers of Harrogath map with the Ghastly prefix
(map_glob_add_mon_ghost, wraithMapMod 1128): `cheat spawn t25,typ m,pre 785,inv 0`. Clear packs in the map until about
40 have spawned (the group weighs 1 of 10), then run the same command.
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
    """PD2's 0x10381160 in whole life points (the log's): the dropped fraction of a point grows by up to the factor (5.6), so within 6."""
    p = maxhp * 3 // 2 if tier < 1 else maxhp * (4 if boss else 3)
    return p + tier * (p * 20 // 100)


@check("uber-scaling", tags=["core-maps-ub2"])
def uber_scaling(ctx):
    """Each scaled fight monster has PD2's maxhp and +skills for its tier, a boss its absorbs, none scaled twice."""
    # unit ids repeat across games, and a tier-0 re-entry remakes the minions with their rooms: one scaling per entry
    games = re.split(r"uber: level \d+ open at tier", _log(ctx))
    rows = [(g,) + tuple(int(v) for v in m.groups()) for g, text in enumerate(games) for m in SCALED.finditer(text)]
    if not rows:
        ctx.skip("no 'uber scaling:' line yet (open an uber area and spawn a fight monster)")
    units = [(r[0], r[5], r[2]) for r in rows]
    assert len(units) == len(set(units)), f"a unit was scaled twice in one entry: {sorted(units)}"
    rows = [r[1:] for r in rows]
    for cls, unit, level, tier, entries, old, new, _, _, skills, absorb, magic, dr, plr in rows:
        boss = cls in BOSSES
        assert cls in FIGHT, f"monster {cls} is not an uber fight monster but was scaled"
        assert abs(new - expected_maxhp(old, tier, boss)) <= 6, f"monster {cls}: maxhp {old} -> {new} at tier {tier}"
        assert skills >= 4 * tier, f"monster {cls}: skills +{skills} at tier {tier}"
        e = max(entries - 1, 0)
        if boss:
            assert absorb >= 7 * tier + 2 * e and magic >= 3 * tier + 2 * e, f"boss {cls}: absorbs {absorb}/{magic}"
    return f"{len(rows)} monsters scaled: " + ", ".join(f"{r[0]} t{r[3]} {r[5]}->{r[6]}" for r in rows[:6])


@check("extra-boss-hook", tags=["core-maps", "game"])
def extra_boss_hook(ctx):
    """core-maps installed the preset spawn hook behind map_glob_extra_boss."""
    line = re.search(rf"{PLUGIN}: stats [^\n]*", _log(ctx))
    if not line:
        ctx.skip("no stats line in the log yet (start the game)")
    assert "extra boss" in line.group(0) and "FAILED" not in line.group(0), line.group(0)
    return line.group(0)


@check("extra-boss-spawn", tags=["core-maps"])
def extra_boss_spawn(ctx):
    """A map with map_glob_extra_boss (area slot 5) spawned a map boss preset a second time."""
    log = _log(ctx)
    if not re.search(r"map stats: stat 551 layer \d+ value [1-9]\d* -> area slot as 5", log):
        ctx.skip("no map with map_glob_extra_boss opened yet")
    spawns = re.findall(r"map area: extra boss (\d+) in level (\d+)", log)
    if not spawns:
        ctx.skip("no extra boss yet (go to the map's boss, then run again)")
    boss, level = spawns[0]
    assert 138 <= int(level) <= 201, f"extra boss in level {level}, outside the map areas"
    return f"monster {boss} spawned twice in level {level}"


# MS7 (#221): a dungeon or unique map rewrites each of its levels (port entry -> the others, map_stats.cpp MultiLevels)
MULTI_LEVELS = {163: [164], 168: [169], 182: [183], 172: [173, 174, 175], 177: [178], 179: [180], 188: [190],
                191: [192], 193: [194, 195]}
AREA = re.compile(r"map area: level (\d+) (before|after): density (\d+)")


@check("multi-level-area", tags=["core-maps"])
def multi_level_area(ctx):
    """Opening a dungeon or unique map rewrote the monster region of each of its levels, after the entry level."""
    rows = [(int(m.group(1)), m.group(2), int(m.group(3))) for m in AREA.finditer(_log(ctx))]
    entries = [i for i, (lv, when, _) in enumerate(rows) if when == "before" and lv in MULTI_LEVELS]
    if not entries:
        ctx.skip("no dungeon or unique map opened yet (e.g. cheat spawn t42,typ r,inv 0)")
    i = entries[-1]
    entry = rows[i][0]
    got = [lv for lv, when, _ in rows[i:] if when == "after"]
    want = [entry] + MULTI_LEVELS[entry]
    assert got[:len(want)] == want, f"map {entry}: rewrote levels {got[:len(want)]}, want {want}"
    return f"map {entry}: levels {want} rewritten, densities " + ", ".join(str(d) for _, _, d in rows[i:i + 2 * len(want)])


@check("added-group-spawn", tags=["core-maps"])
def added_group_spawn(ctx):
    """A map with map_glob_add_mon_* (area slot 3) spawned a monster of the added group."""
    log = _log(ctx)
    added = re.findall(r"map area: level (\d+) adds monster (\d+) as group", log)
    if not added:
        ctx.skip("no map with an added monster group opened yet")
    # the plugin logs each monster kind once per game run, so match the id only
    first = set(re.findall(r"map stats: first monster (\d+) in level", log))
    missing = sorted({m for _, m in added} - first)
    if missing and len(first) < 8:
        ctx.skip(f"only {len(first)} monster kinds seen yet (clear more packs, then run again)")
    assert not missing, f"added monster never spawned: {missing}"
    return ", ".join(f"monster {m} in level {lv}" for lv, m in added)
