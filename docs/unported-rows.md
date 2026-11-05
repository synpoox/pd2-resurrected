# The remaining unported rows in the port

How the port closes the unported-list rows that had no work item yet. Decided in
[#198](https://github.com/synpoox/pd2-resurrected/issues/198) (map [#143](https://github.com/synpoox/pd2-resurrected/issues/143)),
from the [inventory](https://github.com/synpoox/pd2-resurrected/blob/research/remaining-unported-rows/docs/research/remaining-unported-rows.md)
(#196). Each row stays on [unported.md](unported.md) until its work item lands. The work items are placed on
[roadmap.md](roadmap.md) by [#210](https://github.com/synpoox/pd2-resurrected/issues/210).

## Scope

In: every "not yet" row on the unported list that names no open work item, the approximated art and hireling rows, and
the gaps #196 found with no row (HD monsters and overlays, missile stand-ins, PD2's client UI overlays, the Act 4
hireling).

Out: rows owned by an open W, MS, UB, IR or WL item; the core-rules rows after the alpha 0.1.0 cut; the level-pipeline rows
(#151); the Warlock redesign (#149, #195).

## Decisions

- **HD monsters and overlays: check first, inside the work item (UR2).** In HD, Hell, on a level 90+ character: summon
  a Dopplezon, visit the Countess, take a shrine, cast Battle Orders and a curse. If they are invisible, a generated
  `monsters.json` maps each PD2-only monster `Id` to the D2R model of a monster with the same `Code` (525), with a
  hand-picked list for the other 24; shared overlay rows keep D2R's `Character` folder; PD2's 33 new overlays get a
  copied D2R json as stand-in. Both get an approximated row. If nothing is invisible, UR2 closes with no code.
- **The Ascendant is in the alpha (UR3).** PD2's Act 4 hireling (ids 30-35, Dark and Light) and its second Rogue Scout
  set (ids 36-38) move to new hireling ids from 39, with `WType` (engine code), the `hiredesc` strings and the `$2`
  monster's HD model. UR3 first proves ids 39+ load, hire and survive a save and load; if they don't, the Ascendant
  becomes a cut row.
- **PD2's 30 voice and effect WAVs are converted (UR8)**, all in one item, after a one-file WAV-as-FLAC test under
  `data/global/sfx`. The 16 drop sounds stay with the Pyre-Flies filter (P7).
- **Item art is scripted (UR7).** PD2's 219 item pictures are upscaled with P11's pipeline (`.sprite` and
  `.lowend.sprite`, plus the `items.json`, `uniques.json` and `sets.json` entries); the row becomes approximated
  ("upscaled from classic art"). The shared skill icon sheet takes PD2's frames on cels 0-60, with D2R's HD frame kept
  where PD2's classic frame is D2R's (P11's rule). Class icons, tree backgrounds and object graphics stay approximated.
  Missile stand-ins (P10) are final: an approximated row.
- **New core-rules rows.**
  - UR4 reads skills `checkfunc`, `nocostinstate` and `general` in `ProjectDiablo.dll` first, then builds what play
    depends on. `checkfunc` folds into D2R's itype columns if every value matches what they allow; `general` is cut if
    only the UI reads it.
  - UR5 finds D2R's own values for the uber and ultra code odds and `HireableBossDamagePercent` in Ghidra. A rule whose
    value equals PD2's is cut; the others are hooked.
  - UR6 rolls the quivers' `auto prefix` groups (321-326) through an item-spawn hook. Moving the quivers to another
    table would break their item type.
  - Folded into existing rows, with no item of their own: missile `Qty` and `SpecialSetup` and the throwing potions'
    `spelldesc` go to "Throwing potions" (IR5); missile `LevRange` goes to the class or map row that owns each missile;
    `immed` on PD2's three states goes to phasing (IR7).
- **Cut, in one docs item (UR1).** Columns that are empty in PD2 or set only on rows both games share: the skill event
  columns, `blue`, `itemspecific`, `Divide`, the S12/S13 save widths, the affix and rare price columns, itemtypes
  `Charm`, `Gem` and `CostFormula`, the qualityitems classic columns, and the empty or shared-only armor, misc,
  weapons, gems, sets, runes and uniqueitems columns. The tables `cubetype` and `treasureclass`, and `aiparms`,
  `arena`, `chartemplate`, `monname`, `uniquetitle`, `weaponclass` and `miniskillicons`. PD2's panel DC6. Belts and body
  locations close as matching.
  - `cubemain.ladder` is cut. PD2's D2Common loads it as vanilla 1.13c does (a byte at record offset 1); its values 2-4
    mark PD2's online recipe groups, and the merge leaves every recipe on, which is what single-player wants. Where
    PD2's D2Game reads it was not traced.
  - Two checks run inside UR1: one D2R Ghidra lookup for the monster treasure-class drop percent (`monitempercent` is
    cut if it is 20, otherwise it gets a core-rules row), and a sane vendor price for one PD2-only armor subtype (the
    price columns are cut once it passes).
- **List fixes (UR1).** Gheed's event row names MS8. The "other classes" row points at W11-W35. Guild halls name the
  beta. New approximated rows: the missile stand-ins (P10), Buff HUD (buff timers, mini skill icons, the skill bar) and
  Item Roll Ranges (tooltip stat ranges).
- **Client UI.** The DPS counter is a new plugin built in `synpoox/d2r-tools`, as better-xp-bar is, and pinned in
  `companions/manifest.json` (UR9). The potion counter is the plugin D2R Reimagined ships, which the owner provides
  (UR10). It is bundled once its licence or a permission allows it; otherwise it is built in `d2r-tools` too.

## Work items

| # | Work item | Plugin | Blocked by | Issue |
|---|---|---|---|---|
| UR1 | Docs: the cuts and closed rows, the list fixes and plugin rows; the `monitempercent` and armor price checks | none (docs) | none | [#199](https://github.com/synpoox/pd2-resurrected/issues/199) |
| UR2 | HD monsters and overlays: the in-game check, then the generated `monsters.json`, the overlay `Character` fix and the overlay stand-ins | none (data) | none | [#200](https://github.com/synpoox/pd2-resurrected/issues/200) |
| UR3 | The Ascendant and the second Rogue Scout set at hireling ids 39+: the id check first, then `WType`, `hiredesc` and the HD model | core-rules | none | [#201](https://github.com/synpoox/pd2-resurrected/issues/201) |
| UR4 | Skill columns `checkfunc`, `nocostinstate`, `general`: read in `ProjectDiablo.dll`, then built, folded or cut | core-rules | none | [#202](https://github.com/synpoox/pd2-resurrected/issues/202) |
| UR5 | Difficulty constants: the uber and ultra code odds, the hireling boss damage | core-rules | none | [#203](https://github.com/synpoox/pd2-resurrected/issues/203) |
| UR6 | Quiver automagic: an item-spawn hook for misc items' `auto prefix` | core-rules | none | [#204](https://github.com/synpoox/pd2-resurrected/issues/204) |
| UR7 | Art: PD2's item pictures and the shared skill icon sheet, upscaled | none (data) | none | [#205](https://github.com/synpoox/pd2-resurrected/issues/205) |
| UR8 | Audio: PD2's 30 voice and effect WAVs to FLAC, after a one-file test | none (data) | none | [#206](https://github.com/synpoox/pd2-resurrected/issues/206) |
| UR9 | The DPS counter plugin | `d2r-tools` | none | [#207](https://github.com/synpoox/pd2-resurrected/issues/207) |
| UR10 | The potion counter companion: licence, pin, the Slot4 check, an approximated row | companion | the owner provides the plugin | [#208](https://github.com/synpoox/pd2-resurrected/issues/208) |

## Game check

Each work item that changes play passes its test in game, in Hell, on a level 90+ character. UR1 is docs only.
