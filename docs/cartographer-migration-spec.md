## Problem Statement

PD2 Resurrected needs to replace core-maps with Cartographer without losing the PD2 map-event and uber fights already rewritten for the port. Keeping PD2's map-item, tier, affix and crafting systems would add compatibility work the owner has explicitly removed from scope.

The port still needs its generic item/cube behavior, useful event rewards, farmable uber access and a reproducible single-player release. Cartographer must remain independently usable by other projects, and work items must permit up to four agents without overlapping edits.

## Solution

Use Cartographer-native map items and native map-property behavior. Remove PD2 map items, the PD2 map affix/stat system, map tiers and PD2 map cube recipes, with one explicit exception: retain the Catalyst recipe adapted to the new native map items.

Keep the rewritten PD2 map-event fights and all preserved uber encounters together in a Shared core-events plugin. Keep general cube/skill/item/stack behavior and Almanac in core-rules, including corruption, Demonic Cube, retained recipe refusals, uber-key recipes/uber tiers and Desecration. Cartographer stays standalone and exposes only the generic integration points required by optional consumers.

Start with a small native catalogue: one map per game-verified Cartographer tileset, using native defaults and no tiers. Unsupported or unverified tilesets wait. Names, codes and concrete table assignments are reviewed in implementation PRs; there is no requirement to preserve PD2 map identities.

Adapt only the removed-system dependencies of events. Preserve Horazon's summoner fight, then open another eligible native map at the source monster level and drop six native map items. Preserve the Altar/Guardian fight and its 2–3 Catalyst Shards; the retained Catalyst recipe guarantees an event on the resulting map and keeps the existing forced-selection exclusion of the Altar. Keep Lilith, Uber Izual and Uber Duriel in their vanilla arenas. Use D2R's Uber Tristram arena with PD2's Tristram bosses and encounter/reward rules. Build new D2R-native static levels for Dclone, Rathma/Mendeln stages, Lucion and the Temple. Do not carry forward the old converted arenas. Rebuild required fixed event areas such as Bitter Depths independently too, so the PD2 DT1/painter/generator-converter pipeline can be retired completely.

## User Stories

1. As a player, I want native Cartographer map items to open generated areas, so that the replacement mapping system is usable in normal play.
2. As a player, I want a small catalogue of verified tilesets without map tiers, so that progression does not depend on the removed PD2 tier system.
3. As a player, I want native map properties and presentation to work, so that the new items communicate their effects.
4. As a player, I want PD2 map items and their obsolete drops removed, so that I do not receive items for a system the port no longer supports.
5. As a player, I want the PD2 map affix/stat system removed, so that old modifiers cannot accidentally become native player buffs.
6. As a player, I want obsolete map cube recipes removed, so that crafting does not produce retired maps or tiers.
7. As a player, I want the Catalyst recipe to work on native map items, so that Catalyst Shards retain their event-forcing purpose.
8. As a player, I want an event-forced map to retain that property through ordinary item persistence, so that the recipe's result is not lost before use.
9. As a player, I want retained map-event fights to keep their rewritten scripts, so that switching map systems does not replace the encounters.
10. As a player, I want event selection and progression to work in generated rooms, so that events start and finish correctly.
11. As a player, I want forced events to keep the existing Altar exclusion, so that the Catalyst loop retains its intended restriction.
12. As a player, I want the Altar to summon its Guardian and clean up its clones, so that the encounter remains functional.
13. As a player, I want the completed Altar to drop 2–3 Catalyst Shards, so that its reward remains useful.
14. As a player, I want Spire darkness, waves and completion cleanup preserved, so that the rewritten event remains playable.
15. As a player, I want Horazon's fight to open another eligible native map at my source map's monster level, so that the portal remains useful without PD2 tiers.
16. As a player, I want Horazon to drop six native map items, so that his reward survives removal of PD2 maps.
17. As a player, I want the Void/Tomb event to retain Bitter Depths and return travel, so that a required event area is not removed with unique maps.
18. As a player, I want scripted additional areas to preserve valid source/destination state, so that entering a portal does not corrupt the original map.
19. As a player, I want Diablo Clone, Rathma/Mendeln, Lucion and Uber Tristram preserved, so that the port keeps its scripted pinnacle fights.
20. As a player, I want uber keys, recipes, tiers and acquisition retained, so that every preserved encounter remains reachable through normal play.
21. As a player, I want uber entry, stripping/pets, scaling, phases, shared life, re-entry and rewards retained, so that the encounters keep their behavior.
22. As a player, I want Lucion's Temple and Desecration retained, so that completing the fight still unlocks the existing amulet action.
23. As a player, I want generic corruption, Demonic Cube and recipe refusal to remain correct, so that removing map recipes does not break other crafting.
24. As a player, I want stack quantities and leftovers to remain correct, so that generic cube actions consume only the intended ingredients.
25. As a player, I want the Almanac to keep identifying inventory and Cube items without affecting the stash, so that this item-use feature survives.
26. As a player, I want refused or failed item use to consume nothing, so that missing integration or placement failure does not lose items.
27. As a player, I want ordinary areas, HD/legacy views and unrelated UI to remain functional, so that the migration stays within scope.
28. As a player, I want alpha save-breakage expectations stated while keeping my save location, so that the patch follows the existing alpha policy.
29. As a mod author, I want Cartographer to work without PD2 plugins/content, so that it remains reusable.
30. As a maintainer, I want shared engine entry points to have one owner, so that load order cannot silently disable gameplay.
31. As a maintainer, I want deterministic content generation, reserved saved IDs and pinned artifacts, so that builds are reproducible.
32. As a maintainer, I want exactly one mapping backend active after install or upgrade, so that obsolete DLLs cannot conflict.
33. As a maintainer, I want four-agent work to use exclusive edit ownership and frozen contracts, so that parallel branches do not create textual merge conflicts.
34. As a maintainer, I want observable single-player acceptance and a rehearsed full-release rollback, so that a failed cutover can be diagnosed and recovered.

