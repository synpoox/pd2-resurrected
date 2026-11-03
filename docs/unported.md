# Unported list

Every deviation from the PD2 baseline, marked approximated, cut or not yet, and why (see [GLOSSARY.md](../GLOSSARY.md)).
D2R-only content is not a deviation.

`tools/pd2_diff.py` reads the **Gate** column: each backticked `table.row.column` pattern (fnmatch, any case; `*` also
spans dots) covers the table-diff gate's deviations it matches. The gate compares the excel tables only; rows without a
pattern (strings, engine rules, features) are checked by hand or by the game check. A work item that merges a table
removes or narrows its row here; the gate fails while a whole-table pattern (`table.*`) still covers a merged table.

| Deviation | Status | Why | Gate | Work item |
|---|---|---|---|---|
| PD2 skill columns auratgtevent, auratgteventfunc, passiveevent, passiveeventfunc, checkfunc, nocostinstate, general | not yet | D2R's skills table has no such columns; the rules behind them come back as engine work | `skills.*.auratgtevent` `skills.*.auratgteventfunc` `skills.*.passiveevent` `skills.*.passiveeventfunc` `skills.*.checkfunc` `skills.*.nocostinstate` `skills.*.general` | core-rules |
| PD2 state columns immed and blue | not yet | D2R's states table has no such columns | `states.*.immed` `states.*.blue` | core-rules |
| PD2 stat columns Divide, itemspecific and the S12/S13 save widths | not yet | D2R's itemstatcost table has no such columns | `itemstatcost.*.divide` `itemstatcost.*.itemspecific` `itemstatcost.*.save bits s12` `itemstatcost.*.save add s12` `itemstatcost.*.save param bits s12` `itemstatcost.*.save bits s13` `itemstatcost.*.save adds s13` `itemstatcost.*.save param bits s13` | core-rules |
| PD2 missile columns LevRange, Qty and SpecialSetup | not yet | D2R's missiles table has no such columns | `missiles.*.levrange` `missiles.*.qty` `missiles.*.specialsetup` | core-rules |
| Item tables keep D2R's values | not yet | not merged yet | `armor.*` `automagic.*` `belts.*` `bodylocs.*` `books.*` `cubemain.*` `cubemod.*` `gamble.*` `gems.*` `itemratio.*` `itemtypes.*` `lowqualityitems.*` `magicprefix.*` `magicsuffix.*` `misc.*` `properties.*` `qualityitems.*` `rareprefix.*` `raresuffix.*` `runes.*` `setitems.*` `sets.*` `treasureclassex.*` `uniqueitems.*` `weapons.*` | P3 |
| Monster, area and other tables keep D2R's values | not yet | not merged yet | `armtype.*` `automap.*` `charstats.*` `colors.*` `compcode.*` `composit.*` `difficultylevels.*` `elemtypes.*` `events.*` `experience.*` `hireling.*` `hitclass.*` `inventory.*` `levels.*` `lvlmaze.*` `lvlprest.*` `lvlsub.*` `lvltypes.*` `lvlwarp.*` `misscalc.*` `monai.*` `monequip.*` `monlvl.*` `monmode.*` `monplace.*` `monpreset.*` `monprop.*` `monseq.*` `monsounds.*` `monstats.*` `monstats2.*` `montype.*` `monumod.*` `npc.*` `objects.*` `objgroup.*` `objmode.*` `objtype.*` `overlay.*` `pettype.*` `playerclass.*` `plrmode.*` `plrtype.*` `shrines.*` `skillcalc.*` `skilldesc.*` `soundenviron.*` `sounds.*` `storepage.*` `superuniques.*` `uniqueappellation.*` `uniqueprefix.*` `uniquesuffix.*` | P4 |
| PD2 tables cubetype and treasureclass are missing | not yet | D2R's game does not read them; each needs folding into another table or cutting | `cubetype.*` `treasureclass.*` | P3 |
| PD2 tables aiparms, arena, chartemplate, hiredesc, miniskillicons, monitempercent, monname, uniquetitle and weaponclass are missing | not yet | D2R's game does not read them; each needs folding into another table, an engine rebuild, or cutting | `aiparms.*` `arena.*` `chartemplate.*` `hiredesc.*` `miniskillicons.*` `monitempercent.*` `monname.*` `uniquetitle.*` `weaponclass.*` | P4 |
| Guild, arena and ladder labels keep D2R's text (20 keys, `CapsJoinMenu4` to `Guild Portal`) | cut | online features (spec #41) |  | P6 |
| `ModStr5q`, `StrSkill110` and `StrSkill76` keep D2R's text | approximated | PD2's text changes D2R's placeholders, which can crash the game |  | P6 |
| Map items and the PD2 map system (map stats, recipes, events, invaders) | not yet | left out of the alpha (roadmap, spec #41) |  |  |
| New PD2-only areas | not yet | left out of the alpha (roadmap, spec #41) |  |  |
| The Warlock redesign: the alpha ships D2R's Warlock as it is | not yet | left out of the alpha (roadmap, spec #41) |  |  |
| The other classes' engine rules (Necromancer, Paladin, Druid, Sorceress, Assassin) | not yet | left out of the alpha (roadmap, spec #41) |  |  |
| Everything in the core-rules order after the alpha cut (router slot, dead item events, monster and boss kits, cube ops 30/31, investigations) | not yet | left out of the alpha (roadmap, spec #41) |  |  |
| Advanced stats panel | cut | cut by decision (spec #41) |  |  |
| Map-event notifications | cut | cut by decision (spec #41); the map system itself is not yet |  |  |
| Grail panel | cut | cut by decision (spec #41) |  |  |
| PD2's display | cut | cut by decision (spec #41) |  |  |
| Online and controller layers | cut | online play is out of scope (spec #41) |  |  |
| PvP modifiers and battle-royale checks | cut | online features (core-rules) |  |  |
