"""core-rules game checks for d2rAI-toolkit's testing/ framework (game_checks.py).

Each check reads the plugin log, <slot>/d2rloader/logs/core-rules.log; expected values come from PD2's data.

Setup (Slot4-7, game closed):
  powershell -File tools/install.ps1 -Game C:/D2RTestSlots/Slot4 -Data tests/game/fixtures/split-throw
  a Barbarian test save with Double Throw (class skill slot 14) at level 10 in that slot's save folder
Run:
  py testing/game_checks.py run core-rules --slot 4 --path <repo>/tests/game --module core_rules_checks
  then in town: Double Throw on the right button, right-click the ground, and run the same command again
  (the cast checks skip until a cast is in the log). For split-throw-return, walk into Blood Moor and throw at
  monsters (allowed in PD2 Resurrected slots). The fixture adds PD2's splitthrowaxe and splitthrowaxereturn rows
  (PD2's values, cloned from throwaxe) with Town 1: a missile without it dies at once in town, so nothing would be
  drawn. It keeps D2R's quickness state in place of PD2's temp_skill_buff (merged as state 270): the merged tables
  do not pass modkit's validation yet (skilldesc, overlay, pettype, monstats, item events and affix properties are
  later work items), so the fixture cannot build on them. The merged P2 tables were checked on Slot4 (#45): state
  270, attackrate 100 -> 110 -> 100 over one buff, ten casts with no crash.
"""
import os
import re

from game_checks import check
import game_ctl

PLUGIN = "core-rules"
SKILL_SLOTS = [  # table slots it installs
    ("srvdofunc", 189, "split throw"), ("cltdofunc", 118, "split throw"), ("srvstfunc", 79, "temp buff"),
    ("pSrvHitFunc", 65, "return spawn"), ("pSrvDoFunc", 46, "return"), ("pCltDoFunc", 78, "return"),
]
ATTACKRATE = 68  # itemstatcost row of attackrate in D2R and PD2


def _log(ctx) -> str:
    path = os.path.join(ctx.cfg.slot_dir(ctx.slot), "d2rloader", "logs", f"{PLUGIN}.log")
    if not os.path.isfile(path):
        ctx.skip(f"no plugin log yet: {path}")
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    starts = [m.start() for m in re.finditer(rf"{PLUGIN}: \S+ loaded \(", text)]
    return text[starts[-1]:] if starts else ""


@check("core-rules-loads", tags=["core-rules", "game"])
def core_rules_loads(ctx):
    """core-rules loads game-wide and fills every do-table slot it owns."""
    ctx.ensure_running()
    try:
        game_ctl.wait_for(lambda: re.search(rf"{PLUGIN}: skills ", _log(ctx)), 90, 2.0, "the skills part line")
    except game_ctl.WaitTimeout:
        raise AssertionError("the plugin log never showed the skills line (plugin not loaded?)")
    line = re.search(rf"{PLUGIN}: skills [^\n]*", _log(ctx)).group(0)
    n = len(SKILL_SLOTS)
    assert f"skills {n}/{n} installed" in line, line
    for table, slot, name in SKILL_SLOTS:
        assert f"{table} {slot} {name}" in line, f"{table} {slot} missing: {line}"
    return line[len(PLUGIN) + 2:]


@check("split-throw-cast", tags=["core-rules"])
def split_throw_cast(ctx):
    """A Split Throw cast created PD2's missile count, calc1 = par1+min(lvl/5,par2) with Param1 3, Param2 4."""
    casts = re.findall(r"split throw: skill \d+ level (\d+): (\d+) of (\d+) missiles", _log(ctx))
    if not casts:
        ctx.skip("no Split Throw cast in the log yet (cast it in town, then run again)")
    level, created, count = (int(v) for v in casts[0])
    expected = 3 + min(level // 5, 4)
    assert count == expected, f"level {level}: {count} missiles, PD2 makes {expected}"
    assert created == count, f"only {created} of {count} missiles were created"
    return f"level {level}: {created} of {count} missiles"


@check("split-throw-client-cast", tags=["core-rules"])
def split_throw_client_cast(ctx):
    """The client half (cltdofunc 118) drew the same number of missiles as the server half made."""
    log = _log(ctx)
    server = re.findall(r"split throw: skill \d+ level \d+: \d+ of (\d+) missiles", log)
    client = re.findall(r"split throw client: skill \d+ level (\d+): (\d+) of (\d+) missiles", log)
    if not client:
        ctx.skip("no client-side Split Throw cast in the log yet (cast it in town, then run again)")
    level, created, count = (int(v) for v in client[0])
    assert server and int(server[0]) == count, f"client {count} missiles, server {server[:1]}"
    assert created == count, f"the client created only {created} of {count} missiles"
    return f"level {level}: client drew {created} of {count} missiles"


@check("split-throw-temp-buff", tags=["core-rules"])
def split_throw_temp_buff(ctx):
    """srvstfunc 79 put the skill's aurastate on the caster at the start frame with PD2's aurastat1 attackrate =
    aurastatcalc1 par4 (Param4 10). The length is approximated (docs/core-rules.md)."""
    buffs = re.findall(r"temp buff: skill \d+ level (\d+) state (\d+) for (\d+) frames, stat (\d+) \+(-?\d+)", _log(ctx))
    if not buffs:
        ctx.skip("no temp buff in the log yet (cast Split Throw, then run again)")
    level, state, frames, stat, value = (int(v) for v in buffs[0])
    assert state > 0 and frames > 0, buffs[0]
    assert stat == ATTACKRATE, f"aurastat1 is stat {stat}, PD2 gives attackrate ({ATTACKRATE})"
    assert value == 10, f"attack rate +{value}, PD2's par4 is 10"
    return f"level {level}: state {state} for {frames} frames, attackrate +{value}"


@check("split-throw-return", tags=["core-rules"])
def split_throw_return(ctx):
    """A Split Throw axe that hit a monster spawned its return missile (shit 65), and a return reached the thrower
    (sdo 46). Needs a cast at monsters out of town."""
    log = _log(ctx)
    if "return missile: spawned on hit" not in log:
        ctx.skip("no return missile in the log yet (hit a monster with Split Throw, then run again)")
    assert "return missile: caught by the thrower" in log, "a return missile spawned but none reached the thrower"
    return "spawned on hit and caught"


@check("core-rules-no-crash", tags=["core-rules"])
def core_rules_no_crash(ctx):
    """No crash file since the run began and no FAULT line in the plugin log."""
    new = ctx.game.new_crash_files(ctx.shared.get("crash_before", []))
    assert not new, f"new crash files: {new}"
    faults = re.findall(rf"{PLUGIN}: FAULT[^\n]*", _log(ctx))
    assert not faults, faults[0]
    return "no crash"
