# Cartographer migration plan — revised scope

Status: done. The revised scope settled on 2026-11-15 was built and merged in [#1058](https://github.com/synpoox/pd2-resurrected/pull/1058) (closes [#942](https://github.com/synpoox/pd2-resurrected/issues/942)); the plan below is kept as written. This replaces the earlier PD2-map-parity plan. [Spec #942](https://github.com/synpoox/pd2-resurrected/issues/942) is the published implementation specification; [the local spec](migration-spec.md) has the same scope.

## Agreed result

Use Cartographer-native map items, native properties and a small tierless catalogue: one map per game-verified tileset, using native defaults. Defer unsupported/unverified tilesets. Names, codes, concrete table assignments and acquisition weights are reviewed in implementation PRs, with normal map acquisition explicitly tested.

Remove PD2 map items, PD2 map affixes/stat routing, PD2 map tiers and PD2 map cube recipes. Unique maps and the special dungeon system remain cut. **Exception: keep the Catalyst recipe**, adapted to native map items, preserving its eligibility restrictions and forced-event behavior. Do not build a direct-use Catalyst consumable.

Preserve the rewritten PD2 scripted map-event fights and all uber encounters together in Shared core-events. Keep generic cube/skill/item/stack behavior, corruption, Demonic Cube, retained refusal rules, Almanac, uber keys/recipes/uber tiers and Desecration in their appropriate retained owners. Almanac is an item-use feature, not a map cube recipe. Uber tiers are not the removed map tiers.

Keep Cartographer standalone and PD2-neutral, core-rules for general rules and core-ui for unrelated UI. Narrow optional SDK-mediated extensions are allowed. Carry forward single-player-only verification, Hell/level 90+, unchanged player savepath, alpha save-breakage warnings, no save conversion/fresh-character requirement, append-only ID reservations, and at most four concurrent agents with nonoverlapping edits.

## Approved event adaptations

- Keep all already implemented scripted fights, AI, waves, altars, phase/cleanup behavior and surviving rewards; unrelated unfinished deviations are not added to migration scope.
- Event activation uses generated-area identity and room progress instead of old fixed PD2 level/room lists. Preserve the existing rarity-based roll: 11% normal, 17% magic when applicable, 34% other quality, reading native item quality.
- Keep the Altar/Guardian encounter and its 2–3 Catalyst Shard reward. The adapted cube recipe marks a native map to guarantee an event; forced selection excludes the Altar as before. The marker is isolated event metadata, not a PD2 affix system or a native player/monster buff.
- Keep Horazon's summoner fight. Afterwards open another eligible Cartographer map at the source map's monster level and drop six native map items. Remove PD2-tier destination pools, old tier treasure classes, map-stat propagation and the unique-map roll.
- Keep Void/Tomb travel to Bitter Depths, distinct from the removed Outer Void unique map. Build native static Bitter Depths and new Dclone/Rathma/Lucion/Temple levels. Mini-ubers retain vanilla arenas; Uber Tristram uses D2R's arena with PD2 bosses. Do not carry forward converted PD2 arenas.
- Prove source/destination independence and return travel. Do not assume Cartographer's one active map/stat/event state safely supports a second generated area; build a new validated native fixed-area bridge where necessary, without old converted geometry.

## Scope comparison

| Area | New scope |
|---|---|
| PD2 map identity/tiers | Removed; no catalogue conversion or tier preservation |
| PD2 map affixes/Divide routing/fortify/area/drop effects | Removed; use native Cartographer properties |
| PD2 map crafting | Removed except the explicitly retained Catalyst recipe |
| Generic cube behavior | Retained, including applicable non-map refusal rules and stack quantities/leftovers |
| PD2 ordinary map bosses/geometry/assets | Remove when not needed by a retained event/uber; native map bosses replace ordinary mapping content |
| PD2 scripted map events | Retain fights; apply only the approved surrounding adaptations |
| Uber encounters/access/rewards | Retain, including uber tiers and normal ingredient acquisition |
| Alpha saves | Same savepath, no compatibility gate or migration tool |
| Test scope | Single-player only; no multiplayer claim |

The former 26-ticket draft is superseded. The replacement has [25 work items](migration-tickets.md), awaiting breakdown approval before publication. No child issues have been published.

## Inspected implementation and retained dependencies

Baseline port: c2b6af8c7fa095ead030d1ffdc6b03c7449255f5; Cartographer: c79ef104742952f362b8956efbce37c9ed75226b, compared with local 629f08b. Recheck later changes before implementation. At inspection Cartographer was unreleased and the inspected suite release lacked its artifact; obtain/pin a distributable before packaging. Source support is not a fresh game-test pass.

[Event selection](../../plugin/core-events/map_events.cpp) uses item quality, force metadata and fixed level/room lists; [Horazon](../../plugin/core-events/event_portals.cpp) uses tier destination/drop pools. Those dependencies change; the fights do not.

Do not delete mixed modules wholesale:
- [Cube search](../../plugin/core-events/cube_ops.cpp) invokes Desecration before recipe-block scanning; retained non-map op-30 rules exist.
- Removed since this inspection: `map_stats.cpp`. Its generic corruption/Desecration roll helpers now live in [corruption_roll.cpp](../../plugin/core-events/corruption_roll.cpp). Its extra drops in retained uber arenas were part of the same module, and their new home is not recorded here.
- [Key/death dispatch](../../plugin/core-events/key_drops.cpp) mixes obsolete map rolls with preserved uber/key sources and rewards.
- Dclone code also contains the Mendeln/Nihlathak map-event AI. Preserve it and one shared AI registry/level-effects implementation.

Normal acquisition of retained uber ingredients must remain possible. Preserve non-map sources and surviving event rewards; where a required source is the map-boss concept, adapt it to an eligible native boss at retained base odds without PD2 map-affix bonuses. Record any pre-existing missing source instead of calling a console-spawned ingredient an acquisition pass. Prune removed recursive treasure-class branches without inventing compensation loot.

## New native static encounter levels

Mini-ubers (Lilith, Uber Izual, Uber Duriel) retain their vanilla arenas. Uber Tristram uses D2R's arena with PD2's Tristram bosses. New static level sets are required for Dclone, Rathma/Mendeln stages, Lucion arena and Temple. Bitter Depths also needs an independent native replacement; any justified fixed Horazon bridge must be new native content and still meet the approved destination outcome.

Reuse/compose matching D2R-native presets, collision and assets where suitable. Retiring this port's DT1/painter/generator-converter does not prohibit the engine's native DS1/DT1 format. Do not preserve old converted arenas as opaque permanent outputs or build another general procedural converter.

Dclone's centre/add positions, Rathma's stage/portal/arena positions and Lucion's phase corners/Royal Fire/Temple placements are duplicated across AI and skills. A serial shared checkpoint establishes one placement contract used by AI and server/client skills, reserved IDs and private per-encounter assets. Authors must not concurrently edit the common skill dispatcher.

Each fight slice delivers the approved reused/new arena, adapted bindings and playable fight. Keep vanilla mini-uber access/spawns/organ rewards intact and separate from Tristram's D2R-arena/PD2-boss integration. Verify PD2 access/tier/re-entry rules and suppress duplicate stock boss/reward paths in the finale. Preserve fight mechanics and required usable space, collision, line of sight, hazards, phases, arrivals and exits; exact old geometry is not required.

Converter retirement is a separate final work item blocked by all native replacements. Audit active extraction/build/merge/QA consumers, remove the old level conversion/painter path and dedicated coverage artifacts, and use native reference/placement/collision checks plus installed-game tests instead. Keep unrelated PD2 monster/object/animation/missile/item assets and general table/ID tools. A clean build must need no legacy converter output.

## Ownership and proof gates

| Owner | Responsibility |
|---|---|
| Cartographer | Native map generation/properties/presentation, active-area identity and needed optional generic callbacks/portals |
| core-events | PD2 event selection/scripts/adapters, event AI, all uber scripts/access/scaling/rewards, one PD2 AI registry and fixed-area lifecycle |
| core-rules | General cube/corruption/Demonic Cube/refusal/stack/skill rules, Almanac and generic roll helpers |
| core-ui | Unrelated existing UI/legacy/stack functionality |
| Build/data | New native content, deterministic generation, stable IDs and removal of obsolete PD2 map data |

One process has one owner per native hook/slot. Remaining collisions include Cartographer versus core-rules cube execution, item dispatch for ubers/Almanac, monster setup for uber scaling and room/portal lifecycle for events. Optional cube stamping must not install a competing executor hook. Changing a spell number, DLL name or load order does not solve ownership.

Use SDK services first and versioned public contracts with explicit thread/lifetime/registration rules. Missing required capabilities refuse activation without consumption. Do not build a broad custom engine API or preserve a PD2 map-stat policy. Only Catalyst's isolated forcing metadata remains; native property application must exclude it as a buff.

Generate native rows through the existing build, not a mutating installer against an installed release. Cartographer's stock installer rejects additions at stat ID 511, while retained unrelated PD2 data already exceeds that; deleting map stats does not authorize renumbering IDs. Reserve old numeric slots as needed and verify RotW/text lookup consistency.

## Order and four-agent execution

1. Freeze baseline and mechanically separate retained mixed helpers/modules. Publish an exhaustive exact-file ownership manifest, prewire shared source/parts/test registration and retain a working old default.
2. Prove standalone-compatible activation, item/event metadata, room/monster lifecycle and cube/Almanac coexistence; demonstrate one native map in an isolated candidate.
3. Prove a new native static area and freeze shared placement/ID/asset contracts before parallel authoring. Parallel work uses four lanes: native map content, generic rules, PD2 map events and ubers. Each ticket owns private implementation/tests/fixtures; shared files/contracts are frozen. Every wave starts from one reviewed base.
4. Use a serial shared checkpoint for Horazon's additional-area contract, then converge retained events/ubers and Desecration.
5. Remove obsolete PD2 map data/handlers only after retained callers have safe owners; retire the old geometry pipeline after every retained area has a native replacement, stage one pinned release, rehearse install/upgrade/rollback, then retire core-maps and cut over the default.

At most four agents total; the shared owner is not a fifth concurrent agent. Unexpected shared edits become checkpoint prerequisites. Separate worktrees/locked game slots do not isolate shared generated outputs; use independent outputs or serialize those mutations. Check changed-file allowlists before merge and combined behavior before the next wave.

## Verification and rollback

Use the previously approved primary seam: installed-mod single-player game checks, plus existing tool fixtures and focused public-contract tests. No new framework. Test native map acquisition/properties/access/geometry/UI; retained events and approved Horazon/Catalyst changes; the vanilla mini-uber chain under its existing rules and tiered ubers on approved reused/new arenas at tiers 0–2; normal ingredient acquisition; generic recipes/refusals/corruption/Demonic Cube/Almanac; forced Desecration success/failure; absent-capability/refusal paths and ordinary areas.

Test Catalyst eligibility, ingredient safety, persistent forcing metadata, guaranteed event/Altar exclusion and no accidental native buff. Do not retain tests for deleted PD2 map affix/tier/crafting parity. Record fresh pass/fail/skipped evidence and existing deviations separately.

Verify removed PD2 maps/tiers/affixes/recipes no longer drop/roll/craft/apply except the isolated Catalyst adaptation, with no dangling retained output/destination reference. Verify stable IDs, correct table bank, package hashes, exactly one backend and stale-owned-DLL cleanup in both scopes. Keep unrelated user files. Compare matched load/frame-time/memory evidence and investigate repeatable frame-time regressions above 10%.

Read the cheat-command guide first, use approved locked slots in Hell/level 90+, and follow the handoff rule: stop only after 5 minutes stuck with no progress (not a time limit). Test slots use copies of saves; player savepath remains unchanged. No TCP/IP checks or compatibility claims.

Back up the known-good matched package/config/hash set and save snapshots. Rollback with game/loader closed restores the whole DLL/table/content/config set, checks both plugin scopes and deliberately restores baseline saves if needed while preserving newer candidate copies. Rehearse upgrade and rollback; no live hook swapping or promised downgrade compatibility.

Each implementation PR links its issue, uses a changelog fragment and has the final-head table-diff evidence after resolving main. Amend relevant architecture/deviation/user docs and upstream Cartographer's interface restriction; standalone use remains required. Existing alpha save-breakage allowance and append-only ID rules remain.

Clean-build acceptance must prove the old DT1/painter/generator-converter is neither invoked nor required through cached or shipped legacy level output. Verify native HD/legacy collision and placement/phase/return behavior; converter-specific QA does not gate a converter-free build.

No runtime code, tables, level assets or game installations have changed during this scope revision.

Latest clarification: this local plan/spec and the child-ticket draft reflect vanilla mini-ubers and D2R-arena/PD2-boss Tristram. The to-tickets publication workflow leaves the parent issue body/status unchanged; publish the clarification in the affected children.
