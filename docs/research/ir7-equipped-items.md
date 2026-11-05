# IR7: skill while equipped and phasing in ProjectDiablo.dll

Work item IR7 ([#242](https://github.com/synpoox/pd2-resurrected/issues/242), [item-rules.md](../item-rules.md)):
skill while equipped and phasing. The charm "only one unique" rule left IR7: a Charm Inventory companion plugin
takes it (owner, 2026-11-04). What IR7 found about it, for that plugin: PD2's charm check (`0x10381970`, also read by
the tooltip at `0x102b2050`) switches off every unique charm in the inventory (page 0) that shares its unique row with
another one there, so two Annihilus both stop working; nothing is refused. Findings only: no PD2 or Blizzard file
content is in this repo.

## Method

Ghidra 12.2 headless, read-only, with throwaway scripts kept outside the repo: an immediate search for the PD2 ids in
`ProjectDiablo.dll` (`item_skillonequip` 191, `phasing` 278, state `phasing` 244, Phasing SelfAura 605), the strings
`StrPhasingStatDesc` and `CharmOnlySingleUniqueDesc`, decompiles of the hits; then the D2R 93847 dump for the same
paths (stat `item_aura` 151 reads, their callers, the stat callback's switch). Ids below are PD2's; core-rules uses the
merged ids (`item_skillonequip` 375, Phasing SelfAura 674, Armageddon SelfAura 662, MonCharge 486).

## What PD2 does

- **Skill while equipped is D2's item aura on another stat.** Every `equipped-skill` skill is an aura (`aura` 1:
  Energy Shield, Chilling Armor, Quickness, Blade Shield, Armageddon, Thunder Storm and Phasing SelfAura).
  - PD2 wraps 1.13c's stat callback (`0x1036f640`): when `item_skillonequip` changes, it adds the skill to the unit
    (or removes it) and starts (or stops) the item aura the way the `item_aura` case does.
  - It wraps the item aura's tick (`0x10345cc0`, the 1.13c original at `0x10316250`): a skill with an
    `item_skillonequip` level runs at that level, not at `item_aura`'s. The tick casts nothing while the unit uses
    Charge (107) or MonCharge (415), and Armageddon SelfAura (593) only in modes 1, 2, 3, 5 and 6 (standing, walking,
    running, in town or out). Other skills fall through to 1.13c's tick.
  - The tooltip (`0x102ee7a0`) prints `StrPhasingStatDesc` ("Phasing When Equipped") for Phasing SelfAura at level 1.
- **Phasing is a collision change.** The `phasing` stat is read only by its tooltip line. When Phasing SelfAura comes
  on or off through `item_skillonequip`, PD2 calls `0x10380c50(unit, on)`:
  - on: the path's collision mask drops the unit bits (player `0x1c09` -> `0xc09`, monster `0x3c01` -> `0xc01`), and
    a second 1.13c path call gets 7 (player) or 2 (monster);
  - off: the mask goes back (a player in some state keeps `0xc09`), and a third path call gets `0x80` (player) or
    `0x100` (monster), unless the unit has state 54.
  - `0x10345ed0` wraps a 1.13c state function and calls `0x10380c50` again when state 13 or 244 turns on or off.
  The three 1.13c calls are reached through PD2's name resolver, so which D2Common functions they are is not known.

## In the port

D2R's stat callback (`0x46fdd0`, `(game, unit, item, stat << 32 | layer, old, new)`, r9 64-bit) switches on stats
7-209 only, so stat 375 does nothing. Its entry is D2RCore's (`jmp [rip+x]`), so core-rules hooks where units get it
(`0x2f65d0`, the stat list setup) and hands each unit a wrapper:

- the wrapper passes stat 375 to D2R's callback as `item_aura` (151) with the same layer: D2R's `item_aura` case
  (`0x470470`) starts or stops the item aura (`0x581970` / `0x581ae0`), unit event 9;
- the event's tick (`0x437230`) reads `item_aura`'s level. core-rules hooks it: a skill with no `item_aura` level and
  an `item_skillonequip` level gets the tick's body at that level (the cast through the server dispatcher with unit
  flag `0x20000000` and the state key, the re-add perdelay frames on), with PD2's two cast gates. Without a level the
  tick is D2R's, which removes the event;
- Phasing SelfAura: on the stat's change (the callback's new value) and on each tick, the unit's path collision mask
  loses (or gets back) `0x1000` on a player, `0x3000` on a monster.

Left approximated, on the [unported list](../unported.md): PD2's two other path calls and its state 13/244 refresh,
the skill-list add, and the Phasing tooltip line.

## Game check

`tests/game/core_rules_checks.py`, tag `core-rules-ir7`: `skill-on-equip` (Blackoak Shield's Chilling Armor SelfAura
starts at 11 on equip, ticks with state chillingarmor, stops on unequip) and `phasing` (Spectral Plate: the player's mask
`0x1c09` -> `0xc09`, back on unequip).
