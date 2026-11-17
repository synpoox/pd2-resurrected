# Work items for #942 — corrected vanilla arena reuse

Status: done. The child issues of [#942](https://github.com/synpoox/pd2-resurrected/issues/942) were published and merged in [#1058](https://github.com/synpoox/pd2-resurrected/pull/1058); the draft below keeps its own IDs, which are not GitHub numbers. Publication is in dependency order, not numerical order.

## Owner clarification and scope

The latest owner instructions supersede the parent spec's earlier blanket requirement to author every uber arena:

- Lilith, Uber Izual and Uber Duriel use their **vanilla arenas** and key chain.
- Uber Tristram uses **D2R's arena with PD2's Tristram bosses**, behavior and rewards.
- New native static content is required for Dclone, Rathma/Mendeln stages, Lucion/Temple, Bitter Depths and any justified fixed Horazon bridge.

Do not edit or close #942's body/status during to-tickets. Every affected child must repeat the relevant clarification so an agent following the older parent wording does not build unnecessary mini-uber/Tristram arenas. Native child relationships are still required.

Remove PD2 map items/affixes/stats/tiers/recipes except the Catalyst adaptation. Keep scripted PD2 event fights/ubers in core-events, generic cube/corruption/Demonic Cube/refusal/stack behavior, Almanac, uber keys/tiers and Desecration. Use the small tierless native catalogue, Horazon source-level travel/six items and Altar/Guardian/2–3 shards. Single-player only; existing alpha savepath; no save conversion. Retire the old DT1/painter/generator-converter after every needed area has a native path; vanilla D2R assets already satisfy that requirement for reused arenas.

## Four-agent assignment contract

At most four agents total; the shared role is not a fifth. Before parallel assignment, mechanically separate mixed modules, publish an exhaustive exact-file/asset ownership manifest, freeze shared contracts and registration, and give each wave one reviewed base.

Each active lane owns its private implementation/assets/tests/fixtures/evidence and issue-specific changelog fragment. Shared upstream adapters, headers, placement/skill/access/death/AI/effects dispatch, build/parts/source lists, central IDs/merge indices, runners and packaging have one serial owner. An unexpected shared change pauses affected work and becomes a checkpoint prerequisite, not an overlapping edit or helper copy.

AI and server/client skill placements must agree. Encounter authors edit private placement/content inputs, not the common skill dispatcher. Use separate worktrees/locked test slots and independent outputs; serialize mutations of shared generated/cache/junction targets. Check changed-file allowlists before merge and combined behavior before issuing the next base. These controls prevent textual conflicts; runtime integration is still tested.

| Lane | Exclusive responsibility |
|---|---|
| A — native maps | Tierless catalogue/acquisition/geometry/presentation and private checks |
| B — general rules | Generic cube/Almanac/roll/refusal/stack behavior and Catalyst/private checks |
| C — map events | Event scripts/AI/portals and private content, including new Bitter Depths |
| D — ubers | Uber-only access/rewards/fights/private checks; approved reused/new arena bindings |
| Serial shared role | Common contracts/IDs/dispatch/build, upstream changes, convergence, retirement and release |

## Execution waves

| Stage | A | B | C | D |
|---|---|---|---|---|
| Serial foundation | Shared: 1 → 2 → 3 → 4 → 23 | paused | paused | paused |
| Wave 1 | 5 catalogue | 6 generic rules | 7 spawn events | 8 access/scaling |
| Wave 2 | 9 geometry/UI | 10 Catalyst | 11 Guardian | 25 vanilla mini-ubers |
| Wave 3 | idle | idle | 13 Spire | 12 D2R Tristram/PD2 bosses |
| Continuation | idle | idle | idle | 14 new Dclone arena |
| Serial checkpoint | Shared: 15 Horazon/portal | paused | paused | paused |
| Wave 4 | idle | idle | 16 new Bitter Depths | 17 new Rathma stages |
| Continuation | idle | idle | idle | 18 new Lucion/Temple |
| Serial closeout | Shared: 19 → 20 → 24 → 21 → 22 | paused | paused | paused |

Ownership barriers are deliberate blockers. D's fight tickets and C's shared event modules are sequential; shared portal/placement changes are checkpoints. No unrelated lane edits shared files concurrently. Removal can use an integration branch when intermediate default releases cannot stay green; merge only after final acceptance.

## Numbered breakdown

1. **Freeze retained behavior and separate mixed modules** — **Blocked by:** none. **Delivers:** Existing event/uber and generic-rule behavior remains verifiable while the candidate gains exclusive edit boundaries. **Assignment:** Shared.

2. **Prove standalone-compatible encounter integration** — **Blocked by:** 1. **Delivers:** Cartographer works alone and with a minimal optional consumer that observes map/room/monster lifecycle and event metadata. **Assignment:** Shared.

3. **Preserve general cube execution and Almanac with Cartographer** — **Blocked by:** 2. **Delivers:** Native mapping coexists with retained corruption/Demonic Cube and identify-all behavior. **Assignment:** Shared.

4. **Open and acquire one native map in the candidate** — **Blocked by:** 3. **Delivers:** A real native map item is obtainable and opens a generated area with the supported PD2 plugins active. **Assignment:** Shared. **Done in #946:** the frozen contracts, reserved ids and four-lane file ownership are in [cartographer-contracts.md](contracts.md); lanes edit only the files that table gives them.

5. **Complete the small verified native catalogue** — **Blocked by:** 23. **Delivers:** One native map per game-verified tileset is obtainable through normal tierless acquisition. **Assignment:** A — native maps.

6. **Preserve generic recipes, refusals and stack behavior** — **Blocked by:** 23. **Delivers:** Non-map cube and item rules remain correct after their helpers are separated from old map logic. **Assignment:** B — general rules.

7. **Run retained spawn-based map events on native maps** — **Blocked by:** 23. **Delivers:** Existing Wanderer/Mendeln/Treasure Fallen/Gheed scripts and shared event selection run in generated rooms. **Assignment:** C — map events.

8. **Preserve uber access, key acquisition, scaling and re-entry** — **Blocked by:** 23. **Delivers:** Shared uber access, acquisition, scaling and re-entry support the approved vanilla/reused/new arena bindings; fight slices prove each finished destination. **Assignment:** D — ubers.

9. **Verify native geometry, presentation and load** — **Blocked by:** 5. **Delivers:** The small native catalogue is readable, reachable and stable across supported layouts. **Assignment:** A — native maps.

10. **Adapt the Catalyst recipe to native map items** — **Blocked by:** 6, 7. **Delivers:** A Catalyst Shard cubes with an eligible native map to guarantee a retained PD2 event. **Assignment:** B — general rules.

11. **Preserve the Altar and Guardian fight/reward** — **Blocked by:** 7. **Delivers:** The Altar retains its Guardian/clone encounter and 2–3 Catalyst Shard reward. **Assignment:** C — map events.

12. **Use D2R's Uber Tristram arena with PD2 bosses** — **Blocked by:** 25. **Delivers:** D2R's existing Uber Tristram arena hosts PD2's Tristram bosses, behavior and rewards. **Assignment:** D — ubers.

13. **Preserve Spire of Darkness** — **Blocked by:** 11. **Delivers:** Spire darkness, waves and completion cleanup work on native maps. **Assignment:** C — map events.

14. **Build the new static Diablo Clone arena and preserve its fight** — **Blocked by:** 12. **Delivers:** Diablo Clone's scripted fight and rewards run in a newly built native static arena. **Assignment:** D — ubers.

15. **Integrate Horazon's native destination and six-map reward** — **Blocked by:** 9, 10, 13, 14. **Delivers:** Horazon keeps his fight, opens another eligible native map at the source monster level and drops six native map items. **Assignment:** Shared checkpoint.

16. **Build native static Bitter Depths and preserve Void/Tomb travel** — **Blocked by:** 15. **Delivers:** Void/Tomb reaches a newly built converter-independent Bitter Depths with its retained encounter and return path. **Assignment:** C — map events.

17. **Build new static Rathma stages and preserve Rathma/Mendeln** — **Blocked by:** 15. **Delivers:** Rathma/Mendeln's staged fight, shared life and rewards work across newly built native static stage areas. **Assignment:** D — ubers.

18. **Build new static Lucion arena and Temple and preserve the fight** — **Blocked by:** 17. **Delivers:** Lucion's phases, rewards and Temple/chests work in a new native static arena and Temple. **Assignment:** D — ubers.

19. **Converge Desecration and retained generic rules** — **Blocked by:** 10, 16, 18. **Delivers:** A Lucion kill still enables the one-attempt amulet Desecration action through the coordinated cube path. **Assignment:** Shared convergence.

20. **Remove obsolete PD2 map systems and references** — **Blocked by:** 19. **Delivers:** PD2 map items, affixes/stats, tiers and map recipes are cut, with only the approved native Catalyst adaptation. **Assignment:** Shared closeout.

21. **Stage the pinned release and rehearse upgrade/rollback** — **Blocked by:** 24. **Delivers:** The matched candidate installs/upgrades to one map backend and can be rolled back completely. **Assignment:** Shared closeout.

22. **Cut over the default backend with final single-player evidence** — **Blocked by:** 21. **Delivers:** The default release uses native Cartographer maps with preserved PD2 events, ubers and generic rules. **Assignment:** Shared closeout.

23. **Prove native static-level authoring and freeze encounter placements** — **Blocked by:** 4. **Delivers:** A new native static area loads and supports encounter placements independently of the old conversion pipeline. **Assignment:** Shared.

24. **Retire the PD2 DT1/painter/generator-converter and replace its QA** — **Blocked by:** 20. **Delivers:** Clean builds and releases use native map/static content without invoking or depending on the retired conversion pipeline. **Assignment:** Shared closeout.

25. **Preserve the mini-uber key chain in vanilla arenas** — **Blocked by:** 8. **Delivers:** Lilith, Uber Izual and Uber Duriel keep their vanilla arenas, key-chain access and organ rewards. **Assignment:** D — ubers.

## Work-item acceptance criteria

### 1. Freeze retained behavior and separate mixed modules

**Parent:** #942

**What to build:** Existing event/uber and generic-rule behavior remains verifiable while the candidate gains exclusive edit boundaries.

**Assignment:** Shared

**Blocked by:** None (can start immediately)

**Proposed status:** ready-for-agent

- [ ] Pin baseline inputs and capture fresh representative event/uber/cube evidence before removal.
- [ ] Separate general roll/search helpers, event Nihlathak/Mendeln AI, uber/key rewards and entry logic from obsolete map-oriented responsibilities without changing retained behavior.
- [ ] Freeze one AI registry/low-level dispatch, prewire shared build/status/test registration and publish an exhaustive exact-file allowlist for native maps, generic rules, events and ubers.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 2. Prove standalone-compatible encounter integration

**Parent:** #942

**What to build:** Cartographer works alone and with a minimal optional consumer that observes map/room/monster lifecycle and event metadata.

**Assignment:** Shared

**Blocked by:** Draft 1

**Proposed status:** ready-for-agent

- [ ] Add only the generic activation/refusal, area identity/progress, monster setup and teardown capabilities needed by retained scripts; define versions, thread/lifetime and registration rules.
- [ ] Demonstrate missing-capability refusal without consumption, stable source state and custom event metadata excluded from native stat buffs.
- [ ] Verify standalone default mapping/properties and the focused public contract; add no PD2 map-stat or tier policy upstream.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 3. Preserve general cube execution and Almanac with Cartographer

**Parent:** #942

**What to build:** Native mapping coexists with retained corruption/Demonic Cube and identify-all behavior.

**Assignment:** Shared

**Blocked by:** Draft 2

**Proposed status:** ready-for-agent

- [ ] Prove a single cube-execution and item-dispatch owner; optional stamping must not install a second executor hook.
- [ ] Relocate/reuse generic refusal, corruption/roll and Almanac handlers without deleting the retained Desecration search trigger.
- [ ] Demonstrate one-press corruption, Demonic Cube, leftovers and Almanac inventory/Cube/stash boundaries on the exclusive candidate.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 4. Open and acquire one native map in the candidate

**Parent:** #942

**What to build:** A real native map item is obtainable and opens a generated area with the supported PD2 plugins active.

**Assignment:** Shared

**Blocked by:** Draft 3

**Proposed status:** ready-for-agent

- [ ] Generate one verified-tileset definition, native item/properties and a tierless acquisition pool without PD2 map-item/affix/recipe dependencies.
- [ ] Verify real use, failed-use safety, native properties, return travel, current opening restriction and ordinary areas.
- [ ] Publish the frozen consumer contracts, reserved Catalyst metadata definition, shared registration/ID wiring and reviewed base for four nonoverlapping lanes.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 23. Prove native static-level authoring and freeze encounter placements

**Parent:** #942

**What to build:** A new native static area loads and supports encounter placements independently of the old conversion pipeline.

**Assignment:** Shared

**Blocked by:** Draft 4

**Proposed status:** ready-for-agent

- [ ] Build/load a representative native static uber area with matching HD/legacy collision, arrivals, return link and testable encounter regions using stock native building blocks where suitable; this may seed a final fight area rather than create throwaway content.
- [ ] Reserve private IDs/assets only for new Dclone/Rathma/Lucion/Temple/Bitter Depths and any justified bridge; separately freeze vanilla mini-uber and D2R Tristram bindings without reserving replacement arenas.
- [ ] Adapt the shared placement consumers once: AI, server/client skills, stage travel, chests and re-entry must read the same per-level placements. Freeze their interface and shared build/access registrations before parallel authors start.
- [ ] Publish exact per-encounter file ownership; private authors change their level/placement data and fight modules, not a common skill dispatcher or geometry converter. Verify no old generated output is needed for this proof.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 5. Complete the small verified native catalogue

**Parent:** #942

**What to build:** One native map per game-verified tileset is obtainable through normal tierless acquisition.

**Assignment:** A — native maps

**Blocked by:** Draft 23

**Proposed status:** ready-for-agent

- [ ] Use native defaults and review names/codes/assignments; defer unverified tilesets and do not recreate PD2 map tiers or affixes.
- [ ] Provide one explicit native acquisition pool for normal sources and Horazon's six item rolls, preserving unrelated loot.
- [ ] Verify all item routes, native properties/boss reachability and deterministic table/ID generation in private native-map tests.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 6. Preserve generic recipes, refusals and stack behavior

**Parent:** #942

**What to build:** Non-map cube and item rules remain correct after their helpers are separated from old map logic.

**Assignment:** B — general rules

**Blocked by:** Draft 23

**Proposed status:** ready-for-agent

- [ ] Exercise retained recipe families, non-map refusal rows, corruption follow-up search, Demonic Cube and stack quantities/leftovers.
- [ ] Keep uber-key recipes and generic affix/item/skill behavior intact; no map-affix/stat parity work.
- [ ] Leave frozen dispatch and unrelated lane sources untouched; preserve rollback/error handling rather than deleting a map-named module wholesale.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 7. Run retained spawn-based map events on native maps

**Parent:** #942

**What to build:** Existing Wanderer/Mendeln/Treasure Fallen/Gheed scripts and shared event selection run in generated rooms.

**Assignment:** C — map events

**Blocked by:** Draft 23

**Proposed status:** ready-for-agent

- [ ] Use native area identity/progress and item quality with existing 11/17/34 percent odds; read isolated forcing metadata and preserve the Altar exclusion.
- [ ] Keep existing fight/AI/progress/cleanup, prune only removed loot branches and retain useful generic/event/uber ingredients.
- [ ] Force each already implemented event through private checks and record unfinished baseline deviations instead of claiming new parity.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 8. Preserve uber access, key acquisition, scaling and re-entry

**Parent:** #942

**What to build:** Shared uber access, acquisition, scaling and re-entry support the approved vanilla/reused/new arena bindings; fight slices prove each finished destination.

**Assignment:** D — ubers

**Blocked by:** Draft 23

**Proposed status:** ready-for-agent

- [ ] Preserve key recipes/uber tiers, fountain/strip/pet rules, shared opening gate, scaling and tier-0 re-entry, distinguishing stock mini-ubers/D2R Tristram from newly authored encounter areas.
- [ ] Demonstrate one completed native static entry path and safe refusal for unfinished/unavailable destinations; per-fight work proves each final key route on its new levels.
- [ ] Prove normal key/ingredient sources independently of console setup; retain stage counters and drop helpers without editing other lanes or shared placement/skill dispatch.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 9. Verify native geometry, presentation and load

**Parent:** #942

**What to build:** The small native catalogue is readable, reachable and stable across supported layouts.

**Assignment:** A — native maps

**Blocked by:** Draft 5

**Proposed status:** ready-for-agent

- [ ] Verify entrance-to-boss and return paths, collision, maximum size and deterministic rebuild for enabled tileset/shape/size families.
- [ ] Check HD/legacy, automap/names/property text/panel, lighting/Terror Zone restoration and ordinary-area regressions.
- [ ] Record matched frame-time/memory/load/teardown evidence and investigate repeatable frame-time regressions above 10 percent.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 10. Adapt the Catalyst recipe to native map items

**Parent:** #942

**What to build:** A Catalyst Shard cubes with an eligible native map to guarantee a retained PD2 event.

**Assignment:** B — general rules

**Blocked by:** Draft 6, Draft 7

**Proposed status:** ready-for-agent

- [ ] Keep the existing recipe's ingredients/eligibility and isolated force-event metadata while adapting its map input to native items.
- [ ] Verify valid/invalid cube attempts, exact consumption, ordinary save/reload of the result, guaranteed event and Altar exclusion.
- [ ] Do not add a direct-use shard or restore the PD2 map-affix/recipe system; native property application must not treat metadata as a buff.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 11. Preserve the Altar and Guardian fight/reward

**Parent:** #942

**What to build:** The Altar retains its Guardian/clone encounter and 2–3 Catalyst Shard reward.

**Assignment:** C — map events

**Blocked by:** Draft 7

**Proposed status:** ready-for-agent

- [ ] Reuse existing activation, AI, progress and cleanup without redesigning the fight.
- [ ] Verify first use, Guardian and clone cleanup, successful completion and shard reward.
- [ ] Use the frozen metadata/selection contract; recipe implementation stays in the general-rule lane.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 25. Preserve the mini-uber key chain in vanilla arenas

**Parent:** #942

**What to build:** Lilith, Uber Izual and Uber Duriel keep their vanilla arenas, key-chain access and organ rewards.

**Assignment:** D — ubers

**Blocked by:** Draft 8

**Proposed status:** ready-for-agent

- [ ] Retain vanilla mini-uber areas and native assets; do not allocate replacement mini-uber arenas or author new geometry for them.
- [ ] Preserve existing key/cube destination selection, no-repeat behavior, native boss spawning and organ rewards; keep the chain separate from Tristram's D2R-arena/PD2-boss integration.
- [ ] Verify all three mini-uber access/fight/reward/return paths in the supported candidate without the retired converter and without new tier mechanics.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 13. Preserve Spire of Darkness

**Parent:** #942

**What to build:** Spire darkness, waves and completion cleanup work on native maps.

**Assignment:** C — map events

**Blocked by:** Draft 11

**Proposed status:** ready-for-agent

- [ ] Reuse existing activation, sound/darkness, shadow waves and progress scripts.
- [ ] Verify completion, leaving/re-entering and new-game reset without leaked darkness or duplicate waves.
- [ ] Use frozen shared level-effects helpers and private event tests; make no upstream/shared-header changes during a wave.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 12. Use D2R's Uber Tristram arena with PD2 bosses

**Parent:** #942

**What to build:** D2R's existing Uber Tristram arena hosts PD2's Tristram bosses, behavior and rewards.

**Assignment:** D — ubers

**Blocked by:** Draft 25

**Proposed status:** ready-for-agent

- [ ] Reuse D2R's Uber Tristram arena; do not author a replacement finale or use the old converted PD2 geometry.
- [ ] Bind existing PD2 boss definitions/AI, entry/tier/re-entry rules and rewards to that arena. Use one intended spawn path and preserve once-per-prime behavior; no duplicate stock/PD2 bosses, Torch rewards or other stock reward bypass.
- [ ] Open and win at uber tiers 0–2; verify access, reachability/placements, cleanup and guaranteed/controlled rare rewards through the existing single-player seam.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 14. Build the new static Diablo Clone arena and preserve its fight

**Parent:** #942

**What to build:** Diablo Clone's scripted fight and rewards run in a newly built native static arena.

**Assignment:** D — ubers

**Blocked by:** Draft 12

**Proposed status:** ready-for-agent

- [ ] Complete the new native static arena with boss/soul/add placements, matching HD/legacy collision, usable phase space and arrival/exit points.
- [ ] Make AI and summon skills use the frozen new placement contract rather than duplicated old centres; keep extracted event AI intact.
- [ ] Open and win at uber tiers 0–2 on the new arena; verify phases, immunity, waves/souls, re-entry, cleanup and rewards without old converted content.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 15. Integrate Horazon's native destination and six-map reward

**Parent:** #942

**What to build:** Horazon keeps his fight, opens another eligible native map at the source monster level and drops six native map items.

**Assignment:** Shared checkpoint

**Blocked by:** Draft 9, Draft 10, Draft 13, Draft 14

**Proposed status:** ready-for-agent

- [ ] Pause lane edits and let one shared owner land only the needed generic portal/state extension and event adapter.
- [ ] Verify source/destination independence and same-tileset behavior or build a new D2R-native fixed bridge that still meets the approved destination outcome; prove source-level travel, six native items and return. No old converted fallback.
- [ ] Remove old tier pools/map-stat propagation/unique-map rolls; no event reroll/reset, second player-used map or change to the fight. Publish the next reviewed base.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 16. Build native static Bitter Depths and preserve Void/Tomb travel

**Parent:** #942

**What to build:** Void/Tomb reaches a newly built converter-independent Bitter Depths with its retained encounter and return path.

**Assignment:** C — map events

**Blocked by:** Draft 15

**Proposed status:** ready-for-agent

- [ ] Author a new native static side area with matching HD/legacy collision, arrival/return and explicit retained boss/encounter placements.
- [ ] Use the approved portal contract and verify progression, travel/return and cleanup; extract the retained boss behavior from ordinary map-boss rewrites before deleting them.
- [ ] Build and play it without old converted geometry or tools; do not confuse it with the removed Outer Void unique map.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 17. Build new static Rathma stages and preserve Rathma/Mendeln

**Parent:** #942

**What to build:** Rathma/Mendeln's staged fight, shared life and rewards work across newly built native static stage areas.

**Assignment:** D — ubers

**Blocked by:** Draft 15

**Proposed status:** ready-for-agent

- [ ] Author the new static stage set with matching native HD/legacy collision, stage arrivals/exits, boss/clone positions and arena/summon regions.
- [ ] Use one frozen placement contract across AI, travel and skill summons; remove old hard-coded coordinate/level assumptions without changing phase/shared-life behavior.
- [ ] Open and win at uber tiers 0–2 across all new stages; verify transitions, re-entry, rooms, cleanup and rewards without the converter.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 18. Build new static Lucion arena and Temple and preserve the fight

**Parent:** #942

**What to build:** Lucion's phases, rewards and Temple/chests work in a new native static arena and Temple.

**Assignment:** D — ubers

**Blocked by:** Draft 17

**Proposed status:** ready-for-agent

- [ ] Build new arena/Temple content with matching collision and HD/legacy scenes, phase centre/corners, Royal Fire region, entry/Temple/return points and chest/altar placements.
- [ ] Use the shared placement contract in AI and server/client skill handlers; preserve phases, immunity, counters, object behavior and retained helper dependencies.
- [ ] Open and win at uber tiers 0–2 and verify the Temple/chests, cleanup, rewards and Desecration permission on the new content without the converter.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 19. Converge Desecration and retained generic rules

**Parent:** #942

**What to build:** A Lucion kill still enables the one-attempt amulet Desecration action through the coordinated cube path.

**Assignment:** Shared convergence

**Blocked by:** Draft 10, Draft 16, Draft 18

**Proposed status:** ready-for-agent

- [ ] With lane work converged, connect encounter permission and retained search/roll helpers under one owner.
- [ ] Force success and bricked results; verify tier odds, location/item eligibility, one-attempt state and ingredient/refusal safety.
- [ ] Run combined Catalyst, corruption/Demonic Cube, Almanac, retained events and uber regressions without resurrecting PD2 map systems.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 20. Remove obsolete PD2 map systems and references

**Parent:** #942

**What to build:** PD2 map items, affixes/stats, tiers and map recipes are cut, with only the approved native Catalyst adaptation.

**Assignment:** Shared closeout

**Blocked by:** Draft 19

**Proposed status:** ready-for-agent

- [ ] Remove obsolete PD2 map acquisition, recursive loot branches, modifiers/consumables/recipes, unique/dungeon behavior and old level references; keep reserved IDs and every retained native encounter dependency.
- [ ] Keep retained fights, Bitter Depths/uber arenas, generic rules, key/ingredient acquisition and isolated Catalyst metadata.
- [ ] Verify no obsolete drop/roll/craft/apply or dangling retained reference. Perform serial closeout on an integration branch if intermediate default builds cannot remain releasable; merge only after final acceptance.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 24. Retire the PD2 DT1/painter/generator-converter and replace its QA

**Parent:** #942

**What to build:** Clean builds and releases use native map/static content without invoking or depending on the retired conversion pipeline.

**Assignment:** Shared closeout

**Blocked by:** Draft 20

**Proposed status:** ready-for-agent

- [ ] Audit every surviving area/content consumer; remove the old PD2 level-DT1/DS1 translation, painter and preset-converter path from active extraction/merge/build/release workflows only after native replacements are verified.
- [ ] Remove obsolete converter-only tools/tests/coverage/fidelity/baseline artifacts and replace required QA with native references/placements/collision plus installed-game checks; keep the table-diff gate.
- [ ] Preserve general table/ID machinery and unrelated monster/object/animation/missile/item assets; native D2R DS1/DT1 use is allowed.
- [ ] Build and stage cleanly with legacy converted outputs unavailable; verify all required native levels/assets resolve and no old geometry slips into the package.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 21. Stage the pinned release and rehearse upgrade/rollback

**Parent:** #942

**What to build:** The matched candidate installs/upgrades to one map backend and can be rolled back completely.

**Assignment:** Shared closeout

**Blocked by:** Draft 24

**Proposed status:** ready-for-agent

- [ ] Pin Cartographer and content hashes; stage core-events/core-rules/core-ui plus native maps and every new static encounter/side-area asset, with one map backend and no legacy converted-level dependency.
- [ ] Rehearse fresh install, upgrade and stale-owned-DLL cleanup in both scopes while retaining unrelated user files/config.
- [ ] Rehearse full release/config/save-snapshot rollback with game closed, preserve newer saves and keep the player savepath unchanged.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

### 22. Cut over the default backend with final single-player evidence

**Parent:** #942

**What to build:** The default release uses native Cartographer maps with preserved PD2 events, ubers and generic rules.

**Assignment:** Shared closeout

**Blocked by:** Draft 21

**Proposed status:** ready-for-agent

- [ ] Retire core-maps source/build only after retained behavior has verified owners; complete any integration-branch merge here.
- [ ] Run final-head installed-mod acceptance on native maps and every new static encounter/side area; prove a clean no-converter build, distinguish skipped/known deviations and retain the final-commit table-diff report.
- [ ] Update architecture/user/deviation docs and changelog fragments for removed systems, Catalyst exception, alpha saves and unverified multiplayer; retain rollback material.

- [ ] Follow the latest arena clarification: vanilla mini-ubers; D2R Tristram arena with PD2 bosses; new static content only for the remaining required areas.
- [ ] Stay inside the assigned allowlist/reviewed base, use the existing single-player/public-contract/tool-fixture seams, and record pass/fail/skipped and existing deviations separately. Shared changes require a checkpoint.

## Publication structure and review

After approval, publish one ready-for-agent issue per work item in dependency order: 1 → 2 → 3 → 4 → 23 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 25 → 13 → 12 → 14 → 15 → 16 → 17 → 18 → 19 → 20 → 24 → 21 → 22. Use Parent / What to build / Acceptance criteria plus ownership restrictions and the owner clarification. Use native GitHub sub-issue/blocked-by links with database IDs, not only textual references. Omit the body Blocked by section once native edges are set. The sole initial frontier is draft 1.

Verify the repo before creation and use body files. Each implementation PR opens with Closes for its work item or Part of when partial; the child links #942. Use changelog fragments, final-head table-diff evidence, owner-controlled parity-drop rules and toolkit worktree/junction conventions. A cross-repository upstream change needs its linked upstream issue before that PR.

Review requested: does the granularity fit a fresh agent context, are gameplay/ownership blockers correct, and should any work items be merged or split? to-tickets requires approval before publication. No runtime code, assets, branches or child issues are created by this draft.
