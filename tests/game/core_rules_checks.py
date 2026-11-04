"""core-rules game checks for d2rAI-toolkit's testing/ framework (game_checks.py).

Each check reads the plugin log, <slot>/d2rloader/logs/core-rules.log; expected values come from PD2's data.

Setup (Slot4-8, game closed):
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

W11 checks (tag core-rules-w11), same merged data: a Necromancer with Poison Dagger (73), e.g. modkit save-set <save>
level=30 --skill 73=10 --project .; then out of town stab monsters with a dagger a few times and run the same command
with core-rules-w11. D2RCore's console does it without walking: `debug_mode`, `cheat uniques 39` (Gull), pick it up,
`cheat idall`, equip, `cheat god 1`, `cheat goto 2` (Blood Moor), `cheat mspawn zombie1,3`, then Poison Strike on the
right button and right-click a zombie. game_ctl clicks reach the world only with the harness background flag off.
W12 check (tag core-rules-w12), same merged data: a Necromancer with Blood Warp (merged skill 439) and no item that
lowers its life cost, e.g. modkit save-set <save> level=30 --skill 439=10 --project .; then out of town (Teleport
does not work in town) Blood Warp a few times and run the same command with core-rules-w12.
W13 checks (tag core-rules-w13), same merged data: a Necromancer with Desecrate (83), e.g. modkit save-set <save>
level=30 --skill 83=10 --project .; then out of town: cast Desecrate on the ground, wait 20 s (PD2 hides the corpses
after 16), and run the same command with core-rules-w13.
W14 checks (tag core-rules-w14), same merged data: a Necromancer with Clay Golem (class skill slot 9), Golem Mastery
(slot 13) at 10 and Revive (slot 29), no +skills, e.g. modkit save-set <save> --skill 9=1 --skill 13=10 --skill 29=10;
then out of town: run core-rules-w14 and, while golem-count watches (30 s), summon Clay Golem four times; then kill a
monster, Revive its corpse next to other monsters, let the revive fight, and run again.
W16 check (tag core-rules-w16), same merged data: a Paladin with Charge (107) and no Vigor or Might, e.g. modkit
save-set <save> level=30 --skill 107=10 --project .; then out of town Charge a few monsters from several screens away
and run the same command with core-rules-w16.

W26 checks (tag core-rules-w26), same merged data: a Sorceress with Teleport (54) and no +skills, e.g. modkit save-set
<save> level=30 --skill 54=5 --project .; then out of town Teleport a few times and run the same command with
core-rules-w26.
W21 checks (tag core-rules-w21), same merged data: a Druid with Arctic Blast (230) or a Sorceress with Inferno (41),
e.g. modkit save-set <save> level=30 --skill 230=10 --project .; then out of town: hold the skill on the ground for
a second, and run the same command with core-rules-w21. For the take-over checks, press another skill while
holding it; for blade-fury-refused, an Assassin with Blade Fury (266): hold it.
W22 checks (tag core-rules-w22), same merged data: a Druid with Firestorm (225, class skill slot 4) and no +skills
(the log gives the skill level, PD2's calc reads the base level), e.g. modkit save-set <save> level=30 --skill 4=10
--project .; then out of town cast Firestorm a few times and run the same command with core-rules-w22.
W29 checks (tag core-rules-w29), same merged data: an Assassin with the charge-up skills (Tiger Strike 254, Fists of
Fire 259, Cobra Strike 265, Claws of Thunder 269, Blades of Ice 274, Royal Strike 280) and Dragon Talon (255) to
release, e.g. modkit save-set <save> level=30 --skill 254=1 --skill 259=1 --skill 265=1 --skill 269=1 --skill 274=1
--skill 280=1 --skill 255=1 --project .; then out of town hit monsters three times in a row with each of Fists of
Fire, Cobra Strike and Blades of Ice, releasing between them with Dragon Talon (a release runs the same prg slots and
logs the same lines), and run the same command with core-rules-w29.
W17 checks (tag core-rules-w17), same merged data: a Paladin with Holy Light and Fist of the Heavens. Holy Light is
the Paladin's 32nd skill, past the 30 class skills a save holds, so set both in game: `debug_mode`, `cheat sklvl Holy
Light,10`, `cheat sklvl Fist of the Heavens,10`, `cheat goto 2`, `cheat mspawn zombie1,6`; then Holy Light on a
zombie next to others, Fist of the Heavens on the pack (not on a dropped item: the click picks it up), and run the
same command with core-rules-w17.
W23 checks (tag core-rules-w23), same merged data: a Druid with Gust (442), Werebear (228) and Hunger (242), no
+skills; save-set takes class skill slots (Werebear 7, Hunger 21, Gust 30), e.g. modkit save-set <save> --skill
30=5 --skill 7=1 --skill 21=5 --project .; then out of town cast Gust, and in bear form kill a monster and eat its
corpse with Hunger; run the same command with core-rules-w23.
W27 checks (tag core-rules-w27), same merged data: a Sorceress with Static Field (42), Combustion (448) and Inferno
(41) and no Lightning Mastery; save-set takes class skill slots (Inferno 5, Static Field 6, Combustion 31), e.g.
modkit save-set <save> level=30 --skill 6=10 --skill 31=10 --skill 5=10 --project .; then out of town cast Static
Field next to monsters, cast Combustion, hold Inferno for a second, and run the same command with core-rules-w27.
W30 checks (tag core-rules-w30), same merged data and save as W29: out of town hit monsters three times in a row with
each of Fists of Fire, Cobra Strike, Claws of Thunder and Blades of Ice, releasing between them with Dragon Talon, and
run the same command with core-rules-w30. Each charge's client missiles log on the client.
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
    ("cltdofunc", 122, "poison dagger"), ("pSrvHitFunc", 61, "state hit"),
    ("cltdofunc", 121, "desecrate"), ("srvstfunc", 72, "charge"), ("srvdofunc", 180, "charge"),
    ("srvdofunc", 182, "channel"), ("cltdofunc", 111, "channel"),
    ("cltdofunc", 119, "wall"),
    ("mode gate", "busy", "take-over"),  # an inline hook, not a slot
    ("cltdofunc", 22, "strike for all"),  # a code patch, not a slot
    ("auraeventfunc", 32, "revive splash"),  # D2R's own slot, reached by no D2R row
    ("srvdofunc", 58, "revive events"),  # D2R's Revive, wrapped
    ("srvdofunc", 176, "holy light"), ("pCltHitFunc", 66, "holy light"), ("pCltHitFunc", 71, "fist delay"),
    ("srvdofunc", 184, "charge spread"), ("srvdofunc", 185, "charge ring"), ("srvdofunc", 186, "charge fire"),
    ("srvdofunc", 181, "charge cold"), ("cltdofunc", 120, "nova"),
    ("cltdofunc", 127, "charge spread"), ("cltdofunc", 128, "charge ring"), ("pCltDoFunc", 79, "charge explode"),
    ("pCltHitFunc", 67, "charge bolt"),
]
LEAP, LEAP_ATTACK = 132, 143  # D2R skill ids
REVIVE, PROC_SPLASH_DAMAGE = 95, 430  # merged skill ids
LANDED = re.compile(r"leap: skill (\d+) level (\d+) landed: radius (\d+), (knockback|units in range) (\d+), velocity (\d+)")
ATTACKRATE = 68  # itemstatcost row of attackrate in D2R and PD2
HPREGEN = 74  # itemstatcost row of hpregen in D2R and PD2
CHARGE = 107
TELEPORT_DEBUFF = 233  # merged states id of teleportdebuff
COMBUSTION, STATIC_FIELD = 448, 42  # merged skill ids
STATICFIELD_DEBUFF, LIGHTRESIST = 240, 41  # merged states id, itemstatcost id
STATIC_FIELD_MIN = (55, 70, 85)  # PD2's difficultylevels StaticFieldMin: normal, nightmare, hell
HUNGER = 246  # merged states id of hunger
LIFE_DRAIN, VELOCITY = 60, 67  # itemstatcost lifedrainmindam, velocitypercent in D2R and PD2
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _log(ctx) -> str:
    path = os.path.join(ctx.cfg.slot_dir(ctx.slot), "d2rloader", "logs", f"{PLUGIN}.log")
    if not os.path.isfile(path):
        ctx.skip(f"no plugin log yet: {path}")
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    starts = [m.start() for m in re.finditer(rf"{PLUGIN}: \S+ loaded \(", text)]
    return text[starts[-1]:] if starts else ""


@check("core-rules-loads", tags=["core-rules", "core-rules-w10", "core-rules-w6", "core-rules-w11", "core-rules-w12",
                                 "core-rules-w13", "core-rules-w14", "core-rules-w16", "core-rules-w26", "core-rules-w33",
                                 "core-rules-w21", "core-rules-w22", "core-rules-w29", "core-rules-w17",
                                 "core-rules-w23", "core-rules-w27", "core-rules-w30", "game"])
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


DAGGER = re.compile(r"poison dagger: skill 73 level (\d+): (\d+) clouds \(row (\d+), Param3 (-?\d+)\) at -?\d+,-?\d+, "
                    r"poison (-?\d+) length (-?\d+)")


def _elem_min(level: int) -> int:
    """PD2's Poison Dagger EMin at `level` before synergies, shifted by HitShift 4: EMin 1, EMinLev1-5 1/4/14/24/34."""
    per = [(8, 1), (16, 4), (22, 14), (28, 24), (10 ** 9, 34)]
    return (1 + sum(next(v for top, v in per if n <= top) for n in range(2, level + 1))) << 4


