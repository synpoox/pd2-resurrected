---
status: proposed
---

# Cartographer owns native maps; core-events preserves PD2 encounters

The 2026-11-15 scope removes PD2 map items, PD2 map affixes/stat routing, map tiers and map cube recipes, with the explicit exception of the Catalyst recipe adapted to native map items. Use a small Cartographer-native catalogue with one map per game-verified tileset and no tiers. This supersedes the earlier proposal to preserve PD2 map identity, affixes and crafting.

Retain the rewritten PD2 map-event fights and all uber encounters together in Shared core-events, with one shared AI registry and fixed-area lifecycle. Preserve the Altar/Guardian and its 2–3 Catalyst Shards; the retained recipe guarantees an event and excludes the Altar when forcing. Preserve Horazon's fight, then open an eligible native map at the source monster level and drop six native map items. Retain vanilla arenas for Lilith, Uber Izual and Uber Duriel. Use D2R's Uber Tristram arena with PD2 bosses and encounter/reward rules. Build new D2R-native static levels for Dclone, Rathma/Mendeln stages, Lucion arena and Temple. Build Bitter Depths and any justified Horazon bridge independently too.

Cartographer stays standalone, PD2-neutral and usable without a consumer. Add only the narrow optional SDK-mediated integration required by retained encounters, with one native-hook owner. General cube/item/stack/skill rules, corruption, Demonic Cube, retained refusals, Almanac and generic roll helpers remain in core-rules; uber keys/tiers and Desecration remain functional. Do not retain PD2 map-stat policy to support encounters.

Retire this port's PD2 DT1 import/painter/HD generator-converter only after all retained areas have native replacements and a clean build needs no legacy outputs. Keep unrelated monster/object/item assets and general table/ID tools. Freeze shared placements before authors adapt AI and server/client skills; each fight slice includes its new levels and working encounter.

This revises ADR 0005's converted-level policy, ADR 0004's old ownership/no-calls boundary and requires revision of upstream Cartographer's interface restriction. Alpha saves keep their path without conversion or compatibility guarantees; stable saved IDs remain append-only. Verification is single-player only. Four-agent implementation uses disjoint edit ownership and serial shared checkpoints, not a separate core-ubers DLL.

See [the revised migration plan](../cartographer-migration.md) and [specification](../cartographer-migration-spec.md). This record describes the agreed architecture; its required integration contract still needs implementation proof.
