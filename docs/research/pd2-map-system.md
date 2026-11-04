# PD2's map system

Answers research ticket #144 (map #143): what PD2's map system does, in full, and which part lives in the tables and which in
`ProjectDiablo.dll`.

**Sources.** The PD2 baseline tables (`pd2-baseline.json`, extracted with `tools/pd2_extract.py` into the git-ignored `pd2-data/`):
`misc`, `itemtypes`, `magicprefix`, `magicsuffix`, `rareprefix`, `raresuffix`, `uniqueitems`, `itemstatcost`, `properties`,
`cubemain`, `treasureclassex`, `levels`, `monstats`, and the string tables. Printable strings of `ProjectDiablo.dll` (offsets below
are file offsets). The PD2 wiki's [Maps](https://wiki.projectdiablo2.com/wiki/Maps) page for behaviour no table shows (the page calls
itself a stub that lacks most Season 14 changes, so it is the weakest source here). No binary was run or decompiled. Row numbers are
1-based data rows of the PD2 table.

## Answer in brief

- **Map items:** 48 `misc` rows with `pSpell` 12. 41 are tiered maps (T1 10, T2 10, T3 9, dungeons 3 plus the disabled `t41`, uniques 8),
  2 PvP arenas, 5 uber-boss keys (`ubtm`, `dcma`, `rtma`, `luca`, `uba`). The item code does
  not give the tier; the item type does (`t1m`..`t5m`, with `t?me` "exc" twins).
- **Map stats and rolls:** all data. Maps roll Normal/Magic/Rare from 118 magic prefixes and 124 magic suffixes (itype `t1m`-`t4m`),
  plus 18 rare prefixes and 10 rare suffixes that list `map`. Each affix sets one to three of 105 `map_*` stats (`map_mon_*` on monsters,
  `map_play_*` on players, `map_glob_*` on the area). Unique maps (T5) are 8 `uniqueitems` rows with nine fixed-range mods each.
- **Map recipes:** all data except op 30. 189 enabled `cubemain` rows take or make a map or a map ingredient: orb recipes per tier, 3-to-1 upgrades, corruption
  outcomes by `corruptor` band (op 16), heroic, fortify, ears, Catalyst Shard, Horadrim Scarab, uber-key difficulty. The 26 op-30
  rows are all blockers placed just before a real row.
- **Map areas:** 74 `levels` rows (ids 137-210) are PD2-only; 66 of them are map, event or uber areas (the rest are 3 PvP
  arenas and 5 guild halls). The area level a tier opens is `MonLvl3Ex`: T1 87, T2 88, T3 89, dungeons 92. No table links a map item to its area.
- **Map events and invaders:** the monsters, treasure classes and strings are data; the event roll, spawning, timers, Horazon's
  portal, Gheed's shop and the invader brain (`AiThink4.cpp`) are DLL code.
- **In the DLL:** using a map (gate checks, the item-to-area table, opening the area), copying the map's stats onto the area, its
  monsters and players, all `map_glob_*` effects (density, area level, rarity, extra monster groups, extra boss, drop bonuses,
  fortify), the event system, cube op 30, map stacking, the Maps stash tab and `.kills` counters.
- **Hook points core-rules already has:** six map-plan rows (map items, op 16/18, op 30, events and invaders, life regen cap,
  stackable items) and four rows whose map part is deferred (Find Item boss list, Desecrate list, death novas, Teleport 3 levels).

## 1. Map items

All 48 have `pSpell` 12, `levelreq` 80 (uber keys 0), and use the `MapTier` / `DungeonTier` description string with `calc1` = the
tier number. Normal tiered maps stack to 50 (`stackable` 1, `maxstack` 50); unique, uber and Rathma/Lucion keys do not.

