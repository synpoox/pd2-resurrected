# Plugins split by multiplayer scope, companions before our own

PD2's engine features are rebuilt as a few D2RLoader DLLs from one CMake project in `plugin/`, split by multiplayer scope rather than by feature: `core-rules` (Shared: skill and hit functions, cube ops, item rules, server hooks), `core-stash` (Shared, once PluginSDK supports extra pages) and `core-ui` (client-only). Shared code (per-build RVAs, helpers) is a static library linked into each. D2RLoader requires Shared plugins to match on host and client, so a client UI change must never force that match. Plugins never call each other; client-only plugins read what they need from tables or D2R's synced unit state, so they can be dropped without changing gameplay.

Where a bundled companion plugin already covers a PD2 feature (loot filter, stash search, tooltip roll ranges, buff timers), the port uses it instead of building its own, and lists what it lacks on the unported list.

`core-stash` and `core-ui` exist only for what no companion covers; if companions cover a system fully, that plugin is never built.

## Considered Options

- One DLL: simplest, but everything becomes Shared.
- One DLL per feature: dozens of versions to keep matched.
- Our own loot filter reading BH syntax: exact filter parity, but a large engine where a companion already works.

## Consequences

- Gameplay code is host-authoritative from day one, though multiplayer is tested only in its phase.
- Hooks live in one header per D2R build; on an unknown build a plugin logs and leaves the feature off.
- Each per-system plan lists companion hook points to avoid; a game check proves the port runs alongside every bundled companion.

## Amendments

- 2026-11-03: plugins renamed from `pd2-*` to `core-*` so the names stay valid if the port moves away from PD2 branding. Nothing had been built under the old names.
- 2026-11-03: a bundled companion may be Shared (Buff HUD is). Every release pins one version of each companion, so host and client match by construction; the "client UI never forces a match" rule binds only our own plugins.
- 2026-11-04: `core-maps` (Shared) holds the map system's own code (opening maps, map stats, cube op 30, map events, invaders), so it can be built and turned off on its own, and used outside this project; it never calls `core-rules`. Rules the map system shares with other content stay in `core-rules` ([map-system.md](../map-system.md), #152).
- 2026-11-04: `core-maps` also holds the uber bosses' own code (the Blood Fountain opening, area rules, tier scaling, phase scripts, drops and Desecration), since the uber bosses share the maps' opening and one-area-per-game gate; their skill functions stay in `core-rules` ([uber-bosses.md](../uber-bosses.md), #153).
