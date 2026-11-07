# PD2's map system in ProjectDiablo.dll

Resolves [MS1](https://github.com/synpoox/pd2-resurrected/issues/216) ([#216](https://github.com/synpoox/pd2-resurrected/issues/216), map system [#143](https://github.com/synpoox/pd2-resurrected/issues/143), design
[map-system.md](../map-system.md)). It reads `ProjectDiablo.dll` with Ghidra (project `PD2.gpr`, AGENTS.md) to settle
what the table inventory ([#144](https://github.com/synpoox/pd2-resurrected/issues/144)) left to the DLL: which area each map item opens, the gates, how map stats reach monsters,
players and the area, how density and rarity scale, cube op 30, the event roll, and the levels the events open. It ends
with whether a D2R plugin can open a red portal to a map area. Findings only: no PD2 or Blizzard file content is in this repo.

Addresses are RVAs from the DLL's `0x10000000` base. Level ids are PD2's `levels` ids. Stat ids are PD2's
`itemstatcost` ids (the port remaps them, `merge/ids-itemstatcost.tsv`).

## Answer in brief

- **The area is data, not a DLL table.** The map handler evaluates the item's misc `len` column as an item calc
  (D2Common ordinal 10428) and uses the result as the level id. Every map row holds its level there (section 1), so the
  port reads `len` too. The `missiletype` column picks the portal object (584 `xactportal_red` for maps).
- **Gates:** Hell, character level 80+, Rite of Passage done (Hell quest 39), standing in Harrogath (level 109), and no
  map yet in this game (the game's map-event field is 0). Uber areas skip the level-80 check. Levels 152, 153 (the
  disabled Cathedral of Light) and 255 always fail.
- **Map stats are routed by the `Divide` column.** PD2 repurposes `itemstatcost.Divide` on `map_*` stats: below 1000 the
  stat goes to monsters as that vanilla stat id; `1xxx` goes to players as stat `xxx`; `2xxx` is area slot `xxx`; `3xxx`
  is phys-as-extra damage; `1024` (the default) goes to monsters unchanged. Monsters get their list on spawn, players on
  entering a map area, both as a stat list under state 198.
- **Density and rarity scale by `1 + v/100`.** They multiply the level's monster-region `MonDen` and its champion/unique
  pack min and max. Area level adds `v`. Fortify (skirmish mode) multiplies all three by `v/100` and adds fixed monster
  stats.
- **Cube op 30 blocks always.** If an op-30 row matches the cube's contents, the transmute stops and nothing is made, so
  the row shadows the rows after it. Its inputs (for example `nor` maps) decide what it blocks; stacking does not.
- **Event roll:** tiered maps only. Chance by the map's quality: rare 34%, magic 17%, normal 11%; a Catalyst Shard
  (`map_force_event`) forces one. The event is one of 9, uniform (8 when forced: no Altar of the Catalyst). Dungeons,
  unique maps, uber and PvP areas never roll.
- **Events that open areas:** the Void/Tomb event (one event: `MapEventNotifyVoid` is never used) spawns a gold portal to
  level 207. Horazon's death opens a red portal to a random tiered map of the same or the next tier, never the current
  level, and copies the map's area stats onto it.
- **A plugin can open a red portal to a map area.** D2R's portal maker (`0x432ce0` in the 93847 dump) takes the
  destination level and object 60, the red portal. It needs the same act and a level id up to 255; the port's map
  levels (Act V, about 139-212) fit both. PD2's own portal objects (`OperateFn` 77) do not exist in D2R's 75-entry
  operate table, so the port uses object 60.

## 1. Opening a map

The use handler is `0x383060`, called from `0x36b9f0` with (game, player, item). Its order:

1. Refuse if the player unit's `+0x6C` word is set, or the game's map-event field (`game+0x1DF4`) is above 0. Value
   0x28 (set by the Uber Tristram cube recipe, op 28) is the one exception: it lets levels 136 and 185 open.
2. Look up the item's `ItemsTxt` record and evaluate its `len` calc (`+0xB0`) through D2Common ordinal 10428 (the item
   calc evaluator). The result is the target level. `missiletype` (`+0xFA`) is the portal object; 0 means object 60.
3. Refuse levels 152, 153 and 255 (`0x37f450`).
4. PvP arenas (157, 159, 166; `0x37c7b0`) only open from the Rogue Encampment (level 1). Cut with PvP.
5. Otherwise: refuse in Normal and Nightmare. In Hell, refuse a character below level 80 unless the target is in the
   uber set {136, 137, 161, 162, 163, 168, 185, 188, 189}. Refuse if the Hell quest record lacks quest 39 (Rite of
   Passage, the Ancients). Refuse unless the player stands in level 109 (Harrogath).
6. Uber keys whose target is in {137, 161, 168, 188} go to their own branch (uber milestone, not covered here).
7. Make the portal pair next to the player with `0x38a300` (room, x, y, level, object): it places the object, sets its
   destination and builds the return portal in the target level.
8. Route the map's stats (section 2), send the client packet 0x55 with up to 24 of them (the map tooltip in the HUD),
   and apply the area stats to the level (`0x38b4d0`).
9. Set the map-event field: unique maps get their own value (0x1E-0x24, below); otherwise the event roll (section 4).

| Map | Code | Level (`len`) | Map | Code | Level (`len`) |
|---|---|---|---|---|---|
| T1 Ruins of Viz-Jun | t11 | 143 | T2 Horazon's Memory | t12 | 142 |
| T1 Sanatorium | t14 | 167 | T2 Ruined Cistern | t16 | 174 |
| T1 Poisoned Well | t18 | 208 | T2 Halls of Torture | t17 | 193 |
| T1 Phlegethon | t21 | 145 | T2 Sewers of Harrogath | t25 | 141 |
| T1 Torajan Jungle | t22 | 148 | T2 Fortress of Aranoch | t29 | 209 |
| T1 Tomb of Zoltun Kulle | t24 | 151 | T2 Descent from Mount Arreat | t2a | 210 |
| T1 Shadows of Westmarch | t26 | 169 | T2 Throne of Insanity | t32 | 150 |
| T1 Lost Temple | t33 | 158 | T2 Pandemonium Citadel | t37 | 156 |
| T1 Ancestral Trial | t34 | 146 | T2 Canyon of Sescheron | t38 | 160 |
| T1 Fall of Caldeum | t36 | 155 | T2 Ashen Plains | t3a | 175 |
| T3 Bastion Keep | t13 | 149 | Dungeon Plains of Torment | t42 | 164 |
| T3 Royal Crypts | t15 | 170 | Dungeon Sanctuary of Sin | t43 | 171 |
| T3 Arreat Battlefield | t23 | 139 | Dungeon Steppes of Daken-Shar | t44 | 186 |
| T3 Demon Road | t27 | 191 | Unique Zhar's Sanctum | t51 | 176 |
| T3 Skovos Stronghold | t28 | 190 | Unique Warlord of Blood | t52 | 181 |
| T3 River of Blood | t31 | 144 | Unique Fallen Gardens | t53 | 183 |
| T3 Blood Moon | t35 | 154 | Unique Imperial Palace | t54 | 192 |
| T3 Kehjistan Marketplace | t39 | 147 | Unique Outer Void | t55 | 180 |
| T3 Kyovashad | t3b | 201 | Unique City of Ureh | t56 | 195 |
| Disabled Cathedral of Light | t41 | 152 (refused) | Unique Djinn's Domain | t57 | 197 |
| | | | Unique Na-Krul's Abyss | t58 | 200 |

The handler applies the area stats to every level of a multi-level map, hard-coded by entry level: Zhar 176-179,
Warlord of Blood 181-182, Fallen Gardens 183-184, Imperial Palace 192 and 194, Ureh 195-196, Djinn's Domain 197-199, and
the dungeons 164-165, 171-172, 186-187 (and the disabled 152-153). Outer Void 180 and Na-Krul 200 are one level. The
entry level is the `len` value above.

**Event-type values** in `game+0x1DF4`: 0 none yet; 9 dungeon (no event); 10 no event rolled; 11-21 the events
(section 4); 0x1E Zhar (176), 0x1F Warlord of Blood (181), 0x20 Fallen Gardens (183), 0x21 Outer Void (180), 0x22
Imperial Palace (192), 0x23 Ureh (195), 0x24 Djinn's Domain (197); 0x28-0x2D uber areas.

## 2. Map stats

**Routing.** The handler walks the item's stat list and reads each stat's `itemstatcost` record at `+0x0C`, which is
the `Divide` column. PD2 uses it as a route:

| `Divide` | Goes to | Stat applied | Examples |
|---|---|---|---|
| 0 | nothing, except stat 500 `map_force_event`, which forces an event | | |
| 1-999 | monster list | the vanilla stat whose id is the value | `map_mon_firemindam` 48 -> `firemindam`; `map_mon_ac%` 16 -> `item_armor_percent` |
| 1024 (default) | monster list | the stat itself | `heroic`, `corrupted`, `corruptor`, `map_defense` (no effect on monsters) |
| 1000-1999 | player list | stat `Divide - 1000` | `map_play_magicbonus` 1080 -> `item_magicbonus` |
| 2000-2999 | area list | slot `Divide - 2000` | `map_glob_density` 2000 -> slot 0 |
| 3000-3999 | phys-as-extra handler | | the five `map_mon_phys_as_extra_*` |

The 1-999 band holds 57 stats (every `map_mon_*` stat except the five phys-as-extra ones, and the six class ear-drop stats), 1xxx the 22 `map_play_*`
stats, and 2xxx the 25 `map_glob_*` stats plus `treacherous` and `phasing`. The port must keep these `Divide` values, or carry the routing in a table of
its own.

**Area slots** (`game+0x2630`, 8 bytes each: slot, value):

| Slot | Stats | Effect | Read by |
|---|---|---|---|
| 0 | `map_glob_density` | level `MonDen` x (1 + v/100) | `0x38b4d0` |
| 1 | `map_glob_arealevel` | area level + v | `0x38b4d0`, `0x2fc400` |
| 2 | `map_glob_monsterrarity` | champion/unique pack min and max x (1 + v/100) | `0x38b4d0` |
| 3 | the nine `map_glob_add_mon_*` | one more monster group: v is the monstats id (13 groups at most) | `0x38b4d0` |
| 4 | `map_glob_skirmish_mode` (fortify) | density and pack counts x v/100; monsters +100 `item_maxhp_percent`, +40 `damagepercent`, +40 to each of the four elemental masteries, +20 `passive_mag_mastery`, and `map_glob_skirmish_mode` (493) = v | `0x38b4d0` |
| 5 | `map_glob_extra_boss` | extra boss | `0x38bfc0` |
| 6 | `map_glob_dropcorrupted` | v% chance a drop is corrupted | drop hook `0x372180` |
| 7-9, 15 | boss drop stats (skillers, corrupted unique, uber mats, puzzle box, facet, treasure) | boss drops | not read here |
| 10 | `map_glob_sundermonsters` | (also the Horazon entry, section 4) | not read here |
| 11 | `map_glob_dropsocketed` | v% chance a drop gets sockets | drop hook `0x372180` |
| 12 | `map_glob_dropbonus` | monsters get `map_glob_dropbonus` (273) = v | `0x38b4d0` |
| 13 | `map_glob_dropethereal` | v% chance a drop is ethereal | drop hook `0x372180` |
| 14 | `treacherous`, `phasing` (ear recipes) | monsters +100 `item_maxhp_percent` and `treacherous` (276) = v | `0x38b4d0` |

**Where the area slots land.** `0x38b4d0` (game, level, first) writes D2Game's monster region of that level
(`game+0xF0 + level*4`): `+0x2B8` `MonDen`, `+0x2BC` and `+0x2BD` the boss (champion/unique pack) min and max, `+0x14` the
13 monster groups, `+0x2DC` and `+0x2E0` the monster level. Those are the same fields D2R fills from the `levels` row
(`MonDen`, `MonUMin`/`MonUMax`, `mon1`-`mon25`, `MonLvlEx`), so the port can scale them where D2R builds a level's
monster region. Density's multiplier is also kept in `game+0x262C`. Slots 4, 12 and 14 add stats to the monster list only
on the first call.

**Monsters.** On spawn (`0x38af90`, called from `0x371590` and `0x371c30`), a monster in a map area (levels 137-210,
not the guild halls 202-206; `0x37c510`) gets the monster list. A monster whose monstats `Align` byte (`+0x4C`) is set
(allies, neutral) gets an empty list. The list is applied by `0x38aa00` as a stat list under state 198. Two cases are
rewritten on the way:

- `item_maxhp_percent` multiplies the monster's current `maxhp` by `v/100` directly.
- Phys-as-extra (stats 432-436, extra elemental damage equal to a share of the monster's physical damage) adds elemental damage computed from the monster's `mindamage` and `maxdamage`: fire 48/49,
  lightning 50/51, magic 52/53, cold 54/55 with length 25, poison 57/58 with length 125.

**Players.** A hook on the player's level change (`0x38b000`) applies the player list under state 198 when the new level
is a map area, and removes state 198 and state 212 when it is not.

## 3. Cube op 30

Two functions decide it. `0x365fe0` collects the candidate rows for the cube's contents. Every other op must pass its
filters there (difficulty, class, input count, and a stackable-item check); op-30 rows skip them. `0x366f40` then walks
the candidates in order. The first row whose inputs fully match (`0x364e30`) is used, and if that row's op is 30, the
function returns without making anything. Op 31 calls `CubeFunctions.cpp` (`0x3669b0`) instead.

So op 30 is **block always**: a matching op-30 row ends the transmute. It narrows by its inputs, as the inventory's first
reading said (`nor` maps for heroic, corrupt, fortify, Catalyst and ears; `uni` maps for ears). Whether the input is a
stack does not matter. D2R passes unknown ops, so without a plugin an op-30 row would run as a recipe ([MS5](https://github.com/synpoox/pd2-resurrected/issues/220)).

## 4. Map events

**Roll** (in `0x383060`, after the stats). Tiered maps only: the roll is skipped when the level is in the dungeon set
{152, 153, 164, 165, 171, 172, 186, 187} (they get 9), the unique-map set {176-184, 192, 194-200}, the uber set or the
PvP set.

- Chance: rare (and any other quality) 34%, magic 17%, normal 11%, by the item's quality. A map carrying stat 500
  `map_force_event` always rolls.
- Pick: up to 40 draws of `rand() % 12`; the first draw that names an event wins, so each of the 9 events has an equal
  chance (8 when forced, which skips the Altar of the Catalyst).

| Draw | Value | Event | Notify string | What spawns |
|---|---|---|---|---|
| 0, 3, 7 | | none, draw again | | |
| 1 | 11 | Shadow of Mendeln | `MapEventNotifyMendeln` | monster 922 `nihlathakMap` |
| 2 | 12 | Horazon | `MapEventNotifyHorazon` | monster 919 `summonerMap` |
| 4 | 14 | Dark Wanderer | `MapEventNotifyDarkWanderer` | monster 921 `darkwandererMap`, up to 3 |
| 5 | 15 | Treasure Fallen | `MapEventNotifyTreasure` | monster 945 `treasurefallenMap`, up to 3 |
| 6 | 16 | Spire of Darkness | `MapEventNotifyTainted` | object 593 `TaintedSunMapEventAltar` and three object 374, then waves of 993-995 (`shadowofhate`, `shadowofterror`, `shadowofdestruction`) |
| 8 | 18 | Gheed | `MapEventNotifyGheed` | monster 147 `gheed` and object 594 `empty_eventmarker` (invisible, AutoMap 1499) at its spot |
| 9 | 19 | Altar of the Catalyst | `MapEventNotifyShard` | object 611 `ForceShardAltar` |
| 10 | 20 | Corrupted Heroes (invaders) | `MapEventNotifyInvaders` | monster 1182 `InvaderPortal`, up to 3 |
| 11 | 21 | Void / Tomb | `MapEventNotifyTomb` | object 586 `xactportal_gold` to level 207, and object 594 |

Value 13 (Butcher's Lair, `MapEventNotifyButcher*`, level 173) and 17 are never drawn: Butcher is disabled, as [#152](https://github.com/synpoox/pd2-resurrected/issues/152) says.

**When it spawns** (`0x36fef0`, run as rooms of a map area activate). The event waits for the level's progress:
`progress = region[+0x04] x 100 / constant(level)`, where the constant is a hard-coded per-level switch (`0x38b1d0`). For
the tiered maps it is 120-280 (for example Arreat Battlefield 139: 240, Phlegethon 145: 270, Lost Temple 158: 120);
unknown levels use 200. `region[+0x04]` counts the level's rooms activated so far: 1.13c D2Game's room monster spawn
(`0x6fc67a20`, which PD2's spawner wraps and calls last) first calls `0x6fc66cd0`, which adds 1 to it
([MS8](https://github.com/synpoox/pd2-resurrected/issues/222)). So the constant is a room count.

- One-shot events (Mendeln, Horazon, Gheed, Altar, Void/Tomb): spawn once when `rand() % 100 + progress > 140`.
- Dark Wanderer and Treasure Fallen: the n-th of 3 spawns when `n x 25 < progress` and `rand() % 100 < 25`.
- Invaders: the n-th of 3 when `25 + (n-1) x 20 < progress` and `rand() % 100 < 25`.
- Spire of Darkness: the altar spawns with a 20% roll once `region[+0x04] > 15`; its waves follow, and at progress above
  90 `0x36eb30` runs.

The spawn point is the activating room's centre. Each spawn sends the notify string (client message).

**The altars** ([#504](https://github.com/synpoox/pd2-resurrected/issues/504), [#505](https://github.com/synpoox/pd2-resurrected/issues/505)). Both use PD2's operate 79 (`0x3aa460`; PD2's
operate table is filled in `0x36c320`: 77 `0x3a9780`, 78 `0x3aa270`, 79 `0x3aa460`, 80 `0x3aa670`). `game+0x2628` is
the event's state:

- Altar of the Catalyst (event 19): the spawn places object 611 by the spot search, state 1. The first use sets the
  altar's mode 1, adds its open event and spawns monster 1064 `GuardianOfFate` 10-29 subtiles from it (40 tries, spot
  size 2), state 2. Its death (`0x378080`, event 19 and state 2) sets state 3 and kills the `GuardianOfFateClone`s
  (1065) around it; the guardian's AI (`0x35a6b0`) summons them. The next use sets mode 3 and drops 2 or 3 `iwss`
  (Catalyst Shard) by the altar at item level 90 (`0x36ebc0`), state 4.
- Spire of Darkness (event 16): the altar goes at a random point of the room (`0x389ea0`), then the spot search; fog
  object 374 at (x+2, y), (x, y+2) and (x-1, y-1); state 1. The use sets the altar's mode 1 and darkens the level
  (`0x37dc70`: packet 0x53, D2's Tainted Sun), state 2. While the state is 2-49 each room activation draws `rand() % 6`:
  1, 2 or 3 spawn a group of 993, 994 or 995 (monstats MinGrp to MaxGrp: 3-6, 8-14, 1) at the room's centre +- 5
  (spot size 10). `0x36eb30`, past progress 90: state 50, the darkness lifted (`0x37dc70` with 0), a sound for the
  level's players (`0x37f240`, packet 0x2c), and every 993-995 in the game killed without drops (`0x356e00`).

**Void / Tomb** (event 21): the spawn places object 586 `xactportal_gold` by the spot search, then `0x38a1f0` places
another 586 in level 207 at its spot (`0x38a000`) and `0x38a510` links the two both ways; object 594 marks the spot.

**Horazon** (`0x377ca0`, on `summonerMap`'s death while the event is live). It builds a pool from three hard-coded level
lists, which match the misc `len` tiers exactly:

- T1 {143, 145, 146, 148, 151, 155, 158, 167, 169, 208}
- T2 {141, 142, 150, 156, 160, 174, 175, 193, 209, 210}
- T3 {139, 144, 147, 149, 154, 170, 190, 191, 201}

Three flags on the event context pick the lists: T1 and T2, T2 and T3, or T3 alone. That fits the wiki's "same or higher
tier". The flags are the dying monster's level: `0x3760a0` sets them when it is in the T1 set (`0x105deb18`), the
T2 set (`0x105debf4`) or the T3 set (`0x105deb08`), the same sets the pool is built from ([MS9](https://github.com/synpoox/pd2-resurrected/issues/223)). The handler
runs only while the event is Horazon's and `game+0x2628` is 1, which its portal raises, so once per game. It draws a
level other than the current one, adds an area entry (slot 10, value = the level), applies the area stats to that
level (`0x38b4d0` with first = 0, so no monster stats are added again), and opens a red portal (object 60) to it at
the monster (`0x38a300`). With a 10% roll it calls `0x381510`. Then it drops from treasureclassex rows 54-56 (`Map Tier
1`, `Map Tier 2`, `Map Tier 3`; `0x37f430` adds the row to the compiled TC base): 3, 2 and 1 picks in a T1 map, 1, 3
and 2 in T2, 1, 2 and 3 in T3, through 1.13c's TC drop (`0x30f120`).

**Void / Tomb.** One event. `MapEventNotifyVoid` has no code reference, so there is no separate Void event or Treasure
Vault: the gold portal leads to level 207, "The Bitter Depths", whose treasure classes are `Map Void`, `Map Void2` and
`Map Void3`.

**No events at all** spawn in the uber areas and unique-map areas (the set at RVA `0x5def44`).

## 5. A red portal from a D2R plugin

D2R's portal maker `0x432ce0` (game, unit, room, x, y, destLevel, &object, objectId, 0, 1), from the toolkit's
[address notes](https://github.com/synpoox/d2rAI-toolkit/blob/main/knowledge/d2r-93847-addresses.md), is the
counterpart of PD2's `0x38a300`: it places the portal object, sets its destination and places the return portal.
Object 60 is the red portal. D2R's limits that apply (toolkit `engine-limits.md`):

- Portals link levels **within one act**. Harrogath and all map areas are Act V.
- The destination setter `0x34e9d0` keeps one byte. The port's map-area level ids (about 139-212) are below 256.
- PD2's portal objects (584 red, 585, 622 for keys) use `OperateFn` 77, past the end of D2R's 75-entry operate table.
  The port uses object 60, or its own object with a function written into a free operate slot (slot 60 is null).

Reimagined's cube output `Red Portal,lvl=<id>` shows the same route reaching Act V levels ([#145](https://github.com/synpoox/pd2-resurrected/issues/145)). Not tested here: no
plugin in this repo calls `0x432ce0` yet.

## Not settled here

- What `0x381510` does (Horazon's 10% roll).
- Slot 5's spawn in D2R terms. [MS4](https://github.com/synpoox/pd2-resurrected/issues/219) found no reader of slots 7-9 and 15, and slot 10 only in `0x38aa00` (Horazon) ([ms4-area-map-stats.md](ms4-area-map-stats.md)).
- A resist rewrite in `0x38aa00` for players, keyed to an area entry whose value is the current level (it may belong to
  Horazon's slot-10 entry). Not decoded.

## Game check

Not run in PD2. These are static readings of PD2's DLL. A check in PD2's single player needs a level 80+ Hell character
with the Ancients done and map items, which we don't have. The port's game checks test the same behaviour instead: [MS2](https://github.com/synpoox/pd2-resurrected/issues/217)
the areas and gates, [MS5](https://github.com/synpoox/pd2-resurrected/issues/220) the recipes and op 30.
