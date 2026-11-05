# [IR3](https://github.com/synpoox/pd2-resurrected/issues/238): combat item stats

Work item [IR3](https://github.com/synpoox/pd2-resurrected/issues/238) ([#238](https://github.com/synpoox/pd2-resurrected/issues/238)), design in [item-rules.md](../item-rules.md).
PD2 side from `ProjectDiablo.dll` (VAs `0x10...`) in Ghidra; D2R side from the 93847 dump (RVAs). PD2's stat ids are
PD2's; the merged ids are in `merge/ids-itemstatcost.tsv`.

## Where PD2 reads the stats

A scan of `ProjectDiablo.dll` for each stat id pushed as a constant:

| Stat (PD2 id) | Readers |
|---|---|
| `item_crit_chance` (258), `item_maxdeadlystrike` (210) | `0x10304710` (the roll) |
| `item_crit_multiplier` (256), `item_ds_multiplier` (257) | `0x103047f0` (the multiplier) |
| `item_crushingblow_efficiency` (268) | `0x103469a0` (event 16, [w19](w19-holy-sword-smite.md)), `0x102b3c70` |
| `openwounds_stack` (189), `deep_wounds` (501) | `0x103463f0` (open wounds event, from the event fill `0x1036c2b0`), `0x102fe6a0`, `0x10376560` |
| `lifedrain_percentcap` (488) | `0x102fbc50` (`0x10296830` is a UI constant) |
| `inc_splash_radius_permissinghp` (489) | `0x102fa640` (`0x1036c410` is a constant) |
| `item_dmgpercent_pereth` (484) | `0x102fa930` |
| `equipped_eth` (183) | `0x102fa640`; `0x103501b0` is the client's character screen |
| `item_dmgpercent_permissinghppercent` (487) | none (no carrier, as [#148](https://github.com/synpoox/pd2-resurrected/issues/148) found) |

## Critical and deadly strike

PD2's roll `0x10304710 (weapon, unit, x)` (disassembled):

1. Crit = the weapon mastery's crit (`0x103065d0 (unit, weapon, x, 2)`, only with a weapon) + `passive_critical_strike`
   (337) + `item_crit_chance` (258). Above 0: at most 75; `rand(0, 100) <` crit returns 1.
2. Else deadly strike = `item_deadlystrike` (141), at most 75 + `item_maxdeadlystrike` (210); a hit returns 2.

PD2's multiplier `0x103047f0 (unit, damage, x)`: on 1, physical (1.13c damage `+8`) times 200 + `item_crit_multiplier`
(256) percent; on 2, 150 + `item_ds_multiplier` (257). Each multiplier adds the stat's per-hand item sum
(`0x10381720`, [w19](w19-holy-sword-smite.md)). Result flag `0x2000`; `0x1037c4c0` does `phys * mult / max(100, 1)` in
doubles, saturated at `0x7fffffff`. Callers: PD2's melee splash (`0x10303160`) and missile hit (`0x103045a0`). PD2's
missile fill `0x102fe730` (its copy of D2Common's missile fill) rolls `0x10304710` on the owner.

D2R rolls in two places, each uncapped and each doubling physical:

- `WeaponDamage` (`0x44c030`): after the physical roll (stored at damage `+0x18`), the mastery crit (`0x33d4f0 (unit,
  weapon, 0, 2)`, only when its a5 is 0), `passive_critical_strike`, then `item_deadlystrike`; any success doubles
  `+0x18` and sets result `0x2000`. The block is `0x44c2b4`-`0x44c3d9`; attacker in RSI, damage in RDI, a5 in R14D.
  Its tail changes physical again (the `+0x149` element conversion, `DamageStep`), so a wrapper cannot redo the roll
  after it returns.
- `0x3ba820 (unit, weapon) -> bool`, the missile roll (callers: D2Common's missile fill `0x3bf9b0`, the server's
  `0x432f90` / `0x4333f0`): the same three rolls. The hit then doubles a missile's physical part when it carries the
  roll (`0x465420`, `0x465d60`).

core-rules (`item_rules.cpp`):

- Replaces the first 28 bytes of `WeaponDamage`'s block (byte-checked, nothing outside jumps into them) with a call
  to `WeaponStrike (attacker, damage, a5)` and a jump to `0x44c3d9`: PD2's roll and multiplier. The volatile
  registers the block left are set again after `0x44c3d9`; R15D (0) is callee-saved.
- Hooks `0x3ba820` with PD2's roll. A missile's hit still doubles (D2R's): PD2's 1.5x deadly strike and the
  multiplier stats are not applied to missiles (approximated).
- Uses the unit's total multiplier stat, not PD2's per-hand sum: the two differ only when dual wielding two
  multiplier weapons.

Logs: `strike: <weapon|missile> unit type T kind K, crit C roll R, deadly D (cap X) roll R2, physical A -> B (xM%)`,
every player roll and a sample of the others.

## Op bases: enhanced damage per ethereal item and per missing life

`item_dmgpercent_pereth` and `item_dmgpercent_permissinghppercent` are op 4 on `equipped_eth` and `missing_hp`, which
nothing sets. PD2's stat evaluator `0x102faa90` (its copy of D2Common's) reads an op base on the owner through
`0x102fa640 (statlist, stat << 16, record)`:

- `equipped_eth`: the owner's (player or monster) worn items (`0x10382bd0`: bodyloc not 0, 11 or 12) that are ethereal
  (D2Common #10225).
- `missing_hp`: `(1 - life / maxlife) * 100` (stats 6 and 7), 0 without max life.
- `inc_splash_radius` (478): + `missing_hp` * `inc_splash_radius_permissinghp` (489), at most 40, when both are above 0.
  Only an op base reaches it, and no stat has `inc_splash_radius` as its op base, so PD2 never applies Nightmare's
  Feast's splash per missing life. Nothing to port.
- any other stat: D2Common's read (`0x103a27e0`).

PD2 also hooks an item stat-change callback (`0x102fa930`) that sets `item_dmgpercent_pereth` again on each worn item
when an item of a player changes, so the cached op values follow equipment changes.

D2R reads an op base through `0x2f9b10 (statlist, stat << 16 | layer, record)` (from `0x2f8ad0` and `0x2fa430`; the
statlist's unit at `+0xa0`). core-rules hooks it: `equipped_eth` counts the unit's bodylocs 1-10 (`0x3886d0`) holding an
ethereal item (`0x3740a0`), `missing_hp` as PD2, the rest D2R's read. Not ported: PD2's refresh callback. D2R caches op
values, so whether an item's damage follows a later equipment change is not checked.

## Life steal cap (Nightmare's Feast)

PD2 replaces D2Game's add-life (1.13c `0x6fcfb570`, its calls at `0xdbb80` in the leech and `0xde23e`) with
`0x10306ca0 (unit, amount)`: life + amount up to the max, unless dead or in state 92, set through `0x102fbc50`. With
`lifedrain_percentcap` C, that sets life only when the new life or the old one is under `(100 - C) / 100` of max life
(stat 7). Nightmare's Feast has C = 25: life steal stops above 75% life.

D2R's add-life is `0x451820 (unit, amount)`, called by the leech `0x450c90` (the attacker) and the hit's absorb heal in
`0x44ce80` (the victim). core-rules hooks it with PD2's test.

## Crushing blow

PD2's event 16 (`0x103469a0`), disassembled; constants from the DLL:

- Divisor: 10 on a player; on a monster, 10 for a hireling (D2Common #11104: class 271, 338, 359, 560, 561), else 8,
  except a prime evil (monstats flag `0x80`, #10278): 30 when in PD2's boss sets (`0x10371290`, filled at run time),
  else `(100 - life %) * 10 + 70` (#11027). A non-hireling monster then adds `0xaf5f0(monster_playercount) / 50`
  (D2Game's table, 0 for 1 player and +1 for each player after it, 2 less from 9). Other units: 8. Missile (event 6):
  times 2 on levels 157, 159 and 166 (PD2 levels, not in the merged data), else times 1.5.
- Blow = life / (divisor / (`item_crushingblow_efficiency` / 100 + effectiveness)); effectiveness 1, or 1 + calc4 /
  100 on Smite. Less `damageresist` (36) percent (`0x1037c4c0`), nothing at 100 or more. Life set to life - blow (at
  least 0), kill flag (`damage + 4 |= 2`) at 0, overlay `0x93` (147), PD2's damage meter.

D2R's event 16 (`0x583150`) rolls and calls `0x581bd0 (game, unit, target, {damage, 4, 10, 10, missile})`, whose
entry is `jmp [0x3e2a448]`. core-rules writes `CrushingBlow` into that qword: PD2's blow for every caller (D2R's
event and [W19](https://github.com/synpoox/pd2-resurrected/issues/94)'s Smite roll). Not ported: Smite's calc4, PD2's boss sets, the per-hand sum of the efficiency.

## Open and deep wounds

PD2's open wounds event (`0x103463f0`) rolls the stat, then builds a timed hit-point regen list (state 62) from the
attacker's level, `deep_wounds` (501) and `openwounds_stack` (189, up to 3 stacks). D2R's event 15 (`0x584170`) is
D2R's own (damage from level, quartered or halved on players, state 62). Not ported: the stacking and deep wounds parts
are not decoded yet, and the game check below found no item event reaching D2R's functions on a player's melee hit.

## Game check

Slot 8, Hell, level 90 Paladin, Blood Moor zombies; stats set with `cheat stat`. Every patch installs (byte checks
pass; `items 7/7`).

- Critical and deadly strike, Attack (srvdo 1): `item_crit_chance` 30, `item_crit_multiplier` 50,
  `item_deadlystrike` 90, `item_maxdeadlystrike` 10, `item_ds_multiplier` 25. Rolls under 30 crit at x250%;
  otherwise deadly strike, capped at 85, at x175%; a crit skips the deadly strike roll. Monster hits run through it
  with chance 0. Sacrifice (srvdo 64) builds its hit with damage flag 1, which skips WeaponDamage's roll block, so
  neither D2R nor core-rules rolls a strike on it.
- Life steal cap, `lifedrainmindam` 30 and `lifedrain_percentcap` 25: the hook runs on each of the player's steals,
  and at full life (god mode) all 50 were blocked. A steal added below 75% life was not seen.
- Crushing blow: with Goblin Toe (25%) and `item_crushingblow` 40, no blow was logged on the player's Attack hits.
  `cheat stat` adds no item event listener, and the boots' durability icon showed red, so this does not yet show
  whether D2R's event 16 runs on a player's hit.
- Enhanced damage per ethereal item: not checked in game (no ethereal carrier spawned).
- Open wounds: a probe on `0x584170` logged nothing with `item_openwounds` 50 (stat only, no listener).
- Spawn commands: `cheat uniques` rows past the "Expansion" separator row are one lower than the merged table's
  (Fleshripper 304, The Gladiator's Bane 250); Attack is the bottom-left icon of the left skill picker.

## Open

- Open and deep wounds: decode the rest of `0x103463f0` and port it.
- Crushing blow and open wounds on a player's hit: check with a fresh crushing-blow item whether item events reach
  D2R's event functions ([#165](https://github.com/synpoox/pd2-resurrected/issues/165)).
