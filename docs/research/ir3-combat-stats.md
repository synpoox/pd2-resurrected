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
| `openwounds_stack` (189), `deep_wounds` (501) | `0x103463f0` (open wounds event, from the event fill `0x1036c2b0`); `0x102fe6a0` and `0x10376560` push 189 as other ids |
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
(256, D2Common #10910: layer 0) + its per-hand sum percent; on 2, 150 + `item_ds_multiplier` (257) + its per-hand sum.
Result flag `0x2000`; `0x1037c4c0` does `phys * mult / max(100, 1)` in doubles, saturated at `0x7fffffff`.

The per-hand sum `0x10381720 (unit, stat)` ([w35](w35-melee-splash-extras.md)) adds the stat's item-type layers that
the wielded weapon (#10061) is of, layer 0 left out: Javelin and Spear Mastery puts `item_crit_multiplier` on `spea`,
Two Hand Mastery `item_crushingblow` and `item_crushingblow_efficiency` on `2han`. 2han (PD2 row 114) counts only when
the other hand (#11139 at the other of bodylocs 4 and 5) is empty or the same item; 1han (116) only when it is not the
same item, or, for a dual-wield class (#10747), a 2han weapon beside another item.

Missiles. `0x103047f0`'s only live caller is `0x10303160`, from PD2's damage builder `0x103022b0`. The missile hit
`0x103045a0` (re-rolls on the owner at the hit, for a skill with SrcDam or Twister, Shock Wave, Tornado) and the gated
missile roll `0x103046a0` have no caller, no pointer in the image and no pointer table entry: dead code. The live
missile fill `0x102fe730` (its copy of D2Common's, in a hook table at `0x104a06f4`) rolls `0x10304710` on the owner
(the first call with no weapon) and sets missile flag 2, which 1.13c's hit doubles. So PD2's missile crit or deadly
strike is 2x, with no multiplier stats.

Sacrifice: PD2's srvdo 64 (`0x103aff30`, from the table fill `0x1036ba20`) runs 1.13c's hit through a resolved pointer
(`0x103a1b60` -> `[0x105e06b8]`) and its own self-damage block (flag `0x1000`); no PD2 strike code is on the path, so
Sacrifice rolls no strike in PD2 either.

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
- Hooks `0x3ba820` with PD2's roll. A missile's hit doubles (D2R's), as PD2's live code does.
- `HandStatSum` ports the per-hand sum (D2R's `UnitWeapon`, `ItemIsType` on each layer, bodylocs 4 and 5); the
  multipliers add it. Not ported: the dual-wield 2han case of the 1han layer, which none of these stats has.

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
statlist's unit at `+0xa0`). D2RCore replaces D2R's stat getters (`0x2f5020` is `jmp [0x3e2a218]`), so that read is
never called: a hook on it logged 0 calls ([#446](https://github.com/synpoox/pd2-resurrected/issues/446)). D2RCore's
op 4 reads the real base stat (`cheat stat equipped_eth,2` gave Purgatory +120%). So core-rules keeps both as base
stats: `equipped_eth` (the unit's bodylocs 1-10, `0x3886d0`, holding an ethereal item, `0x3740a0`) is recounted at the
next game update after a durability callback (the item being taken off is still in its slot during it), `missing_hp`
on each life or max-life callback. The server never sends them, so a player's values are also set on its client unit.

D2R keeps each op's result in a stat list's full stats: `0x2fa430` recomputes a stat and walks the stats its record
lists as op targets (`+0x54`) only when a stat of that list changes. A new op base on the owner leaves a worn item's
op value as it was at equip time ([#320](https://github.com/synpoox/pd2-resurrected/issues/320)), and PD2's own
refresh `0x102fa930` looks like a send of the item's stat to the client (a D2Game call given PlayerData `+0x9c`): PD2
evaluates ops on read (`0x102faa90`). So after each count change core-rules sets `item_dmgpercent_pereth` (or
`..._permissinghppercent`) one higher and back on the base list of each worn item that holds it;
D2R then recomputes `item_maxdamage_percent` / `item_mindamage_percent` with the new base. Seen (Slot 2): the player's
`item_maxdamage_percent` 0 -> 60 -> 120 -> 60. An op stat set on the player's own list (`cheat stat`) adds nothing to
`item_maxdamage_percent`, at any base: test these stats on an item.

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
- Blow = life / (divisor / (efficiency / 100 + effectiveness)); efficiency = `item_crushingblow_efficiency` + its
  per-hand sum; effectiveness 1, or 1 + calc4 / 100 when the unit's current skill (`0x1030dd00`) is Smite (97),
  calc4 evaluated at the unit skill's `+0x28` (hard points). The chance adds the per-hand sum of `item_crushingblow`
  (136). Less `damageresist` (36) percent (`0x1037c4c0`), nothing at 100 or more. Life set to life - blow (at
  least 0), kill flag (`damage + 4 |= 2`) at 0, overlay `0x93` (147), PD2's damage meter.

D2R's event 16 (`0x583150`) rolls and calls `0x581bd0 (game, unit, target, {damage, 4, 10, 10, missile})`, whose
entry is `jmp [0x3e2a448]`. core-rules writes `CrushingBlow` into that qword: PD2's blow for every caller (D2R's
event and [W19](https://github.com/synpoox/pd2-resurrected/issues/94)'s Smite roll), with Smite's calc4 (D2R's `CurrentSkill`, `UnitSkillLevel` with bonus 0) and
the per-hand sums ([#321](https://github.com/synpoox/pd2-resurrected/issues/321)). Not ported: PD2's boss sets.

## Open and deep wounds

PD2's event 15 (`0x103463f0`, thiscall: game in ECX, then attacker, target, a dummy and the node's `stat << 16 |
layer`), disassembled; constants from the Ghidra copy of the DLL (the DLL now on disk has moved them by 8 bytes):

1. Chance: the node's stat on the attacker (`item_openwounds` 135, or `map_mon_openwounds` 407, the only two with
   func 15), `rand % 100 <` it (`0x1036f320`, the attacker's seed).
2. Level part: 1.13c D2Game `0xac940 ({9, 18, 27, 36, 45}, level)` + 40, the level at least 1: `9l + 31` up to 15,
   then slopes 18, 27, 36, 45 from 16, 31, 46 and 61 (D2R's `0x584170` has the same table).
3. `deep_wounds` (501) on the attacker. On a **player** target both parts are cut: the level part to 8 / 6 / 4% by
   difficulty (game `+0x6d`; doubles `0x1044fbc0`, `fbb8`, `fba8`), `deep_wounds` to the same, halved again when the
   attacker is a Blood Golem (class 290), and the part held by the Barbarian's Deep Wounds passive list (state 225 on the
   attacker, D2Common #10871) a third of that (`0x1044fb90` = 3).
4. Damage = level part + 5 x deep. Resist: `damageresist` (36) on the target less the attacker's `passive_phys_pierce`
   (425); 100 or more ignores the pierce; a negative result counts half (to zero). A player takes `x50%` above 50 resist,
   any other unit `x(100 - r)%`. All cuts are `0x1037c4c0` (`a * x / 100` in doubles, truncated).
5. On a player in PD2's levels 157, 159, 166 inside a region (`0x1037c7b0`, `0x1037d470`): no damage. On a monster whose
   owner, or owner's owner, is a player (`0x103749f0 (unit, 1)`): a quarter.
6. Rathma's clones (classes 935, 936, with both boss slots `game +0x2600 / +0x2604` set): the damage is halved and the
   other clone, unless it has `resistall` (state 8), gets the same half as its own list.
7. The list: `0x102fc880 (target, 62, attacker)` finds the target's open wounds list from this attacker. With fewer than
   3 `openwounds_stack` (189) the old `hpregen` (74, negative) is added to the damage and set on it, and the stack goes
   up; at 3 nothing changes. Then PD2's `0x1036d7d0` request (skill 0, level 1, 125 frames, `hpregen` = -damage,
   state 62): an older list of the same skill and level only gets its timer moved, so the stat is set only on a new list.
   `openwounds_stack` is set on the result. A player attacker also adds the damage to PD2's damage meter (PlayerData
   `+0x1a8`).

So open wounds stacks its damage up to three hits, then each hit only refreshes the 5 s. D2R's own event 15 cuts a
player target to a quarter (melee) or an eighth (missile), a player's pet to half, and lasts 200 frames.

The port (`OpenWounds`, `item_rules.cpp`): the crushing blow dispatch wrap also hides the func-15 nodes from D2RCore's
event copy and runs PD2's event once per hidden node after the dispatch, through D2R's `NewStateList` (its request has
the same stat and value fields; D2R's own event 15 passes `hpregen` there). A splash item event hits more units inside
the outer dispatch, where the nodes are hidden: those hits find the outer call's hidden nodes (a thread-local event) and
roll too, as in PD2. Approximated: D2R keeps one open wounds list per target (`UnitStateList`), so all attackers share
its stacks, where PD2 keys it by attacker. Not ported: the PD2-only levels, Rathma's clone split (core-maps'
`rathma_life.cpp` owns the pair) and the damage meter.

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
  whether D2R's event 16 runs on a player's hit. Cause ([#446](https://github.com/synpoox/pd2-resurrected/issues/446)):
  D2RCore's event dispatcher (`0x5881e0` = `jmp [0x3e2a420]`) runs the item event nodes (unit `+0xe0`) through its
  own event copy and applies D2R's quarter-life blow silently. core-rules wraps that slot, hides the crushing-blow nodes
  on events 5 and 6 and rolls the chance itself. Slot 11, Hell, The Cranium Basher (75%): rolls at chance 75, blows at
  divisor 8 less 7% resist (life 1557248 -> 1376217), life unchanged inside D2RCore's dispatch.
- Enhanced damage per ethereal item ([#446](https://github.com/synpoox/pd2-resurrected/issues/446), Slot 11, Hell):
  Purgatory's tooltip read +0%, +60% with an ethereal helm worn, +0% after taking it off. Damage not measured.
- Smite's effectiveness ([#321](https://github.com/synpoox/pd2-resurrected/issues/321), Slot 8, Hell, level 90 Paladin, Smite 20, zombies): each Smite blow
  logged effectiveness 1.41 (calc4 41); life 1422848 -> 1297459 at divisor 8 and resist 50, PD2's formula.
- Open wounds: a probe on `0x584170` logged nothing with `item_openwounds` 50 (stat only, no listener).
- Open wounds, ported (Slot 6, Hell, level 90 Paladin and level 95 Assassin, The Gnasher: 50%, `deep_wounds` 2):
  level part 2731 at 90 and 2956 at 95, + 10 deep, halved by a zombie's 50 resist: 1370 and 1483 /256 per frame;
  Andariel (resist 66) 1008. Stacks on one zombie (`cheat mongod 1`): 1483, 2966, 4449 at stacks 1-3, then 4449 stays
  at stack 3 on later procs; a proc 7.6 s after the last starts at stack 1 again (125 frames). Splash hits roll it too.
- Spawn commands: `cheat uniques` rows past the "Expansion" separator row are one lower than the merged table's
  (Fleshripper 304, The Gladiator's Bane 250); Attack is the bottom-left icon of the left skill picker.

## Open

- Open wounds: the damage over time itself (life falling by `hpregen`) was not measured in game; the list and its stat
  were logged. Rathma's clone split is left to core-maps.
