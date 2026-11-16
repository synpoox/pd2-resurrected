# core-events lane allowlist (#943)

Exact file allowlist of `plugin/core-events/` per lane of the cartographer migration (`docs/cartographer-migration.md`).
A ticket in a lane edits only its lane's files. `tools/tests/test_core_events_lanes.py` fails when a source file is in no
lane, in two, or listed here but missing. Lane **S** is Shared: any edit there is a checkpoint reported in the PR.

Lane column: `A` native maps (PD2 map code, obsolete with the native maps), `B` generic rules, `C` map events,
`D` ubers, `S` shared.

| File | Lane | Notes |
|---|---|---|
| plugin.cpp | S | Installs the registered parts in order and runs the status command. No part lists live here. One AI registry stays in ai_hooks. |
| parts.cpp, parts.h | S | The parts registry: `PartReg` and `CountersReg`, sorted by order. Frozen. |
| parts_shared.cpp | S | Parts of lane S: `pool` (50), `placements` (55, #947) and `keys` (60) and the key drop counters. |
| lane-a/ | A | Every file in the folder is lane A's: `parts.cpp` registers its parts and counters. |
| lane-b/ | B | Every file in the folder is lane B's: `parts.cpp` registers its parts and counters. |
| lane-c/ | C | Every file in the folder is lane C's: `parts.cpp` registers its parts and counters. |
| lane-d/ | D | Every file in the folder is lane D's: `parts.cpp` registers its parts and counters. |
| plugin.rc | S | |
| ai_hooks.cpp, ai_hooks.h | S | The one AI registry and low-level dispatch. |
| ai_common.h | S | Helpers the Dclone AI (D) and the event Nihlathak AI (C) share: counters, idle, cast, kill, nearest player. Frozen. |
| death_steps.h | S | Seam of the monster-death listener: frozen. Call order in key_drops.cpp is part of the RNG stream. |
| key_drops.cpp, key_drops.h | S | The listener and the key rolls (run on every death). Calls the lane steps in a fixed order. |
| tc_roll.cpp, tc_roll.h | B | Odds-only TC roll, shared by the key rolls and the map rolls. |
| corruption_roll.cpp, corruption_roll.h | B | CRT roll and corruption/Desecration roll; map drops and bricks borrow them. |
| cube_ops.cpp, cube_ops.h | B | Cube op 30 (map recipes use it, the op is generic) and the Desecration search trigger: the one owner of the cube recipe search hook. |
| open_gate.cpp, open_gate.h | S | Counters, player level and portal maker shared by map opening and uber entry. |
| map_open.cpp, map_open.h | A | Portal operate wrapper, the uber levels' waypoint and uber scaling spawn hooks; PD2's map item spells are cut (#964), Cartographer owns the item spell dispatchers (#945). The Almanac moved to core-rules (`plugin/core-rules/almanac.cpp`, a write of item spell use slot 14, no hook). |
| entity_pool.cpp, entity_pool.h | S | Engine pool growth, needed by any lane. |
| level_fx.cpp, level_fx.h | C | |
| map_events.cpp, map_events.h | C | |
| event_portals.cpp, event_portals.h | C | The red portal pair (`OpenEventPortal`), shared by Horazon and the Void/Tomb event. |
| horazon.cpp, horazon.h | C | Horazon's portal and map drops (the one monster-death listener also hands the Altar guardian over). |
| nihlathak_ai.cpp, nihlathak_ai.h | C | The event Nihlathak/Mendeln AI, on monai row 99 through ai_hooks. |
| guardian_ai.cpp, guardian_ai.h | C | Altar of the Catalyst guardian. |
| uber_entry.cpp, uber_entry.h | D | Blood Fountain opening, portal entry rules, grey portals, boss rooms kept. |
| uber_deaths.cpp | D | Uber steps of the death listener. |
| uber_drops.cpp, uber_drops.h | D | |
| uber_scaling.cpp, uber_scaling.h | D | |
| uber_tristram.cpp, uber_tristram.h | D | |
| prime_minions.cpp, prime_minions.h | D | |
| lucion.cpp, lucion.h | D | |
| lucion_ai.cpp, lucion_ai.h | D | |
| lucion_chests.cpp, lucion_chests.h | D | |
| rathma.cpp, rathma.h | D | |
| rathma_ai.cpp, rathma_ai.h | D | |
| rathma_life.cpp, rathma_life.h | D | |
| arena_spots.cpp, arena_spots.h | D | Named spots of Cartographer's fixed arenas, read through `cartographer.arena.v1` (#958); the part (57) keeps the context and acquires the service at first use. |
| test_kill.cpp, test_kill.h | S | Game-check aid: `kill <class>` and `life <pct> unit <id>` run on the game thread (shared, no part). |
| dclone_ai.cpp, dclone_ai.h | D | Dclone and his trapped souls. Its install also registers the Nihlathak AI (C), right after the souls, so the part's log line stays one. |

## Cross-lane dependencies (frozen contracts)

- Death listener order: uber early, seed, uber drops, key rolls (PD2's map steps are cut, #964) (`death_steps.h`).
- `OpenCounter`, `g_openFailed`, `OpenPortalAt` (open_gate.h): used by A (map open) and D (uber entry).
- `g_tcMissing` (tc_roll.h): shared warning counter for the key rolls and the map rolls.
- `DropUniqueMap` (A) is called by Horazon in C.

## Registration

Each part and each status counters line is one object in a lane folder: `PartReg r<order>(<order>, "<word>", <install>)`
and `CountersReg c<order>(<order>, <counters>)` (`parts.h`). `plugin.cpp` sorts them by order and installs them, so the
order is the install sequence and the log line order. Orders are one global sequence in steps of 10; a new part takes an
unused number (the test fails on a duplicate). `plugin/CMakeLists.txt` globs `core-events/lane-*/*.cpp`, so a lane adds
files in its own folder and edits neither `plugin.cpp`, `CMakeLists.txt` nor this file; only a file outside the lane
folders (lane S, or a lane's older flat files) needs a row above and a CMake line.

Orders now: cube 10 (B), open 20 (A), mapevents 40 (C), pool 50, keys 60 (S), trist 70, lucion 80, primes 90,
temple 100 (D), events 110 (C), lucionai 120, rathmaai 130, rathmalife 140, dcloneai 150 (D), guardianai 160 (C).
Counters lines: the same steps. Orders 30, 170 and 180 (stats, mapbossai, zhar) were cut with PD2's maps (#964).

## Evidence for a PR

Report pass, fail and skipped separately for each check (clean build, `tools/pd2_diff.py --scope alpha`,
`pytest tools/tests`), and report existing deviations on their own, not mixed into those counts.