| Group | Type | Codes (`misc` code -> English name from `namestr`) |
|---|---|---|
| T1 | `t1m` / `t1me` | t11 Ruins of Viz-Jun, t14 Sanatorium, t18 Poisoned Well, t21 Phlegethon, t22 Torajan Jungle, t24 Tomb of Zoltun Kulle, t26 Shadows of Westmarch, t33 Lost Temple, t34 Ancestral Trial, t36 Fall of Caldeum |
| T2 | `t2m` / `t2me` | t12 Horazon's Memory, t16 Ruined Cistern, t17 Halls of Torture, t25 Sewers of Harrogath, t29 Fortress of Aranoch, t2a Descent from Mount Arreat, t32 Throne of Insanity, t37 Pandemonium Citadel, t38 Canyon of Sescheron, t3a Ashen Plains |
| T3 | `t3m` / `t3me` | t13 Bastion Keep, t15 Royal Crypts, t23 Arreat Battlefield, t27 Demon Road, t28 Skovos Stronghold, t31 River of Blood, t35 Blood Moon, t39 Kehjistan Marketplace, t3b Kyovashad |
| Dungeon (T4) | `t4m` / `t4me` | t42 Plains of Torment, t43 Sanctuary of Sin, t44 Steppes of Daken-Shar; t41 Cathedral of Light is type `ubr`, `spawnable` 0 (disabled) |
| Unique (T5) | `t5m` / `t5me` | t51-t58 (Zhar's Sanctum, Warlord of Blood, Fallen Gardens, Imperial Palace, Outer Void, City of Ureh, Djinn's Domain, Na-Krul's Abyss); `uniqueitems` rows 434-484 |
| PvP | `pvpd`, `pvpm` | t61 Desert Duel Arena, t62 Moor Duel Arena (PvP, cut with multiplayer) |
| Uber keys | `ubr`, `ubmp` | ubtm Pandemonium Talisman, dcma Vision of Terror, rtma Voidstone, luca Shadow of Hatred, uba Relic of the Ancients (these belong to the uber-boss milestone, not the map system) |

The wiki puts Sanatorium in T2; the table types it `t1m`. Item types chain `t3m -> t2m -> t1m -> map` (`itemtypes` rows 108-110), so a
recipe or affix on `t1m` also matches T2 and T3 maps; the `t?me` "exc" types descend straight from `map` and are what recipes use to
pick one tier.

Ingredients (all `misc`, all stackable, names from the string table): `imma` Arcane Orb, `imra` Zakarum Orb, `rera` Horadrim Orb,
`upma` Angelic Orb, `scou` Orb of Destruction, `upmp` Cartographer's Orb, `fort` Orb of Fortification, `scrb` Horadrim Scarab, `wss`
Worldstone Shard, `iwss` Catalyst Shard (`spawnable` 0), `std` Standard of Heroes, and seven invader ears (`ivez` ... ).

**Use (wiki, DLL behaviour):** right-click in Harrogath, Hell difficulty, character level 80+ with the Ancients killed; one map or
uber area per game. Drops: T1 maps rarely in Hell and Act 5 Nightmare (`treasureclassex` `Map Tier 1-3`, `T1-T3 Map Drop`, and the
`Map * t1-t3` per-monster-class rows); most come from inside maps; Anya gives a T1 and sells the orbs.

## 2. Map stats and how they roll

**Stats.** 105 `itemstatcost` rows named `map_*` (all already carry an id in `merge/ids-itemstatcost.tsv`). Prefix tells the target:
`map_mon_*` monsters (damage, masteries, pierce, absorb, speed, HP, regen, life steal, crushing blow, open wounds, splash, curse
resistance, phys-as-extra element, item-class drop bonuses), `map_play_*` players (resists and max resists, AC%, block, FHR, regen,
FCR/IAS, HP/mana %, damage reduction, velocity, light radius, MF, GF, extra experience), `map_glob_*` the area (density, area level,
monster rarity, nine extra monster groups by monstats id, extra boss, skirmish/fortify, drop socketed/ethereal/bonus/corrupted,
boss drop specials). Plus `map_force_event` (500), `heroic` (458), `treacherous` (276), `uber_difficulty` (185), `corrupted` (360),
`corruptor` (361). `properties` rows 214 and 281-450 bind them (mostly func 1/3/8; `map-mon-splash` and `map-mon-skillondeath`
func 11).

**Magic and rare affixes.** 118 magic prefixes (`magicprefix` rows 672-897) and 124 magic suffixes (`magicsuffix` rows 750-1021). Each tier band
is a separate row with itype pairs `t1m,t2m` / `t2m,t3m` / `t3m,t4m`, so (as the wiki says) T1 rolls the first range, T2 the first or
second, T3 the second or third, dungeons the third. Almost every affix carries a filler pair of `map-glob-density` plus
`map-play-addxp` or `map-play-mag-gold%`, which is why the three headline numbers grow with every affix. Specials: Champion's
(monster rarity), Duplicated (extra boss), of the Adventurer (+1 area level, `frequency` 0), of Darkness (light radius, `frequency`
0), and nine monster-group prefixes (Stygian doll 690, Lustful succubus 885, Vampiric 1170, Bovine 391, Reanimated 698, Ghastly 1111,
Souless 918, Shamanic 785, Rampant 1169: the stat value is a monstats row). Rare names reuse 18 rare prefixes and 10 rare suffixes
that list `map`. Affix counts per rarity come from the engine's normal magic/rare rules (the wiki does not state them; not checked).

**Uniques.** Each T5 row has nine props with ranges, for example Zhar the Mad Map: density 125-175, +10-20% exp, MF/GF 50-100, charm
drops, monster HP, phys as poison, -10 all res, -40..-30 regen, -30..-20 FHR. Area-level bonuses: Imperial Palace +1, Zhar +2
(`map-glob-arealevel`).

**Who applies the stats.** No table moves a map stat from the item onto monsters, players or the area: the DLL does that when the
map opens. The `MapStat Drop*` treasure classes (`treasureclassex` 983-989: jewelry, weapons, armor, crafting, charms, Worldstone
Shards, jewels) and `Unique Map MapBoss`, `T4MidMapBoss`, `T4FinalMapBoss` exist for the drop stats to call.

## 3. Map recipes

189 enabled `cubemain` rows take or make a map or a map ingredient (uber keys included):

| Recipe | Rows | Op | Shape |
|---|---|---|---|
| Orbs per tier: upgrade magic to rare (Angelic), reroll rare (Horadrim), imbue magic (Arcane), imbue rare (Zakarum), scour (Destruction), reroll magic (Arcane) | 2-19, 2160-2165 | 18 `corruptor` = 0 | `t?me` + orb (+ rune, gem, jewel) -> `usetype` at the new rarity, ilvl 100 |
| 3 maps + Cartographer's Orb -> next tier | 20-33 | none | `t1me,qty=3` + `upmp` -> `t2me,nor` |
| 3 maps -> same tier (normal, magic, rare) | 2124-2138 | none | |
| 3 dungeons -> rare dungeon; T3 + Horadrim Scarab -> rare dungeon | 2123, 2032-2034 | none | output carries five fixed dungeon mods (velocity, AR/pierce, splash skill 358, no-freeze/HP, exp) |
| Heroic: map + Standard of Heroes | 201, 203 | 30, 18 `heroic` = 0 | `heroed` 1, MF 20, GF 20, density 20, exp 10 |
| Corrupt: map + Worldstone Shard | 202, 204, then 210-277, 1940-1949 | 30, 18, 16 | roll row sets `corruptnum` 1-3000 and returns the shard; outcome rows by `corruptor` band per tier: ten stat outcomes or one of eight unique maps (bands 2971-3000 -> `t51`-`t58`). Same flow as item corruption (core-rules-inventory Q10) |
| Fortify: map + Orb of Fortification | 1916-1921, 2143 | 30, none | `map-glob-skirmish-mode` 50, player velocity 30 (wiki: monsters +100% drops, exp and HP, +20% damage, density -50%: the DLL reads skirmish mode) |
| Forced event: map + Catalyst Shard | 1950-1955 | 30, none | `map-force-event` 63 |
| Ears: map + Corrupted <class>'s Ear | 2185-2205 | 30, 18 `treacherous` = 0 | `<class>-ear-drops` (all bind `map_glob_monsterrarity`, func 8), drop bonus 50, `treacherous` 1, plus two class mods |
| Uber keys: Vision of Terror, Relic, Lucion, Rathma ingredients; difficulty cycle 0 -> 1 -> 2 -> 0 | 199, 1479, 1497, 1963, 2139, 2049-2054, 2140-2142 | 18 `uber_difficulty` | uber-boss milestone |
| Blockers | 1926, 1927, 1930 | 30 | "BLOCK ... CREATES DUPLICATES due to stackable maps" |

**Op 30 (core-rules Q8).** All 26 op-30 rows sit directly before a real row with the same ingredients, and each narrows the input to
the case the real row must not take: `nor` maps for heroic, corrupt, fortify, Catalyst and ears; `uni` maps for ears; and the three
BLOCK rows. The wiki matches the narrowing for corrupt, Catalyst, fortify (Magic/Rare/Unique only) and uniques (only Standard and
Fortification). D2R's recipe search takes the first matching row (Q10), so a row that matches and refuses shadows the row after it.
That reading fits every row except heroic, which the wiki allows on any map. Q8's other guess (refuse a stacked input) also fits,
since only normal maps stack, and would explain heroic. Which of the two it is needs the DLL's `CubeFunctions.cpp` (string at
`0x44d3e0`) or a baseline test with one unstacked normal map.

