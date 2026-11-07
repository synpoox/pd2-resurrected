"""core-maps game checks: the extra boss area stat (MS4c, #289). Each reads <slot>/d2rloader/logs/core-maps.log.

Setup (Slot4-8, game closed): merge, build and install (docs/agents/game-slots.md)
  py -3 tools/pd2_merge.py
  powershell -File plugin/build.ps1
  powershell -File tools/install.ps1 -Game C:/D2RTestSlots/SlotN -Data . -NoValidate
In game, Hell, a level 90+ character in Harrogath: `debug_mode`, `cheat god 1`, then a Sewers of Harrogath map with the
Duplicated prefix (map_glob_extra_boss): `cheat spawn t25,typ m,pre 843,inv 0`, `cheat idall`, right-click the map,
enter the red portal and go to the Sewer Boss (SewerBoss; sewermap1-3.ds1 place it at subtile 399,311, 419,109 or
54,334 of the level). Then:
  py testing/game_checks.py run core-maps --slot N --path <repo>/tests/game --module core_maps_checks
"""
import os
import re

from game_checks import check

PLUGIN = "core-maps"


def _log(ctx) -> str:
    path = os.path.join(ctx.cfg.slot_dir(ctx.slot), "d2rloader", "logs", f"{PLUGIN}.log")
    if not os.path.isfile(path):
        ctx.skip(f"no plugin log yet: {path}")
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    starts = [m.start() for m in re.finditer(rf"{PLUGIN}: \S+ loaded \(", text)]
    return text[starts[-1]:] if starts else ""


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
