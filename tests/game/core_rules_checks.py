"""core-rules game checks for d2rAI-toolkit's testing/ framework (game_checks.py).

Each check reads the plugin log, <slot>/d2rloader/logs/core-rules.log; expected values come from PD2's data.

Setup (Slot4-7, game closed):
  powershell -File tools/install.ps1 -Game C:/D2RTestSlots/Slot4 -Data tests/game/fixtures/split-throw
  (or -Data tests/game/fixtures/leap and a Barbarian with Leap and Leap Attack, for the leap checks: cast both in
  town, then Leap Attack into monsters out of town)
  a Barbarian test save with Double Throw (class skill slot 14) at level 10 in that slot's save folder
Run:
  py testing/game_checks.py run core-rules --slot 4 --path <repo>/tests/game --module core_rules_checks
  then in town: Double Throw on the right button, right-click the ground, and run the same command again
  (the cast checks skip until a cast is in the log). For split-throw-return, walk into Blood Moor and throw at
  monsters (allowed in PD2 Resurrected slots). The fixture adds PD2's splitthrowaxe and splitthrowaxereturn rows
  (PD2's values, cloned from throwaxe) with Town 1: a missile without it dies at once in town, so nothing would be
  drawn. Both also get throwaxe's HD model (missile_hd): D2R's HD renderer draws only missiles named in
  data/hd/missiles/missiles.json, so without it they hit but are invisible. It keeps D2R's quickness state in place of PD2's temp_skill_buff (merged as state 270): the merged tables
  do not pass modkit's validation yet (skilldesc, overlay, pettype, monstats, item events and affix properties are
  later work items), so the fixture cannot build on them. The merged P2 tables were checked on Slot4 (#45): state
  270, attackrate 100 -> 110 -> 100 over one buff, ten casts with no crash.

W10 checks (tag core-rules-w10) run on the merged data, which has PD2's rows and states:
  py -3 tools/pd2_merge.py
  powershell -File tools/install.ps1 -Game C:/D2RTestSlots/SlotN -Data .
  a Barbarian with Bash, Find Potion and Frenzy (two one-handed weapons for Frenzy)
  py testing/game_checks.py run core-rules-w10 --slot N --path <repo>/tests/game --module core_rules_checks
then out of town: Bash monsters 30+ times, Frenzy a few times in a row, Find Potion on a corpse, and run again.
"""
import json
import os
import re
import time

from game_checks import check
import game_ctl

PLUGIN = "core-rules"
SKILL_SLOTS = [  # table slots it installs
    ("srvdofunc", 189, "split throw"), ("cltdofunc", 118, "split throw"), ("srvstfunc", 79, "temp buff"),
    ("pSrvHitFunc", 65, "return spawn"), ("pSrvDoFunc", 46, "return"), ("pCltDoFunc", 78, "return"),
    ("srvstfunc", 78, "splash start"), ("srvstfunc", 82, "find"), ("cltstfunc", 62, "find"),
    ("srvdofunc", 190, "router"),
    ("srvdofunc", 172, "leap"), ("cltdofunc", 115, "leap"),
]
LEAP, LEAP_ATTACK = 132, 143  # D2R skill ids
LANDED = re.compile(r"leap: skill (\d+) level (\d+) landed: radius (\d+), (knockback|units hit) (\d+), velocity (\d+)")
ATTACKRATE = 68  # itemstatcost row of attackrate in D2R and PD2


def _log(ctx) -> str:
    path = os.path.join(ctx.cfg.slot_dir(ctx.slot), "d2rloader", "logs", f"{PLUGIN}.log")
    if not os.path.isfile(path):
        ctx.skip(f"no plugin log yet: {path}")
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    starts = [m.start() for m in re.finditer(rf"{PLUGIN}: \S+ loaded \(", text)]
    return text[starts[-1]:] if starts else ""


@check("core-rules-loads", tags=["core-rules", "core-rules-w10", "game"])
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


