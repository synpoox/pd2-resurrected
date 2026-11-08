# [IR4](https://github.com/synpoox/pd2-resurrected/issues/239): PD2's curse stats in ProjectDiablo.dll

Work item [IR4](https://github.com/synpoox/pd2-resurrected/issues/239) ([#239](https://github.com/synpoox/pd2-resurrected/issues/239), [item-rules.md](../item-rules.md)):
curse effectiveness and max curses. Findings only: no PD2 or Blizzard file content is in this repo.

## Method

Ghidra 12.2 headless, read-only, with throwaway scripts kept outside the repo: an immediate search for the PD2 stat
ids in `ProjectDiablo.dll` (`curse_slots` 190, `max_curses` 368, `curse_effectiveness` 504), decompiles and
disassembly of the hits, PD2's patch table for the 1.13c call sites it redirects, and the D2R 93847 dump for the
functions core-rules hooks. Ids below are PD2's; core-rules uses the merged ids (374, 421, 557,
`merge/ids-itemstatcost.tsv`).

## What PD2 does

PD2 replaces 1.13c D2Game's state list creator (seven call sites, `+0x3acf9` ... `+0x9a22d` in its patch table, the
necromancer curse function among them) with `0x1036d7d0`. It takes the same request D2R's `NewStateList` takes
(caster, target, skill, level, frames, stat, value, state, callback).

- **Curse resistance.** `curse_effectiveness` is read on the **cursed unit**, not the caster: PD2's strings call it
  Curse Resistance, and the items that carry it are defensive (Wraithskin, Templar's Might, The Spirit Shroud; Overlord's
  Helm has -20). On a curse state:
  - the request's stat (aurastat1) becomes `value * (100 - r) / 100`, truncated (`0x1037c4c0`, doubles), with `r`
    capped at 75 on a player and uncapped on a monster; a result of 0 cancels the curse;
  - aurastat2-6, which the necromancer curse function sets after the list exists, go through `0x102fad50`, which PD2
    patches over that one stat-set call (`+0x503b9`): the same formula with `r` capped at 75 for every unit;
  - a negative `r` strengthens the curse.
- **Max curses.** `slots = max_curses(caster) + 1`. PD2 walks the target's stat lists flagged curse (`0x20`): it counts
  them, raises `slots` to the largest `curse_slots` any of them carries and sets a smaller non-zero `curse_slots` to 0,
  and keeps the last timed one in the chain (the oldest) as the one to replace. Then:
  - the target already has this state: the vanilla rule (same skill and level: timer refresh; lower level: refused;
    otherwise replaced);
  - fewer curses than `slots`: the curse is added;
  - otherwise the oldest timed curse is replaced.
  The new list gets `curse_slots = max_curses(caster) + 1` when that is more than the target's total `curse_slots`, so
  the target keeps room for the caster's curses after the caster leaves.