35. As a player, I want mini-ubers to keep vanilla arenas, Uber Tristram to use D2R's arena with PD2 bosses, and the remaining uber encounters to receive new native static levels, so that the port avoids unnecessary authoring and no longer depends on converted PD2 arenas.
36. As a player, I want collision, usable space, hazards, arrivals, phase positions and exits to support the preserved scripts, so that new levels do not break the fights.
37. As a maintainer, I want AI and server/client skills to use the same new placements, so that duplicated old arena coordinates cannot diverge.
38. As a maintainer, I want retained fixed event areas to have an independent native content path, so that pipeline retirement does not strand events.
39. As a maintainer, I want clean builds and release QA to work without the retired geometry tools or cached outputs, so that retirement is complete.

## Implementation Decisions

- **This revision supersedes the previous spec and 26-ticket draft.** Do not implement PD2 map-item/affix/tier/recipe parity. No child issues were published under that draft.
- **Ownership:** Cartographer owns native mapping and native map properties. core-events owns retained PD2 map events, event AI and all uber scripts, common PD2 AI registry, arena lifecycle and encounter rewards. core-rules owns general skill/item/cube/stack behavior and Almanac. core-ui retains unrelated UI duties. Do not create core-ubers or a PD2-specific Cartographer fork.
- **Native catalogue:** use one native map definition per game-verified tileset and native defaults; defer unverified tilesets. Supply actual mod-defined items, properties, catalogue data and normal acquisition rather than assuming the plugin ships a production catalogue. Review names/codes/tables in implementation. Use a single tierless acquisition pool; preserve unrelated loot and make its source/weights explicit. Horazon's six item rolls use that native pool.
- **Cuts:** remove PD2 map acquisition and map-specific items/consumables, affix eligibility/stat routing, fortify/population/drop modifiers, tier logic and map cube recipes except Catalyst. Unique maps and the special dungeon system remain cut. Prune removed branches from event/general treasure classes while keeping surviving loot; do not invent compensation rewards. Keep reserved saved-ID positions where applicable rather than renumber shared data.
- **Catalyst exception:** retain the existing recipe's ingredients, eligibility restrictions, event-forcing semantics and Altar exclusion, adapting its map input to native items. Retain only the isolated recipe/metadata needed for forcing, not the PD2 affix system or other map recipes. Treat the marker as event metadata: native property application must not apply it as a player/monster modifier. No direct-use shard consumable is being built.
- **Event preservation:** retain scripted fights, AI, waves, altars, cleanup and surviving rewards. Adapt active-map membership and progress from fixed PD2 level/room lists to generated-area identity. Keep existing quality-based event odds (normal 11%, magic 17% if applicable, other quality 34%) unless a recorded existing deviation applies. The native item supplies quality; the PD2 affix system is not needed.
- **Horazon:** keep the summoner encounter. Select another eligible Cartographer map, run that destination at the source monster level and give six native map items. Remove PD2-tier destination pools, old tier treasure classes, PD2 map-stat propagation and the unique-map roll. Preserve independent source/destination state and return travel; do not unintentionally reroll the event or reset progress.
- **Event areas:** preserve Bitter Depths for Void/Tomb, distinct from the removed Outer Void unique map. Newly built D2R-native fixed event areas are permitted where needed. Bitter Depths needs a converter-independent replacement with its retained encounter placement. Prove concurrent destination support, including same-tileset cases, or build a new validated native fixed-area bridge; old converted geometry is not a fallback. Ordinary Cartographer opening currently has global state; it is not proof that arbitrary second destinations are safe.
- **Ubers:** retain all existing fights, access/key recipes and uber tiers, which are not map tiers. Reuse the vanilla mini-uber arenas and D2R Uber Tristram arena with PD2 bosses; build new Dclone/Rathma/Lucion/Temple areas and preserve stage/state contracts, scaling, shared life, re-entry, stripping/pets, guaranteed and existing rare rewards, Temple chests and Desecration. Preserve fight mechanics while adapting placements; exact old geometry is not required.
- **New static content:** build the new Dclone, Rathma/Mendeln, Lucion and Temple static content, reusing/composing matching native assets where suitable. Do not build new mini-uber or Uber Tristram arenas: retain the vanilla mini-uber content and use D2R's Tristram arena with PD2 bosses. Preserve PD2 entry/tier/re-entry/reward rules, without duplicate stock boss spawns or stock rewards. Native engine DS1/DT1 resources may still be used; the retired dependency is this port's PD2 DT1/layout translation, painter and HD generator-converter. Do not bake old converted arenas into the permanent replacement.
- **Shared placements:** before parallel arena work, reserve level/preset IDs and disjoint asset namespaces, define arrivals, boss/add/soul/chest placements, phase regions, stage exits and return links, and make AI plus server/client skills consume the same placements. A serial owner changes the shared access/skill contract; fight authors edit private level/placement inputs and fight modules. Use a simple existing data seam where possible, not a general authoring framework.
- **Complete fight slices:** each fight slice delivers a playable encounter on its approved arena, including new static content only where required. Keep the vanilla mini-uber chain separate from D2R-arena/PD2-boss Tristram integration. Retain vanilla mini-uber IDs/portal/spawn behavior unless a proven integration change is necessary; arena reuse must not introduce new geometry or duplicate stock/PD2 spawns and rewards.
- **Acquisition:** prove normal acquisition for every retained uber ingredient after map removal. Preserve non-map key sources and Mendeln's retained rewards. Where an existing source is a map-boss concept, adapt it to an eligible native map boss with the retained base odds rather than carrying PD2 map-affix bonuses or old boss AI solely for acquisition. If no usable baseline source/rate exists, record that existing gap and obtain a concrete source decision; console-spawned test ingredients do not prove acquisition.
- **Generic cube rules:** remove map-only rows/branches, not the general matcher/executor. Retained non-map op-30 refusal rows exist, and the existing corruption follow-up search relies on blocking semantics. Preserve quantity/leftover behavior and Demonic Cube. Desecration is currently triggered from the old map plugin's search hook; keep that trigger and its corruption/Desecration roll helpers after deleting map-stat behavior.
- **Almanac:** retain its item-use implementation even though it is not a map cube recipe. Preserve the existing item-dispatch path rather than substituting an unproven local UI event.
- **Single hook owner:** resolve cube execution shared by Cartographer and core-rules, item use shared with uber/Almanac handling, monster setup needed by uber scaling, and room lifecycle/portal hooks needed by events. Prefer optional Cartographer cube stamping that does not install a second executor hook; prove the chosen seam. Never rely on load order.
- **Small generic contract:** use existing SDK services first and versioned optional extensions only for activation/refusal, area identity, room progress, monster lifecycle, item/event metadata, portals and teardown as required by retained scripts. Document thread/lifetime/registration rules. Missing required capabilities refuse unsafe activation without consumption. Native gaps are justified and executable writes use MutationService. Do not introduce a general engine API or retain a custom PD2 map-stat policy.
- **Shared helpers:** separate mixed map/uber/key/death logic before removal. Event Nihlathak/Mendeln code currently shares Dclone code; general corruption helpers and retained uber arena drops share map-stat code. Preserve those callers, one AI registry and common level effects. Delete ordinary PD2 map-boss behavior only when no retained script uses it.
- **Build:** build required native map/item and authored static-level data deterministically without the retired converter; do not mutate installed player tables after packaging. The stock installer restriction on adding IDs at 511 is not resolved merely by removing map stats, because unrelated retained PD2 stats already exceed that. Keep append-only ID reservations and verify the RotW table bank and text lookups agree.
- **Release:** obtain/pin a real Cartographer distributable and content hashes. Build core-events, retain core-rules/core-ui and retire core-maps after retained behavior has verified owners. Handle stale owned DLLs in both plugin scopes while preserving unrelated user files.
- **Concurrency:** at most four agents total. First separate mixed modules and freeze exact file ownership, shared interfaces, build/dispatch registration and private tests. Parallel lanes cover native maps, generic rules, map events and ubers. Each wave starts from one reviewed base; shared-file/API changes run through a serial owner/checkpoint, not a fifth concurrent agent. Unexpected boundary changes stop affected work rather than create overlapping edits.
- **Alpha saves:** keep the existing player savepath and compatibility as best effort, not a release gate. Add no conversion tool, mandatory fresh characters or retired-item layer. Do not deliberately rewrite/delete player saves. Warn about expected breakage and preserve snapshots for rollback.
- **Order:** freeze retained behavior/ownership; prove a minimal standalone-compatible contract and a native map with generic rules; prove a new native static area and freeze shared placements/IDs/asset ownership; adapt retained events/ubers with their new levels; implement the verified native catalogue and normal acquisition; remove obsolete PD2 maps after retained callers are separated; retire the geometry pipeline only after every retained area has a native replacement; stage, upgrade and rehearse rollback; retire the old backend and document cutover. The replacement work-item graph is reviewed before child publication.
- **Architecture:** revise the port's old core-maps ownership/no-calls decision, generated-PD2-level policy and upstream Cartographer's interface restriction while preserving independent use. Existing append-only saved-ID rules remain.
- **Rollback:** preserve the complete matched old release/config/hash/save snapshots; close game/loader; restore the full plugin/table/content/config set and check both scopes. Preserve newer candidate saves and deliberately restore a baseline snapshot if needed. Do not hot-swap hooks or promise downgrade compatibility.

