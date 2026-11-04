# The remaining unported rows

Resolves research ticket [#196](https://github.com/synpoox/pd2-resurrected/issues/196) (child of map
[#143](https://github.com/synpoox/pd2-resurrected/issues/143)). Findings only: nothing here ports anything, and no PD2 or
Blizzard file content is copied (counts, row keys, column names and short values only). The decisions and slicing come
in a follow-up grilling ticket, the way #148 fed #158.

## Answer in brief

- **Most table rows can close without code.** Their columns and tables can be cut outright or close once one small
  check passes. They are either empty in PD2, set only on rows both games share (vanilla 1.13c leftovers
  that D2R handles its own way), or tied to a cut feature (PvP, ladder, the classic editors). Belts and body locations
  match D2R apart from the classic pixel boxes of one belt, so that row closes.
- **Five data columns need a core-rules row** (engine code): skills `checkfunc`, `nocostinstate` and `general`; missiles
  `Qty` and `SpecialSetup` on PD2's new missiles; difficultylevels `HireableBossDamagePercent` and the uber/ultra code
  odds; and the hireling `WType` columns, which go with the next item.
- **A whole hireling is missing.** PD2's Act 4 hireling, the Ascendant (hireling ids 30-35, two kinds: `ascd`/`ascl`),
  and PD2's second Rogue Scout set (ids 36-38) are not in the merge, because D2R uses ids 30-38 for its own new Desert
  Mercenary and Barbarian rows. The unported row calls this "level brackets left out", but it is the whole Act 4
  hireling. Bringing it back needs new hireling ids past D2R's 38 (untested) and engine code for its weapon types.
- **Art: two gaps with no unported row, which may be visible in HD today.** (1) None of the 549 PD2-only monster rows
  in the merge has an entry in D2R's `data/hd/character/monsters.json`. That includes three superuniques in ported areas
  (The Countess, Bloodwitch the Wild, the Wisp Boss) and summons of ported classes (Dopplezon, Lesser Hydra, the
  Necromancer's Summon Resist archer, the Lightning Sentry's chain sentry). (2) The merged `overlay.txt` takes PD2's
  `Character` values (`all`, `Bar`, `Assasin`, `?`). D2R uses that column as the HD overlay folder name, so 144 vanilla
  overlays (shrines, shouts, curses, the barbarian and assassin overlays) and all 33 PD2-only ones point at folders
  that don't exist. Both findings come from the tables and the toolkit's HD notes. Neither was checked in game; that
  check comes first.
- **Item art and the shared skill icon sheet are scripted art work, no layout decision.** PD2's class-less skills use
  icon cels 0-60, which fits D2R's 62-icon strip, so nothing has to compete for a slot. Where D2R and PD2 put different
  skills on one cel, the D2R skill is a monster skill or shares the picture (Attack, Town Portal), so PD2's frames can
  simply win. Item pictures follow the same upscale-and-sprite pattern as P11.
- **Audio: the cut holds.** Of PD2's 46 WAV, the 16 drop sounds serve the loot filter, and the Pyre-Flies filter ships
  its own 16 (P7; not compared one by one). The other 30 are boss and NPC voice lines and a few effects for areas and
  bosses not ported yet (counts from [pd2-asset-inventory.md](pd2-asset-inventory.md)). They can be cut with the sounds
  row, or converted later, one FLAC per file (open question from #5).
- **Rows to fix on the list itself:** the Gheed event row belongs to MS8. The "other classes" row is stale, since the
  pre-alpha now builds those classes as W11-W35. The level-pipeline rows wait only for #151's work items. The missile
  stand-in models (P10) have no row. Nor do PD2's client UI overlays: the Buff HUD companion covers buff timers, mini
  skill icons and the skill bar, Item Roll Ranges the tooltip stat ranges, and a D2R Reimagined plugin can cover the
  potion counter, so each needs only a row naming its plugin. Only the DPS counter needs a plugin built; it is in the alpha (owner).

## Scope

Every "not yet" row on [docs/unported.md](../unported.md) (at `4c8f3e3`) that names no open work item, plus the
approximated art and hireling rows the ticket names. Rows whose P item is closed (P3, P4, P11) are in scope: the
closed item no longer owns them, as #148 found for P3. Left out:

- Rows naming an open W, MS, UB or IR item.
- Rows marked "after the pre-alpha" (the core-rules rows after the cut, which join the alpha as they are).
- The umbrella row "Everything in the core-rules order after the pre-alpha cut", for the same reason.

The verdicts use the ticket's four outcomes (fold into a table D2R reads, core-rules row, art work, cut). Two more
appear where the data alone can't decide: "check first", when one cheap check (in game, or one DLL read) picks
between two outcomes, and "close the row", when PD2 and D2R already match. No column was traced in Ghidra:
the DLL reads the ticket asks for "where needed" are named per row and left to the core-rules rows they feed.

## Sources and method

| Source | Used for |
|---|---|
| `pd2-data/data/global/excel/*.txt` (the git-ignored baseline from `tools/pd2_extract.py`) | PD2's tables |
| `d2rAI-toolkit/d2r-data/93847/data/global/excel/*.txt`, and D2R's `data/hd/character/monsters.json` and `data/hd/global/ui/spells/submenu/skillicon.sprite` read from the install through `modkit` | D2R's tables and HD files |
| `generated/data/global/excel/*.txt` (the local merge output) | What the merge ships today |
| `C:\Games\PD2\ProjectD2\pd2assets.mpq` (listfile and DC6 headers, through `tools/mpq.py`) | PD2's UI art and icon sheet frame counts |
| PD2's `D2Common.dll`, `D2Game.dll`, `D2Client.dll`, `ProjectDiablo.dll` (byte search for column and table names) | Whether a column is a 1.13c loader column or a PD2 addition (weak evidence: see below) |
| `d2rAI-toolkit/knowledge/hd-graphics.md`, [pd2-asset-inventory.md](pd2-asset-inventory.md), [pd2-engine-dll-triage.md](pd2-engine-dll-triage.md), [docs/roadmap.md](../roadmap.md), [docs/map-system.md](../map-system.md) | HD rules, art counts, UI features, work items |

Method (throwaway scripts in the session scratchpad, not committed):

1. For each missing column: how many PD2 rows set it, and how many of those rows are PD2-only (a key D2R's table lacks).
   A column set only on rows both games share is a vanilla 1.13c leftover: D2R dropped it and handles the rule in its
   own way or not at all. A column PD2 also sets on its own new rows is one PD2 relies on.
2. For art: every merged monster `Id`, overlay and missile D2R lacks, checked against D2R's HD lookup (monsters.json by
   `Id`, overlays by `Character` folder, per `hd-graphics.md`).
3. The DLL byte search found most column names in PD2's replaced `D2Common.dll`, so it marks loader columns, but PD2
   ships its own `D2Common.dll`, so a name found there is not proof that the column is vanilla. No column was traced in
   Ghidra. Where a verdict rests on that, it says so.

## 1. Art

| Row | Status now | Finding | Verdict |
|---|---|---|---|
| PD2-only items borrow a D2R item's HD picture (P3) | approximated | 219 PD2-only invfiles, all with a PD2 DC6 (#5). The P11 icon pipeline (upscale the classic DC6, write `.sprite` + `.lowend.sprite`) fits: 98 px per inventory cell, under `data/hd/global/ui/items/`, with the `items.json` / `uniques.json` / `sets.json` entries the merge already writes | **Art work**, scripted. Keep the approximated row until it lands |
| HD class skill icons are upscales of PD2's 48 px art (P11) | approximated | PD2 has no HD art, so an upscale is the best source there is | **Keep approximated**: no work unless the owner wants hand-drawn icons |
| The shared skill icon sheet keeps D2R's (P11) | not yet | PD2's class-less skills use cels 0-60 (35 distinct). PD2's own classic `skillicon.dc6` has 37 frames, D2R's HD `submenu/skillicon` 40 (130 px, 132 stride), and the strip holds 62. So the sheet fits, and the "competition for slots" in the row does not happen. D2R puts other skills on 15 of PD2's cels, but they are monster skills (Tainted Fire Bolt, Korlic's Bash, Goatman Stun, Colossal Volcano, ...), menu actions, or skills on the same picture: Mark of the Bear and Wolf on Attack's cel 2, Townportal O Skill on Town Portal's cel 18. PD2's cels 37-60 point past PD2's own sheet (VampireMeteor 40, BattleOrdersCTA 46, ZharBarrage 60), so PD2 itself has no picture for them | **Art work**, scripted: extend `tools/pd2_skill_icons.py` to the shared sheet, one frame per cel, with PD2's frames winning; D2R's HD frame stays where PD2's classic frame is D2R's (P11's rule). No layout decision |
| Skill tree backgrounds in D2R's art (P14) | approximated | PD2's tree art is classic size | **Keep approximated** |
| Object graphics keep D2R's Token (P4) | approximated | 30 objects D2R re-tokened, and D2R has HD models only for its own. PD2's 12 new object folders come with the PD2-only areas (#151) | **Keep approximated**; the new objects go with the level pipeline |
| *(no row)* PD2-only monsters have no HD model | none | All 549 PD2-only monster `Id`s in the merge lack a `monsters.json` entry (D2R keys HD models by `Id`). 525 of them reuse a monster `Code` that has a D2R model, so a generated map (`Id` -> the model of a D2R monster with the same `Code`) covers them. The other 24 need a hand pick (Lucion, Iskatu, Na-Krul, Zhar, the Invaders, ...). Reachable today: The Countess, Bloodwitch the Wild, the Wisp Boss (superuniques), Blood Raven's zombies, and the summons Dopplezon (Amazon), Lesser Hydra (Sorceress), the Summon Resist archer (Necromancer) and the chain-lightning sentry (Assassin) | **Check in game first** (summon a Dopplezon, visit the Countess, in HD). If they are invisible: **table data**, a generated `monsters.json` like `tools/pd2_missiles_hd.py`, with a hand-picked list for the 24, plus a new approximated row |
| *(no row)* Overlays lose their HD effect | none | The merge copies PD2's `Character` column. D2R reads it as the folder in `data/hd/overlays/<Character>/<overlay>.json`. PD2's `all` (122 vanilla rows), `?` (8), `Bar` (4) and `Assasin` (10) match none of D2R's folders (`common`, `barbarian`, `assassin`). The 33 PD2-only overlays (teleport_debuff, blood_warp, grimward_bottom, bash_buff, staticfield_debuff, the uber auras, ...) have no HD json anywhere | **Check in game first** (a shrine's shimmer, Battle Orders' cast, a curse). If confirmed, it is a merge bug: keep D2R's `Character` on shared rows (**table data**), give the 33 new overlays a copied D2R json (stand-in) and an approximated row |
| *(no row)* PD2-only missiles draw D2R stand-in models (P10) | none | 383 new missiles mapped in `missiles.json` by `tools/pd2_missiles_hd.py` and `merge/missiles-hd.tsv`. PD2's 37 missile DCC are classic sprites, and an HD missile is a 3D model, so there is no script that converts them | **Add an approximated row**. Real HD art is out of reach, so the stand-ins are final |
| *(no row)* PD2's panels | none | PD2's 26 panel DC6 serve features that are cut or that D2R draws itself: the stash and its categories, search and tabs; the 10x8 inventory (`invchar6`); the compendium/grail; the trophy case; the controller; guild icons; the trade stash; buy/sell; the potion counter | **Cut**: each belongs to a cut row (inventory and stash layouts, grail panel, online and controller layers) or to the UI overlays below |

**Art and audio conversion plan (moved from map #1).** No new pipeline is needed. The pieces are:

- Item pictures and the shared skill icon sheet: P11's scripted upscale of the classic DC6 to `.sprite`.
- Monsters, overlays and objects: map each to a D2R stock model (`monsters.json`, an overlay json, `objects.json`).
- Missiles: P10's stand-in map, which stays final.
- Audio: keep D2R's sounds (the cut stands). If the owner wants PD2's voice lines, test one WAV-as-FLAC under
  `data/global/sfx` first; that is the open question from #5.

Only the item pictures and the shared skill icon sheet use PD2's own art.

## 2. Table data

"Set" counts are PD2 rows with a non-empty, non-zero value. "PD2-only" counts the rows of those that D2R's table lacks.

| Row | Columns | Finding | Verdict |
|---|---|---|---|
| PD2 skill columns (core-rules) | `auratgtevent`, `auratgteventfunc`, `passiveevent`, `passiveeventfunc` | 0 of 614 set | **Cut**: empty in PD2 |
| | `checkfunc` | 39 set, 21 PD2-only. Value 1 on bow, javelin and weapon skills (Fire Arrow, Strafe, the `*temp*` skills, Split Throw); 2 Throw; 3 Left Hand Throw; 4 Left Hand Swing; 5 the scrolls and books; 6 Double Throw. It looks like PD2's "may this skill fire with this weapon" check, where D2R uses `itypea*`/`etypea*` and `ItemCheckStart` | **Core-rules row** (read the check in `ProjectDiablo.dll`). It may fold into D2R's itype columns if every value matches what those already allow |
| | `nocostinstate` | 5 set: Werewolf, Werebear, and PD2's Vampire, Delirium and Cow forms | **Core-rules row**: casting a form while already in it costs no mana (inferred from the rows, unchecked) |
| | `general` | 13 set: Attack, Kick, Throw, Unsummon, the left-hand skills, the `*temp*` skills, Force Move | **Core-rules row** if `ProjectDiablo.dll` reads it (one byte check). It marks the general skills, probably for PD2's skill bar or hotkeys; if only the UI reads it, **cut** with the UI overlays |
| PD2 state columns (core-rules) | `immed`, `blue` | `blue` 4 set, all shared (freeze, cold, ...). `immed` 16 set: 13 shared aura states, plus PD2's `immune_give`, `immune_get`, `phasing` | **Cut** `blue`: vanilla rows only (D2R tints cold through its own colour columns, unchecked for these 4). **`immed` needs a check**: for the three PD2 states, whether the state applies at once rather than on the next frame. Phasing is IR work (#148), so fold the check into that row |
| PD2 stat columns (core-rules) | `Divide`, `itemspecific`, S12/S13 save widths | `itemspecific` 6, all shared. `Divide` 507 (396 at the default 1024), a 1.13c price column beside `Add`/`Multiply`, which D2R kept. S12/S13: PD2's per-season save widths; the plain `Save Bits` of 67 shared stats differ from D2R's anyway | **Cut** `itemspecific` (vanilla). **Cut** `Divide` with the prices row below: it is this table's per-stat price divisor (1024 = 1.0), not the affix `divide` there; D2R prices items with `Add`/`Multiply` alone, so only vendor prices move (inferred, unchecked). **Cut** S12/S13: the port saves through D2R's extended saves (ADR 0003) and P2's id lists, so PD2's season layouts have nothing to read |
| PD2 missile columns (core-rules) | `LevRange`, `Qty`, `SpecialSetup` | `LevRange` 46 set, 15 PD2-only (PD2's fire dots and meteors). `Qty` 60 set, 20 PD2-only (ammo and the throwing potions). `SpecialSetup` 20 set, 12 PD2-only (the `tpot_*` throwing potions) | **Core-rules row**, folded into existing rows: `SpecialSetup` and `Qty` on the potions go with "Throwing potions" (open). `Qty` on the other new arrows checks whether D2R's `decquant` skill column already covers ammo. `LevRange` on the new fire missiles goes with the class or map row that owns each missile |
| PD2 columns read by PD2's DLL (core-rules) | difficultylevels `UberCodeOdds*`, `UltraCodeOdds*`, `HireableBossDamagePercent`; hireling `WType1/2` | The difficultylevels names appear in PD2's `D2Common.dll`, so these are likely 1.13c loader columns that D2R dropped rather than DLL-only ones. PD2's values: code odds Normal 0/0/0/0, Nightmare 10/20/0/0, Hell 20/40/30/40; boss damage 50/35/25. `WType` sets each hireling's weapon types; the Ascendant takes staves | **Core-rules row**: find D2R's constants for the same rules in Ghidra. Where they equal PD2's values, **cut**; otherwise hook them. `WType` goes with the Ascendant (next row) |
| Hireling ids 30 to 38 (P4) | `hireling.*.[*]` | PD2's 156 rows include the Ascendant (Act 4, ids 30-35, kinds Dark and Light in `hiredesc`) and a second Rogue Scout set (ids 36-38). D2R's ids 30-38 are its own Desert Mercenary and Barbarian rows. The merge (135 rows) keeps D2R's and drops PD2's 36 | **New finding**: the Act 4 hireling is missing. It needs PD2's rows at new ids 39+ (whether D2R's hireling table and saves accept ids past 38 is untested), the `$2` monster's HD model (section 1), `WType`, and the `hiredesc` strings. **Engine code** plus **table data**; the grilling decides whether it is in the alpha |
| Hireling descriptions keep D2R's HireDesc (P4) | `hireling.*.hiredesc` | `hiredesc` is a 15-row code list (`farw`, `comb`, `ascd`, `ascl`, `parw`, ...) | **Fold** into D2R's hireling strings when the Ascendant lands; otherwise **keep approximated** |
| Belts and body locations (no work item) | `belts.*`, `bodylocs.*` | `bodylocs` is identical. `belts` has the same rows and slot counts (`numboxes`), and differs only in the classic pixel boxes of `uber belt` and PD2's `boxwidth`/`boxheight` columns, all classic-screen layout | **Close the row**: no gameplay difference. D2R's HD belt layout is its own |
| Quivers lose their automagic mods (P3) | `misc.auto prefix` | 6 quivers (`aqv`, `cqv` and their tiers) roll automagic groups 321-326 | **Core-rules row**: D2R's misc table has no `auto prefix`, so the roll needs an item-spawn hook. A data workaround (moving the quivers to a table with the column) would break their item type |
| PD2 item price and quality columns (P3) | affix `transform`/`divide`, rare `add`/`multiply`/`divide`, itemtypes `Charm`/`Gem`/`CostFormula`, qualityitems' classic columns | `divide`, `add`, `multiply` 0 set. `transform` is set only where `transformcolor` is too, which D2R reads. `Gem` is shared only. `Charm` is set on 4 PD2-only rows, all PvP charms (`chap` and kin). `CostFormula` is set on 9 PD2-only armor subtypes (`bels`, `sshi`, `harm`, ...) that inherit from D2R's armor types. The qualityitems columns are D2R's dropped 1.13c ones | **Cut** all, after one check: a vendor price for one PD2-only armor subtype is sane |
| Other PD2 item columns (P3) | armor `absorbs`/`spelloffset`/`throwable`, misc `special`/`throwable`/`normcode`/`ubercode`/`ultracode`, weapons `special`, gems `nummods`, sets `level`, runes `server`, `cubemain.ladder`, `uniqueitems.ladder`, weapons spell columns | Empty: `spelloffset`, both `throwable`s, misc `special`, runes `server`, `uniqueitems.ladder`, weapons `stat1-3`/`calc1-3`/`spelldesccalc`. Shared only: `absorbs`, weapons `special` (editor notes), `nummods`, sets `level`. `normcode`/`ubercode`/`ultracode` are the quiver tiers. Weapons `spelldesc`/`spelldescstr`: the 12 throwing potions' tooltip line. `cubemain.ladder`: 1,168 rows, values 1-4, where 2-4 mark PD2's own groups (imbues, maps, socket adds, crafting) | **Cut** the empty and shared-only columns. **Fold** the potion `spelldesc` into "Throwing potions". **`cubemain.ladder` needs a check**: if PD2 reads values 2-4 as recipe groups rather than ladder flags, recipes may be gated. The merge leaves D2R's ladder seasons empty, so every recipe works offline today, which suits single-player |
| PD2 tables cubetype and treasureclass (P3) | `cubetype.*`, `treasureclass.*` | `cubetype` is a 15-row lookup of cube input codes for the editors. `treasureclass` is the classic pre-1.10 table: 1.13c and D2R read only `treasureclassex` | **Cut** both |
| PD2 tables aiparms ... weaponclass (P4) | nine tables | `aiparms`: a comment table describing AI params. `arena`: one Deathmatch row. `chartemplate`: 30 ladder/test templates. `monitempercent`: 2 rows (20% treasure class). `monname`: 42 random boss names. `uniquetitle`: 16 rows marked "unused". `weaponclass`: the 15 animation codes D2R hardcodes. `hiredesc`: see above. `miniskillicons`: 532 rows mapping a skill id to a frame of `skillicons_mini.dc6`, with a generated `case` column, so it is source for PD2's mini skill icons UI | **Cut** eight: editor notes, classic or online leftovers, or D2R hardcodes the same set. `monitempercent` needs one check (D2R's monster drop percent equals 20). **Cut `miniskillicons`**: the Buff HUD companion draws the mini skill icons (section 5). **`hiredesc` folds** with the Ascendant |

## 3. Engine code

Engine work from the rows above, with nothing yet in core-rules:

- `checkfunc`, `nocostinstate`, `general` (skills): one row each, or one investigation row that reads all three in
  `ProjectDiablo.dll` first. `checkfunc` is the one most likely to change play: a bow skill fired with no bow.
- Difficulty constants: the code odds and the hireling boss damage, after finding D2R's own values.
- The Ascendant: hireling ids past 38, `WType`, and the hire screen.
- Quiver automagic: an item-spawn hook that rolls `auto prefix` groups for misc items.
- `immed` for the three PD2 states, folded into the phasing row (IR).

## 4. Rows to fix on the list

| Row | Fix |
|---|---|
| PD2's npc row gheedEventInventory (core-rules) | Belongs to **MS8** (Gheed's map event, [map-system.md](../map-system.md)): name MS8 there |
| The other classes' engine rules (no work item) | **Stale**: the pre-alpha now builds the Necromancer, Paladin, Druid, Sorceress and Assassin as W11-W35. Point it at those, or drop it once they pass |
| PD2 tables automap and monpreset, level types, PD2-only areas (level pipeline) | Decided in #151. They need only work items, which come from the final slicing ticket |
| Guild halls (no work item) | Beta (#151). Name the beta, as the trophies row names "beta run 2" |
| The Warlock redesign (no work item) | Owned by #149/#195 |
| Season trophies (beta run 2), Loot allocation (later release), Force Move (third-party plugin) | Settled; not Alpha work |

## 5. Found while looking: no row yet

- **PD2's client UI overlays**: buff timers (`bufficons.dc6`, 37 frames), mini skill icons (`skillicons_mini.dc6`, 242
  frames, and the `miniskillicons` table), PD2's skill bar, the potion counter (`potion_count_overlay`,
  `potiondots_*`), the DPS counter, and tooltip changes (stat ranges, item level). The DLL triage lists them, but the
  unported list has no row for them. Owner, 2026-11-04:
  - Buff timers, mini skill icons and the skill bar: the Buff HUD companion (Pyre-Flies, `companions/manifest.json`).
    `miniskillicons` and the two DC6 sheets are cut with them.
  - Tooltip stat ranges: Item Roll Ranges, presumably (not compared with PD2's).
  - The potion counter: a plugin from D2R Reimagined can cover it (a new companion; its licence terms apply).
  - The DPS counter: in the alpha. No plugin exists, so one needs building (a new plugin; PD2's DPS counter reads a damage feed, per
    the DLL triage).

  Each covered feature gets an approximated row naming its plugin, as for Left Click Auras.
- **Monster and overlay HD stand-ins** (section 1), and **missile stand-ins** (P10, done but unlisted).
- **The Act 4 hireling** (section 2): the row text understates it.

## Open questions for the grilling

1. In game, in HD: are PD2-only monsters (a Dopplezon, the Countess) and vanilla overlays (a shrine, Battle Orders)
   invisible today? That one test decides whether two art rows are bugs to fix before the alpha.
2. Is the Ascendant in the alpha? It needs new hireling ids, which is the biggest unknown here.
3. Which D2R Reimagined plugin gives the potion counter, and do its licence terms suit the port?
4. PD2's 30 voice and effect WAVs: cut with the sounds, or convert later?
