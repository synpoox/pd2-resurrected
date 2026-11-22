# docs index

Lane IDs (W36, IR3, MS4, UB2, ...) in file names are the research ticket codes; the issue is linked at the top of each note.

## Fixed top-level files

- [deviations.md](deviations.md) — every place the port differs from PD2 (CI gate input)
- [roadmap.md](roadmap.md) — alpha roadmap, one row per issue with its merged PR
- [adr/](adr/) — architecture decision records
- [agents/](agents/) — agent conventions (issue tracker, triage, domain docs)

## Player guides

- [linux.md](linux.md) — playing on Linux with Wine/Proton and Lutris

## Cartographer migration (this mod onto the cartographer plugin)

- [cartographer/contracts.md](cartographer/contracts.md) — Cartographer candidate: frozen contracts (#946, epic #942)
- [cartographer/core-events-lanes.md](cartographer/core-events-lanes.md) — core-events lane allowlist (#943)
- [cartographer/migration-spec.md](cartographer/migration-spec.md) — Migration spec: problem statement and scope
- [cartographer/migration-tickets.md](cartographer/migration-tickets.md) — Work items for #942 — corrected vanilla arena reuse
- [cartographer/migration.md](cartographer/migration.md) — Cartographer migration plan — revised scope
- [cartographer/native-check.md](cartographer/native-check.md) — Native catalogue check (#952, lane A)

## Features and rule lists

- [features/core-rules-inventory.md](features/core-rules-inventory.md) — Inventory of core-rules engine rules
- [features/core-rules.md](features/core-rules.md) — core-rules hook list
- [features/d2r-only-content.md](features/d2r-only-content.md) — D2R-only content ledger
- [features/event-link.md](features/event-link.md) — Event link: PD2 events beside a standalone Cartographer (#944)
- [features/item-rules.md](features/item-rules.md) — PD2's remaining item rules in the port
- [features/map-system.md](features/map-system.md) — PD2's map system in the port
- [features/uber-bosses.md](features/uber-bosses.md) — PD2's uber bosses in the port
- [features/warlock.md](features/warlock.md) — The Warlock in PD2's style

## PD2 reference: what PD2 does and what was found in it

- [pd2/asset-inventory.md](pd2/asset-inventory.md) — PD2 art, audio and map asset inventory
- [pd2/dark-wanderer-event.md](pd2/dark-wanderer-event.md) — The Dark Wanderer map event
- [pd2/data-inventory.md](pd2/data-inventory.md) — Inventory of PD2's data against D2R 3.3
- [pd2/engine-dll-triage.md](pd2/engine-dll-triage.md) — Triage of PD2's engine DLLs
- [pd2/map-system-dll.md](pd2/map-system-dll.md) — PD2's map system in ProjectDiablo.dll
- [pd2/origin-bugs.md](pd2/origin-bugs.md) — PD2-origin bugs
- [pd2/pandemonium-uber-tristram.md](pd2/pandemonium-uber-tristram.md) — Inventory of PD2's Pandemonium and Uber Tristram
- [pd2/patch-table-audit.md](pd2/patch-table-audit.md) — PD2's patch table and table writes against our ports
- [pd2/plrmodes.md](pd2/plrmodes.md) — What PD2's PlrModes.cpp changes
- [pd2/strings-inventory.md](pd2/strings-inventory.md) — Inventory of PD2's strings against D2R's
- [pd2/uber-bosses-dll.md](pd2/uber-bosses-dll.md) — PD2's uber bosses in ProjectDiablo.dll
- [pd2/unported-rows.md](pd2/unported-rows.md) — The remaining unported rows in the port

## Process and tooling

- [process/d2rloader-capabilities.md](process/d2rloader-capabilities.md) — D2RLoader 1.3.1 and PluginSDK 0.4.0: capability matrix
- [process/modkit-validation.md](process/modkit-validation.md) — modkit validation errors
- [process/release-rehearsal.md](process/release-rehearsal.md) — Release rehearsal: install, upgrade, roll back (#966)

## Research: skills, missiles and combat hooks (W-lane notes)

- [research/skills/amazon-w6.md](research/skills/amazon-w6.md) — Amazon group (W6): shit 63, dmg 17, cltdo 22, chit 12
- [research/skills/druid-w21.md](research/skills/druid-w21.md) — Inferno and Arctic Blast (W21): srvdo 182, cltdo 111, the channel take-over
- [research/skills/druid-w22.md](research/skills/druid-w22.md) — Firestorm and the wall makers (W22): srvdo 166, cltdo 119
- [research/skills/holy-sword-parry.md](research/skills/holy-sword-parry.md) — Holy Sword parry (#1466): PD2 avoid roll `0x103035f0`, class 5, cap 75, D2R hook `0x44ef70`
- [research/skills/druid-w23.md](research/skills/druid-w23.md) — Gust and Hunger (W23): srvdo 156 and 165
- [research/skills/druid-w24.md](research/skills/druid-w24.md) — Fire Claws and the Spirit Wolf start (W24): srvdo 175, cltdo 110, srvst 77
- [research/skills/lightning-javelins.md](research/skills/lightning-javelins.md) — Lightning javelins (srvdo 188, cltdo 117)
- [research/skills/necromancer-w11.md](research/skills/necromancer-w11.md) — Necromancer: Poison Dagger
- [research/skills/necromancer-w13.md](research/skills/necromancer-w13.md) — Desecrate (W13): srvdo 161, cltdo 101
- [research/skills/necromancer-w14.md](research/skills/necromancer-w14.md) — Necromancer W14: Golem start, Revive event, PlrModes gate B
- [research/skills/nova-orb-hits.md](research/skills/nova-orb-hits.md) — Nova and orb hits (shit 5, chit 4)
- [research/skills/skill-function-tables.md](research/skills/skill-function-tables.md) — D2R 93847 skill and missile function tables
- [research/skills/sorceress-w26.md](research/skills/sorceress-w26.md) — Sorceress: Teleport and blocked areas (W26)
- [research/skills/states-immed.md](research/skills/states-immed.md) — #342: PD2's states column `immed`
- [research/skills/w10-barbarian.md](research/skills/w10-barbarian.md) — Barbarian group: Bash, Find Potion / Find Item, Frenzy start
- [research/skills/w12-blood-warp.md](research/skills/w12-blood-warp.md) — Necromancer: Blood Warp (W12)
- [research/skills/w16-charge.md](research/skills/w16-charge.md) — Paladin: Charge (W16)
- [research/skills/w17-holy-light.md](research/skills/w17-holy-light.md) — Holy Light and the Fist of the Heavens hit (W17)
- [research/skills/w18-vengeance-joust.md](research/skills/w18-vengeance-joust.md) — Vengeance and Joust (W18)
- [research/skills/w19-holy-sword-smite.md](research/skills/w19-holy-sword-smite.md) — Holy Sword and Smite starts, Smite's event, Run/Concentration (W19)
- [research/skills/w20-blessed-hammer.md](research/skills/w20-blessed-hammer.md) — Paladin: Blessed Hammer (W20)
- [research/skills/w25-assassin-finishers.md](research/skills/w25-assassin-finishers.md) — Assassin charge-up finishers: hook points (W25)
- [research/skills/w27-static-field-combustion.md](research/skills/w27-static-field-combustion.md) — Sorceress: Static Field, Combustion and the boss novas (W27)
- [research/skills/w28-sorceress-bolts.md](research/skills/w28-sorceress-bolts.md) — Sorceress: bolt hits (shit 62) and Chilling Armor's event 3 (W28)
- [research/skills/w29-assassin-finishers.md](research/skills/w29-assassin-finishers.md) — Assassin charge-up finishers: server half (W29)
- [research/skills/w30-assassin-finishers.md](research/skills/w30-assassin-finishers.md) — Assassin charge-up finishers: client half (W30)
- [research/skills/w31-dragon-flight-psychic-hammer.md](research/skills/w31-dragon-flight-psychic-hammer.md) — Dragon Flight and Psychic Hammer's damage (W31)
- [research/skills/w32-blade-fury.md](research/skills/w32-blade-fury.md) — Blade Fury's fragment (W32): shit 34 -> 69, chit 6 -> 68, dmg 19, and the channel check
- [research/skills/w33-melee-splash.md](research/skills/w33-melee-splash.md) — Melee splash (W33)
- [research/skills/w34-gate-b.md](research/skills/w34-gate-b.md) — Gate B (W34): the get-hit/block end window and the cast stamp
- [research/skills/w35-melee-splash-extras.md](research/skills/w35-melee-splash-extras.md) — Melee splash: the current skill's damage and radius extras (W35)
- [research/skills/w36-monster-kits.md](research/skills/w36-monster-kits.md) — Monster kits (W36)

## Research: item rules (IR, P and stack notes)

- [research/items/ir3-combat-stats.md](research/items/ir3-combat-stats.md) — IR3: combat item stats
- [research/items/ir4-curses.md](research/items/ir4-curses.md) — IR4: PD2's curse stats in ProjectDiablo.dll
- [research/items/ir6-cube-rules.md](research/items/ir6-cube-rules.md) — IR6: cube rules and stacking in ProjectDiablo.dll
- [research/items/ir7-equipped-items.md](research/items/ir7-equipped-items.md) — IR7: skill while equipped and phasing in ProjectDiablo.dll
- [research/items/p12-ormus-robes.md](research/items/p12-ormus-robes.md) — P12: Ormus' Robes and skill on cast
- [research/items/stack-split.md](research/items/stack-split.md) — #316: PD2's stack split
- [research/items/pd2-remaining-items.md](research/items/pd2-remaining-items.md) — The remaining PD2 items and item rules
- [research/items/split-throw.md](research/items/split-throw.md) — Spike: PD2 Split Throw in a free srvdofunc slot
- [research/items/suite-companions.md](research/items/suite-companions.md) — RuffnecKk's suite plugins as item-rule companions

## Research: map opening and uber fights (MS, UB notes)

- [research/maps/ms2-open-map.md](research/maps/ms2-open-map.md) — MS2: opening a map in D2R
- [research/maps/ms3-map-stats.md](research/maps/ms3-map-stats.md) — MS3: monster and player map stats in D2R
- [research/maps/ms4-area-map-stats.md](research/maps/ms4-area-map-stats.md) — MS4: area map stats in D2R
- [research/maps/ub2-open-uber-fight.md](research/maps/ub2-open-uber-fight.md) — UB2: opening an uber fight in D2R
- [research/maps/anya-maps.md](research/maps/anya-maps.md) — Anya's Hell maps (#1323)
- [research/maps/lp3-border-painter.md](research/maps/lp3-border-painter.md) — LP3 spike: Jungle Map HD presets with a border painter (#213)
- [research/maps/lp3-tile-table.md](research/maps/lp3-tile-table.md) — LP3: a classic tile -> HD piece table learned from D2R
- [research/maps/map-events-audit.md](research/maps/map-events-audit.md) — Map events audit: PD2 against core-events
- [research/maps/map-stacking.md](research/maps/map-stacking.md) — Map stacking (#1326)
- [research/maps/pd2-map-system.md](research/maps/pd2-map-system.md) — PD2's map system
- [research/maps/pd2-only-levels.md](research/maps/pd2-only-levels.md) — PD2-only levels and their tiles
- [research/maps/pd2-uber-bosses.md](research/maps/pd2-uber-bosses.md) — Inventory of PD2's uber bosses
- [research/maps/reimagined-map-system.md](research/maps/reimagined-map-system.md) — D2R Reimagined's map system
- [research/maps/uber-arenas.md](research/maps/uber-arenas.md) — Uber and event areas after the converter: how to build them

## Research: PD2 server rules and unported rows

- [research/pd2/remaining-unported-rows.md](research/pd2/remaining-unported-rows.md) — The remaining unported rows
- [research/pd2/w37-server-rules.md](research/pd2/w37-server-rules.md) — PD2's unidentified server rules (W37)

## Research: HD maps and name plates

- [research/hd/hd-hover-nameplates.md](research/hd/hd-hover-nameplates.md) — HD hover name plates for PD2-only monster codes (#1092)
- [research/hd/hd-parity-priority.md](research/hd/hd-parity-priority.md) — HD parity tuning order on main 9665a1e
- [research/hd/hd-parity-strategy.md](research/hd/hd-parity-strategy.md) — HD:legacy map parity: a strategy for one week

## Research: controller

- [research/controller/controller-audit.md](research/controller/controller-audit.md) — Controller support audit (#340): what D2R covers, what the port's screens miss

## Research: process and tooling

- [research/process/documentation-audit-2026-11-09.md](research/process/documentation-audit-2026-11-09.md) — PD2 Resurrected documentation audit
- [research/process/pd2-merge-speed.md](research/process/pd2-merge-speed.md) — pd2_merge speed (#803)

## Research: multiplayer

- [research/multiplayer/multiplayer-options.md](research/multiplayer/multiplayer-options.md) — TCP/IP over D2RLoader, hosted servers, and how Reimagined runs its ladder
- [research/multiplayer/hosted-leagues.md](research/multiplayer/hosted-leagues.md) — hosted leagues end to end: headless servers, saves, accounts, server needs, providers and monthly cost
