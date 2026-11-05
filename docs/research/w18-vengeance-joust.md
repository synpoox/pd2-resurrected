# Vengeance and Joust ([W18](https://github.com/synpoox/pd2-resurrected/issues/93))

Answers work item [W18](https://github.com/synpoox/pd2-resurrected/issues/93) ([#93](https://github.com/synpoox/pd2-resurrected/issues/93)). PD2 side from `ProjectDiablo.dll` (VAs `0x10...`) and the 1.13c `D2Game.dll` of the PD2
install in Ghidra; D2R side from the 93847 dump, RVAs. PD2's install routine `0x1036ba20` writes the 1.13c do table
(4-byte slots): srvdo 174 (`+0x2b8`) = `0x103b3570`, srvdo 162 (`+0x288`) = `0x103b21e0`.

Vanilla calls resolved through PD2's version-indexed entries (column 0 is 1.13c's offset, negative for a D2Common
ordinal):

| PD2 call | Entry | 1.13c | What it is | D2R |
|---|---|---|---|---|
| `0x103a1d90` (`[0x105e0ed8]`) | `0x104a9cf4` | D2Game `0xdbed0` | roll with a percent (below) | `RollPercent` in core-rules |
| `0x103a2550` (`[0x105e0864]`) | `0x104a96c4` | D2Game `0x9f210` | the skill's EMin-EMax roll, put by EType | ElementalDamage `0x437d00` |
| `0x1030f8f0` | `0x1049b1f8` | D2Game `0xa1210` | area callback: copy the block, area apply | AreaApply `0x436e90` |
| `0x10307e50` | `0x1049bbb8` | D2Common [#10973](https://github.com/synpoox/pd2-resurrected/issues/10973) | unit stat, total | UnitStat `0x2f5020` |
| `0x10307eb0` | `0x104a862c` | D2Common [#10887](https://github.com/synpoox/pd2-resurrected/issues/10887) | set unit stat (base) | SetUnitStat `0x2f7d10` |
| `0x1037c4c0` | (PD2's own) | | `trunc(ECX * XMM2 / EDX)`, ECX left as it was | `Percent` in core-rules |

## srvdo 174 (Vengeance)

1. Skill record; the caster's path target after `0xe07a0` refreshes it. None: return 1.
2. A melee hit: to-hit = the skill's ToHit (`0x1030b960`), roll (`0x103048a0`), hit flags `|= 2`, the fill
   (`0x1031b4a0`) and queue (`0x103a1630`) at SrcDam 0x80, apply (`0x10310eb0`, also on a miss). A miss returns 1.
3. A second block, result flags 1, for the burst:
   - per element E (fire 48/49, cold 54/55, lightning 50/51): min/max = the caster's stat total less the same stat on
     its lists of states holyfire 35 and enchant 16 (fire), holywind 43 and coldenchant 188 (cold; merged 223), holyshock 46
     (lightning); pct = calc1/calc2/calc3 + passive_fire/cold/ltng_mastery (329/331/330);
     E = `0xdbed0(min << 8, max << 8, pct)`;
   - X = the skill's EMin-EMax roll (`0x9f210`; Vengeance's EType is none, so it lands in physical, which is then
     cleared); each E += X + trunc(X * mastery_E / 100);
   - cold length = ELen (`+0x214`, 80).
4. auratargetstate (`vengeance`) on the target for Param5 frames (expiry `0x10312350`, vanilla's).
5. D2Game's area search around the target, radius aurarangecalc (7), unit mask `0x8583`, callback `0x103041f0` with
   {block, calc4 (chain %), link 0, Param5 (delay), Param6 (reach)}.
6. State 107 (`shatter`) off on the target. Return 1.

`0xdbed0`: with max > 0, lo = min + min * pct / 100 and hi = max + max * pct / 100 (the product split for large
values); lo + rand(hi - lo) on the unit's seed when hi > lo; never below 0. PD2 passes the same pct for both.

Callback `0x103041f0`, per unit: the area apply with a copy of the block (every unit in range, the target included).
Then, if the unit has a path and no `justhit` (86), and is closer than Param6 to the caster (squared distances by
`pow`): a `justhit` list on it for Param5 + 1 frames; if it is not the caster's path target and rand(its seed) % 100 <
calc4, a game timer (event 7) Param5 frames on calls `0x10303cd0` with the unit, the caster's id and link + 1.

`0x10303cd0` (a link): finds the caster by id, recomputes the block as in step 3 (skill 111 hard-coded, the caster's
current level of it), cold length max(ELen - 15 * link, 0); auratargetstate on the linked unit; the area search around
it with radius max(aurarangecalc - link, 2) and the same callback; `shatter` off.

PD2's Vengeance row: calc1-3 `ln12 + (Holy Fire + Holy Freeze + Holy Shock blvl) * par8 + Conviction blvl * par7`
(Param1 5, Param2 2, par7 2, par8 2), calc4 `min(ln34, 95)` (Param3 22, Param4 2), Param5 4, Param6 24, aurarangecalc
7, EMin 6 / EMax 8 with per-level steps, ELen 80, srvstfunc 23 (the charge-up start: queues nothing).

## srvdo 162 (Joust)

1. Skill record; the caster's path target (none: 0); PD2's teleport core `0x1036cd20(game, unit, 0)` (the 0 only
   skips the Hell list, which is never filled: [sorceress-w26](sorceress-w26.md)); a refusal returns 0.
2. A melee hit: to-hit (`0x1030b960`), roll (`0x103048a0`) into the block's result, damage% = calc1 (1.13c block
   `+0xc`), passive_critical_strike (337) set to min(its total + calc2, 100) for the queue (`0x103a1630`, SrcDam from
   the row) and apply (`0x10310eb0`), then set back to the old total.
3. `0x1036cb90(game, unit, skill, level, 1, 0)`: aurastate (`joust_buff`) for auralencalc frames with aurastat1-6
   (velocitypercent `min(3 + 2 * lvl, 65)`), PD2's timed-state helper. Return 1.

## core-rules

`plugin/core-rules/skill_functions.cpp`:

- **Vengeance** keeps slot 174 (free in D2R). Its melee hit takes D2R's Leap Attack hit (`0x56b170`) steps without the
  damage%, skill elemental or states: to-hit `0x339040`, roll `0x44b720`, fill and queue `0x44b600` at SrcDam 0x80,
  apply `0x44b2b0`. The burst builds a D2R block (fire `+0x20`, cold `+0x34`, lightning `+0x2c`, cold length `+0x118`,
  as ElementalDamage fills them) and calls AreaApply per unit, as Holy Light does. A link waits on its `justhit`
  marker: the marker's expiry runs the next burst, then removes the state.
- **Joust** joins the router (D2R fills 162; `tools/pd2_ids.py` FUNCS). TeleportCore (D2R's srvdo 27), then D2R's Leap
  Attack hit (`SkillMeleeHit`), which does step 2 (to-hit, damage% calc1, SrcDam), with the critical strike stat
  raised around it, then `TimedStatList` for the aurastate. D2R's hit also gives the aurastate on a hit (`0x566bc0`);
  the list core-rules makes next extends that one.

Deviations (approximated, on the deviation list):

- Vengeance's target is D2R's skill target (PD2 refreshes the caster's path target), as Holy Light.
- Vengeance's melee hit is D2R's: a miss is not applied and the fill is D2R's (what PD2's `0x1031b4a0` adds beyond
  weapon damage at SrcDam is not read).
- Joust's target is D2R's skill target, its teleport is TeleportCore (Teleport's deviations) and its hit is D2R's
  Leap Attack hit, which also gives the aurastate on a hit.
- A link runs when the `justhit` marker runs out: after Param5 frames, as PD2's timer, but the marker lasts Param5
  frames, not Param5 + 1. A link keeps the cast's level (PD2 reads the caster's current one). At most 256 links wait at
  once (the oldest is dropped) and a chain stops after 64 links.

## Checks

`tests/game/core_rules_checks.py`, tag `core-rules-w18`: `vengeance-hit` (percentages from calc1-3, chain % calc4,
delay Param5, reach Param6, cold length ELen), `vengeance-link` (a link's cold length drops by 15), `joust-cast`
(damage% calc1, critical strike raised by calc2 up to 100, joust_buff for 50 frames with velocitypercent).