- **Also in the same function, not [IR4](https://github.com/synpoox/pd2-resurrected/issues/239)'s stats** (on the [deviation list](../deviations.md)):
  - curse resistance stat 109 (`curse_resistance`) shortens the duration with the same 75 cap on players; D2R refuses a
    curse at 100+ and has no cap;
  - PD2 reads the attract state but no longer refuses a curse on an attracted unit; D2R does;
  - Dark Pact (state 196, `dark_pact`): see "Dark Pact" below ([#265](https://github.com/synpoox/pd2-resurrected/issues/265)).

## Dark Pact

[#265](https://github.com/synpoox/pd2-resurrected/issues/265). In `0x1036d7d0`, state 196 skips the curse gate (stat 109 and curse resistance). After the slot walk
(count `n`, oldest timed curse) it:

1. frees the first curse-flagged list in the chain that is not the oldest timed one (`0x1030c530`): with one curse,
   none goes;
2. returns 0 (no list) when `n < 1`;
3. rolls the skill's elemental damage on the caster at the cast level (`0x103a2550` -> 1.13c D2Game `0x9f210`, entry
   `0x104a96c4`; Dark Pact's EType is mag, so `+0x20`), sets hit flags 1 and result flags 5, and scales magic by
   `(n - 1.0) * 1.1 + 1.0` (doubles `0x1044fb18` = 1.0, `0x1044fb28` = 1.1; the magic read unsigned, truncated by
   `0x103b8a40`);
4. searches around the target's x/y (`0x103a1450` -> D2Game `0xa0c70`, entry `0x104a93fc`) from the caster with
   radius `calc1 + n - 1` (`0x1030ad70`, D2Common #10786 on the skill's `+0x138`, calc1: 4, "base aoe range"), unit
   mask `0x8583`, callback `0x1030f8f0` (D2Game `0xa1210`: copy the block, area apply), so this "follow-up" is an
   explosion, not a skill cast or missile;
5. puts `dark_pact` on the target: below the slot count through the usual same-state check, at or above it with no
   curse replaced.

The port (core-rules `ApplyDarkPact`) does the same through D2R's ElementalDamage, AreaUnits and AreaApply, then
ApplyCurse with every other curse hidden. Left out: at or above the slot count PD2 adds a second `dark_pact` list when
one is still on the target; NewStateList refreshes the old one. Its aurastate lasts 1 frame (auralencalc 1).

## In the port

D2R's `NewStateList` (`0x433d20`) starts with `jmp [rip+x]`: D2RCore hooks it, so core-rules cannot. The curse srvdo
(slot 30, `0x55c840`, every player curse, Dark Pact included) hands D2R's necromancer curse function `0x55a7e0`
(`SkillNec.cpp`, `(target, &CurseParams)`) to its area walk, once per target; `0x5598f0` and `0x568ad0` call it too.
Its entry is D2R's own. core-rules hooks it (`item_rules.cpp`):

- before: PD2's slot walk on the target's first chain of stat lists (unit `+0x88`, chain `+0x90`, flags `+0x1c`, next
  `+0x68`). `NewStateList` removes the first curse-flagged list it finds, so every curse list that must stay loses its
  curse flag while `0x55a7e0` runs: all of them while a slot is free, all but the oldest when full, all but this state's
  own when the target has it;
- after: the flags come back; a new list gets `curse_slots`; each stat `0x55a7e0` set is set again, resisted. The
  value comes from D2R's own per-stat helper `0x55bfb0` (a fifth of a resist cut on an immune monster), so the order is
  PD2's: the immunity cut, then resistance. aurastat1 is resisted only on a new list, as PD2 does it before the
  refresh check.

PD2's gate ([#264](https://github.com/synpoox/pd2-resurrected/issues/264)), before the slot walk: a curse whose aurastat1 resists to 0 (the cap above) is cancelled;
stat 109 cuts the frames to `frames * (100 - r) / 100`, r at most 75 on a player, and 0 frames cancel it. D2R's
NewStateList, for a curse state, refuses at stat 109 >= 100, otherwise takes `frames * r / 100` off uncapped
(`0x433f32`), then refuses a unit with attract (state 57, `0x433fc8`). Its entry is D2RCore's, so core-rules runs
`0x55a7e0` with the target's stat 109 cleared on its base layer (`SetUnitStat` to `base - r`) and the PD2 frames in
CurseParams, and with the attract bit off (the state bits at stat owner `+0xaf0` that `0x2f6220` reads; `SetUnitState` writes a second array, so it does not do); both come back after the call, the bit only while the
attract list is still there.

## Game check

`tests/game/core_rules_checks.py`, tag `core-rules-ir4`: `max-curses` (a curse joins only with a free slot, a full
target loses one, and a monster holds a second curse) and `curse-resistance` (each resisted stat matches PD2's formula
and cap), `curse-resisted-to-0`, `curse-duration` (PD2's frames and the 75 cap on a player), `curse-attract` and `dark-pact`
(the consumed curse, radius and scaled magic from the log).