@check("poison-dagger-clouds", tags=["core-rules-w11"])
def poison_dagger_clouds(ctx):
    """A Poison Dagger hit (srvdofunc 190) rings the target with PD2's Param3 10 -> 11 poisondaggercloud missiles,
    each given the hit's poison, which is at least PD2's EMin for the level (synergies only add)."""
    casts = DAGGER.findall(_log(ctx))
    if not casts:
        ctx.skip("no Poison Dagger hit in the log yet (stab a monster out of town, then run again)")
    level, created, row, param3, poison, length = (int(v) for v in casts[-1])
    assert row == _rows("missiles").index("poisondaggercloud"), f"cloud row {row}"
    assert param3 == 10 and created == 11, f"{created} clouds for Param3 {param3}, PD2 makes 11"
    assert poison >= _elem_min(level), f"level {level}: poison {poison} under PD2's EMin {_elem_min(level)}"
    assert length > 0, f"poison length {length}"
    return f"level {level}: {created} clouds, poison {poison} over {length} frames"


@check("poison-cloud-state", tags=["core-rules-w11"])
def poison_cloud_state(ctx):
    """A cloud's hit (pSrvHitFunc 61) gives the unit PD2's poison_strike state (sHitPar1) for 52 frames (sHitPar2)
    and returns PD2's 2 + CollideKill: D2R's collision deals the cloud's damage only on bit 2."""
    row = _rows("missiles").index("poisondaggercloud")
    hits = re.findall(rf"state hit: missile row {row} skill 73 level \d+: state (\d+) for (\d+) frames.*?"
                      r", result (-?\d+)", _log(ctx))
    if not hits:
        ctx.skip("no cloud hit in the log yet (stab a monster with Poison Dagger, then run again)")
    state, frames, result = (int(v) for v in hits[-1])
    assert state == _rows("states").index("poison_strike"), f"state {state}"
    assert frames == 52, f"{frames} frames, PD2's sHitPar2 is 52"
    assert result == 2, f"result {result}: D2R deals the cloud's damage on 2 (CollideKill 0)"
    return f"state {state} for {frames} frames, result {result}"
@check("desecrate-cast", tags=["core-rules-w13"])
def desecrate_cast(ctx):
    """Desecrate (router srvdo 190) raised PD2's calc1 corpses, 5 at every level, and the client (cltdofunc 121) drew
    its three missiles."""
    log = _log(ctx)
    casts = re.findall(r"desecrate: skill 83 level (\d+): (\d+) of (\d+) corpses in level (\d+), cloud (\d)", log)
    if not casts:
        ctx.skip("no Desecrate cast in the log yet (cast it out of town, then run again)")
    level, created, count, area, cloud = (int(v) for v in casts[0])
    assert count == 5, f"level {level}: {count} corpses, PD2's calc1 makes 5"
    assert created == count, f"only {created} of {count} corpses spawned"
    assert cloud == 1, "no poisonexplosioncloud (srvmissilea) at the point"
    client = re.findall(r"desecrate client: skill 83 level \d+: (\d+) missiles", log)
    assert client and client[0] == "3", f"client missiles: {client[:1]}, PD2 draws cltmissile a, b and c"
    return f"level {level}: {created} of {count} corpses in level {area}, the cloud, 3 client missiles"


@check("desecrate-corpses-hide", tags=["core-rules-w13"])
def desecrate_corpses_hide(ctx):
    """Desecrate's corpses are hidden (corpse_nodraw) when their 16 s timer runs out."""
    ctx.ensure_running()
    ctx.game.console_text(PLUGIN)  # the counters line goes to the plugin log, not the console reply
    time.sleep(0.5)
    lines = [l for l in _log(ctx).splitlines() if "corpses hidden" in l]
    text = lines[-1] if lines else ""
    casts, hidden = _counter(text, "desecrates"), _counter(text, "corpses hidden")
    if casts == 0:
        ctx.skip("no Desecrate cast yet (cast it out of town, wait 20 s, then run again)")
    assert hidden > 0, f"{casts} Desecrate cast(s), no corpse hidden: the state-list timer does not run on a corpse"
    return f"{hidden} corpse(s) hidden after {casts} cast(s)"


INFERNO = 41  # D2R skill id, PD2's too; Arctic Blast is 230


