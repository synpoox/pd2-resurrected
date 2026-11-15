# core-maps lane allowlist (#943)

Exact file allowlist of `plugin/core-maps/` per lane of the cartographer migration (`docs/cartographer-migration.md`).
A ticket in a lane edits only its lane's files. `tools/tests/test_core_maps_lanes.py` fails when a source file is in no
lane, in two, or listed here but missing. Lane **S** is Shared: any edit there is a checkpoint reported in the PR.

Lane column: `A` native maps (PD2 map code, obsolete with the native maps), `B` generic rules, `C` map events,
`D` ubers, `S` shared.

| File | Lane | Notes |
|---|---|---|
| plugin.cpp | S | Parts registry `g_parts[]` (install order) and the status command. One AI registry stays in ai_hooks. |
| plugin.rc | S | |
| ai_hooks.cpp, ai_hooks.h | S | The one AI registry and low-level dispatch. |
| death_steps.h | S | Seam of the monster-death listener: frozen. Call order in key_drops.cpp is part of the RNG stream. |
| key_drops.cpp, key_drops.h | S | The listener and the key rolls (run on every death). Calls the lane steps in a fixed order. |
| tc_roll.cpp, tc_roll.h | B | Odds-only TC roll, shared by the key rolls and the map rolls. |
| corruption_roll.cpp, corruption_roll.h | B | CRT roll and corruption/Desecration roll; map drops and bricks borrow them. |
| almanac.cpp, almanac.h | B | Horadric Almanac identify-all. Called by map_open.cpp's item spell hook. |
| cube_ops.cpp, cube_ops.h | B | Cube op 30 (map recipes use it, the op is generic). |
| open_gate.cpp, open_gate.h | S | Counters, player level and portal maker shared by map opening and uber entry. |
| map_open.cpp, map_open.h | A | Item spell hooks (map spell 16, Almanac spell 18), portal operate wrapper. |
| map_stats.cpp, map_stats.h | A | |
| map_bosses.cpp, map_bosses.h | A | |
| map_boss_ai.cpp, map_boss_ai.h | A | |
| map_deaths.cpp | A | Map steps of the death listener. |
| unique_maps.cpp, unique_maps.h | A | |
| unique_map_drops.cpp, unique_map_drops.h | A | Horazon (lane C) also calls `DropUniqueMap`. |
| zhar.cpp, zhar.h | A | |
| entity_pool.cpp, entity_pool.h | S | Engine pool growth, needed by any lane. |
| level_fx.cpp, level_fx.h | C | |
| map_events.cpp, map_events.h | C | |
| event_portals.cpp, event_portals.h | C | Horazon. |
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
| dclone_ai.cpp, dclone_ai.h | D | Also holds the event Nihlathak/Mendeln AI (lane C): not yet split. |

## Cross-lane dependencies (frozen contracts)

- Death listener order: uber early, map early, seed, uber drops, map roll + drop, key rolls (`death_steps.h`).
- `OpenCounter`, `g_openFailed`, `OpenPortalAt` (open_gate.h): used by A (map open) and D (uber entry).
- `g_tcMissing` (tc_roll.h): shared warning counter for the key rolls and the map rolls.
- `DropUniqueMap` (A) is called by Horazon in C.

## Not yet done

Nihlathak/Mendeln AI is still in dclone_ai.cpp; Horazon is still in event_portals.cpp with the rest of the events;
`g_parts[]` is still one list in plugin.cpp (no per-lane registration).
