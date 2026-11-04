# PD2's remaining item rules in the port

How the port builds the PD2 item rules that have no work item yet, decided in
[#158](https://github.com/synpoox/pd2-resurrected/issues/158) (map [#143](https://github.com/synpoox/pd2-resurrected/issues/143)).
What is left is in the
[inventory](https://github.com/synpoox/pd2-resurrected/blob/research/pd2-remaining-items/docs/research/pd2-remaining-items.md)
(#148). Each rule is a row in [core-rules.md](core-rules.md) and on [unported.md](unported.md). The work items are placed
on [roadmap.md](roadmap.md) by #143's final ordering ticket.

## Scope

In: the item-event and item-rule rows of core-rules that have no work item; the 14 PD2-only item stats that only
`ProjectDiablo.dll` reads (new rows); the item rules found only on the PD2 wiki; the unported data rows whose P3 item is
closed.

Out:

- Skill on cast and Ormus' Robes were built in P12. Skill on cast moves to a companion plugin (below).
- Skill on crit and skill on pierce (`docrit`, `dopierce`) stay unreachable: no PD2 item carries them. A cast-on-crit
  modifier is new content for a patch after the launch.
- Map items and recipes belong to the map system ([map-system.md](map-system.md)); uber keys, organs and the Rathma
  tomes' source to the uber bosses ([uber-bosses.md](uber-bosses.md)); item art to #143's art plan. PvP items are cut.
- The gamble refresh button and buying back a unique charm sold by mistake are cut, unless a companion gives them
  (IR1). Stash search and gold caps went with the stash layouts.

## Decisions

- **Parity: Exact.** One core-rules row per rule, not per stat: crit chance and multiplier are one row, deadly strike
  multiplier and maximum one, Nightmare's Feast's two stats one, enhanced damage per ethereal item and per missing life
  one. Crushing blow efficiency joins the existing crushing blow formula row (both are PD2's event 16).
- **DLL-only is checked in the work item.** #148 found the 14 stats DLL-only from the tables, not in Ghidra. Each work
  item starts by tracing its stats in `ProjectDiablo.dll`; a stat D2R already handles shrinks to data.
- **Companion plugins first.** RuffnecKk's suite (MIT) has a plugin for several rules. IR1 checks each against PD2's
  numbers and against our hooks:
  - Cast Triggers takes skill on block and skill on cast. P12's listener walk is removed once it passes; Ormus' Robes
    (func 25 -> 26) stays in core-rules. If it clashes with core-rules' hooks (`0x43acb0`, event slot 16), it is forked
    into core-rules instead.
  - Ethereal Item Rules, Item Durability, Progressive Affixes, Mass Identify and Vendor Stock Refresh are candidates
    for the ethereal rules, the rare affix minimum, the Almanac's identify-all and the gamble refresh.
  - Mercenary drop on hire (yinyin333333, closed source) gives a mercenary's gear back when another is hired.
  A plugin that matches PD2 closes its row; one that only comes close makes its row approximated.
- **The dye stat** (`transform_dye`) tints the output of 35 PD2 cube rows (the Annihilus corruptions and the magic-item
  "added mod" rows). The recipes work in the port without the tint: an approximated row. How D2R Reimagined tints
  items is researched for a later patch ([#190](https://github.com/synpoox/pd2-resurrected/issues/190)).
- **Wiki-only rules** get a core-rules row with status `verify`: the open wounds rework, ethereal durability and set
  items rolling ethereal, the minimum rare affix count with no upper item level for staffmods, loot from flying
  monsters landing on walkable tiles, and monsters killed by Chaos Sanctuary seals dropping nothing. Potions going to
  the belt and items that can't be sold are data checks. A rule the DLL does not have is dropped.
- **Work items are bundled by the code they share**, not one per rule.

## Work items

| # | Work item | Plugin | Blocked by |
|---|---|---|---|
| IR1 | Investigation ([#189](https://github.com/synpoox/pd2-resurrected/issues/189)): RuffnecKk's suite plugins next to core-rules on 93847, each against PD2's rule | none | none |
| IR2 | Companions: bundle Cast Triggers (skill on block, skill on cast; P12's listener walk removed), mercenary drop on hire and the suite plugins that pass IR1, with their config | companions, core-rules (removal) | IR1 |
| IR3 | Combat stats: the crit rework, deadly strike multiplier and maximum, the crushing blow formula and efficiency, deep wounds and the open wounds rework, Nightmare's Feast (life-steal cap, splash per missing life), enhanced damage per ethereal item and per missing life | core-rules | none |
| IR4 | Curses: curse effectiveness, max curses | core-rules | none |
| IR5 | Item use and charges: unlimited TP and ID books (`pSpell` 13 -> 17), throwing potions with PD2's weapon spell columns, item casts (`ItemEffect`), replenish charges | core-rules | none |
| IR6 | Cube: the demonic cube reroll (op 31), corruption in one press, stackable runes and gems (shared with MS6's map stacking) | core-rules | none |
| IR7 | Equipped items: skill while equipped and phasing, the charm "only one unique" rule | core-rules | none |
| IR8 | Item generation and loot: the wiki rules IR2 does not close (ethereal, rare affixes, loot placement, seal kills), the belt and no-sell data checks | core-rules | IR1 |

## Game check

Each work item's rows pass their core-rules test, on a level 90+ character in Hell.
