# Inventory of PD2's Pandemonium and Uber Tristram

Resolves research ticket #185 (feeds UB1 and UB3 in `docs/uber-bosses.md`, #153). It covers PD2's Pandemonium chain: the
three keys, the three mini-uber areas, the organs, the Pandemonium Talisman (`ubtm`) and Uber Tristram. The shape follows
the uber-boss inventory (#147), which did not cover this chain. Findings only: no PD2 or Blizzard file content is in this
repo. Table, row and string names below are short identifiers.

## Answer in brief

- **Most of the chain is D2R's own.** D2R ships the vanilla Pandemonium Event: the keys drop from treasure classes, two
  cube rows open the red portals, levels 133-136 hold the fights, and its engine spawns the ubers and drops the organs and
  the Torch. PD2 keeps the first half of that and replaces the second half.
- **PD2 keeps D2R's keys and mini-uber areas.** The three keys `pk1`-`pk3` still open one of Matron's Den, Forgotten
  Sands or Furnace of Pain (levels 133-135, almost unchanged from D2R). PD2 adds one more boss source per key and raises
  the drop weights.
- **The ticket's premise is wrong in one place.** Level 156 "Pandemonium Map" is not part of this chain. It is the map
  area "The Pandemonium Citadel", opened by the tier 3 map item `t37` (boss Dark Commander Alma). This matters for UB3
  and for #153's decision on D2R's levels 133-136 (section 7).
- **PD2 replaces the finale.** The organs no longer open a portal. Cube row 182 makes the Pandemonium Talisman (`ubtm`),
  a map-spell item (`pSpell` 12) that is used at the Blood Fountain in Harrogath. It opens level 185 "PD2 Pandemonium
  Finale", a copy of D2R's level 136 on a new preset (`pd2_ubertrist.ds1`). There the three primes fight with the plain
  act-boss AIs, not D2R's uber AIs.
- **Tables versus DLL.** The tables hold the keys' drops, the recipes, the levels, the monsters, their skills and AIs,
  and the Torch's unique row. The DLL holds what the cube's `useitem` rows do, the Talisman's opening, the area rules (pre-buffs
  stripped, three silver portals, teleport in), the organ, Torch, Black Soulstone and Prime Evil Soul drops, and
  the prime auras. None of that code is reverse-engineered yet.
- **Existing rows** cover the Talisman's spell ("Map items"), its no-drop ("Map items never drop"), the recipe op 28,
  MonSmite (built, W19), DiabWall (built, W22) and the Torch's charm rule. The rest is new (section 7).

## 1. Sources and method

- **PD2 data:** `pd2-data/` from `tools/pd2_extract.py` (the baseline in `pd2-baseline.json`). I read `misc`, `cubemain`,
  `levels`, `lvlprest`, `monstats`, `skills`, `treasureclassex`, `uniqueitems`, `properties` and `states` with throwaway
  scripts kept outside the repo, plus `patchstring.tbl` through `tools/pd2_strings.py`'s `read_tbl`. "Row N" means the
  Nth data row, counted from 0 after the header. Where a number is a file line (the header is line 1), it is called a
  line. The ticket's "rows 181 and 184" are lines of the merged `out/.../cubemain.txt`; they are data rows 179 and 182.