## Testing Decisions

The previously confirmed primary seam remains the built, installed mod through the existing single-player game-check framework. Use existing tool fixtures for generation, recipes, packaging and upgrades, and focused tests at Cartographer's public extension contract. No new test framework or TCP/IP tests.

Good tests observe player behavior, public contract results and generated/installed artifacts. They do not mirror implementation loops, source layout or private call counts. Use fresh run evidence; a skipped check or old log is not a pass.

- **Environment:** single-player, Hell, level 90+, locked approved test slot and copied saves. Read the cheat-command guide first and follow the repo's ten-minute no-progress handoff rule.
- **Public contract/coexistence:** test standalone Cartographer plus the optional consumer, one owner per entry point, refusal without consumption, missing service/version behavior, teardown, native property behavior and safe additional destinations. Verify Catalyst metadata is not a native stat buff.
- **Native maps:** verify each item/catalogue route for the small verified-tileset set, normal acquisition, native properties/text/rarity, entrance-to-boss collision, return travel, maximum size and deterministic room rebuild. No PD2 map identity/tier/affix parity is required.
- **Catalyst:** verify the adapted recipe's valid/invalid inputs, original eligibility and ingredient consumption, persisted forcing marker, guaranteed event and Altar exclusion. Native properties must otherwise remain intact.
- **Events:** force every already implemented retained event, checking fight/AI/progress/spawns, Altar/Guardian/clone cleanup, 2–3 shard reward, Spire effects/cleanup, Horazon's source-level destination and six native items, and Bitter Depths travel/return. Preserve scripts; distinguish deliberate dependency adaptations from existing unfinished features.
- **Static levels:** independently build/load every new level set without legacy converted outputs. Check matching HD/legacy collision/layout, usable arena space/line of sight/hazards, arrivals, phase/boss/add/soul/chest placements, stage exits and return links. Verify AI and server/client skills agree on placements.
- **Ubers:** verify the mini-uber chain in vanilla areas under its existing rules and win the tiered fights on their approved reused/new arenas at uber tiers 0–2; verify normal ingredient acquisition separately from console setup, access, stripping/pets, scaling, phases/immunity/shared life, re-entry/healing, arena drops and controlled rare-roll evidence. Verify Temple chests, forced Desecration success/failure and one-attempt rules.
- **Generic rules:** exercise retained recipe families, non-map blocking, corruption including its follow-up search, Demonic Cube, stack quantities/leftovers and Almanac inventory/Cube/stash boundaries.
- **Removal:** old PD2 map items, tiers, affixes, modifiers and recipes no longer drop/roll/craft/apply, except the explicit Catalyst adaptation. Prune recursive treasure-class references; no retained script/key/recipe references a removed output or destination. Shared retained helpers and generic affix/cube systems remain functional.
- **Pipeline retirement:** clean extraction/build/staging must neither invoke the old PD2 DT1 import/painter/converter nor depend on their cached or shipped legacy level outputs. Audit all remaining consumers before deletion; retain unrelated monster/object/animation/missile/item assets and general table/ID tools. Replace converter-specific coverage/fidelity/edge-tour QA with native asset-reference/placement/collision checks and installed-game tests. Keep the table-diff gate.
- **Build/package/UI:** stable reserved IDs and repeatable generation, correct RotW lookup, pinned artifacts, one active backend, fresh install/upgrade/stale-file handling, ordinary areas, unrelated UI, HD/legacy, automap, names, panel and light/Terror Zone restoration. Compare matched load/frame-time/memory evidence, investigating repeatable frame-time regressions above 10%.
- **Rollback:** rehearse full matched package/config/save recovery with the game closed, preserving newer candidate saves and unrelated user files.
- **Repository evidence:** each implementation PR links its issue, uses changelog fragments and has the table-diff report for its final head after resolving main. Known approved content cuts are explicit; follow the project's owner-controlled parity-drop rules where applicable.
- **Parallel safety:** validate changed-file ownership before merging lane PRs; integrate/check each wave before issuing the next base. Different worktrees do not isolate shared generated outputs—use separate outputs or serialize those mutations.

