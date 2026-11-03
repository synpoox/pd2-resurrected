# Unported list

Every deviation from the PD2 baseline, marked approximated, cut or not yet (see [GLOSSARY.md](../GLOSSARY.md)).
D2R-only content is not a deviation.

`tools/pd2_diff.py` reads the **Gate** column: each backticked `table.row.column` pattern (fnmatch, any case; `*` also
spans dots) covers the table-diff gate's deviations it matches. Rows without a pattern are deviations the gate cannot see.
A work item that merges a table removes or narrows its row here.

| Deviation | Status | Gate | Work item |
|---|---|---|---|
| PD2 skill columns D2R has no place for (aura target and passive events, checkfunc, nocostinstate, general); their rules come back as engine work | not yet | `skills.*.auratgtevent` `skills.*.auratgteventfunc` `skills.*.passiveevent` `skills.*.passiveeventfunc` `skills.*.checkfunc` `skills.*.nocostinstate` `skills.*.general` | core-rules |
| PD2 state columns `immed` and `blue` have no D2R column | not yet | `states.*.immed` `states.*.blue` | core-rules |
| PD2 stat columns D2R has no place for (Divide, itemspecific, the S12/S13 save widths) | not yet | `itemstatcost.*.divide` `itemstatcost.*.itemspecific` `itemstatcost.*.save bits s12` `itemstatcost.*.save add s12` `itemstatcost.*.save param bits s12` `itemstatcost.*.save bits s13` `itemstatcost.*.save adds s13` `itemstatcost.*.save param bits s13` | core-rules |
| PD2 missile columns LevRange, Qty and SpecialSetup have no D2R column | not yet | `missiles.*.levrange` `missiles.*.qty` `missiles.*.specialsetup` | core-rules |
| Item tables not merged yet: the mod still uses D2R's | not yet | `armor.*` `automagic.*` `belts.*` `bodylocs.*` `books.*` `cubemain.*` `cubemod.*` `gamble.*` `gems.*` `itemratio.*` `itemtypes.*` `lowqualityitems.*` `magicprefix.*` `magicsuffix.*` `misc.*` `properties.*` `qualityitems.*` `rareprefix.*` `raresuffix.*` `runes.*` `setitems.*` `sets.*` `treasureclassex.*` `uniqueitems.*` `weapons.*` | P3 |
| Monster, area and other tables not merged yet: the mod still uses D2R's | not yet | `armtype.*` `automap.*` `charstats.*` `colors.*` `compcode.*` `composit.*` `difficultylevels.*` `elemtypes.*` `events.*` `experience.*` `hireling.*` `hitclass.*` `inventory.*` `levels.*` `lvlmaze.*` `lvlprest.*` `lvlsub.*` `lvltypes.*` `lvlwarp.*` `misscalc.*` `monai.*` `monequip.*` `monlvl.*` `monmode.*` `monplace.*` `monpreset.*` `monprop.*` `monseq.*` `monsounds.*` `monstats.*` `monstats2.*` `montype.*` `monumod.*` `npc.*` `objects.*` `objgroup.*` `objmode.*` `objtype.*` `overlay.*` `pettype.*` `playerclass.*` `plrmode.*` `plrtype.*` `shrines.*` `skillcalc.*` `skilldesc.*` `soundenviron.*` `sounds.*` `storepage.*` `superuniques.*` `uniqueappellation.*` `uniqueprefix.*` `uniquesuffix.*` | P4 |
| PD2 tables D2R's game does not read (aiparms, arena, chartemplate, cubetype, hiredesc, miniskillicons, monitempercent, monname, treasureclass, uniquetitle, weaponclass); each needs a decision: fold into another table, rebuild in the engine, or cut | not yet | `aiparms.*` `arena.*` `chartemplate.*` `cubetype.*` `hiredesc.*` `miniskillicons.*` `monitempercent.*` `monname.*` `treasureclass.*` `uniquetitle.*` `weaponclass.*` | P4 |
| Guild, arena and ladder labels keep D2R's text (20 keys, `CapsJoinMenu4` to `Guild Portal`): online features | cut |  | P6 |
| `ModStr5q`, `StrSkill110` and `StrSkill76` keep D2R's text: PD2's changes D2R's placeholders | approximated |  | P6 |
| Map items and the PD2 map system (map stats, recipes, events, invaders) | not yet |  |  |
| New PD2-only areas | not yet |  |  |
| The Warlock redesign: the alpha ships D2R's Warlock as it is | not yet |  |  |
| The other classes' engine rules (Necromancer, Paladin, Druid, Sorceress, Assassin) | not yet |  |  |
| Everything in the core-rules order after the alpha cut (router slot, dead item events, monster and boss kits, cube ops 30/31, investigations) | not yet |  |  |
| Advanced stats panel | cut |  |  |
| Map-event notifications | cut |  |  |
| Grail panel | cut |  |  |
| PD2's display | cut |  |  |
| Online and controller layers | cut |  |  |
| PvP modifiers and battle-royale checks | cut |  |  |
