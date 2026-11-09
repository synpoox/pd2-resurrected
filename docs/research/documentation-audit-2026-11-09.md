# PD2 Resurrected documentation audit

Audited 2026-11-09 against main 326b039323a34d8fe4b21a61d27ef857fa1b3195. Read top-level documentation, agent guides, ADRs and relevant research; scanned all Markdown documentation for pending/completion claims and local links. Cross-referenced 729 GitHub issue/PR records and relevant implementation code. No repository or tracker changes made. This is a documentation audit, not a new game validation pass.

## Confirmed findings

### 1. High: clean-build fetch instructions omit a required input

README.md:91-95 fetches tiles, HD presets and model_lod_desc.bin but omits data/global/animdata.d2. The current animation merge raises AnimDataError if that file is absent (tools/pd2_animdata.py:82). The release workflow already fetches it (.github/workflows/release.yml:35), as does the gate workflow. A pre-populated cache hides the documentation bug.

Correction: add the animdata prefix to the README command; keep model_lod_desc.bin for coverage. Verify the documented build on an empty cache rather than assuming a developer's cache is representative.

### 2. High: entire completed uber suite remains listed as unbuilt

The feature deviation at docs/deviations.md:62 lists opening, tiers, area rules, phase scripts, drops and Desecration as “not yet.” UB2-UB7 (#227-232) are all closed completed. Current core-maps includes the Lucion, Rathma, shared-life and Dclone implementations and their installation paths; mod.json and README describe alpha 0.2.0 with maps and uber bosses.

Correction: remove the blanket not-built claim. Keep only specific known deviations or unresolved bugs; do not turn issue completion into a claim of perfect parity or a fresh game-test result.

### 3. High: Terror Zone drop implementation remains “not yet”

docs/deviations.md:63 assigns Talisman/Worldstone Shard Terror Zone drops to #348, which is closed completed. core-maps' key-drop rules explicitly include lucc/TerrorZone and wss/TerrorZone, with the latter using the Corrupted Zone treasure-class odds.

Correction: retain the deliberate approximation (Terror Zones replace corrupted zones) but describe the drop rule as implemented, with #348 as evidence.

### 4. Medium: Cast Triggers docs omit the established failure

docs/core-rules.md:177-178 and docs/deviations.md:165 describe a companion that loads but has not yet been seen proccing, awaiting owner confirmation. #385's existing game-check comment establishes a more specific failure: native measurement/call-site witnesses reject installation and the plugin remains inert. docs/item-rules.md:37 also says P12's listener is removed “once it passes,” although core-rules.md already records its removal.

Correction: document the actual known incompatibility, link #385, distinguish loaded DLL from active hooks, and state that P12 was already removed. Do not imply successful proc support simply because IR2 bundling is completed.

### 5. Medium: map-system umbrella understates implemented scope

docs/deviations.md:59 describes only the early map/event state and says the rest is designed. MS4 (#219), MS5 (#220), MS8 (#222) and MS9 (#223) are completed, and map-event and event-portal code is present. docs/deviations.md:169 also lumps implemented map events with unfinished invader work under “D2R's own behaviour.”

Correction: split completed area stats, recipes and event behavior from remaining readers and invaders. Keep #224/#225 open scope; #527/#533 still track unfinished unique-map/dungeon readers. Do not delete the entire umbrella merely because some milestones closed.

### 6. Medium: quiver-tier gap has no appropriate open work item

docs/deviations.md:57 points normcode/ubercode/ultracode support at UR6 #204. #204 is closed completed for quiver automagic and explicitly excludes the remaining tier-column deviation. tools/pd2_ids.py adds misc auto prefix, not those three columns.

Correction: preserve the genuine gap but give it its own open work item, or make and document an explicit decision to cut it. Do not reopen or misrepresent completed automagic work as unfinished.

### 7. Medium: status sections contain contradictory rows

Actual row statuses in docs/deviations.md:

| Section heading | Rows present |
|---|---|
| Features / Not yet (20) | 18 not yet, 2 approximated |
| Features / Approximated (62) | 60 approximated, 1 not yet, 1 cut |
| Engine / Not yet (16) | 13 not yet, 3 approximated |

The numerical headings count rows, but the category headings misclassify their content. Examples include cube art under Not yet; “Unique map and dungeon readers left out” under Approximated despite status not yet; several engine approximations under Not yet.

Correction: regroup by the actual status, recalculate headings and ensure gate-pattern coverage still matches the intended active deviations. Do not change data-diff exemptions solely to tidy prose.

### 8. Medium: roadmap still waits for the completed W7 game check

docs/roadmap.md:63 says the nova/orb-hit game check waits on the owner's run. #50 is closed completed. The nova-orb research records a manual Slot6 run observing the expected 64 Thunder Storm client missiles.

Correction: replace the outstanding wait with a link to the recorded result, retaining exactly what was tested rather than expanding it to every related skill.

### 9. Low: post-alpha catch-all still includes completed work

docs/deviations.md:69 groups monster/boss kits with outstanding server rules. W36 #244 and UB2-UB7 are completed; W37 #245 and W38 #246 remain open.

Correction: narrow the remaining-work description to the genuinely open rules, retaining any specific residual deviations. The umbrella currently makes finished monster/boss work look wholly unbuilt.

## Reconciliation needed before changing claims

- Game-slot ownership: docs/agents/game-slots.md:9 and AGENTS.md restrict PD2 testing to Slot4-8, while newer core-rules test records cite Slot11. This is not enough to infer new ownership. Check the actual testing configuration and slot registry, then update the guide if ownership expanded. Historical test evidence should not be rewritten to a different slot.
- Force Move: docs/deviations.md:162 and core-rules.md still describe a future third-party implementation, while #689 documents an available Move Only skill. The current companion manifest contains no obvious Move Only entry. Determine whether that skill is loader-provided, separately installed or bundled before claiming completion.
- Stack splitting and quantity UI: #316/#359 are completed but core-rules retains specific untested cases. Closed issues alone do not prove every listed edge case was checked. Review their game-check comments/PR evidence before removing remaining-test notes.
- The map-stat HUD entry remains not yet. There is no basis in this audit to mark it complete merely because map stat application exists.

## Material that should remain

- #214/#215 (HD kits), #224/#225 (invaders), #245/#246 (server rules), #206 (audio), #208 (potion counter) and Warlock balance #234-236 remain open. Their unfinished scope is not stale merely because alpha 0.2.0 shipped.
- The single zhar sound test is consistent with merge/sounds.tsv; the open audio conversion work should not be declared finished.
- The README alpha 0.2.0 status matches mod.json version 0.2.0-alpha and the dated changelog section.
- ADRs and dated research describe decisions/evidence at a point in time. Preserve them as history; add current-status pointers where old results could be mistaken for today's behavior.
- The roadmap explicitly says issue status is authoritative. Its historical work-item tables are not automatically stale because they include completed items. The obsolete execution lanes/date estimates could be labelled historical rather than deleted.

## Link check and limits

No missing relative file targets were found in README.md, AGENTS.md or docs/**/*.md. This was a file-existence check, not validation of Markdown anchors or every external/research-branch URL. No new live game tests were performed. The audit compared tracked source rather than generated PD2 tables, which are intentionally not committed.

## Suggested cleanup order

1. Correct the fresh-build command and Cast Triggers failure description.
2. Reconcile completed uber/key/map work in the active deviation list; retain specific residual gaps.
3. Move misclassified rows and recalculate category counts; verify the table-diff gate after any gate-pattern edits.
4. Replace the W7 wait, narrow completed-work catch-alls, and provide an appropriate owner/work item for quiver tiers.
5. Confirm slot ownership and Move Only provenance; update only after evidence resolves them.
6. Add a lightweight documentation check for status-heading consistency and missing local files. Use closed linked issues as review candidates, not automatic instructions to delete a deviation.