def _channel_values(skill: int, level: int) -> tuple[int, int]:
    """PD2's calc3 (missiles) and calc1 (range, ln12 = par1 + (lvl - 1) * par2) for Inferno and Arctic Blast."""
    if skill == INFERNO:
        return min(2 + level // 4, 5), max(min((22 + (level - 1) * 2) // 2, 32), 1)
    return 2, max(min((24 + (level - 1) * 2) // 2, 24), 1)


@check("channel-cast", tags=["core-rules-w21", "core-rules-w27"])
def channel_cast(ctx):
    """Inferno / Arctic Blast (srvdofunc 182) send PD2's calc3 missiles with range calc1."""
    casts = re.findall(r"channel: skill (\d+) level (\d+): (\d+) of (\d+) missiles \(row \d+\), range (\d+)", _log(ctx))
    if not casts:
        ctx.skip("no Inferno or Arctic Blast cast in the log yet (cast it out of town, then run again)")
    skill, level, created, count, rng = (int(v) for v in casts[0])
    want_count, want_range = _channel_values(skill, level)
    assert (count, rng) == (want_count, want_range), \
        f"skill {skill} level {level}: {count} missiles range {rng}, PD2's calcs give {want_count} range {want_range}"
    assert created == count, f"only {created} of {count} missiles created"
    return f"skill {skill} level {level}: {created} missiles, range {rng}"


@check("channel-client", tags=["core-rules-w21", "core-rules-w27"])
def channel_client(ctx):
    """The client half (cltdofunc 111) ran with the same count and range; it draws each flame with chance 2 / count."""
    casts = re.findall(r"channel client: skill (\d+) level (\d+): (\d+) of (\d+) flames drawn, range (\d+)", _log(ctx))
    if not casts:
        ctx.skip("no client channel cast in the log yet (cast it out of town, then run again)")
    skill, level, drawn, count, rng = (int(v) for v in casts[0])
    assert (count, rng) == _channel_values(skill, level), f"skill {skill} level {level}: {count} flames range {rng}"
    assert drawn <= count, f"{drawn} flames drawn of {count}"
    return f"skill {skill} level {level}: {drawn} of {count} flames drawn, range {rng}"


def _take_overs(ctx) -> list[tuple[int, int, int, int, str]]:
    """(current skill, its srvdo, requested skill, its srvdo, allowed / refused) per logged take-over."""
    return [(int(a), int(b), int(c), int(d), e) for a, b, c, d, e in re.findall(
        r"take-over: skill (\d+) \(srvdo (\d+)\) mid-channel, mode \d+ request skill (\d+) \(srvdo (\d+)\): (\w+)",
        _log(ctx))]


def _held(ctx) -> list[tuple[int, int, int]]:
    """(skill, srvdo, D2R's gate result) per logged re-request of the skill being held."""
    return [(int(a), int(b), int(c)) for a, b, c in re.findall(
        r"held: skill (\d+) \(srvdo (\d+)\) mode \d+ to D2R's gate: (-?\d+)", _log(ctx))]


@check("channel-take-over", tags=["core-rules-w21"])
def channel_take_over(ctx):
    """PD2's gate A: another skill requested mid-Inferno / Arctic Blast (srvdofunc 182) starts at once. The held skill
    itself goes to D2R's gate (D2R re-sends it every frame), so it never shows here."""
    rows = [r for r in _take_overs(ctx) if r[1] == 182]
    if not rows:
        ctx.skip("no take-over in the log yet (hold Inferno or Arctic Blast, press another skill, then run again)")
    assert all(r[4] == "allowed" and r[2] != r[0] for r in rows), f"take-overs mid-channel: {rows[:3]}"
    return f"skill {rows[0][0]} gave way to skill {rows[0][2]}"


@check("blade-fury-refused", tags=["core-rules-w21"])
def blade_fury_refused(ctx):
    """PD2's gate A: a Blade Fury (srvdofunc 48) is not cut short by another Blade Fury; another skill is allowed. The
    held Blade Fury's own re-request goes to D2R's gate, which must not allow it (0 would restart the channel)."""
    rows = [r for r in _take_overs(ctx) if r[1] == 48]
    held = [h for h in _held(ctx) if h[1] == 48]
    if not held:
        ctx.skip("no held Blade Fury in the log yet (hold Blade Fury out of town, then run again)")
    assert all(h[2] != 0 for h in held), f"D2R's gate let a held Blade Fury restart: {held[:3]}"
    assert all((r[4] == "refused") == (r[3] == 48) for r in rows), f"Blade Fury gate: {rows[:3]}"
    return (f"{len(held)} held re-requests to D2R's gate (results {sorted({h[2] for h in held})}), "
            f"{sum(r[4] == 'refused' for r in rows)} refused, {sum(r[4] == 'allowed' for r in rows)} allowed")


GOLEM_MASTERY_LEVEL = 10  # the W14 save's base level (setup above)


FIRESTORM = 225  # merged skill id (PD2's too)


def _wall_casts(ctx, prefix: str) -> tuple[int, int, int]:
    """(level, makers created, makers asked) of the first logged Firestorm cast on that side; PD2's calc1 is
    par1 + blvl / 5 with par1 3 (the check's character has no +skills, so blvl = level)."""
    casts = [tuple(int(v) for v in c[1:]) for c in re.findall(
        rf"{prefix}: skill (\d+) level (\d+): (\d+) of (-?\d+) makers", _log(ctx)) if int(c[0]) == FIRESTORM]
    if not casts:
        ctx.skip(f"no {prefix} line for Firestorm in the log yet (cast it out of town, then run again)")
    level, created, count = casts[0]
    want = 3 + level // 5
    assert count == want, f"{prefix}: Firestorm level {level}: {count} makers, PD2's calc1 gives {want}"
    assert created == count, f"{prefix}: only {created} of {count} makers created"
    return level, created, count


@check("wall-cast", tags=["core-rules-w22"])
def wall_cast(ctx):
    """Firestorm (srvdofunc 166, router 190) sends PD2's calc1 makers, all created."""
    level, created, _ = _wall_casts(ctx, "wall")
    return f"Firestorm level {level}: {created} makers"


@check("wall-client", tags=["core-rules-w22"])
def wall_client(ctx):
    """The client half (cltdofunc 119) draws the same makers."""
    level, created, _ = _wall_casts(ctx, "wall client")
    return f"Firestorm level {level}: client drew {created} makers"


@check("golem-count", tags=["core-rules-w14"])
def golem_count(ctx):
    """Golems follow PD2's count rule as data: petmax 1 + Golem Mastery blvl / 5 (+ extra_golem) caps Clay, Blood and Fire
    Golems together, and a new one replaces the oldest. Watches the client's monsters for 30 s while golems are
    summoned. The Iron Golem keeps its own pet type and does not take a place (docs/unported.md)."""
    rows = _rows("monstats")
    golems = {rows.index(n) for n in ("claygolem", "bloodgolem", "firegolem")}
    limit = 1 + GOLEM_MASTERY_LEVEL // 5
    ctx.ensure_running()
    game = missile_probe.Game(ctx.game.pids()[0])
    seen, most = set(), 0
    end = time.monotonic() + 30
    while time.monotonic() < end:
        alive = {g for g, row in game.units(missile_probe.MONSTER).items() if row in golems}
        seen |= alive
        most = max(most, len(alive))
        time.sleep(0.05)
    if len(seen) <= limit:
        ctx.skip(f"{len(seen)} golems summoned in 30 s, need {limit + 1} (summon Clay Golem while this runs)")
    assert most == limit, f"{most} golems at once after {len(seen)} summons, PD2 keeps {limit}"
    return f"{len(seen)} summons, at most {most} golems at once (Golem Mastery {GOLEM_MASTERY_LEVEL})"


@check("revive-event", tags=["core-rules-w14"])
def revive_event(ctx):
    """Revive's aura event (auraeventfunc 32) fired on the revived monster when it hit (core-rules registers Revive's
    events on it, D2R's Revive doesn't) and casts proc_SplashDamage (merged 430) at level 1 from it, as PD2 does."""
    events = re.findall(r"revive event: event \d+ skill (\d+) level \d+ on unit type (\d+) class \d+ owner (-?\d+)/\d+, "
                        r"target type (\d+): splash skill (\d+) cast (-?\d+)", _log(ctx))
    if not events:
        ctx.skip("no revive event in the log yet (Revive a corpse next to monsters, let it fight, then run again)")
    skill, unit_type, _, target_type, splash, cast = (int(v) for v in events[0])
    assert skill == REVIVE, f"event from skill {skill}, expected Revive ({REVIVE})"
    assert unit_type == 1, f"fired on unit type {unit_type}, not a revived monster"
    assert splash == PROC_SPLASH_DAMAGE, f"splash skill {splash}, expected proc_SplashDamage ({PROC_SPLASH_DAMAGE})"
    assert cast != 0, "the splash was refused (proc_SplashDamage's do function, W33)"
    return f"fired on a revive hitting unit type {target_type}, splash cast {cast}"


@check("melee-splash", tags=["core-rules-w33"])
def melee_splash(ctx):
    """A splash (proc_SplashDamage, Golem Splash, Skeleton Splash through the srvdo 190 router) hits the units within
    calc3 of the struck unit, the struck unit aside. proc_SplashDamage's calc3 is PD2's
    max(6 - lvl + inc_splash_radius / 20, 0)."""
    casts = re.findall(r"splash: skill (\d+) level (\d+) from unit type \d+ at unit type \d+: radius (-?\d+) "
                       r"\(inc_splash_radius (-?\d+)\), units hit (\d+)", _log(ctx))
    casts = [tuple(int(v) for v in c) for c in casts]
    if not casts:
        ctx.skip("no splash in the log yet (hit a pack with item_splashonhit, or Revive and let it fight, then run again)")
    for skill, level, radius, inc, _ in casts:
        if skill == PROC_SPLASH_DAMAGE:
            assert radius == max(6 - level + int(inc / 20), 0), f"level {level}, inc_splash_radius {inc}: radius {radius}"
    hits = [c for c in casts if c[4] > 0]
    if not hits:
        ctx.skip(f"{len(casts)} splashes logged, none with units in range (splash a pack, then run again)")
    skill, level, radius, inc, units = hits[0]
    return f"skill {skill} level {level}: radius {radius} (inc_splash_radius {inc}), {units} units hit"


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
    125 frames. Life is in 1/256. Every cast's list is new, extended (a recast at the same level keeps the running
    regen, as PD2's timed-state helper does) or refused (a lower level)."""
    log = _log(ctx)
    lists = re.findall(r"blood warp: skill 439 [^\n]*, list (\w+)\)", log)
    assert "none" not in lists, "a Blood Warp built no hpregen list"
    casts = re.findall(r"blood warp: skill 439 level (\d+): life (\d+), cost (\d+), regen (\d+) for (\d+) frames "
                       r"\(stat (\d+)", log)
    if not casts:
        ctx.skip("no Blood Warp in the log yet (cast it out of town, then run again)")
    level, life, cost, regen, frames, stat = (int(v) for v in casts[0])
    expected = max(int(12 / 100 * life), min((2 + (level - 1) * 2) * 256, 15360))
    assert cost == expected, f"level {level}, life {life}: cost {cost}, PD2 gives {expected}"
    assert cost < life, f"cost {cost} would kill at life {life}"
    assert (frames, stat) == (125, HPREGEN), f"{frames} frames of stat {stat}, PD2: 125 of hpregen ({HPREGEN})"
    assert regen == cost // 125 + 1, f"regen {regen}, PD2 gives {cost // 125 + 1}"
    return f"level {level}: life {life // 256}, cost {cost / 256:.1f} life, hpregen {regen} for {frames} frames, lists {sorted(set(lists))}"


@check("charge-hit", tags=["core-rules-w16"])
def charge_hit(ctx):
    """A Charge (srvstfunc 72, srvdofunc 180) ran at PD2's speed base * (max(velocitypercent, 50) + Param1 150) / 100,
    and its hit had PD2's calc1 damage% ln34 = 100 + (level - 1) * 20 (no Vigor or Might) and splash radius
    distance / 4 * 20 from where it started."""
    log = _log(ctx)
    starts = re.findall(rf"charge: skill {CHARGE} level (\d+) start: velocity (\d+) \(base (\d+), velocitypercent "
                        r"(-?\d+), param1 (\d+)\)", log)
    hits = re.findall(rf"charge: skill {CHARGE} level (\d+) hit frame: damage% (\d+), distance (\d+), splash radius (\d+)", log)
    if not starts or not hits:
        ctx.skip("no Charge start and hit in the log yet (charge a monster out of town, then run again)")
    _, velocity, base, percent, param1 = (int(v) for v in starts[0])
    assert param1 == 150, f"Param1 {param1}, PD2's Charge has 150"
    expected = base * (max(percent, 50) + param1) // 100
    assert velocity == expected, f"velocity {velocity}, PD2 gives {expected}"
    level, damage, distance, splash = (int(v) for v in hits[0])
    assert damage == 100 + (level - 1) * 20, f"level {level}: damage% {damage}, PD2's ln34 gives {100 + (level - 1) * 20}"
    assert splash == distance // 4 * 20, f"distance {distance}: splash radius {splash}, PD2 gives {distance // 4 * 20}"
    return f"velocity {velocity} (base {base}), level {level}: damage% {damage}, distance {distance} -> splash {splash}"


@check("teleport-cast", tags=["core-rules-w26"])
def teleport_cast(ctx):
    """A Teleport (router srvdofunc 190) gave teleportdebuff for auralencalc max(0, 25 - pvp_lld_cd) = 25 frames, each
    mastery lowered by reduction + mastery% of it (truncated), reduction = Param2 50 - calc1 min(edmn, 50), edmn the
    skill's EMin 0 + EMinLev1 1 per level after the first. Teleport has no srvmissilea, so no missiles."""
    casts = re.findall(r"teleport: skill 54 level (\d+): at -?\d+,-?\d+, (\d+) missiles [^\n]*state (-?\d+) for (\d+) "
                       r"frames \(list (\w+)\), reduction (-?\d+): fire (-?\d+)/(-?\d+) cold (-?\d+)/(-?\d+) "
                       r"ltng (-?\d+)/(-?\d+) pois (-?\d+)/(-?\d+)", _log(ctx))
    if not casts:
        ctx.skip("no Teleport in the log yet (Teleport out of town, then run again)")
    level, missiles, state, frames, lst, reduction, *pairs = casts[-1]
    level, missiles, state, frames, reduction = (int(v) for v in (level, missiles, state, frames, reduction))
    assert lst != "refused", f"level {level}: the debuff list was refused"
    assert missiles == 0, f"{missiles} missiles, PD2's Teleport makes none"
    assert (state, frames) == (TELEPORT_DEBUFF, 25), f"state {state} for {frames} frames, PD2: {TELEPORT_DEBUFF} for 25"
    expected = 50 - min(level - 1, 50)
    assert reduction == expected, f"level {level}: reduction {reduction}, PD2 gives {expected}"
    seen = []
    for name, debuff, mastery in zip(("fire", "cold", "ltng", "pois"), pairs[0::2], pairs[1::2]):
        want = -(reduction + int(int(mastery) * reduction / 100))
        assert int(debuff) == want, f"{name} mastery {mastery}: debuff {debuff}, PD2 gives {want}"
        seen.append(f"{name} {mastery} -> {debuff}")
    return f"level {level}, reduction {reduction}, {frames} frames, list {lst}: {', '.join(seen)}"


@check("gust-cast", tags=["core-rules-w23"])
def gust_cast(ctx):
    """A Gust (router srvdofunc 190) teleported and hit the units within calc3 par3 = 4 with cold length calc1 ln12 =
    12 (Param2 0 per level), the row's HitFlags 1, ResultFlags 5 and HitClass 2. Its srvmissilea (gust) is thrown only
    with a throwing weapon in hand: PD2's create call (1.13c 0xa28f0 with its last argument 1) checks the weapon's
    quantity (0xa1790), and so does D2R's ThrowSkillMissile, so either outcome is PD2's."""
    casts = re.findall(r"gust: skill 442 level (\d+): missile (-?\d+) (thrown|not thrown), radius (-?\d+) \(calc3\), "
                       r"cold length (-?\d+) \(calc1\), hit flags (\d+), result flags (\d+), hit class (-?\d+)",
                       _log(ctx))
    if not casts:
        ctx.skip("no Gust in the log yet (cast it out of town, then run again)")
    level, missile, thrown, *values = casts[-1]
    radius, chill, hit_flags, result_flags, hit_class = (int(v) for v in values)
    assert (radius, chill) == (4, 12), f"radius {radius}, cold length {chill}; PD2: 4, 12"
    flags = (hit_flags, result_flags, hit_class)
    assert flags == (1, 5, 2), f"hit flags, result flags, hit class {flags}; PD2: (1, 5, 2)"
    return f"level {level}: missile {missile} {thrown}, radius {radius}, cold length {chill}"


@check("hunger-hit", tags=["core-rules-w23"])
def hunger_hit(ctx):
    """A Hunger (router srvdofunc 190) ate its corpse and gave the hunger state for auralencalc 475 + lvl * 25 frames
    with lifedrainmindam par3 + blvl / 4 = 3 + lvl / 4 and velocitypercent par4 + lvl = 19 + lvl (no +skills, so blvl =
    lvl); item_openwounds (dm12 + 5) and deep_wounds (edmx) are logged."""
    casts = re.findall(r"hunger: skill 242 level (\d+): corpse eaten, state (-?\d+) for (-?\d+) frames \(list (\w+)\), "
                       r"stats (-?\d+)=(-?\d+) (-?\d+)=(-?\d+) (-?\d+)=(-?\d+) (-?\d+)=(-?\d+)", _log(ctx))
    if not casts:
        ctx.skip("no Hunger in the log yet (bear form, eat a corpse out of town, then run again)")
    level, state, frames, lst, *stats = casts[-1]
    level, state, frames = int(level), int(state), int(frames)
    pairs = dict(zip((int(s) for s in stats[0::2]), (int(v) for v in stats[1::2])))
    assert lst != "refused", f"level {level}: the hunger list was refused"
    want = 475 + level * 25
    assert (state, frames) == (HUNGER, want), f"state {state} for {frames} frames; PD2: {HUNGER} for {want}"
    assert pairs.get(LIFE_DRAIN) == 3 + level // 4, f"lifedrainmindam {pairs.get(LIFE_DRAIN)}; PD2: {3 + level // 4}"
    assert pairs.get(VELOCITY) == 19 + level, f"velocitypercent {pairs.get(VELOCITY)}; PD2: {19 + level}"
    return f"level {level}: {frames} frames, list {lst}, stats {pairs}"


@check("teleport-blocked-areas", tags=["core-rules-w26"])
def teleport_blocked_areas(ctx):
    """Teleport is refused where PD2 refuses it: the merged levels.txt Teleport column (0 refuses, 2 checks collision
    at the point), which D2R's Teleport (srvdo 27, inside the router's teleport) reads, equals PD2's for every level
    both have. PD2's map levels (Teleport 0 and 3) are not merged."""
    def column(path):
        with open(path, encoding="latin-1") as f:
            rows = [line.rstrip("\r\n").split("\t") for line in f]
        name, tele = rows[0].index("Name"), rows[0].index("Teleport")
        return {r[name]: r[tele] for r in rows[1:] if len(r) > tele}
    pd2 = column(os.path.join(REPO, "pd2-data", "data", "global", "excel", "levels.txt"))
    mod = column(os.path.join(REPO, "generated", "data", "global", "excel", "levels.txt"))
    shared = [n for n in mod if n in pd2]
    differ = [(n, pd2[n], mod[n]) for n in shared if pd2[n] != mod[n]]
    assert not differ, f"Teleport differs from PD2 (level, PD2, mod): {differ}"
    blocked = sorted(f"{n} ({mod[n]})" for n in shared if mod[n] not in ("1", ""))
    return f"{len(shared)} levels as in PD2; not plain: {', '.join(blocked)}"


CHARGE_UP = re.compile(r"charge up: skill (\d+) level \d+: shared (\d+), charges (\d+) -> (\d+), steal (\d+), "
                       r"prg (\d+) \(prgcalc (-?\d+)\)")
COBRA, RAKANOTH = 265, 630  # merged skill ids
# merged skill -> charge -> (srvprgfunc slot, PD2's prgcalc or None where it isn't checked), from PD2's skills.txt
FINISHER_PRG = {
    259: {1: (186, None), 2: (185, 64), 3: (184, 1)},  # Fists of Fire: PD2 39, 172, 171
    265: {1: (184, 7), 3: (185, 64)},  # Cobra Strike: 171 (par5), 172
    269: {2: (36, None), 3: (37, 4)},  # Claws of Thunder: vanilla, prgcalc3 par1
    274: {2: (181, None), 3: (143, 6)},  # Blades of Ice: 139, vanilla 143 (par2)
    280: {1: (40, None), 2: (143, None), 3: (41, 24)},  # Royal Strike: vanilla, prgcalc3 par5
    630: {1: (184, 8), 3: (185, 64)},  # RakanothStrike: 171 (par5), 172
}
CHARGE_MISSILE = {  # merged skill -> charge -> PD2's srvmissilea/b/c
    259: {2: "fistsoffirenova", 3: "fistsoffireorbdelay"},
    265: {1: "cobrastrikepoisonbolt", 3: "cobrastrikepoisoncloud"},
    274: {2: "boiwavemaker"},
    630: {1: "bladefragment1", 3: "fistsoffireexplode"},
}


def _logged(ctx, pattern: str, hint: str) -> list[tuple]:
    found = [tuple(int(v) for v in r) for r in re.findall(pattern, _log(ctx))]
    if not found:
        ctx.skip(f"no {hint} in the log yet (hit monsters with it three times out of town, then run again)")
    return found


@check("charge-up-hits", tags=["core-rules-w29"])
def charge_up_hits(ctx):
    """A charge-up hit (srvdofunc 190, PD2 srvdo 170) ends at the shared count + 1 (at most 3) and fires PD2's
    srvprgfunc for that charge with its prgcalc; Cobra Strike's steal goes on before a hit that would pass 2."""
    ups = _logged(ctx, CHARGE_UP.pattern, "charge-up skill")
    hits = [u for u in ups if u[3] > u[2]]
    if not hits:
        ctx.skip("no charge-up hit in the log yet, only misses (hit monsters, then run again)")
    for skill, shared, before, after, _, prg, calc in hits:
        assert after == min(shared + 1, 3), f"skill {skill}: shared {shared}, {before} -> {after} charges"
        slot, expected = FINISHER_PRG.get(skill, {}).get(after, (0, None))
        assert prg == slot, f"skill {skill} charge {after}: prg {prg}, PD2's maps to {slot}"
        assert expected is None or calc == expected, f"skill {skill} charge {after}: prgcalc {calc}, PD2 {expected}"
    for skill, shared, before, _, steal, _, _ in ups:
        want = int(skill in (COBRA, RAKANOTH) and before <= 1 and shared >= 1)
        assert steal == want, f"skill {skill}: shared {shared}, charges {before}: steal {steal}"
    return f"{len(hits)} hits, skills {sorted({h[0] for h in hits})}, prg {sorted({h[5] for h in hits})}"


@check("charge-spread", tags=["core-rules-w29"])
def charge_spread(ctx):
    """srvprgfunc 184 (PD2 171) throws prgcalc[charge] of the charge's missile: Cobra Strike 7, RakanothStrike 8 at
    charge 1, Fists of Fire 1 at charge 3; only Cobra Strike's carry the skill's poison."""
    rows = _rows("missiles")
    out = set()
    for skill, _, charge, created, count, row, calc, poison in _logged(
            ctx, r"charge spread: skill (\d+) level (\d+) charge (\d+): (\d+) of (\d+) missiles \(row (\d+), prgcalc "
                 r"(-?\d+)\), poison (-?\d+)", "Cobra Strike"):
        slot, expected = FINISHER_PRG[skill][charge]
        assert slot == 184 and calc == expected, f"skill {skill} charge {charge}: prgcalc {calc}, PD2 {expected}"
        assert count == min(max(calc, 1), 64), f"skill {skill}: {count} missiles for prgcalc {calc} (1-64)"
        assert created == count, f"skill {skill}: {created} of {count} missiles created"
        assert row == rows.index(CHARGE_MISSILE[skill][charge]), f"skill {skill} charge {charge}: missile row {row}"
        # PD2's Cobra Strike has no poison of its own (EMin empty): only items' poison rides along
        assert poison >= 0 if skill == COBRA else poison == 0, f"skill {skill}: poison {poison}"
        out.add(f"{skill}/{charge}: {count}")
    return ", ".join(sorted(out))


@check("charge-ring", tags=["core-rules-w29"])
def charge_ring(ctx):
    """srvprgfunc 185 (PD2 172) rings the target with 64 of the charge's missile (Fists of Fire's nova at charge 2,
    Cobra Strike's cloud at charge 3)."""
    rows = _rows("missiles")
    out = set()
    for skill, _, charge, created, row, calc in _logged(
            ctx, r"charge ring: skill (\d+) level (\d+) charge (\d+): (\d+) missiles \(row (\d+), prgcalc (-?\d+)\)",
            "Fists of Fire"):
        assert FINISHER_PRG[skill][charge] == (185, 64) and calc == 64, f"skill {skill} charge {charge}: prgcalc {calc}"
        assert row == rows.index(CHARGE_MISSILE[skill][charge]), f"skill {skill} charge {charge}: missile row {row}"
        assert created > 0, f"skill {skill}: no ring missile created"
        out.add(f"{skill}/{charge}: {created} of row {row}")
    return ", ".join(sorted(out))


@check("charge-fire", tags=["core-rules-w29"])
def charge_fire(ctx):
    """srvprgfunc 186 (PD2 39) runs vanilla srvdo 38 and 39 on Fists of Fire's first charge."""
    fires = _logged(ctx, r"charge fire: skill (\d+) level (\d+) charge (-?\d+): srvdo 38 (-?\d+), 39 (-?\d+)",
                    "Fists of Fire")
    for skill, _, charge, _, _ in fires:
        assert (skill, charge) == (259, 1), f"charge fire on skill {skill} charge {charge}"
    return f"{len(fires)} logged, srvdo 38/39 returned {fires[-1][3]}/{fires[-1][4]}"


@check("charge-cold", tags=["core-rules-w29"])
def charge_cold(ctx):
    """srvprgfunc 181 (PD2 139) throws the charge's missile: Blades of Ice's second charge throws boiwavemaker."""
    row = _rows("missiles").index("boiwavemaker")
    colds = _logged(ctx, r"charge cold: skill (\d+) level (\d+) charge (\d+): missile row (\d+)", "Blades of Ice")
    for skill, _, charge, missile in colds:
        assert (skill, charge, missile) == (274, 2, row), f"skill {skill} charge {charge}: row {missile}, PD2 {row}"
    return f"{len(colds)} logged, row {row}"


@check("holy-light-cast", tags=["core-rules-w17"])
def holy_light_cast(ctx):
    """Holy Light (srvdofunc 176) heals cltcalc2-cltcalc3: ln12 + Prayer blvl * par6 to ln34 + Prayer blvl * par7
    (Param1-4 30, 5, 50, 10; par6 5, par7 10), within aurarangecalc 6 of the target, and halves the side the target is
    not on: the heal on an enemy, the damage on an ally."""
    casts = re.findall(r"holy light: skill \d+ level (\d+) on an (enemy|ally): heal (\d+)-(\d+)( halved)?, "
                       r"damage( halved)?, radius (\d+): healed (\d+), hit (\d+)", _log(ctx))
    if not casts:
        ctx.skip("no Holy Light cast in the log yet (cast it on a monster out of town, then run again)")
    for level, side, lo, hi, half_heal, half_damage, radius, _, _ in casts:
        level, lo, hi = int(level), int(lo), int(hi)
        prayer = (lo - (30 + (level - 1) * 5)) / 5
        assert prayer >= 0 and prayer == int(prayer), f"level {level}: heal min {lo} is not ln12 + Prayer blvl * 5"
        assert hi == 50 + (level - 1) * 10 + int(prayer) * 10, f"level {level}, Prayer {int(prayer)}: heal max {hi}"
        assert int(radius) == 6, f"radius {radius}, PD2's aurarangecalc is 6"
        assert (bool(half_heal), bool(half_damage)) == (side == "enemy", side == "ally"), f"on an {side}: {casts[0]}"
    level, side, lo, hi, *_, healed, hit = casts[-1]
    return f"level {level} on an {side}: heal {lo}-{hi}, healed {healed}, hit {hit} ({len(casts)} casts)"


@check("holy-light-client-hit", tags=["core-rules-w17"])
def holy_light_client_hit(ctx):
    """The client drew holylightdelay (D2R's cltdo 36) and its hit ran PD2's chit 66 with radius cHitPar1 6; PD2's row
    names no CltHitSubMissile1, so the hit creates nothing more."""
    hits = re.findall(r"holy light client hit: missile row (\d+) skill \d+ level \d+: radius (\d+), sub-missile (-?\d+), "
                      r"(\d+) created", _log(ctx))
    if not hits:
        ctx.skip("no holylightdelay client hit in the log yet (cast Holy Light, then run again)")
    row, radius, sub, created = (int(v) for v in hits[0])
    assert _rows("missiles")[row].lower() == "holylightdelay", f"row {row} is {_rows('missiles')[row]}"
    assert radius == 6 and sub < 0 and created == 0, hits[0]
    return f"holylightdelay hit: radius {radius}, no sub-missile"


@check("fist-delay-hit", tags=["core-rules-w17"])
def fist_delay_hit(ctx):
    """Fist of the Heavens' delay hit (pCltHitFunc 71, PD2's 1.13c 26) sends a fistoftheheavensbolt from the hit to every
    unit within aurarangecalc 20 (cHitPar1 0), the struck unit included."""
    hits = re.findall(r"fist delay client hit: skill \d+ level (\d+): radius (\d+), sub-missile (\d+), (\d+) bolts",
                      _log(ctx))
    hits = [tuple(int(v) for v in h) for h in hits]
    if not hits:
        ctx.skip("no Fist of the Heavens hit in the log yet (cast it on a monster out of town, then run again)")
    level, radius, sub, bolts = hits[0]
    assert radius == 20, f"radius {radius}, PD2's aurarangecalc is 20"
    assert _rows("missiles")[sub].lower() == "fistoftheheavensbolt", f"sub-missile row {sub}"
    assert bolts >= 1, "no bolt, not even to the struck unit"
    return f"level {level}: radius {radius}, {bolts} bolts"


# merged skill -> charge -> (cltprgfunc slot, PD2's cltcalc, PD2's cltmissilea/b/c), from PD2's skills.txt
FINISHER_CLIENT = {
    259: {2: (128, 64, "fistsoffirenova"), 3: (127, 1, "fistsoffireorbdelay")},  # Fists of Fire: PD2 107, 106
    265: {1: (127, 7, "cobrastrikepoisonbolt"), 3: (128, 64, "cobrastrikepoisoncloud")},  # Cobra Strike: 106, 107
    630: {1: (127, 8, "bladefragment1")},  # RakanothStrike: 106 (par5)
}


@check("charge-client-spread", tags=["core-rules-w30"])
def charge_client_spread(ctx):
    """cltprgfunc 127 (PD2 106) draws cltcalc[charge] of the charge's client missile along the spread: Cobra Strike 7
    poison bolts at charge 1, Fists of Fire 1 orb at charge 3."""
    rows = [r.lower() for r in _rows("missiles")]
    out = set()
    for skill, _, charge, created, count, row, calc in _logged(
            ctx, r"charge spread client: skill (\d+) level (\d+) charge (\d+): (\d+) of (\d+) missiles \(row (\d+), "
                 r"cltcalc (-?\d+)\)", "Cobra Strike"):
        assert charge in FINISHER_CLIENT.get(skill, {}), f"skill {skill} charge {charge}: PD2 has no client prg here"
        slot, expected, missile = FINISHER_CLIENT[skill][charge]
        assert slot == 127 and calc == expected, f"skill {skill} charge {charge}: cltcalc {calc}, PD2 {expected}"
        assert count == min(max(calc, 1), 64) and created == count, f"skill {skill}: {created} of {count} for {calc}"
        assert row == rows.index(missile), f"skill {skill} charge {charge}: missile row {row}, PD2's {missile}"
        out.add(f"{skill}/{charge}: {created}")
    return ", ".join(sorted(out))


@check("charge-client-ring", tags=["core-rules-w30"])
def charge_client_ring(ctx):
    """cltprgfunc 128 (PD2 107) rings the target with the charge's client missile (Fists of Fire's nova at charge 2,
    Cobra Strike's cloud at charge 3), cltcalc 64."""
    rows = [r.lower() for r in _rows("missiles")]
    out = set()
    for skill, _, charge, created, row, calc in _logged(
            ctx, r"charge ring client: skill (\d+) level (\d+) charge (\d+): (\d+) missiles \(row (\d+), cltcalc "
                 r"(-?\d+)\)", "Fists of Fire"):
        assert charge in FINISHER_CLIENT.get(skill, {}), f"skill {skill} charge {charge}: PD2 has no client prg here"
        slot, expected, missile = FINISHER_CLIENT[skill][charge]
        assert slot == 128 and calc == expected, f"skill {skill} charge {charge}: cltcalc {calc}, PD2 {expected}"
        assert row == rows.index(missile), f"skill {skill} charge {charge}: missile row {row}, PD2's {missile}"
        assert created > 0, f"skill {skill}: no ring missile drawn"
        out.add(f"{skill}/{charge}: {created} of row {row}")
    return ", ".join(sorted(out))


@check("charge-client-explode", tags=["core-rules-w30"])
def charge_client_explode(ctx):
    """pCltDoFunc 79 (PD2 69) places CltSubMissile1 on the explosion's first frame: fistsoffireexplode's
    fistsoffirefirewall (Fists of Fire's first charge); bladesoficecubes' bladesoficeexplode if anything makes it."""
    rows = [r.lower() for r in _rows("missiles")]
    subs = {"fistsoffireexplode": "fistsoffirefirewall", "bladesoficecubes": "bladesoficeexplode"}
    found = re.findall(r"charge explode client: missile row (\d+) skill -?\d+ level \d+: sub-missile (-?\d+) at "
                       r"\d+,\d+( \(not created\))?", _log(ctx))
    if not found:
        ctx.skip("no Fists of Fire explosion in the log yet (charge it on monsters, then run again)")
    for row, sub, failed in found:
        name = rows[int(row)]
        assert name in subs and rows[int(sub)] == subs[name], f"{name}: sub-missile {rows[int(sub)]}"
        assert not failed, f"{name}: {subs[name]} not created"
    return ", ".join(sorted({rows[int(r)] for r, _, _ in found}))


@check("charge-client-bolt", tags=["core-rules-w30"])
def charge_client_bolt(ctx):
    """pCltHitFunc 67 drops clawsofthunderbolt's cotthunderstorm on the unit hit, raised CHitCalc1 280 and falling
    cHitPar1-3 (0, 0, -81920) per frame."""
    rows = [r.lower() for r in _rows("missiles")]
    found = re.findall(r"charge bolt client hit: missile row (\d+) skill -?\d+ level \d+: sub-missile (-?\d+) at "
                       r"\d+,\d+, height (-?\d+), motion (-?\d+),(-?\d+),(-?\d+)( \(not created\))?", _log(ctx))
    if not found:
        ctx.skip("no Claws of Thunder bolt hit in the log yet (hit monsters with its third charge, then run again)")
    row, sub, z, mx, my, mz, failed = found[0]
    assert rows[int(row)] == "clawsofthunderbolt" and rows[int(sub)] == "cotthunderstorm", found[0]
    assert (int(z), int(mx), int(my), int(mz)) == (280, 0, 0, -81920) and not failed, found[0]
    return f"{len(found)} logged: height {z}, motion {mx},{my},{mz}"


@check("core-rules-no-crash", tags=["core-rules", "core-rules-w10", "core-rules-w6", "core-rules-w11", "core-rules-w12",
                                    "core-rules-w13", "core-rules-w14", "core-rules-w16", "core-rules-w26", "core-rules-w21",
                                    "core-rules-w22", "core-rules-w29", "core-rules-w17",
                                    "core-rules-w23", "core-rules-w30"])
def core_rules_no_crash(ctx):
    """No crash file since the run began and no FAULT line in the plugin log."""
    new = ctx.game.new_crash_files(ctx.shared.get("crash_before", []))
    assert not new, f"new crash files: {new}"
    faults = re.findall(rf"{PLUGIN}: FAULT[^\n]*", _log(ctx))
    assert not faults, faults[0]
    return "no crash"


def static_field_damage(life: int, max_life: int, calc1: int, calc2: int, field_min: int, resist: int) -> int:
    """PD2's per-unit damage (ProjectDiablo.dll 0x10302830), in life's 1/256, read from its disassembly: calc1% of
    current life, at least calc2, or (100 - StaticFieldMin)% of max life when the resisted damage beats that share
    (resisted too, never raised)."""
    def muldiv(a, b, c):
        return int(a * b / max(c, 1))
    cap = muldiv(max_life, (100 - field_min) & 0xffffffff, 100)
    damage = muldiv(life, calc1, 100)
    limit = min(muldiv(cap, 100 - resist, 100), cap)
    out = calc2 if (damage & 0xffffffff) < (calc2 & 0xffffffff) else damage
    return cap if muldiv(damage, 100 - resist, 100) > limit else out


STATIC_FIELD_CAST = re.compile(
    rf"static field: skill {STATIC_FIELD} level (\d+): radius (-?\d+), calc1 (-?\d+), calc2 (-?\d+), min (-?\d+); units hit (\d+), "
    r"first: life (-?\d+) of (-?\d+), lightresist (-?\d+), damage (-?\d+); state (-?\d+) for (-?\d+) frames, "
    r"stat (-?\d+) (-?\d+), new lists (\d+) \(first (-?\d+)\)")


@check("static-field-cast", tags=["core-rules-w27"])
def static_field_cast(ctx):
    """A Static Field (router srvdofunc 190) hit the units within PD2's aurarangecalc min(ln12 / 2, 14) (ln12 = Param1 8
    + Param2 1 per level after the first) for PD2's damage (static_field_damage, calc1 Param4 8, calc2 Param3 0,
    StaticFieldMin 55/70/85), then put staticfield_debuff on them for auralencalc 125 + 5 * Lightning Mastery frames
    with lightresist from aurastatcalc1. PD2's -min((par5+(lvl<22)?lvl:...), 100): the calc parser binds ?: tighter
    than +, so it is -min(par5 + (lvl or 22 + (lvl-22)/2), 100), Param5 4 (-14 at level 10 in game); a monster immune
    to lightning gets half."""
    casts = [tuple(int(v) for v in c) for c in STATIC_FIELD_CAST.findall(_log(ctx))]
    hit = [c for c in casts if c[5] > 0]
    if not hit:
        ctx.skip("no Static Field that hit a unit in the log yet (cast it next to monsters out of town, then run again)")
    (level, radius, calc1, calc2, field_min, units, life, max_life, resist, damage, state, frames, stat, value, lists,
     first) = hit[0]
    assert radius == min((8 + level - 1) // 2, 14), f"level {level}: radius {radius}"
    assert (calc1, calc2) == (8, 0), f"calc1 {calc1}, calc2 {calc2}, PD2: 8, 0"
    assert field_min in STATIC_FIELD_MIN, f"StaticFieldMin {field_min}, PD2's are {STATIC_FIELD_MIN}"
    want = static_field_damage(life, max_life, calc1, calc2, field_min, resist)
    assert damage == want, f"life {life} of {max_life}, resist {resist}: damage {damage}, PD2 gives {want}"
    pd2 = -min(4 + (level if level < 22 else 22 + (level - 22) // 2), 100)
    assert (state, stat, value) == (STATICFIELD_DEBUFF, LIGHTRESIST, pd2), \
        f"state {state} stat {stat} {value}, PD2: {STATICFIELD_DEBUFF} {LIGHTRESIST} {pd2}"
    assert frames >= 125 and (frames - 125) % 5 == 0, f"{frames} frames, PD2: 125 + 5 per Lightning Mastery level"
    if lists:
        assert first in (value, int(value / 2)), f"first new list's lightresist {first}, PD2: {value} or half"
    return (f"level {level}: radius {radius}, {units} unit(s) hit, first {life >> 8}/{max_life >> 8} life, resist "
            f"{resist}: {damage >> 8} damage; debuff {value} for {frames} frames on {lists} new list(s)")


def _missile_row(path: str, name: str) -> dict[str, str]:
    with open(path, encoding="latin-1") as f:
        rows = [line.rstrip("\r\n").split("\t") for line in f]
    return next(dict(zip(rows[0], r)) for r in rows[1:] if r and r[0] == name)


@check("combustion-cast", tags=["core-rules-w27"])
def combustion_cast(ctx):
    """A Combustion (router srvdofunc 190, cltdofunc 120) made PD2's ring of calc2 = Param1 32 combustion missiles
    around the caster on the server and the client; the ring's radius is the missile's Vel and Range, PD2's."""
    text = _log(ctx)
    casts = re.findall(rf"nova: skill {COMBUSTION} level (\d+): (\d+) missiles \(row (\d+), calc2 (-?\d+)\)", text)
    clients = re.findall(rf"nova client: skill {COMBUSTION} level (\d+): (\d+) missiles \(row (\d+), calc2 (-?\d+)\)",
                         text)
    if not casts or not clients:
        ctx.skip("no Combustion (server and client) in the log yet (cast it out of town, then run again)")
    for kind, (level, created, row, count) in (("server", casts[0]), ("client", clients[0])):
        assert (int(count), int(created)) == (32, 32), f"{kind}: {created} missiles, calc2 {count}; PD2: 32 of 32"
        assert _rows("missiles")[int(row)] == "combustion", f"{kind}: missile row {row} is {_rows('missiles')[int(row)]}"
    excel = ("data", "global", "excel", "missiles.txt")
    pd2 = _missile_row(os.path.join(REPO, "pd2-data", *excel), "combustion")
    mod = _missile_row(os.path.join(REPO, "generated", *excel), "combustion")
    for col in ("Vel", "MaxVel", "VelLev", "Range"):  # D2R has no LevRange (unported list)
        assert pd2[col] == mod[col], f"combustion {col} {mod[col]}, PD2's {pd2[col]}"
    return f"level {casts[0][0]}: 32 missiles on server and client, Vel {mod['Vel']}, Range {mod['Range']}"
