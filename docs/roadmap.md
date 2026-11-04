# Roadmap

Milestones and the work items in each. Decided by "Order the alpha roadmap and slice it into work items" ([#33](https://github.com/synpoox/pd2-resurrected/issues/33)). A work item
is about one agent session. `P` items are listed here; `W` items are engine rules, listed with their rows in
[core-rules.md](core-rules.md#work-items) and only placed here. **Issue** links each item's ticket
(sub-issues of the spec [#41](https://github.com/synpoox/pd2-resurrected/issues/41) through M2, of [#85](https://github.com/synpoox/pd2-resurrected/issues/85) for the pre-alpha, and of the map [#143](https://github.com/synpoox/pd2-resurrected/issues/143) for the alpha), where its status lives, with the PR that closed it. P1, P2, P10 and W1 had no issue, so they link their PR.

**Open items:** [M1 and M2](https://github.com/synpoox/pd2-resurrected/issues?q=is%3Aopen+parent-issue%3Asynpoox%2Fpd2-resurrected%2341), [pre-alpha](https://github.com/synpoox/pd2-resurrected/issues?q=is%3Aopen+parent-issue%3Asynpoox%2Fpd2-resurrected%2385), [alpha](https://github.com/synpoox/pd2-resurrected/issues?q=is%3Aopen+parent-issue%3Asynpoox%2Fpd2-resurrected%23143).

Target: D2R 3.3 build 93847 + D2RLoader 1.3.1 on a frozen install copy, testable M2 within 1-2 weeks of 2026-11-03, 5
build sessions in parallel; the pre-alpha and the alpha have no fixed date. Build PRs merge on the game check, the table-diff gate and a code-review agent; the owner
reviews P1, W1, W2, the first PR in each class lane, and the M2 and pre-alpha releases.

**Names used in the roadmap and the design docs.** Each prefix is numbered on its own; the doc named holds its list.

| Prefix | Meaning | Listed in |
|---|---|---|
| M1, M2 | Milestones before the pre-alpha (then the pre-alpha, alpha, beta and launch, by name) | this file |
| P | Port item: data, pipeline, art or release work, not an engine rule | this file |
| LP | Level pipeline for PD2-only areas ([#151](https://github.com/synpoox/pd2-resurrected/issues/151), [ADR 0005](adr/0005-hd-presets-generated-from-pd2-ds1.md)) | this file |
| W | Engine-rule work item (core-rules plugin) | [core-rules.md](core-rules.md#work-items) |
| IR | Item rules | [item-rules.md](item-rules.md) |
| MS | Map system | [map-system.md](map-system.md) |
| UB | Uber bosses | [uber-bosses.md](uber-bosses.md) |
| WL | Warlock redesign | [warlock.md](warlock.md) |
| UR | Remaining unported-list rows | [unported-rows.md](unported-rows.md) |
| Q | An open research question in core-rules, not a work item | [core-rules.md](core-rules.md) |

## M1: a new character in Act 1

PD2's tables, strings and stand-ins merged, playable in Act 1, with the table-diff gate and the unported list in place.

| # | Work item | Blocked by | Lane | Issue |
|---|---|---|---|---|
| P1 | Baseline and layer: extract PD2 into the hash-pinned baseline, add the `merge/` -> `generated/` modkit layer (ADR 0002), switch on extended saves (ADR 0003) | none | A | [#35](https://github.com/synpoox/pd2-resurrected/pull/35) |
| P2 | Remapped ids: skill, stat, state and missile ids remapped through append-only id lists in `merge/` | P1 | A | [#40](https://github.com/synpoox/pd2-resurrected/pull/40) |
| P3 | Item tables: items, affixes, runewords, cube, treasure classes; map items blocked from dropping | P2 | A | [#47](https://github.com/synpoox/pd2-resurrected/issues/47) |
| P4 | Monster and area tables: monsters, levels and the remaining tables | P2 | D | [#48](https://github.com/synpoox/pd2-resurrected/issues/48) |
| P5 | Table-diff gate (`pd2_diff`) and `docs/unported.md`, seeded with the gaps found so far | P1 | D | [#49](https://github.com/synpoox/pd2-resurrected/issues/49) |
| P6 | Strings: the strings build ("Decide how the mod ships PD2's strings", [#29](https://github.com/synpoox/pd2-resurrected/issues/29)) | P1 | D | [#43](https://github.com/synpoox/pd2-resurrected/issues/43) |
| W3 | Stand-ins and data rows (core-rules) | P1 | C | [#46](https://github.com/synpoox/pd2-resurrected/issues/46) |
| P10 | HD missiles: every missile PD2 adds gets a `missiles.json` model (stand-in art), so none is invisible in HD | P2 | A | [#74](https://github.com/synpoox/pd2-resurrected/pull/74) |
| P11 | Skill icons: every class's icon sheet rebuilt from PD2's art, so PD2's new skills show their icons | P1 | D | [#79](https://github.com/synpoox/pd2-resurrected/issues/79) |
| P14 | Skill tree backgrounds: each class's tree boxes and arrows redrawn from the merged tables, so they follow PD2's skill positions | P1 | D | [#84](https://github.com/synpoox/pd2-resurrected/issues/84) |

## M2: Amazon and Barbarian complete

M1 plus the companions, corruption and the Amazon and Barbarian engine rules; the other classes stay on stand-ins.
Was the alpha until [#85](https://github.com/synpoox/pd2-resurrected/issues/85) widened it (that milestone is now the pre-alpha).

| # | Work item | Blocked by | Lane | Issue |
|---|---|---|---|---|
| W1 | Plumbing and Split Throw server half (core-rules) | none | B | [#37](https://github.com/synpoox/pd2-resurrected/pull/37) |
| W2 | Client-half example (core-rules) | W1 | B | [#45](https://github.com/synpoox/pd2-resurrected/issues/45) |
| W4 | Investigation: PlrModes (core-rules) | none | C | [#42](https://github.com/synpoox/pd2-resurrected/issues/42) |
| W5, W6 | Amazon (core-rules) | W2, P2 | B | [#52](https://github.com/synpoox/pd2-resurrected/issues/52), [#53](https://github.com/synpoox/pd2-resurrected/issues/53) |
| W7 | Nova and orb hits (core-rules) | W1, P2 | C | [#50](https://github.com/synpoox/pd2-resurrected/issues/50); game check waits on the owner's run |
| W8, W9 | Barbarian (core-rules) | W2, P2 | B | [#54](https://github.com/synpoox/pd2-resurrected/issues/54), [#55](https://github.com/synpoox/pd2-resurrected/issues/55) |
| W10 | Barbarian group (core-rules) | W1, P2 | C | [#51](https://github.com/synpoox/pd2-resurrected/issues/51) |
| P7 | Companions: hash-checked manifest, release-script download, hand-written `filter.json`, Pyre's 16 `Filter*` sound rows and flacs merged into our data | none | C | [#44](https://github.com/synpoox/pd2-resurrected/issues/44) |
| P9 | Corruption: starts with the outcome check (core-rules Q10), includes the cube op 16/18 check for corruption recipes | P3 | A | [#56](https://github.com/synpoox/pd2-resurrected/issues/56) |
| P8 | M2 release: local 7z and release notes | every item above | A | [#57](https://github.com/synpoox/pd2-resurrected/issues/57) |

**Lanes** for four sessions (M2): A (data) P1 -> P2 -> P3 -> P9 -> P8; B (engine) W1 -> W2 -> class items; C W4 and P7 first,
then W3, W7, W10; D P4, P5, P6 once P1 is in. **Critical path:** P1 -> P2 -> P3 -> P9 -> P8.

**M2 exit bar:** a fresh character of any class levels through Acts 1-5 Normal on the merged data; W1-W10 and
corruption game checks pass; the table-diff gate is green for the pre-alpha scope; every companion arms on Slot4; the owner
tests the local 7z alone.

## Pre-Alpha: every PD2 class complete

M2 plus the Necromancer, Paladin, Druid, Sorceress and Assassin as complete classes (spec [#85](https://github.com/synpoox/pd2-resurrected/issues/85)). Class-themed item rules
are "not yet" rows on the unported list; the Warlock ships as D2R has it.

| # | Work item | Blocked by | Lane | Issue |
|---|---|---|---|---|
| W11-W14 | Necromancer (core-rules); W14's get-hit window split off to W34 | none | B | [#86](https://github.com/synpoox/pd2-resurrected/issues/86), [#122](https://github.com/synpoox/pd2-resurrected/issues/122), [#87](https://github.com/synpoox/pd2-resurrected/issues/87), [#88](https://github.com/synpoox/pd2-resurrected/issues/88), [#89](https://github.com/synpoox/pd2-resurrected/issues/89) |
| W15-W20 | Paladin (core-rules) | none | C | [#90](https://github.com/synpoox/pd2-resurrected/issues/90), [#91](https://github.com/synpoox/pd2-resurrected/issues/91), [#92](https://github.com/synpoox/pd2-resurrected/issues/92), [#93](https://github.com/synpoox/pd2-resurrected/issues/93), [#94](https://github.com/synpoox/pd2-resurrected/issues/94), [#165](https://github.com/synpoox/pd2-resurrected/issues/165), [#95](https://github.com/synpoox/pd2-resurrected/issues/95) |
| W21-W24 | Druid (core-rules) | none | D | [#96](https://github.com/synpoox/pd2-resurrected/issues/96), [#97](https://github.com/synpoox/pd2-resurrected/issues/97), [#98](https://github.com/synpoox/pd2-resurrected/issues/98), [#99](https://github.com/synpoox/pd2-resurrected/issues/99) |
| W25 | Investigation: Assassin finisher hook points (core-rules) | none | E | [#100](https://github.com/synpoox/pd2-resurrected/issues/100) |
| W26-W28 | Sorceress (core-rules) | W21 (W27 only) | E | [#101](https://github.com/synpoox/pd2-resurrected/issues/101), [#102](https://github.com/synpoox/pd2-resurrected/issues/102), [#103](https://github.com/synpoox/pd2-resurrected/issues/103) |
| W29-W32 | Assassin (core-rules) | W25 (W29), W29 (W30), W21 (W32) | first free class lane | [#104](https://github.com/synpoox/pd2-resurrected/issues/104), [#105](https://github.com/synpoox/pd2-resurrected/issues/105), [#106](https://github.com/synpoox/pd2-resurrected/issues/106), [#107](https://github.com/synpoox/pd2-resurrected/issues/107) |
| W33 | **Rush:** melee splash (srvdo 163), which item_splashonhit, Frenzy, Tiger Strike and Revive's event cast (core-rules) | none | A, as a rush job | [#119](https://github.com/synpoox/pd2-resurrected/issues/119) |
| W34 | Get-hit/block end window and cast/sequence delay, every class (core-rules), split off W14 | none | first free class lane | [#120](https://github.com/synpoox/pd2-resurrected/issues/120) |
| W35 | Melee splash: the current skill's damage and radius extras (core-rules), after W33 | W33 | first free class lane | [#125](https://github.com/synpoox/pd2-resurrected/issues/125) |
| P12 | Class item rules: Ormus' Robes' random sorceress skill (properties func 25 -> 26) and skill on cast for every cast-skill item, its trigger approximated on the unported list | none | A | [#108](https://github.com/synpoox/pd2-resurrected/issues/108) |
| P13 | Pre-Alpha release: local 7z and release notes | P8 and every item above | A | [#109](https://github.com/synpoox/pd2-resurrected/issues/109) |

**Lanes** for five sessions: A P9 -> P8, then P12 and rush jobs, then P13; B Necromancer; C Paladin; D Druid; E W25, then
Sorceress. The Assassin build goes to the first class lane that frees up. A rule shared by two classes belongs to the
earliest class in the order Necromancer, Paladin, Druid, Sorceress, Assassin; the later class checks it. The owner reviews
the first PR in each class lane.

**Engine changes for classes:** before porting a class rule, check what it changes in play. PD2 class engine code that
changes only input or the skill bar, not what a skill does, comes from a third-party plugin instead: auras on the left
button (cltst 54, cltdo 103) from the [Left Click Auras](https://github.com/CelestialRayOne/d2rloader_plugins/tree/main/plugins/left-click-auras)
companion, and Force Move. The merge keeps D2R's columns for those rows, and the unported list names the plugin. Only
rules that change a skill's effect, damage, missiles or timing get core-rules code.

**Pre-Alpha exit bar:** every PD2 class is a complete class: each of its rows has a passing game check or an approximated row
on the unported list; a fresh character of every class levels through Acts 1-5 Normal on the merged data; the table-diff
gate is green for the pre-alpha scope; every companion arms on Slot4; the owner tests the local 7z alone.

**Left out of the pre-alpha** (unported list, "not yet"; the alpha's scope): map items and the map system, new PD2-only areas, the Warlock
redesign, class-themed item rules, monster-only engine rules, everything in the core-rules order after the cut.

## Alpha: PD2 complete for single-player

The pre-alpha plus every PD2 feature left out of it: the map system, the Warlock redesign (goals in [warlock.md](warlock.md); its three new skills wait for the beta), the uber bosses and their
areas (Uber Tristram, Diablo Clone, Rathma, Lucion with Desecration; designed in [uber-bosses.md](uber-bosses.md)), the remaining items and item rules and the remaining unported-list rows.
The core-rules rows after the cut join as W36-W38. Ordered and sliced in "Order the Alpha roadmap and slice it into work
items" ([#210](https://github.com/synpoox/pd2-resurrected/issues/210)).

| # | Work item | Blocked by | Lane | Issue |
|---|---|---|---|---|
| LP1 | Level tables and DS1 rewrite: a Ghidra check of PD2's negative object ids first, then PD2's rows appended after D2R's, each DS1 rewritten at build time; the 64 areas in scope (all 74 but PvP, guild halls, PD2's Uber Ancients and the Butcher's Lair) load through `cheat goto` | none | B | [#211](https://github.com/synpoox/pd2-resurrected/issues/211) |
| LP2 | Automap for level types 36+ (core-ui) | LP1 | B | [#212](https://github.com/synpoox/pd2-resurrected/issues/212) |
| LP3 | HD preset generator (DS1 -> layout -> preset) for the levels on D2R tiles; Jungle Map within 10% of a D2R area's frame rate | LP1 | B | [#213](https://github.com/synpoox/pd2-resurrected/issues/213) |
| LP4 | HD stand-in kits, part 1: the PD2 tile sets only map areas load | LP3 | B | [#214](https://github.com/synpoox/pd2-resurrected/issues/214) |
| LP5 | HD stand-in kits, part 2: the sets uber areas load and the rest | LP3 | B | [#215](https://github.com/synpoox/pd2-resurrected/issues/215) |
| MS1-MS5, MS7-MS11 | Map system ([map-system.md](map-system.md)); MS6's stacking moved to IR6, MS11 is PD2's invader brain | MS1 (MS2, MS5), LP1 (MS2, MS7), rest in map-system.md | C | [#216](https://github.com/synpoox/pd2-resurrected/issues/216)-[#225](https://github.com/synpoox/pd2-resurrected/issues/225) |
| UB1-UB7 | Uber bosses ([uber-bosses.md](uber-bosses.md)) | UB1, MS2 and LP1 (UB2); UB2 (UB3-UB6); UB6 (UB7) | D | [#226](https://github.com/synpoox/pd2-resurrected/issues/226)-[#232](https://github.com/synpoox/pd2-resurrected/issues/232) |
| WL1-WL4 | Warlock ([warlock.md](warlock.md)) | WL1 (WL2); WL1, WL2 (WL3, WL4) | D | [#233](https://github.com/synpoox/pd2-resurrected/issues/233)-[#236](https://github.com/synpoox/pd2-resurrected/issues/236) |
| IR2-IR8 | Item rules ([item-rules.md](item-rules.md)); IR1 done in [#189](https://github.com/synpoox/pd2-resurrected/issues/189) | none | E | [#237](https://github.com/synpoox/pd2-resurrected/issues/237)-[#243](https://github.com/synpoox/pd2-resurrected/issues/243) |
| W36 | Monster kits (core-rules) | none | D | [#244](https://github.com/synpoox/pd2-resurrected/issues/244) |
| W37 | Investigation: PD2's unidentified server rules (core-rules Q6) | none | E | [#245](https://github.com/synpoox/pd2-resurrected/issues/245) |
| W38 | Server rules: `.set-home`, `skilldelay`, the Terror Zone comparison, what W37 finds | W37 | E | [#246](https://github.com/synpoox/pd2-resurrected/issues/246) |
| UR1, UR4-UR6 | Unported rows ([unported-rows.md](unported-rows.md)): docs, skill columns, difficulty constants, quiver automagic | none | A | [#199](https://github.com/synpoox/pd2-resurrected/issues/199), [#202](https://github.com/synpoox/pd2-resurrected/issues/202)-[#204](https://github.com/synpoox/pd2-resurrected/issues/204) |
| UR2, UR7, UR8 | HD monsters and overlays, art, audio | none | B | [#200](https://github.com/synpoox/pd2-resurrected/issues/200), [#205](https://github.com/synpoox/pd2-resurrected/issues/205), [#206](https://github.com/synpoox/pd2-resurrected/issues/206) |
| UR3, UR9 | The Ascendant, the DPS counter | none | E | [#201](https://github.com/synpoox/pd2-resurrected/issues/201), [#207](https://github.com/synpoox/pd2-resurrected/issues/207) |
| UR10 | Potion counter companion; off the release path, moves to the beta if the plugin has not come by P15 | the owner provides the plugin | first free lane | [#208](https://github.com/synpoox/pd2-resurrected/issues/208) |
| P15 | Alpha release: local 7z and release notes | P13 and every item above but UR10 | A | [#247](https://github.com/synpoox/pd2-resurrected/issues/247) |

**Order.** The order level pipeline -> map system -> Warlock -> uber bosses -> items -> unported rows sets lane priority,
not blockers. The only chains are LP1 -> MS2 -> UB2 -> UB3-UB6 -> UB7, MS1 -> MS2 and MS5, WL1 -> WL2 -> WL3 and WL4,
and P13 -> P15. Gameplay waits on LP1 alone: dev builds run without HD presets (ADR 0005), so LP3-LP5 block only P15.

**Lanes** for five sessions: A finishes the pre-alpha (W7's game check, W19, W34a-c, #141 and #171, P13), then UR1, UR4,
UR5, UR6, then P15; B LP1 first, then LP2-LP5, then UR2, UR7, UR8; C MS1 and MS5 while LP1 builds, then MS2 -> MS3 ->
MS4 -> MS7 -> MS8 -> MS9 -> MS10 -> MS11; D UB1, WL1 and W36 while MS2 builds, then UB2-UB7, with WL2-WL4 whenever the
owner can play; E IR2-IR8, then W37 -> W38, UR3, UR9. Overflow goes to the first free lane; MS8, MS10 and MS11 may move
there once MS4 is in. **Critical path:** LP1 -> MS2 -> MS4 -> MS8 -> MS9 -> MS10 -> MS11 -> P15.

**Owner reviews** the first PR in each lane, LP3 (the generator decides how 64 areas look), MS2 (the first `core-maps`
code and the shared gate), IR2 (it removes P12's code), every WL item (each opens with the owner's play session) and
P15. The rest merge on the game check, the table-diff gate and a code-review agent.

**Alpha exit bar:** the pre-alpha bar holds; the map-system, uber-boss, IR and UR game checks pass; each Warlock tree's
endgame build meets its target; all 64 in-scope PD2-only areas load with a generated HD preset and Jungle Map runs within
10% of a same-size D2R area's frame rate; the table-diff gate is green for the alpha scope; the unported list's "not yet"
rows are only those the beta owns (guild halls, the Warlock's three new skills, league rewards, UR10 if it moved); every
companion arms on Slot4; the owner tests the local 7z alone.

Multiplayer comes after the alpha, in the beta: run 1 over TCP/IP, run 2 on hosted servers with a short beta league,
then the launch with league 1.
