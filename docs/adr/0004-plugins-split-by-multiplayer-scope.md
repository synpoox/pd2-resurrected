# Plugins split by multiplayer scope, companions before our own

PD2's engine features are rebuilt as a few D2RLoader DLLs from one CMake project in `plugin/`, split by multiplayer scope rather than by feature: `pd2-rules` (Shared: skill and hit functions, cube ops, item rules, server hooks), `pd2-stash` (Shared, once PluginSDK supports extra pages) and `pd2-ui` (client-only). Shared code (per-build RVAs, helpers) is a static library linked into each. D2RLoader requires Shared plugins to match on host and client, so a client UI change must never force that match. Plugins never call each other; client-only plugins read what they need from tables or D2R's synced unit state, so they can be dropped without changing gameplay.

Where a bundled companion plugin already covers a PD2 feature (loot filter, stash search, tooltip roll ranges, buff timers), the port uses it instead of building its own, and lists what it lacks on the unported list.

## Considered Options

- One DLL: simplest, but everything becomes Shared.
- One DLL per feature: dozens of versions to keep matched.
- Our own loot filter reading BH syntax: exact filter parity, but a large engine where a companion already works.

## Consequences

- Gameplay code is host-authoritative from day one, though multiplayer is tested only in its phase.
- Hooks live in one header per D2R build; on an unknown build a plugin logs and leaves the feature off.
- Each per-system plan lists companion hook points to avoid; a game check proves the port runs alongside every bundled companion.