def dm34(level: int) -> int:
    """PD2 Frenzy's calc2 dm34: Param3 + (Param4 - Param3) * (110 * lvl / (lvl + 6)) / 100, with Param3 0, Param4 45."""
    return 0 + (45 - 0) * (110 * level // (level + 6)) // 100


def _counter(text: str, name: str) -> int:
    m = re.search(rf"{name} (\d+)", text)
    return int(m.group(1)) if m else 0


@check("find-normal-corpse", tags=["core-rules-w10"])
def find_normal_corpse(ctx):
    """Find Potion / Find Item (srvstfunc 82) searched a monster corpse that no player owns, so PD2's owner guard lets
    normal corpses through."""
    finds = re.findall(r"find: skill (\d+) on unit type (\d+) owner (-?\d+)/(\d+)", _log(ctx))
    if not finds:
        ctx.skip("no Find in the log yet (kill a monster, use Find Potion or Find Item on its corpse, run again)")
    skill, type_, owner_type, owner_id = (int(v) for v in finds[0])
    assert type_ == 1, f"Find ran on unit type {type_}, expected a monster (1)"
    return f"skill {skill} on a monster corpse, owner fields {owner_type}/{owner_id}"


@check("bash-knockback", tags=["core-rules-w10"])
def bash_knockback(ctx):
    """Bash (srvdofunc 190) knocks back about half the hits that land (PD2 srvdo 158: rand % 100 < 50)."""
    ctx.ensure_running()
    ctx.game.console_text(PLUGIN)  # the counters line goes to the plugin log, not the console reply
    time.sleep(0.5)
    lines = [l for l in _log(ctx).splitlines() if "bash hits" in l]
    text = lines[-1] if lines else ""
    hits, knockbacks = _counter(text, "bash hits"), _counter(text, "knockbacks")
    if hits < 30:
        ctx.skip(f"{hits} Bash hits so far, need 30 (Bash monsters, then run again)")
    rate = knockbacks / hits
    assert 0.3 <= rate <= 0.7, f"{knockbacks} knockbacks in {hits} hits ({rate:.0%}), PD2 knocks back 50%"
    return f"{knockbacks} knockbacks in {hits} hits ({rate:.0%})"


@check("frenzy-splash", tags=["core-rules-w10"])
def frenzy_splash(ctx):
    """Frenzy's start (srvstfunc 78) sets inc_splash_radius = calc2 * frenzy charges / calc4, with PD2's calc2 dm34
    and calc4 5. Needs a start with charges: attack with Frenzy a few times in a row."""
    starts = re.findall(r"splash start: skill 147 level (\d+) charges (\d+) calc2 (-?\d+): splash radius (-?\d+)",
                        _log(ctx))
    starts = [tuple(int(v) for v in s) for s in starts]
    charged = [s for s in starts if s[1] > 0]
    if not charged:
        ctx.skip("no Frenzy start with charges in the log yet (Frenzy monsters a few times, then run again)")
    level, charges, calc2, value = charged[0]
    assert calc2 == dm34(level), f"level {level}: calc2 {calc2}, PD2's dm34 gives {dm34(level)}"
    assert value == calc2 * charges // 5, f"splash radius {value}, PD2 gives {calc2} * {charges} / 5"
    return f"level {level}, {charges} charges: splash radius {value}"


@check("split-throw-hd-models", tags=["core-rules"])
def split_throw_hd_models(ctx):
    """The installed data maps both Split Throw missiles to an HD model; without one they hit but are not drawn."""
    path = os.path.join(ctx.cfg.slot_dir(ctx.slot), "mods", ctx.cfg.mod_folder(), f"{ctx.cfg.mod_folder()}.mpq",
                        "data", "hd", "missiles", "missiles.json")
    if not os.path.isfile(path):
        ctx.skip(f"no installed missiles.json: {path}")
    with open(path, encoding="utf-8-sig") as f:
        models = json.load(f)
    missing = [m for m in ("splitthrowaxe", "splitthrowaxereturn") if not models.get(m)]
    assert not missing, f"no HD model for {missing} in {path}"
    return ", ".join(f"{m} {models[m]}" for m in ("splitthrowaxe", "splitthrowaxereturn"))

def _landings(ctx, skill: int) -> list[tuple]:
    return [(int(m[1]), int(m[2]), m[3], int(m[4]), int(m[5])) for m in LANDED.findall(_log(ctx)) if int(m[0]) == skill]


@check("leap-landing", tags=["core-rules"])
def leap_landing(ctx):
    """A Leap landed with PD2's knockback radius calc3 = min(par3+blvl*par4, 15) (Param3 7, Param4 1) and speed
    0xb00 + calc2*0x50 (calc2 80). Assumes no +skills: blvl is the logged level."""
    landed = _landings(ctx, LEAP)
    if not landed:
        ctx.skip("no Leap landing in the log yet (Leap in town, then run again)")
    level, radius, kind, _, velocity = landed[0]
    assert kind == "knockback", landed[0]
    assert radius == min(7 + level, 15), f"level {level}: radius {radius}, PD2 gives {min(7 + level, 15)}"
    assert velocity == 0xb00 + 80 * 0x50, f"velocity {velocity}, PD2 gives {0xb00 + 80 * 0x50}"
    return f"level {level}: knockback radius {radius}, velocity {velocity}"


@check("leap-attack-landing", tags=["core-rules"])
def leap_attack_landing(ctx):
    """A Leap Attack landed hitting units within calc3 = par5 (8), at speed 0xb00 + calc2*0x50 (calc2 60)."""
    landed = _landings(ctx, LEAP_ATTACK)
    if not landed:
        ctx.skip("no Leap Attack landing in the log yet (Leap Attack in town, then run again)")
    level, radius, kind, _, velocity = landed[0]
    assert kind == "units hit" and radius == 8, f"{kind} radius {radius}, PD2 hits units within 8"
    assert velocity == 0xb00 + 60 * 0x50, f"velocity {velocity}, PD2 gives {0xb00 + 60 * 0x50}"
    return f"level {level}: radius {radius}, velocity {velocity}"


@check("leap-attack-hits", tags=["core-rules"])
def leap_attack_hits(ctx):
    """A Leap Attack landing among monsters hit them. Needs a cast out of town."""
    hits = [h for *_, h, _ in _landings(ctx, LEAP_ATTACK) if h > 0]
    if not hits:
        ctx.skip("no Leap Attack landing hit a unit yet (Leap Attack into monsters out of town, then run again)")
    return f"{len(hits)} landings hit {sum(hits)} units"


@check("leap-client", tags=["core-rules"])
def leap_client(ctx):
    """The client half (cltdofunc 115) set the same speed as the server half for each leap skill."""
    client = {int(s): int(v) for s, v in re.findall(r"leap client: skill (\d+) level \d+: velocity (\d+)", _log(ctx))}
    if not client:
        ctx.skip("no client-side leap in the log yet (Leap in town, then run again)")
    for skill, velocity in client.items():
        server = [v for *_, v in _landings(ctx, skill)]
        assert server and server[0] == velocity, f"skill {skill}: client velocity {velocity}, server {server[:1]}"
    return ", ".join(f"skill {s} velocity {v}" for s, v in client.items())


@check("core-rules-no-crash", tags=["core-rules", "core-rules-w10"])
def core_rules_no_crash(ctx):
    """No crash file since the run began and no FAULT line in the plugin log."""
    new = ctx.game.new_crash_files(ctx.shared.get("crash_before", []))
    assert not new, f"new crash files: {new}"
    faults = re.findall(rf"{PLUGIN}: FAULT[^\n]*", _log(ctx))
    assert not faults, faults[0]
    return "no crash"