## 4. Map areas

`levels` ids 137-210 are past D2R's vanilla rows and all are `Act` 4 (Act 5) except PvP and guild halls (`Act` 0). Area level is
`MonLvl3Ex`: Kurast 87, Arcane 88, River of Blood 89, Realm of Terror 92, matching the wiki's T1 87 / T2 88 / T3 89 / dungeon 92.
The item-to-area pairing below is by name (no column links them; the DLL holds it):

| Map | Level ids |
|---|---|
| T1-T3 maps (29) | 139-151, 154-156, 158, 160, 167, 169, 170, 174, 175, 190, 191, 193, 201, 208-210 (one level each; for example Kurast 143, Lava 145, Siege 139) |
| Dungeons | Torment 164 + Hole of Terror 165, Sanctuary of Sin 171 + Black Abyss 172, Daken-Shar 186-187; disabled Monastery 152-153 |
| Uniques | Zhar 176-179 (four variants), Outer Void 180, Warlord of Blood 181-182, Fallen Gardens 183 (+ Diamond Gate 184?), Imperial Palace 192 + boss 194, Ureh 195 + Nemyr Monastery 196, Djinn's Domain 197-199, Na-Krul 200 |
| Event areas | Butcher Event Map 173, Tomb Event Map 207 ("The Bitter Depths"); the Treasure Vault, Horazon and Void areas are not identified by name |
| Uber areas | 137 (Uber Diablo), 138 Mesa?, 168 Uber Ancients, 161-163 Necropolis (Rathma), 185 Pandemonium Finale, 188-189 Lucion |
| Not map system | 157, 159, 166 PvP; 202-206 guild halls |

