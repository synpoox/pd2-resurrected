# PD2's uber bosses in the port

How the port rebuilds PD2's uber bosses, decided in [#153](https://github.com/synpoox/pd2-resurrected/issues/153)
(map [#143](https://github.com/synpoox/pd2-resurrected/issues/143)). What PD2's Diablo Clone, Rathma, Lucion and
Desecration do is in the
[inventory](https://github.com/synpoox/pd2-resurrected/blob/research/pd2-uber-bosses/docs/research/pd2-uber-bosses.md)
([#147](https://github.com/synpoox/pd2-resurrected/issues/147)). PD2's Pandemonium and Uber Tristram are inventoried in
[#185](https://github.com/synpoox/pd2-resurrected/issues/185). What `ProjectDiablo.dll` does for each fight ([UB1](https://github.com/synpoox/pd2-resurrected/issues/226)) is in
[pd2-uber-bosses-dll.md](research/pd2-uber-bosses-dll.md). Terms (uber boss, uber key, uber tier, Desecration,
Terror Zone) are in [GLOSSARY.md](../GLOSSARY.md). The work items are placed on [roadmap.md](roadmap.md) by
[#210](https://github.com/synpoox/pd2-resurrected/issues/210).

## Scope

In: PD2's Pandemonium chain (keys `pk1`-`pk3`, the mini-ubers in D2R's levels 133-135, Horn, Eye and Brain, `ubtm`)
through Uber Tristram (level 185); Diablo Clone (`dcma`); Rathma and Mendeln (`rtma`); Lucion (`luca`) with the Temple of the Triune
and Desecration. That covers their uber keys and tiers, areas, area rules, monsters and phase scripts, and drops.

Out:

- PD2's Uber Ancients (`uba`, level 168) is cut. D2R has its own Uber Ancients, which stays.
- D2R's Uber Tristram (the organ portal and level 136) and its world-event Diablo Clone (`diabloclone`) are turned
  off, because PD2's fights replace them ([d2r-only-content.md](d2r-only-content.md)). D2R's keys and mini-uber levels
  133-135 stay: PD2 uses them almost unchanged ([#185](https://github.com/synpoox/pd2-resurrected/issues/185)).
- The season trophies and "of Clout" uniques wait for beta run 2, since they reward a league. The uber bosses'
  `.kills` counters are cut, as the map counters were.

## Decisions

- **Parity: Exact**, as for the map system. If a mechanic cannot be reproduced, only that mechanic gets its own
  approximated row on the deviation list. Phase thresholds and odds known only from the wiki become the baseline only
  after a check on the PD2 install.
- **Opening:** an uber key is used at the Blood Fountain in Harrogath (misc `pSpell` 12 -> 16), as maps are. PD2's
  one map or uber area per game is a single gate shared with the map system.
- **Plugin:** the uber bosses' code goes in `core-maps` (Shared): the opening, the area rules, the tier scaling, the
  phase scripts, the guaranteed drops and rolls, the key drop rule and Desecration
  ([ADR 0004](adr/0004-plugins-split-by-multiplayer-scope.md), amended). The boss skill functions already on
  core-rules rows (UberDiabSummon/Run, RathmaSummon, the Lucion kit, the death novas, the bone spear, the uber
  immolation arrow) stay in `core-rules` and are built in the work item of the fight that uses them.
- **Talisman of Transgression:** PD2's corrupted zones are cut, so it drops from monsters in an active Terror Zone at
  PD2's `lucc` odds. That is an approximated row.
- **Desecration:** made in the cube in the Temple of the Triune, one attempt per Lucion kill. D2R has no Desecration
  panel. The roll stays Exact: 50% at tier 0 and 60% at tiers 1 and 2, PD2's eight-mod pool, and on failure a rare
  that keeps two random corruption mods. Using the cube instead of the panel is an approximated row.
- **Ingredients from other features** are built with those features: Pure Demonic Essence from map bosses (map stat
  211, [MS4](https://github.com/synpoox/pd2-resurrected/issues/219)) and the Splinter of the Void from the Shadow of Mendeln event ([MS8](https://github.com/synpoox/pd2-resurrected/issues/222)). They do not block a fight: a fight's
  game check spawns its ingredients from the console.

## Work items

| # | Work item | Plugin | Blocked by |
|---|---|---|---|
| [UB1](https://github.com/synpoox/pd2-resurrected/issues/226) | Investigation, Ghidra on `ProjectDiablo.dll`: the fountain opening, the area rules, the tier scaling (`uberscaling`), the phase scripts, the guaranteed drops and rolls, the key drop odds (the odds-only TCs), Desecration; the wiki-only numbers checked on the PD2 install | none | [#185](https://github.com/synpoox/pd2-resurrected/issues/185) |
| [UB2](https://github.com/synpoox/pd2-resurrected/issues/227) | Opening: uber keys used at the Blood Fountain, the uber tiers and their scaling, the area rules (buffs stripped, mercenary killed, tier-0 portals), the one-per-game gate shared with maps; uber keys drop again; the key drop rule | core-maps | [UB1](https://github.com/synpoox/pd2-resurrected/issues/226), [MS2](https://github.com/synpoox/pd2-resurrected/issues/217), the level pipeline for the uber areas |
| [UB3](https://github.com/synpoox/pd2-resurrected/issues/228) | Pandemonium and Uber Tristram: the keys' portal to levels 133-135, the mini-ubers, Horn, Eye and Brain, `ubtm`, level 185 and the fight, the Black Soulstone and Hellfire Torch drops | core-maps | [UB2](https://github.com/synpoox/pd2-resurrected/issues/227) |
| [UB4](https://github.com/synpoox/pd2-resurrected/issues/229) | Diablo Clone: the phases, UberDiabSummon/Run, the adds and trapped souls, Annihilus, the 1-in-200 uniques | core-maps, core-rules (skills) | [UB2](https://github.com/synpoox/pd2-resurrected/issues/227) |
| [UB5](https://github.com/synpoox/pd2-resurrected/issues/230) | Rathma and Mendeln: the six stages, shared life, RathmaSummon, the bone spear, the death nova, the cold missiles (srvdo 139, data), the Tainted Worldstone Shard and the other rolls | core-maps, core-rules (skills) | [UB2](https://github.com/synpoox/pd2-resurrected/issues/227) |
| [UB6](https://github.com/synpoox/pd2-resurrected/issues/231) | Lucion: the kit, Royal Fire (srvdo 143, cltdo 83; Radament's too), the portal phases, the death novas, the Temple and its chests, the Demonic Cube roll, the Talisman's Terror Zone source | core-maps, core-rules (skills) | [UB2](https://github.com/synpoox/pd2-resurrected/issues/227) |
| [UB7](https://github.com/synpoox/pd2-resurrected/issues/232) | Desecration in the cube (above), stats 206/207 | core-maps | [UB6](https://github.com/synpoox/pd2-resurrected/issues/231) |

## Game check

The uber bosses are done when all of the following pass, on a level 90+ character in Hell:

- Each fight opens at tiers 0, 1 and 2 and is won.
- Each fight's guaranteed drop appears, and each 1-in-N roll is logged.
- One Desecration success and one bricked amulet are seen with the roll forced.
