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

Double Throw (W9) has its own fixture, tests/game/fixtures/double-throw, which sets the same row to PD2's Double
Throw (srvdofunc 183, cltdofunc 116, PD2's calcs and params) and adds PD2's doublethrowaxe/knife/jav rows (Town 1).
It throws only with a throwing weapon in hand (no ammo, no missile): in game, console `debug_mode`, then
`cheat uniques 291,292` drops Deathbit (Battle Dart, column b); identify it with the scroll before equipping (unidentified,
it is refused) - the fixture sets the Battle Dart and Francisca reqdex to 0 so the test save can wield them. Cast it out
of town (Blood Moor) for the cast checks; double-throw-bounce needs a throw at a monster.

W6 checks (tag core-rules-w6), same merged data: an Amazon with Exploding Arrow (16), Multiple Shot (12), Pierce (33)
and Valkyrie (32), e.g. modkit save-set <save> level=30 --skill 16=10 --skill 12=10 --skill 33=20 --skill 32=10
--project .; then out of town: Exploding Arrow and Multiple Shot into packs (Pierce lets arrows reach a second
monster), summon a Valkyrie next to monsters, and run the same command with core-rules-w6.

W12 check (tag core-rules-w12), same merged data: a Necromancer with Blood Warp (merged skill 439) and no item that
lowers its life cost, e.g. modkit save-set <save> level=30 --skill 439=10 --project .; then out of town (Teleport
does not work in town) Blood Warp a few times and run the same command with core-rules-w12.
"""
import json
import os
import re
import time

from game_checks import check
import game_ctl
import missile_probe

PLUGIN = "core-rules"
SKILL_SLOTS = [  # table slots it installs
    ("srvdofunc", 189, "split throw"), ("cltdofunc", 118, "split throw"), ("srvstfunc", 79, "temp buff"),
    ("pSrvHitFunc", 65, "return spawn"), ("pSrvDoFunc", 46, "return"), ("pCltDoFunc", 78, "return"),
    ("srvstfunc", 78, "splash start"), ("srvstfunc", 82, "find"), ("cltstfunc", 62, "find"),
    ("srvdofunc", 190, "router"), ("srvstfunc", 80, "blood warp"), ("cltstfunc", 63, "blood warp"),
    ("srvdofunc", 172, "leap"), ("cltdofunc", 115, "leap"),
    ("srvdofunc", 183, "double throw"), ("cltdofunc", 116, "double throw"), ("pSrvHitFunc", 66, "bounce"),
    ("pSrvDmgFunc", 21, "first hit"),
    ("pSrvHitFunc", 63, "explode spread"), ("pSrvDmgFunc", 20, "pierce falloff"),
    ("cltdofunc", 22, "strike for all"),  # a code patch, not a slot
]
LEAP, LEAP_ATTACK = 132, 143  # D2R skill ids
LANDED = re.compile(r"leap: skill (\d+) level (\d+) landed: radius (\d+), (knockback|units in range) (\d+), velocity (\d+)")
ATTACKRATE = 68  # itemstatcost row of attackrate in D2R and PD2
HPREGEN = 74  # itemstatcost row of hpregen in D2R and PD2


def _log(ctx) -> str:
    path = os.path.join(ctx.cfg.slot_dir(ctx.slot), "d2rloader", "logs", f"{PLUGIN}.log")
    if not os.path.isfile(path):
        ctx.skip(f"no plugin log yet: {path}")
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    starts = [m.start() for m in re.finditer(rf"{PLUGIN}: \S+ loaded \(", text)]
    return text[starts[-1]:] if starts else ""


@check("core-rules-loads", tags=["core-rules", "core-rules-w10", "core-rules-w6", "core-rules-w12", "game"])
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


def _rows(name: str) -> list[str]:
    """Row names of the installed (built) table, in row order: a missile unit's class is its row index."""
    path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "out", "data",
                        "global", "excel", f"{name}.txt")
    with open(path, encoding="latin-1") as f:
        return [line.split("\t", 1)[0] for line in f.read().splitlines()[1:]]


