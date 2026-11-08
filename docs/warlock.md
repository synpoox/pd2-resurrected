# The Warlock in PD2's style

The Warlock is the one class not held to the PD2 baseline. It is D2R's Warlock on build 93847, changed only where a
skill falls short of PD2's style: a skill that already meets the bar below stays as D2R has it. No full redesign is
needed. These are its goals, set in [#149](https://github.com/synpoox/pd2-resurrected/issues/149). The tree is not
redrafted ([#150](https://github.com/synpoox/pd2-resurrected/issues/150) was dropped): changes go into the change log one
skill at a time. Balance targets and items were set in [#195](https://github.com/synpoox/pd2-resurrected/issues/195).

## Goals

1. **Identity.** Keep D2R's three trees and their themes: Demon (summons, blood), Eldritch (sword, blades, hexes) and
   Chaos (fire, sigils, miasma).
2. **Skill count.** The class ends at 33 skills, 11 per tree, like PD2's classes. The alpha ships D2R's 30 skills,
   changed where needed, with one grid position per tree left empty; the three new skills fill those positions in the beta.
3. **The PD2-style bar.**
   - Every tree carries at least one full endgame build.
   - Main skills get PD2-style synergies written on their descriptions, mostly from their own tree.
   - No dead skills: each skill is a main skill, a synergy, a working one-point utility or a prerequisite worth taking.
   - A mastery per tree is not required.
4. **Allowed changes.** Numbers, synergies, prerequisites, a skill's position inside its own tree, and reworking how one
   skill works while it keeps its theme. Not allowed: moving a skill to another tree, replacing a skill, re-theming a
   tree or reworking a whole tree; PD2 has never done these.
5. **Engine budget.** Only engine rules the port already has or builds anyway: D2R's Warlock functions and PD2's
   functions ported for the other classes. Each new engine rule needs the owner's approval.
6. **Baseline.** D2R's Warlock on build 93847. The change log below records every change from it and why. A D2R patch
   that changes the Warlock is reviewed as a re-baseline, not followed.

## Balance targets

Each tree has one endgame build, measured against a PD2 reference build. Exactly one endgame build is top tier,
bordering on overpowered; the other two sit mid-pack among PD2's classes. All picks are provisional: the owner has not
played the Warlock yet, so each tree's work item opens with a play session that may change its pick, recorded here.

| Tree | Endgame build | Reference build | Target |
|---|---|---|---|
| Demon | Summoner (Summon Goatman, Summon Tainted, Summon Defiler, Bind Demon); Summon Tainted the likely main summon | PD2's Druid summoner | Mid-pack. Players rank D2R's pure summoner B to C tier, so it needs the most buffs |
| Eldritch | Blade melee (Echoing Strike, Blade Warp, Mirrored Blades) | PD2's Sacrifice Paladin | Mid-pack. Players rank Echoing Strike D2R's strongest Warlock build, so it comes down |
| Chaos | Fire (Ring of Fire, Flame Wave, Apocalypse) | PD2's Combustion Sorceress | Top tier |

Hexes and Miasma stay as synergies or second builds with no target of their own.

**Endgame gear.** Both builds of a test wear the same tier of gear: full skillers, Hellfire Torch, Annihilus, GG
corruptions, high-end runewords and chase uniques from the shared drop pool, but no unique that drops from only one boss.
Each work item writes its exact item list.

**The test.** The reference build is played in the port itself (PD2's classes are complete and Exact from
alpha 0.1.0), on the same spawned gear and in the same place, on a level 90+ character in Hell. Compared: kill time on a
fixed Hell pack, kill time on one boss, and whether the build survives the pack. Mid-pack means a kill time within about
±20% of the reference's; top tier means at or faster than the Combustion Sorceress. [WL2](https://github.com/synpoox/pd2-resurrected/issues/234) may tune the ±20%.

## Warlock items

The alpha adds no Warlock-only items. D2R's stay as they are (D2R-only content): the 15 grimoire bases, the uniques
Measured Wrath, Ars Dul'Mephistos, Ars Tor'Baalos and Ars Al'Diablolos, Horazon's Secrets, the runeword Vigilance on
grimoires, the Warlock tree affixes and the Warlock skillers. What the alpha does is make PD2's per-class item rules reach
the Warlock ([WL1](https://github.com/synpoox/pd2-resurrected/issues/233)): Hellfire Torch and Spirit Ward roll a class skill from classes 0-6 only (`randclassskill`), and
grimoires need their corruption outcomes and class-item crafting. Annihilus rolls all skills and needs nothing.

What [WL1](https://github.com/synpoox/pd2-resurrected/issues/233) found and did ([#233](https://github.com/synpoox/pd2-resurrected/issues/233)):

- Hellfire Torch and Spirit Ward roll classes 0-7 (`patches/030-warlock-item-rules.json`), as D2R's own Torch does.
- Corruption and crafting already reach grimoires: PD2's rows name item types (`shld,nos`, `shld,mag,noe`), and a
  grimoire is a `shld`.
- PD2 made class affixes ten times as common (frequency 1, 2, 4 -> 10, 20, 40); D2R's Warlock affixes take the same
  (the patch).
- PD2 added, for each other class, +1 class skills on body armor, shields and helms, and two kinds with `rare` 3:
  +2 class skills on the same, and +3 to each tree on the class's items. `rare` 3 means rare items only
  ([#254](https://github.com/synpoox/pd2-resurrected/issues/254)): ProjectDiablo.dll's affix picker (rva `0x389080`,
  its quality from D2Common #10106; asm `0x3892c1`-`0x38934c`) takes `rare` 0 on no rare, crafted or tempered item
  (quality 6, 8, 9), 1 on any, 2 on no crafted or tempered item (no PD2 row has 2), and 3 on quality 6 only. D2R
  reads the column as a flag (`0x58c940`), so core-rules refuses 2 and 3 where PD2 does (`loot_rules.cpp`,
  `docs/core-rules.md`). The Warlock gets the same five rows, the
  tree ones on daggers and grimoires as its other tree affixes, at ids 1042-1046 (the `+` lines in
  `merge/ids-magicprefix.tsv`).
- PD2's grand-charm skillers also roll on the PvP charm (`lchp`); PvP is cut, so the Warlock's don't.

## Work items

| # | Work item | Plugin | Blocked by |
|---|---|---|---|
| [WL1](https://github.com/synpoox/pd2-resurrected/issues/233) | PD2's per-class item rules reach the Warlock: Hellfire Torch and Spirit Ward roll the Warlock (class 7) too, grimoires get corruption outcomes and class-item crafts like PD2's other class items, and any other per-class rule found is fixed or gets an deviation row | none (data) | none |
| [WL2](https://github.com/synpoox/pd2-resurrected/issues/234) | Demon summoner against the Druid summoner: play session, gear list, test, skill changes in the change log | none, unless a change needs an engine rule (goal 5) | [WL1](https://github.com/synpoox/pd2-resurrected/issues/233), the Druid complete |
| [WL3](https://github.com/synpoox/pd2-resurrected/issues/235) | Eldritch blade melee against the Sacrifice Paladin, as [WL2](https://github.com/synpoox/pd2-resurrected/issues/234) | as [WL2](https://github.com/synpoox/pd2-resurrected/issues/234) | [WL1](https://github.com/synpoox/pd2-resurrected/issues/233), [WL2](https://github.com/synpoox/pd2-resurrected/issues/234), the Paladin complete |
| [WL4](https://github.com/synpoox/pd2-resurrected/issues/236) | Chaos fire against the Combustion Sorceress, as [WL2](https://github.com/synpoox/pd2-resurrected/issues/234) | as [WL2](https://github.com/synpoox/pd2-resurrected/issues/234) | [WL1](https://github.com/synpoox/pd2-resurrected/issues/233), [WL2](https://github.com/synpoox/pd2-resurrected/issues/234), the Sorceress complete |

## Changes from D2R's Warlock

| Skill | Change | Why |
|---|---|---|