Prior art remains the port's existing map/core-rules log-assisted game checks, item/ID/recipe fixtures, companion hash/overlay and release-staging tests, plus Cartographer's existing contract/table/layout tests. Remove obsolete PD2 map-parity assertions rather than spending work preserving tests for deleted features.

## Out of Scope

- PD2 map item/identity, map-tier, map-affix/stat or map-crafting parity; Catalyst is the sole explicit map-recipe exception.
- Replacing preserved fights with Cartographer generic event templates, rebalancing their AI/phases or completing unrelated existing encounter deviations.
- A separate core-ubers plugin, PD2-specific Cartographer fork or broad custom engine framework.
- Unverified tilesets, a large new catalogue, a new tier/progression system or compensation loot for removed branches.
- TCP/IP co-op/multiplayer testing or compatibility claims from this migration.
- Save conversion, a new player savepath, mandatory fresh characters or an unusable-item compatibility layer.
- Unrelated PD2 re-baselining, toolkit refactors, class/item changes or deletion of retained event/uber monster/object/missile/item assets. Old converted level geometry and its tools are explicitly retired after native replacements pass.
- Runtime implementation during planning/spec revision; running both map backends or live hook swapping.

## Further Notes

This revision records the owner's 2026-11-15 scope change and subsequent confirmations: native map items; small verified-tileset catalogue; preserved fights; Horazon's native destination/six items; Catalyst recipe retained instead of a direct-use consumable. All earlier requirements to preserve PD2 maps/affixes/tiers/recipes are superseded.

