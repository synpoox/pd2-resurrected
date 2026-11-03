# Roadmap

Milestones and the work items in each. Decided by "Order the alpha roadmap and slice it into work items" (#33). A work item
is about one agent session. `P` items are listed here; `W` items are engine rules, listed with their rows in
[core-rules.md](core-rules.md#work-items) and only placed here.

Target: D2R 3.3 build 93847 + D2RLoader 1.3.1 on a frozen install copy, testable alpha within 1-2 weeks of 2026-11-03, 4+
build sessions in parallel. Build PRs merge on the game check, the table-diff gate and a code-review agent; the owner
reviews P1, W1, W2 and the alpha release.

## M1: a new character in Act 1

PD2's tables, strings and stand-ins merged, playable in Act 1, with the table-diff gate and the unported list in place.

| # | Work item | Blocked by | Lane |
|---|---|---|---|
| P1 | Baseline and layer: extract PD2 into the hash-pinned baseline, add the `merge/` -> `generated/` modkit layer (ADR 0002), switch on extended saves (ADR 0003) | none | A |
| P2 | Remapped ids: skill, stat, state and missile ids remapped through append-only id lists in `merge/` | P1 | A |
| P3 | Item tables: items, affixes, runewords, cube, treasure classes; map items blocked from dropping | P2 | A |
| P4 | Monster and area tables: monsters, levels and the remaining tables | P2 | D |
| P5 | Table-diff gate (`pd2_diff`) and `docs/unported.md`, seeded with the gaps found so far | P1 | D |
| P6 | Strings: the strings build ("Decide how the mod ships PD2's strings", #29) | P1 | D |
| W3 | Stand-ins and data rows (core-rules) | P1 | C |

## Alpha

M1 plus the companions, corruption and the Amazon and Barbarian engine rules; the other classes stay on stand-ins.

| # | Work item | Blocked by | Lane |
|---|---|---|---|
| W1 | Plumbing and Split Throw server half (core-rules) | none | B |
| W2 | Client-half example (core-rules) | W1 | B |
| W4 | Investigation: PlrModes (core-rules) | none | C |
| W5, W6 | Amazon (core-rules) | W2, P2 | B |
| W7 | Nova and orb hits (core-rules) | W1, P2 | C |
| W8, W9 | Barbarian (core-rules) | W2, P2 | B |
| W10 | Barbarian group (core-rules) | W1, P2 | C |
| P7 | Companions: hash-checked manifest, release-script download, hand-written `filter.json`, Pyre's 16 `Filter*` sound rows and flacs merged into our data | none | C |
| P9 | Corruption: starts with the outcome check (core-rules Q10), includes the cube op 16/18 check for corruption recipes | P3 | A |
| P8 | Alpha release: local 7z and release notes | every item above | A |

**Lanes** for four sessions: A (data) P1 -> P2 -> P3 -> P9 -> P8; B (engine) W1 -> W2 -> class items; C W4 and P7 first,
then W3, W7, W10; D P4, P5, P6 once P1 is in. **Critical path:** P1 -> P2 -> P3 -> P9 -> P8.

**Alpha exit bar:** a fresh character of any class levels through Acts 1-5 Normal on the merged data; W1-W10 and
corruption game checks pass; the table-diff gate is green for the alpha scope; every companion arms on Slot4; the owner
tests the local 7z alone.

**Left out of the alpha** (unported list, "not yet"): map items and the map system, new PD2-only areas, the Warlock
redesign (the alpha ships D2R's Warlock as it is), the other classes' engine rules, everything in the core-rules order
after the cut.

## After the alpha

No milestones yet; items are ordered by rows unlocked, as in [core-rules.md](core-rules.md#after-the-cut-by-rows-unlocked-per-session).
