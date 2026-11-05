# Amazon group (W6): shit 63, dmg 17, cltdo 22, chit 12

Answers work item W6 (#53). PD2 side from the baseline install `C:\Games\PD2\ProjectD2` (1.13c `D2Game.dll` /
`D2Client.dll` plus `ProjectDiablo.dll`), D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). Addresses are RVAs
(ProjectDiablo.dll: offsets from its 0x10000000 base).

## Which functions PD2 really runs

A static scan of ProjectDiablo.dll finds no writes to the missile tables, but the DLL fills slots at run time. So the
tables were read from a running PD2 `Game.exe` (memory dump of D2Game `0x10da48` srvhit, `0x10db68` srvdmg, D2Client
clthit and cltdo):

| Slot | PD2 at run time | D2R 93847 | Result |
|---|---|---|---|
| srvhit 63 | ProjectDiablo `0x3a8800` | empty | port into 63 |
| srvdmg 17 | ProjectDiablo `0x3a6ea0` | `0x4549f0` (ringoffire) | port into 20 |
| cltdo 22 | D2Client `0x15660` (vanilla) | `0x2aed10` | same plus a check: 1-byte patch |
| clthit 12 | D2Client `0xbb070` (vanilla) | `0x1acda0` | same: data only |

The same dump shows PD2 also replaces vanilla srvhit 1, 6, 7, 30, 34, 50, 52, 60 and clthit 5, 6 (not part of W6).
srvhit 5 and clthit 4 are vanilla, which agrees with [nova-orb-hits](nova-orb-hits.md).

## chit 12 (Immolation, Blaze, Rathma fire client hits)

D2R `0x1acda0` has 1.13c `0xbb070`'s structure: missile record fields moved by +0x24, the fallback length is the calc at
record `+0xbc` evaluated at level - 1. Data only. Not compared: its helper `0xb9740` against D2R `0x1b8b00`.

## cltdo 22 (Power Strike, Lightning Strike client half)

D2R `0x2aed10` is 1.13c `0x15660` with one addition at the start: it looks up the local player (`0x8b2d0`, `0x9a480`)
and returns 1 without a missile unless the caster is that player (`cmp rbp, rax; je` at `0x2aed4a`, bytes `74 0a`). The
strike missiles (powerstrikenova1, lightningstrike, powerstrikenova1valk) have no `ClientSend`, so in D2R another
player's, a Valkyrie's or a boss's strike draws nothing. core-rules writes `eb` (jmp) at `0x2aed4a`
(`rva::CltDoStrikeLocalOnly`), with `74` expected.

## dmg 17 (Multiple Shot arrows and bolts)

PD2 `0x3a6ea0 (game, missile, target, damage)`:

- n = stat pierce_count (PD2 id 469) of the missile; return if n - 1 <= 0 or there is no owner.
- step = DmgCalc1 of the missile row; percent = max(dParam1, 100 - (n - 1) * step).
- Physical, fire, lightning, magic, cold, poison and the three leeches become value * percent / 100 (capped).

PD2's pierce hook `0x3027a0` adds 1 to pierce_count each time a missile pierces. Multiple Shot's rows have DmgCalc1 `20`
and dParam1 20: each further unit takes 20% less, down to 20%.

D2R: the damage block (built by `0x465d60`) holds physical `+0x18`, fire `+0x20`, lightning `+0x2c`, magic `+0x30`,
cold `+0x34`, poison `+0x38`, leeches `+0x120/+0x124/+0x128`; the missile record has dParam1 `+0x9c` and DmgCalc1 `+0xb4`
(D2R dmg 1 `0x453490` evaluates it with `0x3b4a90(dataFlag, missile, owner, calc, -1, -1)`). D2R's dmg table has 31
slots, 18-30 empty; ours takes 20 so PD2's own 18 and 19 keep their numbers.

Deviation (approximated, on the deviation list): without PD2's pierce hook, core-rules counts hits on pierce_count
(merged id 522) inside the function. Every hit but the last matches; the last, not piercing hit takes one step less.

## shit 63 (Fire Arrow, Exploding Arrow)

PD2 `0x3a8800 (game, missile, hit)`:

1. HitSubMissile1-4 at the missile, as D2R's srvhit 4 does.
2. Only on a unit hit, with the skill of the missile row (missiles.txt Skill: 1.13c `+0x194`, D2R `+0x1b6`) and the missile's level, from the hit point H:
   - n = unit vector from H toward the owner, side = (-n.y, n.x);
   - base = H - n * calc3; corners = base +- side * Param4 / 2 (a cone past the target);
   - calc2 explosions; explosion i lands in a random point of the triangle H/corners between fractions (i/2)/(calc2/2)
     and (i/2+1)/(calc2/2) of the way out (`0x3813a0`), one pair per band;
   - a timer fires each at frame + calc4 + 2, + 4, ... (`0x3164f0`, callback `0x3044b0`), which creates Exploding
     Arrow's (skill 16) srvmissile a and b at that point with flags 1, and sends b to clients then removes it.

PD2's data: Exploding Arrow calc2 min(par2 + lvl/5, 8) (3 to 8), calc3 par3 = 10, calc4 par5 = 2, Param4 25,
srvmissilea explodingarrowexp3, srvmissileb fireexplosion3. Fire Arrow has no calc2: sub-missiles only.

Skills record offsets (1.13c / D2R): calc1-4 `0x138-0x144` / `0x190-0x19c`, Param1 `0x148` / `0x1b8`, Param4 `0x154` /
`0x1c4` (D2R field table `0x305e68`).

Deviation (approximated, on the deviation list): core-rules creates all explosions at the hit with
MissileParams `activation` set to the delay instead of queueing timers, rolls the points on the owner's seed, and lets
fireexplosion3 play out.

## Checks

- `tests/game/core_rules_checks.py`, tag `core-rules-w6` (Slot5): status counters and the plugin log.