The former 26-ticket draft is void; no child issues had been published. A shorter four-agent, ownership-constrained breakdown must be approved before its publication.

Inspected baselines: port c2b6af8c7fa095ead030d1ffdc6b03c7449255f5 and Cartographer c79ef104742952f362b8956efbce37c9ed75226b. At inspection Cartographer was unreleased and the observed suite asset lacked it. Recheck changes and obtain a real pinned artifact; do not assume a production native item/drop catalogue or the required extension exists.

The [inspected upstream standalone decision](https://github.com/synpoox/d2r-tools/blob/c79ef104742952f362b8956efbce37c9ed75226b/docs/cartographer/adr-0001-cartographer-standalone.md) explicitly excludes this integration today, so the narrow reusable interface work remains explicit scope. Coordinate any overlap with #934; the old #927/#928 research issues do not substitute for this migration.

The latest owner clarification exempts mini-ubers and Uber Tristram from new arena authoring: mini-ubers use vanilla areas, while Tristram uses D2R's arena with PD2 bosses. New static levels remain required for Dclone, Rathma/Mendeln, Lucion/Temple and retained fixed event content, followed by retirement of the port's DT1/painter/generator-converter. This supersedes earlier wording carrying forward converted fixed arenas. Native replacements for every required fixed event area and a clean no-converter build are necessary for full retirement.

No runtime changes, builds, level assets or fresh game checks were performed while revising the spec. Alpha save breakage and single-player-only verification remain explicit limitations.

This local revision includes the latest arena clarifications. During to-tickets, the parent issue body/status is left unchanged; child tickets must explicitly carry these owner clarifications wherever the older blanket new-arena wording would conflict.