Map areas need the PD2-only-area level pipeline first (their `lvlprest` / `lvltypes` rows and `pd2maps.mpq` presets; unported rows
"New PD2-only areas" and "PD2 level types table"). Several dungeon and uber levels have `Teleport` 3 (core-rules' Teleport path
search, unported row "Teleport, Blink...").

## 5. Map events and invaders

Event chance per area (wiki): Rare 33%, Magic 22%, Normal 11%; `map_force_event` 63 (Catalyst Shard) forces one. Notifications are
string keys the DLL names (`0x44ca3c`-`0x44e544`); the event list:

| Event | String key | Data it uses |
|---|---|---|
| Dark Wanderer: becomes a random boss, extra Worldstone Shards | `MapEventNotifyDarkWanderer` | `monstats` `darkwandererMap` (923) |
| Shadow of Mendeln: undead waves, Splinter of the Void | `MapEventNotifyMendeln` | |
| Horazon: portal to a same or higher tier area with the same mods, 6 maps | `MapEventNotifyHorazon` | |
| Treasure Fallen: flees, drops loot on damage, up to 3 | `MapEventNotifyTreasure` | `treasurefallenMap` (947), TC `Treasure Fallen Map` |
| Spire of Darkness: darkness, shadow monsters, uber keys | `MapEventNotifyTainted`, `TaintedSunMapEventAltar` | `shadowofhate/terror/destruction` (995-997) |
| Gheed: sells unidentified uniques | `MapEventNotifyGheed` | `gheed` (148) |
| Altar of the Catalyst: boss, then up to 3 Catalyst Shards | `MapEventNotifyShard` | `iwss` |
| Butcher's Lair: 30-second sealed lair | `MapEventNotifyButcher1/2`, `MapEventNotifyTimer` | `butcherMap` (946), level 173 |
| Void / Tomb (Treasure Vault, "The Bitter Depths") | `MapEventNotifyVoid`, `MapEventNotifyTomb` | TCs `Map Void`, `Map Void2`, `Map Void3`; level 207 |
| Corrupted Heroes (invaders): up to 3 portals, 5 invaders, rare per slot plus an ear | `MapEventNotifyInvaders`, `MapEventNotifyInvaders<Class>` | `monstats` 1184-1193 |

