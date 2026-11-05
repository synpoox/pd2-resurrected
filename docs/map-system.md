# PD2's map system in the port

How the port rebuilds PD2's map system, decided in [#152](https://github.com/synpoox/pd2-resurrected/issues/152)
(map [#143](https://github.com/synpoox/pd2-resurrected/issues/143)). What PD2's system does is in the
[inventory](https://github.com/synpoox/pd2-resurrected/blob/research/pd2-map-system/docs/research/pd2-map-system.md)
(#144). How D2R Reimagined built its own is in their
[study](https://github.com/synpoox/pd2-resurrected/blob/research/reimagined-map-system/docs/research/reimagined-map-system.md)
(#145). Terms (map item, map tier, map area, map stat, map event, invader) are in [GLOSSARY.md](../GLOSSARY.md). The
work items are placed on [roadmap.md](roadmap.md) by [#210](https://github.com/synpoox/pd2-resurrected/issues/210); MS6 was folded into IR6 there.

## Scope

In: the tiered maps (map tiers 1-3), dungeons and unique maps; their affixes and uniques; the map recipes; the map stats
on monsters, players and the area; the map events; the invaders.

Out:

- Uber keys (`ubtm`, `dcma`, `rtma`, `luca`, `uba`) belong to the uber-boss milestone.
- The PvP arena maps (`t61`, `t62`) are cut, as all PvP is (#151).
- The Butcher's Lair event is cut because PD2 has it disabled.
- The Maps stash tab is cut, as PD2's stash layouts are, so maps sit in the normal stash. The `.kills maps/dungeons`
  counters are cut too.

The item codes do not give the tier. The item type does (`t1m`..`t3m`): for example, `t12` Horazon's Memory is a tier-2
map and `t14` Sanatorium is tier 1. The research's "T4" and "T5" are only the item types of dungeons and unique maps.

## Decisions

- **Parity: Exact.** The map tables (items, affixes, uniques, recipes, treasure classes) are already merged (P3). The
  code is built to PD2's behaviour. If a map stat cannot be reproduced, only that stat gets its own approximated row on
  the unported list; no whole group of stats is approximated up front.
- **Opening a map:** a right-click in Harrogath, as in PD2 (misc `pSpell` 12 -> 16). PD2's gates apply: Hell, level 80+,
  the Ancients killed, one map or uber area per game. The plugin makes the red portal itself.
  Reimagined's cube output `Red Portal,lvl=<id>` shows that a red portal reaches any Act V level, but it is not used as
  a recipe, because a recipe would change the controls and skip the gates.
- **Which area a map opens** comes from `ProjectDiablo.dll`, read with Ghidra, never paired by name. MS1 found that
  the DLL takes it from the misc `len` column, so the port reads that column
  ([pd2-map-system-dll.md](research/pd2-map-system-dll.md), which also holds the gates, stat routing, op 30 and the
  event roll).
- **Map drops:** the P3 no-drop lines in `merge/item-tables.txt` come back in the same work item that makes maps openable,
  so no build drops a map you cannot use.
- **Stacking:** normal maps stack to 50 and split as in PD2 (`HOTKEY_ItemQuantity`). Every PD2 stackable shares this,
  so it is built once in IR6 ([item-rules.md](item-rules.md)), not in a map item (#210).
- **Invaders** run their table AIs (the vanilla `BloodRaven`, `Summoner` and so on). That is an approximated deviation.
  PD2's own invader brain (`AiThink4.cpp`) replaces them in MS11.
- **Plugins:** `core-maps` (Shared) holds the code that belongs only to the map system: opening maps, map stats, cube
  op 30, map events and invaders. It is a plugin of its own because the map system is large enough to be used outside
  this project ([ADR 0004](adr/0004-plugins-split-by-multiplayer-scope.md), amended). Rules the map system shares with
  other content stay in `core-rules`: the Teleport 3 path search, the
  Find Item and Desecrate monster lists, and the death novas. `core-maps` never calls `core-rules`.
- **Rules deferred to the map plan** (core-rules rows and their unported rows) join the work item that first brings in
  their levels or monsters (table below).
- **D2R Reimagined:** we copy no code from them. Their GPL-3.0 code stays out (ADR 0005), and their map plugin is
  closed source. We use their engine findings as facts, with credit in the research doc: one world rectangle per Act V
  level, automap `Layer` under 100, `Position=0` on new levels, a self-linked entry warp so the portal lands at the
  start, and maze routines that place stairs only for fixed level ids. If a build item hits an engine limit that we
  cannot solve, reverse engineering their `d2rl-maps.dll` with Ghidra becomes an investigation of its own. It is for
  knowledge only, and no code is copied.
- **Level ids:** D2R's merged `levels` table has 138 rows, so PD2's 74 areas append to about ids 139-212, under the 255
  limit.

## Work items

| # | Work item | Plugin | Blocked by |
|---|---|---|---|
| MS1 | Investigation, Ghidra on `ProjectDiablo.dll`: which area each map item opens, the gate checks, how map stats are applied and how density and rarity scale, cube op 30's meaning (block always or block stacked), the event roll and the levels the Vault, Horazon and Void events open; plus whether a plugin can open a red portal to a map area | none | none |
| MS2 | Open a map: `pSpell` 16, the gates, one per game, the red portal to its area; maps drop again; the Find Item and Desecrate lists for map-area monsters | core-maps, core-rules (lists) | MS1, the level pipeline for the tiered map areas |
| MS3 | Monster and player map stats (`map_mon_*`, `map_play_*`); the level life regen cap, which MS3 found applies only in the PvP arenas, so it is cut with PvP | core-maps | MS2 |
| MS4 | Area map stats (`map_glob_*`): density, area level, monster rarity, extra monster groups, extra boss, fortify, drop stats | core-maps | MS2 |
| MS5 | Map recipes: cube op 30, and one recipe per group checked in game | core-maps | MS1 |
| MS7 | Dungeons and unique maps: their multi-level areas, the Teleport 3 path search, the Nihlathak map death nova | core-maps, core-rules (Teleport 3, nova) | MS2, the level pipeline for those areas |
| MS8 | Events that only spawn: Dark Wanderer, Shadow of Mendeln, Treasure Fallen, Gheed, Altar of the Catalyst, Spire of Darkness | core-maps | MS4 |
| MS9 | Events that open areas: Horazon, Void/Tomb | core-maps | MS8, MS7 |
| MS10 | Invaders on their table AIs (approximated), with their ears | core-maps | MS8 |
| MS11 | PD2's invader brain (`AiThink4.cpp`) in place of the table AIs | core-maps | MS10 |

## Game check

The map system is done when all of the following pass, on a level 90+ character in Hell:

- One tier-1, one tier-2 and one tier-3 map, one dungeon and one unique map open, each with its map stats logged on the
  monsters, the players and the area.
- Every event in scope can be forced with a Catalyst Shard (`map_force_event`).
- One recipe from each recipe group in the inventory works.
