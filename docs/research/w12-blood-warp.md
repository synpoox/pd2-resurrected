# Necromancer: Blood Warp (W12)

Answers work item W12 (#87). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) by static disassembly
(capstone), D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). Addresses are D2R RVAs unless they start with
`0x10`.

## Where PD2 puts them

The install routine `0x1036bb1c` writes srvdo 157 = `0x103b1aa0` (D2Game do table, `+0x274`) and srvst 66 =
`0x103b6460` (start table `[0x105de8ec]`, `+0x108`). The client start table (`[0x105e2110]`, routine `0x103ad290`)
gets cltst 9 and 54-58; cltst 56 = `0x103acda0`.

D2R fills all three slots (srvdo 157, srvst 66 `0x42fa90`, cltst 56 `0x2b67e0`), so the merge moves them: srvdo 157 to
the router (srvdo 190, by skill id; merged Blood Warp is 439), srvst 66 to 80, cltst 56 to 63 (the first free client
start slot after Find's 62).

## The cost (all three functions)

`cost = max((double)(uint32)calc1 / 100.0 * life, calc2)`, truncated with `cvttsd2si`; life is stat 6 (hitpoints,
in 1/256). PD2's row: calc1 `12 - stat('blood_warp_life_reduction'.accr)` (percent of current life), calc2
`min(ln34 * 256, 15360)` with Param3 2, Param4 2 (2 life at level 1, 2 more per level, at most 60). calc1 is converted
as unsigned, so a reduction over 12 makes the product overflow to `INT32_MIN` and calc2 is the cost.

- srvst 66 and cltst 56: return 1 when `cost < life`, else 0 (the cast is refused). Same body on both sides.
- srvdo 157:
  1. PD2's teleport core `0x1036cd20(game, unit, 1)`, shared with Teleport's srvdo 155; return 0 if it fails.
  2. The cost again, at the new point; return 0 if `cost >= life`.
  3. Life = life - cost (`0x10307eb0`, stat 6).
  4. A timed state through PD2's helper `0x1036d7d0` with {owner, unit, skill, level, frames = auralencalc (125),
     stat = aurastat1 (hpregen), value = cost / frames + 1, state = aurastate (bloodwarp)}. The helper's
     curse-length branches (stats 109 / 504, states 58, 60, 89, 196) don't apply to bloodwarp; the rest of it was not
     read.

PD2's regen hook reads the bloodwarp list's value too ([plrmodes](plrmodes.md), life regen).

## D2R

D2R's Teleport (srvdo 27, `0x554c40`) does the first half of PD2's core: room, level (type byte 0 refuses, 2 checks
collision `0x804` at the point), then `0x491ca0`, which returns 1 on a teleport and 0 otherwise. PD2's core adds a check
on 214 (`0x1030c320(unit, 0xd6)`, likely a state), a map-related lookup when its third argument is set, and a
free-spot search for level type 3; those belong to the Teleport row (srvdo 155), not here.

core-rules (`plugin/core-rules/skill_functions.cpp`): srvst 80 / cltst 63 are the cost check; the router's Blood Warp
calls D2R's srvdo 27, pays the cost, and builds the hpregen list the way Frozen Armor builds one (`NewStateList`,
`SetListStat`, `SetUnitState`), dropping an older bloodwarp list first. D2R's skills record keeps auralencalc at
`+0x80` (1.13c `+0x60`, like aurastat1 `+0x54 -> +0x74` and aurastatcalc1 `+0x68 -> +0x88`).

Deviation (approximated, on the unported list): D2R's teleport instead of PD2's core, and the list built D2R's way.

## Checks

- `tests/game/core_rules_checks.py`, tag `core-rules-w12`: the cast's cost, regen and length against PD2's calc.
- The cost formula has compile-time checks (`static_assert`) beside `BloodWarpCost`.
