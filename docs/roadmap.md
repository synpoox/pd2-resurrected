# Roadmap

Milestones and the work items in each. Decided by "Order the alpha roadmap and slice it into work items" ([#33](https://github.com/synpoox/pd2-resurrected/issues/33)). A work item
is about one agent session. `P` items are listed here; `W` items are engine rules, listed with their rows in
[core-rules.md](core-rules.md#work-items) and only placed here. **Issue** links each item's ticket
(sub-issues of the spec [#41](https://github.com/synpoox/pd2-resurrected/issues/41) through M2, and of [#85](https://github.com/synpoox/pd2-resurrected/issues/85) for the alpha), where its status lives; built items link their merged PR.

Target: D2R 3.3 build 93847 + D2RLoader 1.3.1 on a frozen install copy, testable M2 within 1-2 weeks of 2026-11-03, 5
build sessions in parallel; the alpha has no fixed date. Build PRs merge on the game check, the table-diff gate and a code-review agent; the owner
reviews P1, W1, W2, the first PR in each class lane, and the M2 and alpha releases.

## M1: a new character in Act 1

PD2's tables, strings and stand-ins merged, playable in Act 1, with the table-diff gate and the unported list in place.

| # | Work item | Blocked by | Lane | Issue |
|---|---|---|---|---|
| P1 | Baseline and layer: extract PD2 into the hash-pinned baseline, add the `merge/` -> `generated/` modkit layer (ADR 0002), switch on extended saves (ADR 0003) | none | A | [#35](https://github.com/synpoox/pd2-resurrected/issues/35) (PR) |
| P2 | Remapped ids: skill, stat, state and missile ids remapped through append-only id lists in `merge/` | P1 | A | [#40](https://github.com/synpoox/pd2-resurrected/issues/40) (PR) |
| P3 | Item tables: items, affixes, runewords, cube, treasure classes; map items blocked from dropping | P2 | A | [#62](https://github.com/synpoox/pd2-resurrected/issues/62) (PR) |
| P4 | Monster and area tables: monsters, levels and the remaining tables | P2 | D | [#64](https://github.com/synpoox/pd2-resurrected/issues/64) (PR) |
| P5 | Table-diff gate (`pd2_diff`) and `docs/unported.md`, seeded with the gaps found so far | P1 | D | [#59](https://github.com/synpoox/pd2-resurrected/issues/59) (PR) |
| P6 | Strings: the strings build ("Decide how the mod ships PD2's strings", [#29](https://github.com/synpoox/pd2-resurrected/issues/29)) | P1 | D | [#38](https://github.com/synpoox/pd2-resurrected/issues/38) (PR) |
| W3 | Stand-ins and data rows (core-rules) | P1 | C | [#46](https://github.com/synpoox/pd2-resurrected/issues/46), [#60](https://github.com/synpoox/pd2-resurrected/issues/60) (PR) |
| P10 | HD missiles: every missile PD2 adds gets a `missiles.json` model (stand-in art), so none is invisible in HD | P2 | A | [#74](https://github.com/synpoox/pd2-resurrected/issues/74) (PR) |
| P11 | Skill icons: every class's icon sheet rebuilt from PD2's art, so PD2's new skills show their icons | P1 | D | [#83](https://github.com/synpoox/pd2-resurrected/issues/83) (PR) |
| P14 | Skill tree backgrounds: each class's tree boxes and arrows redrawn from the merged tables, so they follow PD2's skill positions | P1 | D | [#84](https://github.com/synpoox/pd2-resurrected/issues/84), [#110](https://github.com/synpoox/pd2-resurrected/issues/110) (PR) |

## M2: Amazon and Barbarian complete

M1 plus the companions, corruption and the Amazon and Barbarian engine rules; the other classes stay on stand-ins.
Was the alpha until [#85](https://github.com/synpoox/pd2-resurrected/issues/85) widened it.

| # | Work item | Blocked by | Lane | Issue |
|---|---|---|---|---|
| W1 | Plumbing and Split Throw server half (core-rules) | none | B | [#37](https://github.com/synpoox/pd2-resurrected/issues/37) (PR) |
| W2 | Client-half example (core-rules) | W1 | B | [#45](https://github.com/synpoox/pd2-resurrected/issues/45), [#61](https://github.com/synpoox/pd2-resurrected/issues/61) (PR), [#69](https://github.com/synpoox/pd2-resurrected/issues/69) (PR) |
| W4 | Investigation: PlrModes (core-rules) | none | C | [#36](https://github.com/synpoox/pd2-resurrected/issues/36) (PR) |
| W5, W6 | Amazon (core-rules) | W2, P2 | B | [#52](https://github.com/synpoox/pd2-resurrected/issues/52), [#80](https://github.com/synpoox/pd2-resurrected/issues/80) (PR), [#111](https://github.com/synpoox/pd2-resurrected/issues/111) (PR) |
| W7 | Nova and orb hits (core-rules) | W1, P2 | C | [#50](https://github.com/synpoox/pd2-resurrected/issues/50), [#66](https://github.com/synpoox/pd2-resurrected/issues/66) (PR); game check waits on the owner's run |
| W8, W9 | Barbarian (core-rules) | W2, P2 | B | [#82](https://github.com/synpoox/pd2-resurrected/issues/82) (PR), [#81](https://github.com/synpoox/pd2-resurrected/issues/81) (PR) |
| W10 | Barbarian group (core-rules) | W1, P2 | C | [#71](https://github.com/synpoox/pd2-resurrected/issues/71) (PR) |
| P7 | Companions: hash-checked manifest, release-script download, hand-written `filter.json`, Pyre's 16 `Filter*` sound rows and flacs merged into our data | none | C | [#39](https://github.com/synpoox/pd2-resurrected/issues/39) (PR) |
| P9 | Corruption: starts with the outcome check (core-rules Q10), includes the cube op 16/18 check for corruption recipes | P3 | A | [#56](https://github.com/synpoox/pd2-resurrected/issues/56), [#68](https://github.com/synpoox/pd2-resurrected/issues/68) (PR) |
| P8 | M2 release: local 7z and release notes | every item above | A | [#57](https://github.com/synpoox/pd2-resurrected/issues/57) |

**Lanes** for four sessions (M2): A (data) P1 -> P2 -> P3 -> P9 -> P8; B (engine) W1 -> W2 -> class items; C W4 and P7 first,
then W3, W7, W10; D P4, P5, P6 once P1 is in. **Critical path:** P1 -> P2 -> P3 -> P9 -> P8.

**M2 exit bar:** a fresh character of any class levels through Acts 1-5 Normal on the merged data; W1-W10 and
corruption game checks pass; the table-diff gate is green for the alpha scope; every companion arms on Slot4; the owner
tests the local 7z alone.

## Alpha: every PD2 class complete

M2 plus the Necromancer, Paladin, Druid, Sorceress and Assassin as complete classes (spec [#85](https://github.com/synpoox/pd2-resurrected/issues/85)). Class-themed item rules
are "not yet" rows on the unported list; the Warlock ships as D2R has it.

| # | Work item | Blocked by | Lane | Issue |
|---|---|---|---|---|
| W11-W14 | Necromancer (core-rules); W14's get-hit window split off to W34 | none | B | [#86](https://github.com/synpoox/pd2-resurrected/issues/86), [#123](https://github.com/synpoox/pd2-resurrected/issues/123) (PR), [#114](https://github.com/synpoox/pd2-resurrected/issues/114) (PR), [#115](https://github.com/synpoox/pd2-resurrected/issues/115) (PR), [#121](https://github.com/synpoox/pd2-resurrected/issues/121) (PR) |
| W15-W20 | Paladin (core-rules) | none | C | [#117](https://github.com/synpoox/pd2-resurrected/issues/117) (PR), [#129](https://github.com/synpoox/pd2-resurrected/issues/129) (PR), [#135](https://github.com/synpoox/pd2-resurrected/issues/135) (PR), [#93](https://github.com/synpoox/pd2-resurrected/issues/93), [#94](https://github.com/synpoox/pd2-resurrected/issues/94), [#95](https://github.com/synpoox/pd2-resurrected/issues/95) |
| W21-W24 | Druid (core-rules) | none | D | [#127](https://github.com/synpoox/pd2-resurrected/issues/127) (PR), [#136](https://github.com/synpoox/pd2-resurrected/issues/136) (PR), [#139](https://github.com/synpoox/pd2-resurrected/issues/139) (PR), [#99](https://github.com/synpoox/pd2-resurrected/issues/99) |
| W25 | Investigation: Assassin finisher hook points (core-rules) | none | E | [#124](https://github.com/synpoox/pd2-resurrected/issues/124) (PR) |
| W26-W28 | Sorceress (core-rules) | W21 (W27 only) | E | [#128](https://github.com/synpoox/pd2-resurrected/issues/128) (PR), [#140](https://github.com/synpoox/pd2-resurrected/issues/140) (PR), [#103](https://github.com/synpoox/pd2-resurrected/issues/103) |
| W29-W32 | Assassin (core-rules) | W25 (W29), W29 (W30), W21 (W32) | first free class lane | [#130](https://github.com/synpoox/pd2-resurrected/issues/130) (PR), [#105](https://github.com/synpoox/pd2-resurrected/issues/105), [#106](https://github.com/synpoox/pd2-resurrected/issues/106), [#107](https://github.com/synpoox/pd2-resurrected/issues/107) |
| W33 | **Rush:** melee splash (srvdo 163), which item_splashonhit, Frenzy, Tiger Strike and Revive's event cast (core-rules) | none | A, as a rush job | [#126](https://github.com/synpoox/pd2-resurrected/issues/126) (PR) |
| W34 | Get-hit/block end window and cast/sequence delay, every class (core-rules), split off W14 | none | first free class lane | [#120](https://github.com/synpoox/pd2-resurrected/issues/120) |
| P12 | "Not yet" rows for class item rules (Ormus' Robes and the like) on the unported list | none | A | [#108](https://github.com/synpoox/pd2-resurrected/issues/108) |
| P13 | Alpha release: local 7z and release notes | P8 and every item above | A | [#109](https://github.com/synpoox/pd2-resurrected/issues/109) |

**Lanes** for five sessions: A P9 -> P8, then P12 and rush jobs, then P13; B Necromancer; C Paladin; D Druid; E W25, then
Sorceress. The Assassin build goes to the first class lane that frees up. A rule shared by two classes belongs to the
earliest class in the order Necromancer, Paladin, Druid, Sorceress, Assassin; the later class checks it. The owner reviews
the first PR in each class lane.

**Engine changes for classes:** before porting a class rule, check what it changes in play. PD2 class engine code that
changes only input or the skill bar, not what a skill does, comes from a third-party plugin instead: auras on the left
button (cltst 54, cltdo 103) from the [Left Click Auras](https://github.com/CelestialRayOne/d2rloader_plugins/tree/main/plugins/left-click-auras)
companion, and Force Move. The merge keeps D2R's columns for those rows, and the unported list names the plugin. Only
rules that change a skill's effect, damage, missiles or timing get core-rules code.

**Alpha exit bar:** every PD2 class is a complete class: each of its rows has a passing game check or an approximated row
on the unported list; a fresh character of every class levels through Acts 1-5 Normal on the merged data; the table-diff
gate is green for the alpha scope; every companion arms on Slot4; the owner tests the local 7z alone.

**Left out of the alpha** (unported list, "not yet"): map items and the map system, new PD2-only areas, the Warlock
redesign, class-themed item rules, monster-only engine rules, everything in the core-rules order after the cut.

## After the alpha

No milestones yet; items are ordered by rows unlocked, as in [core-rules.md](core-rules.md#after-the-cut-by-rows-unlocked-per-session).
