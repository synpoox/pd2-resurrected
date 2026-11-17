# PD2's map system in the port

How the port rebuilds PD2's map system, decided in [#152](https://github.com/synpoox/pd2-resurrected/issues/152)
(map [#143](https://github.com/synpoox/pd2-resurrected/issues/143)). What PD2's system does is in the
[inventory](https://github.com/synpoox/pd2-resurrected/blob/research/pd2-map-system/docs/research/pd2-map-system.md)
([#144](https://github.com/synpoox/pd2-resurrected/issues/144)). How D2R Reimagined built its own is in their
[study](https://github.com/synpoox/pd2-resurrected/blob/research/reimagined-map-system/docs/research/reimagined-map-system.md)
([#145](https://github.com/synpoox/pd2-resurrected/issues/145)). Terms (map item, map tier, map area, map stat, map event, invader) are in [GLOSSARY.md](../../GLOSSARY.md). The
work items are placed on [roadmap.md](../roadmap.md) by [#210](https://github.com/synpoox/pd2-resurrected/issues/210); MS6 was folded into [IR6](https://github.com/synpoox/pd2-resurrected/issues/241) there.

## The design now (#1303)

[#942](https://github.com/synpoox/pd2-resurrected/issues/942) (merged in [#1058](https://github.com/synpoox/pd2-resurrected/pull/1058)) cut PD2's map items, map stats, tiers and the generated PD2-only map areas: maps are native Cartographer maps (`m01`-`m25`), and `core-events` keeps the map events and uber fights ([ADR 0006](../adr/0006-cartographer-and-core-events.md), [cartographer-migration.md](../cartographer/migration.md)). The owner reversed part of that cut on 2026-11-17 ([#1303](https://github.com/synpoox/pd2-resurrected/issues/1303)): the Cartographer maps get PD2's map progression back, with table edits wherever tables can express it. The sections below `## Scope` describe the older PD2-parity plan; read them for what PD2 does, not for what the port builds.

### Tiers ([#1304](https://github.com/synpoox/pd2-resurrected/issues/1304))

- Every map has one tier, like PD2: T1 opens at area level 87, T2 at 88, T3 at 89 (`maps.txt` `Level`; Cartographer stamps it on each new map as `cartographer_map_level`, shown as `Area Level: N`). A map that already exists keeps the level it was made with (alpha: no conversion).
- A map's misc `type` is its tier type, `t1m`, `t2m` or `t3m`. Each is a direct child of `cmap` (`Equiv1 cmap`, no chain between tiers, so a T3 map is not a `t2m`). The PD2 rows were reused and repointed, so no code is new. `Magic`/`Rare`/`Normal` match `cmap`.
- The tier line is one string per tier: misc `spelldesc 1`, `spelldescstr` `MapTier1`/`MapTier2`/`MapTier3` ("Tier: 1" ...), with `spelldesccalc`, `stat1` and `calc1` empty. A `spelldesc 2` line with `stat1 hpregen` (#1304's carrier) scaled the number like a healing potion, so the tier read 1/3/4 or 2/4/6 depending on the class ([#1316](https://github.com/synpoox/pd2-resurrected/issues/1316)). The map-open log reads `0 item stat(s)`.
- No map rolls magic naturally. `cmap` and the tier types read Magic 0, Rare 1, Normal 0; Cartographer's hook turns a natural magic roll into normal (`Magic` 1 would mean always magic). A magic map made by a cheat stays magic.
- `levelreq` is unchanged. The Catalyst recipe (`cmap` + `iwss`) still matches every map, since each tier type is a child of `cmap`.
- Tier assignment, about 8 per tier, with outdoor and indoor tilesets in each (PD2's own tier where the map has a PD2 namesake: Phlegethon, Tomb of Zoltun Kulle, Shadows of Westmarch, Lost Temple, Sanatorium T1; Horazon's Memory, Ruined Cistern T2; Bastion Keep, Royal Crypts, Arreat Battlefield, Kehjistan Marketplace T3):

| Tier | Area level | Type | Maps (item, tileset; outdoor in bold) |
|---|---|---|---|
| T1 | 87 | `t1m` | m01 Tunneling Depths (Cave), m04 Overgrown Reach (**Jungle**), m05 Frostbitten Fields (**Snow**), m06 Phlegethon (Lava), m12 Shadows of Westmarch (**Wilderness**), m20 Sanatorium (Basement), m23 Tomb of Zoltun Kulle (Tombs), m24 Lost Temple (Spider) |
| T2 | 88 | `t2m` | m02 Bone Vaults (Crypt), m03 Frozen Hollows (Ice), m08 Horazon's Memory (Arcane), m10 Howling Plateau (**Mesa**), m13 Ruins of Corvus (**Kurast**), m15 Cursed Hold (Jail), m16 Khazra Den (Lair), m21 Ruined Cistern (Sewer) |
| T3 | 89 | `t3m` | m07 Battlefields of Eternity (Durance), m09 Arreat Battlefield (**Siege**), m11 Kehjistan Marketplace (**Desert**), m14 Leoric's Passage (Dungeon), m17 Cellar of the Damned (KurastSewer), m18 Bastion Keep (Barracks), m19 Imperial Palace (Harem), m22 Royal Crypts (Catacombs), m25 Dark Temple (Temple) |

### Map monsters ([#1322](https://github.com/synpoox/pd2-resurrected/issues/1322))

`maps.txt` `Monsters` (up to 10 monstats Ids, the column Cartographer writes into the map's level) lists only the `*map` variants restored in #1253. PD2 gave each map area its own per-area monster copies (`zombieSiege`, `mummyIce`, ...); the port has one `*map` row per base class, and its `TreasureClass(H)`, `Champ` and `Unique` cells are PD2's own `Map H2H`/`Map Cast`/`Map Miss`/`Map Swarm` `t1` or `t3` classes (rows unchanged). So a map's list is: the monsters of the PD2 area it is paired with (levels.txt `mon1`-`mon10`, the area's entry level from the misc `len` column, [map-system-dll.md](../pd2/map-system-dll.md)), reduced to the base classes that have a `*map` row, and topped up from the closest-themed area when that left fewer than 4. Excluded: `baalminionmap` (Rarity 0), the event and boss rows (`summonerMap`, `griswoldmap`, ...) and the Void `*UniqueMap` rows. Every listed row is enabled, `isSpawn` 1 and Rarity above 0 (unit test `test_map_monsters.py`).

Tier limit: no `*map` row uses a `t2` class (PD2's `t2` classes live on its per-area copies, which the port does not have), so a T2 map's drops come from its `t1`/`t3` members, and T1 and T3 lists lean `t1` and `t3`. The classes differ only in the `Map Good` area level (85/86/87), so the loot is the same; the difference is for #1307's tier pools. A listed monster whose class is tiered differently from its map is marked in the table.

| Map | PD2 area (code, tier) | Monsters | Why |
|---|---|---|---|
| m01 Tunneling Depths (T1) | Ancestral Trial (t34, T1) | minion, pantherwoman, fallen, slinger, siegebeast, bloodlord, councilmember, reanimatedhorde, fallenshaman | its whole list has `*map` rows; all `t1` |
| m04 Overgrown Reach (T1) | Torajan Jungle (t22, T1) + the jungle level | mosquito, unraveler, cr_archer + sandleaper, thornhulk, councilmember | Torajan Jungle has 3 variants; jungle level fills; all `t1` |
| m05 Frostbitten Fields (T1) | Zhar Ice (T2, nearest snow list) + Frozen Forest | fallenshaman, fallen, reanimatedhorde, siegebeast, sandleaper | no T1 snow area; all `t1` |
| m06 Phlegethon (T1) | Phlegethon (t21, T1) | doomknight, vilemother, zealot, deathmauler, sk_archer, regurgitator, councilmember | same map; PD2's own list is `t3` classes (`baalminion` dropped, Rarity 0) |
| m12 Shadows of Westmarch (T1) | Shadows of Westmarch (t26, T1) | reanimatedhorde, pantherwoman, bloodlord, bighead, siegebeast | same map, all its variants |
| m20 Sanatorium (T1) | Sanatorium (t14, T1) + Blood Moon | sk_archer, bighead + reanimatedhorde, pantherwoman, bloodlord | Sanatorium has 2; the monastery basement level (same theme) fills |
| m23 Tomb of Zoltun Kulle (T1) | Tomb of Zoltun Kulle (t24, T1) | cr_lancer, cr_archer + unraveler, pantherwoman | 2 variants; Act 2 desert classes fill |
| m24 Lost Temple (T1) | Lost Temple (t33, T1) | minion, mosquito, unraveler, arach | same map; all `t1` |
| m02 Bone Vaults (T2) | PD2 Crypts level (nearest crypt) + Zhar Ice (T2) | sk_archer, bighead, fallenshaman, reanimatedhorde, fallen | no T2 crypt; undead fill from the T2 ice area |
| m03 Frozen Hollows (T2) | Zhar Ice (T2) + Ice level | arach, fallenshaman, fallen, reanimatedhorde, siegebeast | ice theme in the same tier |
| m08 Horazon's Memory (T2) | Horazon's Memory (t12, T2) + Arcane level | fetish, minion, mosquito, zealot, cantor, doomknight | same map plus the arcane list; mixed `t1`/`t3` |
| m10 Howling Plateau (T2) | Fortress of Aranoch (t29, T2; outdoor) | minion, deathmauler, sk_archer, bighead | outdoor, same tier |
| m13 Ruins of Corvus (T2) | Ashen Plains (t3a, T2) + Kurast level | unraveler, cr_lancer, goatman, cr_archer, cantor, thornhulk, sandleaper, mosquito | Ashen Plains has 1; Kurast list fills; all `t1` |
| m15 Cursed Hold (T2) | Halls of Torture (t17, T2) | cr_lancer, succubuswitch, sk_archer, zealot, councilmember, regurgitator | jail theme; its level and the torture-halls list |
| m16 Khazra Den (T2) | Throne of Insanity (t32, T2) | goatman, fallen, fallenshaman, siegebeast | Khazra are goatmen; same tier |
| m21 Ruined Cistern (T2) | Horazon's sewer level + Ruined Cistern (T2) | minion, mosquito, fetish, siegebeast, fallenshaman | sewer theme, same tier |
| m07 Battlefields of Eternity (T3) | Kyovashad (t3b, T3) | minion, vilemother, cantor + doomknight, oblivionknight, regurgitator | Act 4 classes for Durance |
| m09 Arreat Battlefield (T3) | Arreat Battlefield (t23, T3) + Siege level | goatman, bighead, reanimatedhorde, thornhulk, mosquito, deathmauler | its own level lists 1 variant; Siege level fills |
| m11 Kehjistan Marketplace (T3) | Kehjistan Marketplace (t39, T3) + Desert level | mosquito, cr_archer, unraveler, arach, sandleaper | same map and desert list |
| m14 Leoric's Passage (T3) | Blood Moon (t35, T3; dungeon) | reanimatedhorde, pantherwoman, bloodlord, bighead, siegebeast | dungeon theme, same tier |
| m17 Cellar of the Damned (T3) | River of Blood (t31, T3; Kurast level) | cr_lancer, goatman, cr_archer, bighead, cantor, unraveler, arach, thornhulk, sandleaper, mosquito | 10 of 10, Kurast sewer; all `t1` |
| m18 Bastion Keep (T3) | Bastion Keep (t13, T3) + Fortress level | goatman, fallen, fallenshaman, siegebeast, sandleaper, deathmauler, councilmember | same map plus the fortress list |
| m19 Imperial Palace (T3) | Skovos Stronghold (t28, T3; Imperial level) | fetish, succubuswitch, cantor, zealot | palace list (`baalminion` dropped) |
| m22 Royal Crypts (T3) | Royal Crypts (t15, T3) + Crypts level | cr_archer, sk_archer, bighead, zealot, bloodlord | same map plus the crypt list |
| m25 Dark Temple (T3) | PD2 Dark Temple level | zealot, siegebeast, councilmember, doomknight | same theme |

Not done here (other children of #1303): the map affixes and their tier bands (no affix table names `t1m`/`t2m`/`t3m` yet; [#1305](https://github.com/synpoox/pd2-resurrected/issues/1305)), recipes, drops.

## Scope

In: the tiered maps (map tiers 1-3), dungeons and unique maps; their affixes and uniques; the map recipes; the map stats
on monsters, players and the area; the map events; the invaders.

Out:

- Uber keys (`ubtm`, `dcma`, `rtma`, `luca`, `uba`) belong to the uber-boss milestone.
- The PvP arena maps (`t61`, `t62`) are cut, as all PvP is ([#151](https://github.com/synpoox/pd2-resurrected/issues/151)).
- The Butcher's Lair event is cut because PD2 has it disabled.
- The Maps stash tab is cut, as PD2's stash layouts are, so maps sit in the normal stash. The `.kills maps/dungeons`
  counters are cut too.

The item codes do not give the tier. The item type does (`t1m`..`t3m`): for example, `t12` Horazon's Memory is a tier-2
map and `t14` Sanatorium is tier 1. The research's "T4" and "T5" are only the item types of dungeons and unique maps.

## Decisions

- **Parity: Exact.** The map tables (items, affixes, uniques, recipes, treasure classes) are already merged ([P3](https://github.com/synpoox/pd2-resurrected/issues/47)). The
  code is built to PD2's behaviour. If a map stat cannot be reproduced, only that stat gets its own approximated row on
  the deviation list; no whole group of stats is approximated up front.
- **Opening a map:** a right-click in Harrogath, as in PD2 (misc `pSpell` 12 -> 16). PD2's gates apply: Hell, level 80+,
  the Ancients killed, one map or uber area per game. The plugin makes the red portal itself.
  Reimagined's cube output `Red Portal,lvl=<id>` shows that a red portal reaches any Act V level, but it is not used as
  a recipe, because a recipe would change the controls and skip the gates.
- **Which area a map opens** comes from `ProjectDiablo.dll`, read with Ghidra, never paired by name. [MS1](https://github.com/synpoox/pd2-resurrected/issues/216) found that
  the DLL takes it from the misc `len` column, so the port reads that column
  ([pd2-map-system-dll.md](../pd2/map-system-dll.md), which also holds the gates, stat routing, op 30 and the
  event roll).
- **Map drops:** the [P3](https://github.com/synpoox/pd2-resurrected/issues/47) no-drop lines in `merge/item-tables.txt` come back in the same work item that makes maps openable,
  so no build drops a map you cannot use.
- **Stacking:** normal maps stack to 50 and split as in PD2 (Ctrl+left-click with the stack held). Every PD2 stackable
  shares this, so it is built once in [#316](https://github.com/synpoox/pd2-resurrected/issues/316), not in a map item ([#210](https://github.com/synpoox/pd2-resurrected/issues/210)).
- **Invaders** run their table AIs (the vanilla `BloodRaven`, `Summoner` and so on). That is an approximated deviation.
  The portal's think and the invaders' ears and drops are PD2's (MS10, `plugin/core-events/lane-c/invaders.cpp`).
  PD2's own invader brain (`AiThink4.cpp`) replaces them in [MS11](https://github.com/synpoox/pd2-resurrected/issues/225).
- **Plugins:** `core-events` (Shared) holds the code that belongs only to the map system: opening maps, map stats, cube
  op 30, map events and invaders. It is a plugin of its own because the map system is large enough to be used outside
  this project ([ADR 0004](../adr/0004-plugins-split-by-multiplayer-scope.md), amended). Rules the map system shares with
  other content stay in `core-rules`: the Teleport 3 blocker check, the
  Find Item and Desecrate monster lists, and the death novas. `core-events` never calls `core-rules`.
- **Rules deferred to the map plan** (core-rules rows and their deviation rows) join the work item that first brings in
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
| [MS1](https://github.com/synpoox/pd2-resurrected/issues/216) | Investigation, Ghidra on `ProjectDiablo.dll`: which area each map item opens, the gate checks, how map stats are applied and how density and rarity scale, cube op 30's meaning (block always or block stacked), the event roll and the levels the Vault, Horazon and Void events open; plus whether a plugin can open a red portal to a map area | none | none |
| [MS2](https://github.com/synpoox/pd2-resurrected/issues/217) | Open a map: `pSpell` 16, the gates, one per game, the red portal to its area; maps drop again; the Find Item and Desecrate lists for map-area monsters | core-events, core-rules (lists) | [MS1](https://github.com/synpoox/pd2-resurrected/issues/216), the level pipeline for the tiered map areas |
| [MS3](https://github.com/synpoox/pd2-resurrected/issues/218) | Monster and player map stats (`map_mon_*`, `map_play_*`); the level life regen cap, which [MS3](https://github.com/synpoox/pd2-resurrected/issues/218) found applies only in the PvP arenas, so it is cut with PvP | core-events | [MS2](https://github.com/synpoox/pd2-resurrected/issues/217) |
| [MS4](https://github.com/synpoox/pd2-resurrected/issues/219) | Area map stats (`map_glob_*`): density, area level, monster rarity, extra monster groups, extra boss, fortify, drop stats | core-events | [MS2](https://github.com/synpoox/pd2-resurrected/issues/217) |
| [MS4b](https://github.com/synpoox/pd2-resurrected/issues/286) | The extra boss and the drop chance area stats (corrupted, socketed, ethereal), [#286](https://github.com/synpoox/pd2-resurrected/issues/286) | core-events | [MS4](https://github.com/synpoox/pd2-resurrected/issues/219) |
| [MS5](https://github.com/synpoox/pd2-resurrected/issues/220) | Map recipes: cube op 30, and one recipe per group checked in game | core-events | [MS1](https://github.com/synpoox/pd2-resurrected/issues/216) |
| [MS7](https://github.com/synpoox/pd2-resurrected/issues/221) | Dungeons and unique maps: their multi-level areas, the Teleport 3 blocker check, the Nihlathak map death nova | core-events, core-rules (Teleport 3, nova) | [MS2](https://github.com/synpoox/pd2-resurrected/issues/217), the level pipeline for those areas |
| [MS8](https://github.com/synpoox/pd2-resurrected/issues/222) | Events that only spawn: Dark Wanderer, Shadow of Mendeln, Treasure Fallen, Gheed, Altar of the Catalyst, Spire of Darkness | core-events | [MS4](https://github.com/synpoox/pd2-resurrected/issues/219) |
| [MS9](https://github.com/synpoox/pd2-resurrected/issues/223) | Events that open areas: Horazon, Void/Tomb | core-events | [MS8](https://github.com/synpoox/pd2-resurrected/issues/222), [MS7](https://github.com/synpoox/pd2-resurrected/issues/221) |
| [MS10](https://github.com/synpoox/pd2-resurrected/issues/224) | Invaders on their table AIs (approximated), with their ears | core-events | [MS8](https://github.com/synpoox/pd2-resurrected/issues/222) |
| [MS11](https://github.com/synpoox/pd2-resurrected/issues/225) | PD2's invader brain (`AiThink4.cpp`) in place of the table AIs | core-events | [MS10](https://github.com/synpoox/pd2-resurrected/issues/224) |

## Game check

The map system is done when all of the following pass, on a level 90+ character in Hell:

- One tier-1, one tier-2 and one tier-3 map, one dungeon and one unique map open, each with its map stats logged on the
  monsters, the players and the area.
- Every event in scope can be forced with a Catalyst Shard (`map_force_event`).
- One recipe from each recipe group in the inventory works.
