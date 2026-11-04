# IR3: combat item stats

Work item IR3 ([#238](https://github.com/synpoox/pd2-resurrected/issues/238)), design in [item-rules.md](../item-rules.md).
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
| `lifedrain_percentcap` (488) | `0x10296830`, `0x102fbc50` |
| `inc_splash_radius_permissinghp` (489) | `0x102fa640`, `0x1036c410` |
| `item_dmgpercent_pereth` (484) | `0x102fa930` |
| `equipped_eth` (183) | `0x103501b0` |
| `item_dmgpercent_permissinghppercent` (487) | none (no carrier, as #148 found) |

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

## Game check

Slot 8, Hell, level 90 Paladin, Blood Moor zombies: both patches install (byte checks pass), and the zombies' melee
hits run through `WeaponStrike` with no crash (physical unchanged at chance 0). Not yet seen: a player's roll. The
Paladin's attacks did not reach `WeaponDamage` in that run (left skill Sacrifice, boxed in by the pack), so the crit
and deadly strike numbers are still unchecked in game. Set the stats with `cheat stat item_crit_chance, 30` (and
`item_crit_multiplier`, `item_deadlystrike`, `item_maxdeadlystrike`, `item_ds_multiplier`); `cheat uniques` rows past
the "Expansion" separator row are one lower than the merged table's (Fleshripper 304, The Gladiator's Bane 250).

## Open

Crushing blow, open and deep wounds, Nightmare's Feast and enhanced damage per ethereal item are not built. Crushing
blow and open wounds are item events, which D2R does not call through the table a plugin can reach on a player's
melee hit (#165); they need another hook point.
