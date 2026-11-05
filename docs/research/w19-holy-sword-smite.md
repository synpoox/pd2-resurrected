# Holy Sword and Smite starts, Smite's event, Run/Concentration (W19)

Answers work item W19 (#94). PD2 side from `ProjectDiablo.dll` (VAs `0x10...`) and the 1.13c `D2Game.dll` of the PD2
install in Ghidra; D2R side from the 93847 dump, RVAs. PD2's start table fill (`[0x105de8ec]`, in `0x1036ba20`) writes
srvst 73 (`+0x124`) = `0x103b6990` and srvst 75 (`+0x12c`) = `0x103b6a20`; the event fill `0x1036c2b0` writes event
16 (`[0x105de8f8] + 0x40`) = `0x103469a0`. D2R leaves srvst 73 and 75 empty, so both keep PD2's numbers.

## srvst 73 (Holy Sword start)

`(game, unit)`: the weapon from the unit's inventory (`0x1030ba80([unit + 0x60])`) must be item type 0x72 (`2han`) and
0x1e (`swor`) (`0x10309c60`, item is of type); then 1, else 0. Merged rows: `swor` 30, `2han` 120 (D2R skips the
Expansion row). core-rules asks the same of D2R's weapon (`UnitWeapon`, `0x4242b0`) with `0x373890 (item, type)`.

## srvst 75 (Smite start)

`(game, unit, skill)`: with the skill's record (1.13c `0x1037c110`, stride 0x23c) and `auraevent1` (`+0x84`) >= 0:
unless the unit already has an event of kind 2 keyed `0x880000` (1.13c D2Game `0x370e0`, walks the unit's event list
at `+0x90`), register `auraeventfunc1` (`+0x8a`) for `auraevent1` keyed `0x880000`, level 0 (1.13c `0x9e290` ->
`0x37190`, `SUnitEvent.cpp`). Returns 1. `0x880000` is `crushingblow` (stat 136) << 16: the key
`item_crushingblow`'s own event carries, so a Paladin with a crushing-blow item gets no second event.

D2R's register (`0x438230`, reached from srvst 66 `0x42fa90`) and its clean-up `0x588670` are jumps into code outside
the dump, so core-rules cannot key its event as PD2 does. It calls srvst 66 (`RegisterSkillEvents`), which registers
Smite's events under its skill id; `0x588670 (game, unit, 1, skill)` runs first and reads as removing the skill's old
ones (not seen). The game check `smite-event` fails if two Smite events fire in one frame.

## Event 16 (`0x103469a0`): PD2's crushing blow

Event 16 is `item_crushingblow`'s event in both games (itemstatcost `itemeventfunc1` 16), so PD2's event 16 is its
crushing blow for every item, with Smite's chance added. `(game, event, unit, target, damage, key, level)`:

1. Nothing for a player target when the unit's owner is a player the target may not be hit by (`0x1037d4c0`); nothing
   unless damage flags (`+0`) have 0x20.
2. Chance: when the unit's current skill is Smite (97), calc3 (`+0x140`, `5 + blvl`); plus `0x10381720 (unit, 136)`
   (PD2's per-hand item stat sum); plus the key's stat (`key >> 16`, the unit's `crushingblow`) when it is 1 or more.
   Roll `% 100` on the unit's seed.
3. The blow, PD2's own: blow = life * (effectiveness + stat 268 / 100) / divisor, less the target's `damageresist` %
   (nothing at 100 % or more). Divisor: 10 for a player or a monster `0x1030c980` passes; for a monster `0x1030ca40`
   passes, 30 when `0x10371280` does, else (100 - `0x1030dd60`) * 10 + 70; 8 otherwise; a monster then adds
   `0x103a2ae0(players)` / 50. Event 6 (missile): * 2 when `0x1037c7b0` passes for the weapon, else * 1.5.
   Effectiveness 1, or `1 + calc4 / 100` (Smite's `edmx`) on Smite. Life set down, kill flag (`damage + 4 |= 2`), PD2's
   damage meter, flag 0x93. The tests (`0x1030c980` ...) are not decoded further.

D2R's event 16 (`0x583150`): chance = the unit's stat `key >> 16` at layer `key & 0xffff`, roll `% 100` on its seed,
then `0x581bd0 (game, unit, target, {damage, 4, 10, 10, event == 6})` (itself a jump outside the dump).

D2R never calls slot 16 on a player's melee hit (a build that logged every slot 1-40 saw nothing on the Paladin's
hits, #165), so core-rules rolls Smite's crushing blow on the hit path instead. It wraps Smite's do (srvdo 150, D2R's
`0x564930`) to mark the thread as smiting, and hooks D2R's hit queue (`0x44b600 (game, attacker, target, damage,
flag)`). A queued hit that landed (result `+4` has bit 1 and none of `0x8380`) while smiting rolls once with
`crushingblow` + calc3 and calls D2R's blow (`0x581bd0`) on the damage block before it is queued. PD2's blow formula,
calc4, stat 268 and the per-hand sum are not ported (deviation list; the formula is its own core-rules row).

## Run and Concentration (gate A, PD2 `0x10373290`)

D2R's busy gate `0x42d840` (decompiled): for a request with no skill, or with a skill that has the `+0x24` flag, into
neutral, walk, run, town walk or town neutral, it buffers (4) while more than 1 frame is left of the mode and allows (0)
after: a run asked for mid-attack is taken at the end, which is PD2's added run. The `bear` (140) and Concentration
(42) rolls are only reached for the other modes (get-hit, block, ...) and only when the current skill is
interruptible (`+0x27`); a request with a skill is buffered within 6 frames of the end and refused before, roll or no
roll. So Concentration blocks get-hit and not the next attack, which is PD2's change. Neither needs code: core-rules
logs the gate's answers for run requests and for a player under Concentration (`gate:` lines) so the game check sees
them. The windows are D2R's, not PD2's frame check `0x77e00` (approximated, deviation list).

## Checks

`tests/game/core_rules_checks.py`, tag `core-rules-w19`: `holy-sword-start`, `smite-start`, `smite-event`,
`run-queued`, `concentration-window`.

Game check (Slot 5, Hell, level 90 Paladin, Smite 20, Holy Sword 10, Concentration 20): Holy Sword's start accepted with
Shadowfang; Smite's start accepted, and Smite deals damage (combat log 37-44 a hit); Concentration refused get-hits from
attack mode. Smite's event never fired on slot 16 (#165); with the hit-queue roll, 30 Smites on Hell zombies logged 22
landed hits at chance 25 (calc3 25 at level 20) and 8 crushing blows, one roll a hit. No run request was logged
mid-attack.