@check("exploding-arrow-spread", tags=["core-rules-w6"])
def exploding_arrow_spread(ctx):
    """Exploding Arrow's hit (pSrvHitFunc 63) makes PD2's calc2 explosions, min(3 + lvl/5, 8), from frame calc4 + 2."""
    hits = re.findall(r"explode spread: skill 16 level (\d+): (\d+) of (\d+) explosions from frame (-?\d+)", _log(ctx))
    if not hits:
        ctx.skip("no Exploding Arrow hit in the log yet (shoot it at a monster, then run again)")
    level, created, count, start = (int(v) for v in hits[0])
    assert count == min(3 + level // 5, 8), f"level {level}: {count} explosions, PD2 makes {min(3 + level // 5, 8)}"
    assert created == count, f"{created} of {count} explosion missiles created"
    assert start == 4, f"first explosion at frame {start}, PD2: calc4 (2) + 2"
    return f"level {level}: {count} explosions from frame {start}"


@check("multiple-shot-falloff", tags=["core-rules-w6"])
def multiple_shot_falloff(ctx):
    """Multiple Shot's damage (pSrvDmgFunc 20): the n-th unit one arrow hits takes max(20, 100 - (n-1)*20) percent."""
    hits = [tuple(int(v) for v in h) for h in re.findall(r"pierce falloff: [^\n]*hit (\d+): (\d+)% damage", _log(ctx))]
    if not hits:
        ctx.skip("no second unit hit by one Multiple Shot arrow yet (Pierce, shoot into a pack, then run again)")
    wrong = [(n, p) for n, p in hits if p != max(20, 100 - (n - 1) * 20)]
    assert not wrong, f"hit, percent pairs off PD2's falloff: {wrong}"
    return ", ".join(f"hit {n}: {p}%" for n, p in hits[:4])


@check("valkyrie-power-strike", tags=["core-rules-w6"])
def valkyrie_power_strike(ctx):
    """The client draws a Valkyrie's Power Strike (cltdofunc 22 without D2R's local-player check): her
    powerstrikenova1valk client missiles show up."""
    row = _rows("missiles").index("powerstrikenova1valk")
    ctx.ensure_running()
    game = missile_probe.Game(ctx.game.pids()[0])
    sizes = missile_probe.bursts(game, row, 30)
    if not sizes:
        ctx.skip("no Valkyrie strike in 30 s (summon her next to monsters, then run again)")
    return f"{len(sizes)} Valkyrie strike(s) drawn, {sizes[0]} missile(s) in the first"


@check("throw-hd-models", tags=["core-rules"])
def throw_hd_models(ctx):
    """The installed data maps every fixture missile (Split Throw's or Double Throw's) to an HD model; without one
    they hit but are not drawn."""
    mpq = os.path.join(ctx.cfg.slot_dir(ctx.slot), "mods", ctx.cfg.mod_folder(), f"{ctx.cfg.mod_folder()}.mpq", "data")
    path = os.path.join(mpq, "hd", "missiles", "missiles.json")
    if not os.path.isfile(path):
        ctx.skip(f"no installed missiles.json: {path}")
    with open(path, encoding="utf-8-sig") as f:
        models = json.load(f)
    with open(os.path.join(mpq, "global", "excel", "missiles.txt"), encoding="utf-8", errors="replace") as f:
        rows = {line.split("	", 1)[0] for line in f}
    names = [m for m in ("splitthrowaxe", "splitthrowaxereturn", "doublethrowaxe", "doublethrowknife",
                         "doublethrowjav") if m in rows]
    if not names:
        ctx.skip("neither throw fixture is installed")
    missing = [m for m in names if not models.get(m)]
    assert not missing, f"no HD model for {missing} in {path}"
    return ", ".join(f"{m} {models[m]}" for m in names)


@check("double-throw-cast", tags=["core-rules"])
def double_throw_cast(ctx):
    """A Double Throw cast threw a doublethrow missile with PD2's calc2 bounces = min(par5 + lvl/par6, 8) (Param5 3,
    Param6 3) and damagepercent = calc1 = ln12 + (Double Swing + Frenzy blvl)*par8 (Param1 45, Param2 8, Param8 6)."""
    casts = re.findall(r"double throw: skill \d+ level (\d+): missile (\d+) \(column \d\), damage% (-?\d+), "
                       r"bounces (-?\d+)", _log(ctx))
    if not casts:
        ctx.skip("no Double Throw cast in the log yet (cast it out of town, then run again)")
    level, missile, damage, bounces = (int(v) for v in casts[0])
    assert bounces == min(3 + level // 3, 8), f"level {level}: {bounces} bounces, PD2 gives {min(3 + level // 3, 8)}"
    base = 45 + (level - 1) * 8
    assert damage >= base and (damage - base) % 6 == 0, f"level {level}: damage% {damage}, PD2 gives {base} + 6 per synergy level"
    return f"level {level}: missile {missile}, damage% {damage} ({base} + {(damage - base) // 6} synergy levels), {bounces} bounces"


@check("double-throw-client-cast", tags=["core-rules"])
def double_throw_client_cast(ctx):
    """The client half (cltdofunc 116) threw its copy with the same bounces as the server's."""
    log = _log(ctx)
    server = re.findall(r"double throw: skill \d+ level \d+: .*?bounces (-?\d+)", log)
    client = re.findall(r"double throw client: skill \d+ level (\d+): missile (\d+), bounces (-?\d+)", log)
    if not client:
        ctx.skip("no client-side Double Throw cast in the log yet (cast it out of town, then run again)")
    level, missile, bounces = (int(v) for v in client[0])
    assert missile > 0, f"the client threw no missile ({missile})"
    assert server and int(server[0]) == bounces, f"client {bounces} bounces, server {server[:1]}"
    return f"level {level}: client missile {missile}, {bounces} bounces"


@check("double-throw-bounce", tags=["core-rules"])
def double_throw_bounce(ctx):
    """A doublethrow missile that hit a monster threw itself at the next one (shit 66) within PD2's range, sHitPar1 0
    so aurarangecalc par3 = 15, with one bounce less. Needs a throw at monsters out of town."""
    hits = re.findall(r"double throw bounce: missile \d+ range (\d+), bounces (\d+) -> (\d+)", _log(ctx))
    if not hits:
        ctx.skip("no bounce in the log yet (throw Double Throw at a group of monsters, then run again)")
    rng, before, after = (int(v) for v in hits[0])
    assert rng == 15, f"bounce range {rng}, PD2's par3 is 15"
    assert after == before - 1, f"bounces {before} -> {after}"
    return f"range {rng}, bounces {before} -> {after} ({len(hits)} bounces logged)"

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
    assert kind == "units in range" and radius == 8, f"{kind} radius {radius}, PD2 hits units within 8"
    assert velocity == 0xb00 + 60 * 0x50, f"velocity {velocity}, PD2 gives {0xb00 + 60 * 0x50}"
    return f"level {level}: radius {radius}, velocity {velocity}"


@check("leap-attack-hits", tags=["core-rules"])
def leap_attack_hits(ctx):
    """A Leap Attack landed among monsters, so its hit was rolled on them. Needs a cast out of town."""
    hits = [h for *_, h, _ in _landings(ctx, LEAP_ATTACK) if h > 0]
    if not hits:
        ctx.skip("no Leap Attack landing had units in range yet (Leap Attack into monsters out of town, then run again)")
    return f"{len(hits)} landings rolled hits on {sum(hits)} units"


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


@check("blood-warp-cast", tags=["core-rules-w12"])
def blood_warp_cast(ctx):
    """A Blood Warp (router srvdofunc 190) cost PD2's max(calc1% of life, calc2), calc1 12 (no blood_warp_life_reduction)
    and calc2 min(ln34 * 256, 15360) (Param3 2, Param4 2), and gave it back as hpregen = cost / 125 + 1 for auralencalc
    125 frames. Life is in 1/256."""
    casts = re.findall(r"blood warp: skill 439 level (\d+): life (\d+), cost (\d+), regen (\d+) for (\d+) frames "
                       r"\(stat (\d+)", _log(ctx))
    if not casts:
        ctx.skip("no Blood Warp in the log yet (cast it out of town, then run again)")
    level, life, cost, regen, frames, stat = (int(v) for v in casts[0])
    expected = max(int(12 / 100 * life), min((2 + (level - 1) * 2) * 256, 15360))
    assert cost == expected, f"level {level}, life {life}: cost {cost}, PD2 gives {expected}"
    assert cost < life, f"cost {cost} would kill at life {life}"
    assert (frames, stat) == (125, HPREGEN), f"{frames} frames of stat {stat}, PD2: 125 of hpregen ({HPREGEN})"
    assert regen == cost // 125 + 1, f"regen {regen}, PD2 gives {cost // 125 + 1}"
    return f"level {level}: life {life // 256}, cost {cost / 256:.1f} life, hpregen {regen} for {frames} frames"


@check("core-rules-no-crash", tags=["core-rules", "core-rules-w10", "core-rules-w6", "core-rules-w12"])
def core_rules_no_crash(ctx):
    """No crash file since the run began and no FAULT line in the plugin log."""
    new = ctx.game.new_crash_files(ctx.shared.get("crash_before", []))
    assert not new, f"new crash files: {new}"
    faults = re.findall(rf"{PLUGIN}: FAULT[^\n]*", _log(ctx))
    assert not faults, faults[0]
    return "no crash"