**Invaders.** `InvaderPortal` (AI `Overseer`), seven `Invader<Class>` monsters level 90 (AIs reused from vanilla: Amazon `BloodRaven`,
Assassin, Barbarian, Paladin `Duriel`, Druid, Necromancer, Sorceress `Summoner`), and two Necromancer pets. The table AIs are
placeholders: the DLL's `..\Source\D2Game\Ai\AiThink4.cpp` (`0x44ca9c`, next to the invader notify keys) is a rebuilt 1.13c AI file,
so the invader brain is DLL code. Their art is the classic player DCC in `pd2monchars.mpq` ([pd2-asset-inventory](pd2-asset-inventory.md)).
The "Map-event notifications" row on `docs/unported.md` is already cut; the events themselves are not.

## 6. Tables versus ProjectDiablo.dll

| Part | Tables | DLL |
|---|---|---|
| Map items, tiers, stacking | `misc`, `itemtypes` | `pSpell` 12 use: gate checks, item -> area table, open the area; one per game; stack split (`HOTKEY_ItemQuantity`) |
| Affixes, uniques, roll ranges | `magicprefix`, `magicsuffix`, `rareprefix`, `raresuffix`, `uniqueitems`, `properties`, `itemstatcost` | none for the roll |
| Stat effects | stat rows only | copy `map_mon_*` to monsters, `map_play_*` to players; density, area level, rarity, extra groups, extra boss, fortify (skirmish), drop stats -> `MapStat Drop*` TCs |
| Recipes | `cubemain` (ops 16, 18 are D2R's) | op 30 |
| Areas | `levels`, `lvlprest`, `lvltypes`, `pd2maps.mpq` | which area a map opens; the life regen cap (core-rules row, `0x102fc000`) |
| Events, invaders | `monstats`, `treasureclassex`, strings | event roll and spawn, timers, Horazon portal, Gheed shop, notifications, `AiThink4` |
| UI | strings `StashCategoryMaps`, `StashSubCategoryMapsTier1-3` | Maps stash tab (stash layouts are cut), `.kills maps/dungeons` counters |

## 7. Hook points core-rules already has

| Row (`docs/core-rules.md`) | Hook | Status |
|---|---|---|
| Map items (48 items) | misc `pSpell` 12 -> 16 | open, map plan |
| Item stat less than / equal (1,126 rows) | cubemain op 16, 18 (data; stat ids remapped) | data, P9 + map plan |
| Map recipes (26 rows) | cubemain op 30 | verify, Q8 |
| Map events and invaders | ProjectDiablo.dll, no slot | verify, map plan |
| Level life regen cap (30, `bloodwarp` lifts it) | ProjectDiablo.dll `0x102fc000` | verify, map plan |
| Stackable items / item quantity | `HOTKEY_ItemQuantity` | verify |
| Find Potion, Find Item | srvst 82 / cltst 62; 46 map/uber boss ids deferred | built (W10), list is map plan |
| Death novas (Rathma, Nihlathak map, Lucion, Doll) | srvdo 169 -> 202, cltdo 105 -> 125 | open |
| Desecrate | 45 map/uber/Rathma monsters deferred (unported) | map plan |
| Teleport `Teleport` 3 levels | srvdo router 190 (W26) | left out until map levels exist |

Data rows on `docs/unported.md` that the map system will reopen: map items never drop (treasure classes and `spawnable`, `merge/item-tables.txt`
no-drop lines 19-32), automap and monpreset (map presets), level types, new PD2-only areas.

## Not settled here

- Cube op 30: block-always or block-stacked (section 3).
- The item -> area table and the event roll live in the DLL; reading them needs Ghidra on `ProjectDiablo.dll` (AGENTS.md).
- Affix count per rarity on maps and how the DLL scales `map_glob_density` and `map_glob_monsterrarity`: needs the DLL or a baseline run.
- Which levels the Treasure Vault, Horazon and Void events open.