- **D2R data:** the 93847 tables (`d2rAI-toolkit/d2r-data/93847`), to tell PD2's changes from what D2R already ships.
- **ProjectDiablo.dll:** printable strings only (ASCII and UTF-16), no disassembly.
- **PD2 wiki:** [Uber tristram](https://wiki.projectdiablo2.com/wiki/Uber_tristram) (a stub marked "outdated or missing
  info (from S5 changes and later)"), [Recipes](https://wiki.projectdiablo2.com/wiki/Recipes),
  [Patch:Season 3](https://wiki.projectdiablo2.com/wiki/Patch:Season_3) and
  [Patch:Season 5](https://wiki.projectdiablo2.com/wiki/Patch:Season_5). The wiki is a secondary source. Where it and the
  data disagree, the data wins and the disagreement is noted.

## 2. The keys and the mini-uber portal

| Key | Code | String | PD2 sources (Hell TC, weight) | D2R sources | Wiki rate |
|---|---|---|---|---|---|
| Key of Terror | `pk1` | `pk1` | `Countess Item (H)` 20; `Blood Raven (H)` 26 | Countess only | Countess 1:14, Blood Raven 1:9 |
| Key of Hate | `pk2` | `pk2` | `Summoner (H)` 27; `Bloodwitch (H)` 26 | Summoner only | Summoner 1:8, Bloodwitch 1:9 |
| Key of Destruction | `pk3` | `pk3` | `Nihlathak (H)` 24; `Izual (H)` 26 | Nihlathak only | Nihlathak 1:8, Izual 1:9 |

- The keys are misc rows 140-142: type `ubr`, stackable to 50, not spawnable, not usable. D2R's keys do not stack.
- The drops are plain treasure-class entries. The port's merged tables already carry PD2's weights. D2R's own
  `Pandemonium Key A/B/C` TCs (D2R's Terror Zone and desecrated rows) are not PD2's.
- **Recipe (cubemain row 179, "Pandemonium key - PD2"):** `pk1` + `pk2` + `pk3`, op 28 param 1, min diff 2 (Hell),
  output `useitem`. PD2 turned off the vanilla row 178 (output `Pandemonium Portal`). With output `useitem` and no mods,
  the table alone does nothing useful, so the DLL must open the portal. The wiki says it opens "a portal to one of the
  mini-uber zones", random, in Harrogath, Hell only, and that an area does not repeat in one game. That is D2R's own
  behaviour for its own row.
- **Areas:** levels 133 "Matron's Den", 134 "Forgotten Sands", 135 "Furnace of Pain" (strings `Pandemonium Run 1-3`).
  They differ from D2R's rows only in empty-versus-0 cells, entry files, monster levels and one monster (`cr_lancer8` ->
  `cr_lancer9` in 133). Same layers, layouts and monster lists.

## 3. The mini-ubers and the organs

| Monster | Row | Area (wiki) | AI | Hell level, HP | Resists dm/ma/fi/li/co/po | Skills | TC | Organ (wiki) |
|---|---|---|---|---|---|---|---|---|
| Lilith (`uberandariel`) | 708 | Matron's Den | `Andariel` | 110, 6500-6600 | 50/50/90/90/90/90 | AndrialSpray, AndyPoisonBolt | `Uber Andariel` | Diablo's Horn `dhn` |
| Uber Duriel (`uberduriel`) | 709 | Forgotten Sands | `Duriel` | 110, 6500-6600 | 0/50/90/90/90/95 | MonCharge, MonJab, MonSmite, MonHolyFreeze; adds 42-84 cold | `Uber Duriel` | Baal's Eye `bey` |
| Uber Izual (`uberizual`) | 707 | Furnace of Pain | `UberIzual` | 110, 6500-6600 | 50/50/90/90/90/95 | Frost Nova 28, MonTeleport | `Uber Izual` | Mephisto's Brain `mbr` |

- **PD2's changes from D2R:** resists moved toward 90 elemental and 50 physical/magic, AC halved, regen 4 -> 1, Izual's
  Chilling Armor removed and Frost Nova raised 8 -> 28. Uber Duriel's Charge, Jab and Smite are swapped for PD2's monster
  copies (MonCharge, MonJab, MonSmite).
- **Their skill functions:** MonCharge is srvdo 67 and MonJab srvdo 7, D2R's own Charge and Jab functions. MonSmite is
  srvdo 150, PD2's Smite (built in core-rules, W19). So the mini-ubers need no new skill code.
- **Treasure classes:** `Uber Andariel`, `Uber Duriel` and `Uber Izual` hold only `Act 5 (H) Uitem C` and
  `Act 5 (H) Cpot C` (picks -4). D2R's rows have no TC. **No organ appears in any treasure class**, so the organ drops are
  code. In D2R that code is D2R's own. Whether PD2's DLL adds to it is unchecked. Season 5 notes say "organs back to 100%
  drop rates".
- **Spawn:** no table places the mini-ubers in 133-135 (no level `mon*` column, no superunique, no preset), so the spawn is
  engine code, D2R's own in D2R.
- **The organs:** misc rows 143-145 (`dhn`, `bey`, `mbr`), type `ubr`, stackable to 50 (D2R's do not stack).

## 4. The Pandemonium Talisman and the opening

- **Recipe (cubemain row 182, "Pandemonium Finale key 3"):** `dhn` + `bey` + `mbr`, no op, no difficulty limit, output
  `useitem` and output b `ubtm`. PD2 turned off rows 180 and 181 (output `Pandemonium Finale Portal`). Output b makes the
  Talisman in the table alone. What output `useitem` does to the first organ is unchecked (it should be consumed).
- **The item:** misc row 210, code `ubtm`, name "Uber Trist Map", shown as "Pandemonium Talisman" (string `ubtm`),
  description `UberTristDesc` (a recommended level of 80 and a pointer to the Blood Fountain). It is level 99,
  spawnable, usable, not stackable, `pSpell` 12 (the map-item spell) and `spelldesc` 1.
- **The opening is DLL code.** `ubtm` sits in the DLL's map-item code list (near 0x37bd77) with every map code and the
  other uber keys `dcma`, `rtma`, `uba` and `luca`. So the Talisman shares the map items' opening code, though it is an
  uber key, not a map item. The wiki says it
  opens "for all players near the blood fountain", and that only one boss area or map can be opened per game.
- **Area rules (wiki only):** pre-buffs are removed on entry; the player is teleported inside, and a portal opens when the
  last prime dies (Season 3); three single-use silver portals give one attempt plus three; there is no free re-entry while
  monsters remain; the ubers do not regenerate. **New**, and the same rule set the other uber areas use (#147 section 2).
- **The area:** level 185 "PD2 Pandemonium Finale" (shown as "Tristram"), preset row 1141 `Act1/Tristram/pd2_ubertrist.ds1`.
  It is D2R's level 136 with another id, `OffsetY` 1100 and white lighting (`Intensity` 140, RGB 255). D2R's level 136
  uses `Tri_Town4.ds1`. The new preset is in `pd2maps.mpq` (not extracted, so its monster and object presets are
  unread). It falls under unported "New PD2-only areas".
- **Not this chain:** level 156 "Pandemonium Map" / "The Pandemonium Citadel" (presets `Pandemonium.ds1`,
  `Pandemonium2.ds1`, `Pandemonium3.ds1`) is a map area, opened by the tier 3 map item `t37` (misc row 229 "Pandemonium
  Map", shown as "Pandemonium Citadel Map"),
  with boss string `PandemoniumBoss` "Dark Commander Alma". Its monsters are `*Pandemonium` map rows (Hell level 89, map
  TCs). It belongs to the map system (map #143), not to the uber bosses.

## 5. Uber Tristram: the primes

| Monster | Row | AI (PD2 / D2R) | Hell level, HP | Resists dm/ma/fi/li/co/po | Skills (level) |
|---|---|---|---|---|---|
| Uber Mephisto (`ubermephisto`) | 705 | `Mephisto` / `UberMephisto` | 120, 5695 | 40/40/60/60/60/60 | PrimeLightning 6, PrimeBolt 20, PrimePoisonNova 7, MephistoMissile 5, MephFrostNova 1, MonBlizzard 5 |
| Uber Diablo (`uberdiablo`) | 706 | `Diablo` / `UberDiablo` | 120, 6427 | 40/40/60/60/60/60 | DiabLight 16, DiabCold 2, DiabFire 14, DiabWall 12, DiabRun 5, PrimeFirewall 8, DiabPrison 1, Diablogeddon 10 |
| Uber Baal (`uberbaal`, "Baal Crab") | 710 | `BaalCrab` / `UberBaal` | 120, 6336 | 40/40/60/60/60/60 | Baal Nova 18, Baal Inferno 16, Baal Tentacle 15, Baal Cold Missiles 36, Baal Teleport 1, Baal Lowres 13, Blood Mana 3 |

- **The AI swap is the biggest change.** D2R's `UberMephisto`, `UberDiablo` and `UberBaal` AIs are the vanilla Uber
  Tristram scripts (Mephisto's minion spawns among them). PD2 uses the act bosses' plain AIs. Any minion spawning PD2
  keeps then comes from the preset or the DLL. Unread.
- **Other changes from D2R:** level 110 -> 120 (the Season 3 note: "this was done to fix the bug with torches dropping from
  mini ubers"), HP fixed and slightly lower, resists lower (Season 5: -10 physical and magic, -20 elemental), AC about a
  third, regen 4 -> 0, D2R's Blizzard -> MonBlizzard, Baal's Defense Curse -> Baal Lowres, Baal's Chilling Armor removed,
  Baal's melee damage up (A1 330-380 -> 500-550), most skill levels raised.
- **Their skills are data.** All rows but three are D2R's with empty-versus-0 differences. DiabWall is srvdo 166, which
  core-rules routes (built, W22). Baal Lowres (skill 480) is PD2-only but runs D2R's curse function (srvdo 30, the one
  Amplify Damage uses) with missile `curselowerresist`. MonBlizzard (205) is D2R's.
- **Auras (wiki only):** Mephisto has level 20 Conviction (-50% resistances, was -125%), Baal casts his own Lower Resist
  (-75%, about 25 seconds). The monster rows hold no Conviction. The aura source is unchecked (`monstats2`, the preset or
  the DLL); state 184 `uberminion` and 241 `uberscaling` exist.
- **Prime Evil rules (wiki):** they deal 200% damage to minions and mercenaries and are immune to slows, Dim Vision,
  Terror, Confuse, Attract and Sanctuary's physical-resist cut. The rows carry `primeevil` 1. Which of these rules the DLL
  adds on that flag is UB1 work.
- **Minions:** vanilla's Uber Tristram minions (`skeleton8`, `sk_archer11`, `skmage_*7`, rows 726-729) are still in
  monstats. Season 3: "Uber Tristram's minions are no longer immune to any damage type".

## 6. Drops

| Item | Code | Source | Where it lives |
|---|---|---|---|
| Diablo's Horn, Baal's Eye, Mephisto's Brain | `dhn`, `bey`, `mbr` | Lilith, Uber Duriel, Uber Izual (one each, 100%) | code: in no TC |
| Hellfire Torch | unique row 401 on `cm2` | when the last prime dies | code: rarity 0, in no TC |
| Black Soulstone | `dcho` (misc 213, "Uber Horn") | with the Torch; Season 5 patch 6 "+20%" | code: in no TC |
| Prime Evil Soul | `dcso` (misc 214, "Uber Soul") | Hell Mephisto, Diablo (~1:21) and Baal (~1:13) | code (#147); the primes' TC columns name "Uber Soul", which is the misc name, not a TC |
| Hellfire Ashes | `cm2f` (misc 265, "Torch Fragment") | cube 1-7 Torches, with a key (rows 1955-1961) or without (rows 2260-2266) | data |

- **The primes' TC.** `ubermephisto`, `uberdiablo` and `uberbaal` have `Uber Soul` in all three Hell TC columns. No TC has
  that name; `Uber Soul` is the name of misc row 214 (`dcso`). Whether the engine resolves it to an item, or the DLL reads
  the column, is unchecked. **Open.**
- **The Torch's row** differs from D2R's: PD2 has `randclassskill1` (+2 to a random class's skills, a PD2-only property),
  Vitality 30-60, Energy 10-20, All Resist 10-20, Light 4-8, level 120, carry1 empty. D2R has `randclassskill` 0-7 with
  all-stats, a level-5 Hydra charge and an on-hit skill, carry1 1003. The port's merged row already is PD2's.
- **The Torch feeds Rathma:** Voidstone row 1478 (Torch) and row 1962 (Ashes), the Jawbone row 1479 (#147). The Demonic
  Cube rerolls it (row 2153, op 31). The Black Soulstone feeds Dclone (`dcma`, row 198).
- **The DLL names the codes.** The chain's codes come up in three places: an item-code compare list at 0x2aeeb1 (`ceh`,
  `bet`, `tes`, `fed`, `toa`, `dhn`, `bey`, `mbr`, `pk1`-`pk3`), a code-pair table at 0x468a74 (keys, organs, then the
  other uber ingredients; probably the "Ubers" stash tab, `StashCategoryBossing`), and `dcho`/`dcso`/`cm2f` as immediates
  in about 40 copies of one inlined routine.
- Kill counters: the DLL's `.kills` command has `Uber Tristram Kills:`. Cut, as for the other uber bosses (#153).

## 7. Mapped to the existing lists

| Piece | Row | Status |
|---|---|---|
| Keys' drops (`pk1`-`pk3` in six Hell TCs) | none needed: PD2's TCs are already merged | existing (data) |
| Key recipe's op 28 (row 179) | core-rules "Quest item difficulty check" (op 28, data) | existing |
| Talisman's spell (`misc.ubtm.pSpell`) | unported "Map items"; core-rules map-items row (`pSpell` 12 -> 16) | existing |
| Talisman never drops | unported "Map items never drop" (`misc.ubtm.spawnable`, `merge/item-tables.txt`) | existing |
| Level 185 and its preset | unported "New PD2-only areas" | existing |
| MonSmite (Uber Duriel) | core-rules Smite, srvdo 150 (built, W19) | existing |
| DiabWall (Uber Diablo) | core-rules "Firestorm and wall makers" (built, W22) | existing |
| MonCharge, MonJab, Baal Lowres | D2R's own functions (67, 7, 30) | existing (data) |
| Torch's one-unique-charm rule (carry1 empty) | core-rules "Charm 'only one unique' rule" | existing |
| Torch to Ashes, Torch reroll | cubemain data; core-rules "Demonic cube reroll" (op 31) | existing |
| Levels 133-135 and the mini-uber spawn | D2R's own | existing (D2R); they stay on (section 7) |
| Key recipe's `useitem` -> random mini-uber portal | none | **new**, or D2R's own portal row (merged line 2390) if PD2's row is dropped |
| Organ drops | none (D2R's own in D2R) | **new** if PD2 changed them, else D2R's |
| Talisman opening at the Blood Fountain -> level 185 | the map-item opening (MS2) plus the uber-key gate (UB2) | **new** (UB2/UB3) |
| Uber Tristram area rules (buffs stripped, teleport in, silver portals, no re-entry, exit portal) | none | **new**, shared with UB2 |
| Primes on plain AIs; any minion spawns | none | **new** (UB3), once the preset and DLL are read |
| Prime auras (Conviction, Lower Resist) and Prime Evil rules | none | **new** |
| Torch and Black Soulstone on the last prime's death; Prime Evil Soul from Hell primes | none | **new** |
| The primes' `Uber Soul` TC column | none | **open** |
| `.kills` Uber Tristram counter | none | cut (#153) |

### Effect on the decided scope

#153's decision (PR #186) first turned off all of D2R's Pandemonium Event, levels 133-136, and its UB3 named the
Pandemonium Map with the mini-ubers. The data says PD2 uses levels 133-135 and D2R's mini-uber fights, almost unchanged,
and replaces only the finale (136 -> 185). The owner agreed, and the PR adding this inventory (#185) narrows #153's row to D2R's Uber Tristram (its
organ portal row, merged line 2391, and level 136). D2R's keys, the levels 133-135 and the mini-uber spawns stay on.
Level 156, the Pandemonium Citadel (`t37`), is a map area and belongs to the map system.

In the port's merged `cubemain` today, PD2's row 179 (line 181, `useitem`) comes before D2R's portal row (line 2390). If
the cube takes the first match, cubing the keys does what PD2's bare row does, which is nothing useful without PD2's DLL,
and D2R's portal never opens. A game check (cube the keys in Hell Harrogath) settles it.

## 8. Open questions

- What does PD2's DLL do for the two `useitem` rows (179, 182)? Is the key portal D2R's own code, reached through the
  output, or PD2's? Ghidra on `ProjectDiablo.dll` (`PD2.gpr`), from `CubeFunctions.cpp`'s routines and the compare list at
  0x2aeeb1.
- What does `pd2_ubertrist.ds1` place: the primes, their minions, the exit portal? It needs `pd2maps.mpq` extracted.
- Where do Mephisto's Conviction and Baal's Lower Resist come from, and is the Torch's drop still D2R's (vanilla's
  "level 136, last prime dead") code pointed at 185?
- Does the engine read `Uber Soul` in the primes' TC columns?
- The wiki's Uber Tristram page is marked outdated. Its numbers (auras, portals, odds) need a game check on the PD2 install
  before they count as the port's baseline.
