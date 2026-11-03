# Inventory of core-rules engine rules

Resolves research ticket #19 (framework from #14). The hook list it fills is [docs/core-rules.md](../core-rules.md). Findings only: no
PD2 or Blizzard file content is in this repo; skill, missile, stat and recipe names below are short identifiers.

## Answer in brief

- **98 hook-list rows** (92 in the first pass, 6 added by the second review in section 5). 78 need plugin code (bucket c: 38 S,
  36 M, 4 L), 2 are a service (b), 12 are data only (a) and are listed so their check is not lost, 5 are unreachable and 1 is cut.
  About a third are `verify`: their bucket or slot depends on a check that has not been done.
- **Two corrections to the data inventory (#2) change the plan:**
  1. PD2's server do functions **153-170** are PD2's own (vanilla stops at 152), and D2R 3.x uses 153-171 for its own RotW
     skills. So 17 PD2 functions collide, not 3. With PD2's 15 own numbers in 173-189 that is 32 functions for 19 free slots
     (172-190). **The srvdofunc table must be extended** (a dispatcher detour for values 191+), or the plan does not fit. The same
     collision hits `cltdofunc` 97-105 and 109 (9 values, which do fit in 110-129) and `srvstfunc` 64, 66, 69, 70.
  2. Misc `pSpell` 12 and 13 are inside D2R's range but mean something else in D2R: 12 is D2R's Resist Scroll, used by **PD2's 48
     map items**, and 13 is D2R's respec token, used by PD2's unlimited TP/ID books. Both are engine rules (re-map to 16, 17).
- **Unreachable:** item events `docrit`, `dopierce` and `corpsefinish` (so only 2 of the 4 event stats are live: skill on cast with
  62 item rows, skill on block with 5), `descfunc` 30, the boss skill Power Strike Tomb Boss and 5 missile rows (listed below).
  Every function-number value from #2's section 4 has at least one reachable row.
- **Values inside D2R's range checked against D2R's own data guide:** cube ops 16 and 18 (item stat less than / equal to) and every
  `descfunc` PD2 uses except 30 have the vanilla meaning in D2R, so they are data. The skill and missile values inside D2R's range
  (srvdo 41/143/163, cltdo 15/22/83, hit 5/6/30, client hit 4/11/12/26, missile do 40/72/12) could not be checked: D2R's function
  tables are filled at run time, so they stay `verify`.
- **Server rules from #3:** confirmed by strings (`.noexp`, `.set-home`, stat commands, corrupted zones, teleport-blocked areas,
  loot allocation, PvP and battle-royale checks, map events and invaders). Phasing, the charm rule, the Act 3 quest 5 patch and the
  skill palette / player mode / NPC patches exist but their effect is not identified, so they are `verify`. Pass 1 also found three
  item rules the earlier tickets missed: skill while equipped, the map item spell and the corruption roll.

## 1. Sources and method

| Source | Used for |
|---|---|
| `d2rAI-toolkit/pd2-data/s14-alliance/data/global/excel/*.txt` | PD2 tables. #2 showed they are byte-identical to `pd2data.mpq` in the install, so they stand for the PD2 baseline |
| `d2rAI-toolkit/d2r-data/93847/data/global/excel/*.txt` and `base/*.txt` | D2R 3.3 build 93847 tables, RotW bank and base bank |
| `d2rAI-toolkit/d2r-data/93847/data/global/dataguide/data/files/*.js` | Blizzard's data guide shipped with D2R: the meaning of each cube op, `descfunc` and `pSpell` value |
| `d2rAI-toolkit/knowledge/engine-limits.md`, `d2r-93847-addresses.md`, `porting-classic-mods.md` | D2R table addresses, bounds, free slots and known D2R functions |
| `C:\Games\PD2\ProjectD2\ProjectDiablo.dll` | Printable strings only (source file names, chat commands, game fields) |
| `C:\Program Files\Diablo II Resurrected\D2R.exe` (3.3.93847) | PE section layout only |
| Branches `research/pd2-data-inventory`, `research/pd2-engine-dll-triage`, `spike/srvdofunc-split-throw` | #2, #3 and #13 findings |

Throwaway scripts outside the repo read the tables as tab-separated text, skipping `Expansion` rows as D2R does.

**Pass 1, reachability.** A skill is reachable if it is on a class tree (`charclass` plus a skilldesc row with a `SkillPage`), is
given by `charstats`, is cast by an enabled monster (`Skill1-8`) or a hireling, is granted by an item or monster property (any
skill-type property in uniqueitems, setitems, sets, runes with `complete 1`, spawnable affixes, automagic, qualityitems, gems,
enabled cube outputs, monprop), or is named by another reachable skill or a reachable skill's summon. A missile is reachable if a
reachable skill, a state, an enabled monster (`MissA1`-`MissSQ`) or an item (`missiletype`) names it, or a reachable missile names it
as a sub-missile or parameter. Cube rows count if `enabled 1`; stats count if a reachable item or monprop row carries them. Each
function value was then listed with every row that uses it. "Unreachable" means no data path reaches the row; ProjectDiablo.dll may
still pick a row by number in code, which strings can't show.

**Pass 2, D2R comparison.** For every value, compared with (1) D2R's rows that use the same number, (2) D2R's row with the same name
(what D2R itself does for that skill or missile, the fallback if the rule is not built), (3) whether the value is in D2R's base bank
(vanilla-era, max srvdofunc 152, cltdofunc 97) or only in the RotW bank (D2R 3.x additions), and (4) the data guide where it
documents the column. A value only counts as "same as D2R" when D2R uses it for the same skill or the data guide states the same
meaning. Everything else inside D2R's range stays `verify`.

Not done: no binary was run or disassembled. ProjectDiablo.dll was not reversed, so whether PD2 also changed the body of a vanilla
function it kept the number of is unknown (Q4).

## 2. Pass 1: rows per value and reachability

Counts are live rows / all rows. Names are a sample.

| Column | Values (PD2 rows using them) | Unreachable rows |
|---|---|---|
| srvdofunc 173-189 | 173 PoisonEggSpawn, 174 Vengeance, 175 Fire Claws, 176 Holy Light, 177 UberDiabSummon, 178 Dragon Flight, 179 RathmaSummon, 180 Charge, 181 TorajanBossNest, 182 Inferno + Arctic Blast, 184-186 Lucion Nova/Dash/Punish, 188 Lightning Bolt + Lightning Fury, 189 Split Throw. 183 and 187 have no rows | none |
| srvdofunc 153-170 (missed by #2) | 153 Leap x3, 154 Double Throw x2, 155 Teleport/Blink x4, 156 Gust, 157 Blood Warp, 158 Bash, 159 Combustion + 6 boss novas, 160 Static Field x3, 161 Desecrate, 162 Joust, 164 Poison Dagger x2, 165 Hunger, 166 Firestorm + 4 walls, 167 Force Move, 168 Find Potion/Item, 169 6 death skills, 170 7 Assassin finishers | UberTalicBash (158): no monster or item uses it |
| srvdofunc 41, 143, 163, 109 | 41 WispBossNova; 143 two Royal Fire bosses; 163 proc_SplashDamage (melee splash, on 223 live item rows via `item_splashonhit`), Golem Splash and Skeleton Splash (monprop); 109 MonFrenzy, BloodLordFrenzy | none |
| cltdofunc 110-119 | 110 Fire Claws, 111 Inferno/Arctic Blast, 112 Charge, 113 LucionBlink, 114 LucionDash, 117 Lightning Bolt/Fury, 118 Split Throw, 119 Firestorm | none |
| cltdofunc 94-109 | 94, 95 are D2R's own rows with the same name (data). 97 Leap x3, 98 Double Throw x2, 100 Combustion x7, 101 Desecrate, 102 Poison Dagger x2, 103 19 Paladin auras + Force Move + 5 more, 104 Find Potion/Item, 105 7 death skills, 109 PoisonEggSpawn. 96, 99, 106-108 have no PD2 rows | none (the Korlic and Sanctuary boss aura rows and Lucion/Doll death rows are reached through monprop) |
| cltdofunc 15, 22, 83 | 15 WispBossNova; 22 Power Strike, Lightning Strike, Cow King and Valkyrie variants; 83 two Royal Fire bosses | **Power Strike Tomb Boss**: no monster, item or monprop row uses it |
| srvstfunc 71-79, 64, 66, 69, 70 | 71 UberDiabRun, 72 Charge, 73 Holy Sword, 74 three golems, 75 Smite, 76 LucionDash, 77 Spirit Wolf + Fenris, 78 Frenzy + Tiger Strike, 79 Fire Claws + Split Throw; 64 MonFrenzy, 66 Blood Warp, 69 Force Move, 70 Find Potion/Item | none |
| cltstfunc 54-58, 82 | 54 19 Paladin auras, 55 Find Potion/Item, 56 Blood Warp, 57 Charge, 58 LucionDash, 82 Baal Corpse Explode | none |
| prg funcs | server 139 (Blades of Ice), 171, 172 (Fists of Fire, Cobra Strike, RakanothStrike); client 81, 106, 107 (same skills). 143/39 and 9/83 are vanilla numbers PD2 moved between the same skills | none |
| pSrvHitFunc 61-65 | 61 poisondaggercloud + 2 Kanemith dots, 62 firebolt, icebolt, blizzard1/4, chillingarmorbolt, 63 firearrow, explodingarrow, 64 uberimmolationarrow, 65 three splitthrow missiles | 12 `tpot_*` rows on 61 are reached only by throwing potions (`missiletype`), so they are live. **splitthrowglaive, splitthrowpilum** (65) and **blizzard2, blizzard3** (62): no data path (likely chosen by code, see Q5) |
| pSrvHitFunc 5, 6, 30, `*12` | 5 nine nova/orb missiles; 6 five summon-on-hit missiles; 30 three doublethrow missiles; `*12` royalstrikechainlightning, boichaosice | **shatteringarrow** (5): its skill Shattering Arrow has no skill-tree page and nothing else grants it |
| pCltHitFunc 66, 67, 4, 6, 11, 12, 26 | 66 holylightdelay, 67 clawsofthunderbolt, 4 same nine as hit 5, 6 bladefragment1, 11 desertfireball, 12 17 fire missiles, 26 fistoftheheavensdelay | shatteringarrow (4) |
| pSrvDoFunc 40, pCltDoFunc 12, 72 | 40 and 72 blessedhammer; 12 siege beast stomp missile | none |
| pSrvDmgFunc 18, 19 (not in #2) | 18 rathmabonespear, 19 bladefragment1 | none |
| auraeventfunc 32, 3; states remfunc 12 (not in #2) | 32 Revive, 3 Chilling Armor (+ SelfAura); 12 state `skilldelay` | none |
| cube op | 16: 989 enabled / 1,028; 18: 137 / 139; 28: 5 / 8; 30: 26 / 26; 31: 16 / 16 | disabled rows only |
| descfunc | 0 corruptor, 1-4 and 6-9 (hundreds of stats), 20 six pierce/target-AC stats; 30 `item_shiny_appearance` | **descfunc 30**: `item_shiny_appearance` is on no item or monprop row |
| events.txt extra rows | `dospellcast` -> `item_skilloncast` (62 live rows, itemeventfunc 33), `doblock` -> `item_skillonblock` (5 live rows) | **`docrit`** (`item_skilloncrit`: 0 rows), **`dopierce`** (`item_skillonpierce`: 0 rows), **`corpsefinish`** (no stat or skill listens). `map_mon_skillondeath` is also on no row |
| misc pSpell (missed by #2) | 12: 48 map items; 13: TP Book Unlimited, ID Book Unlimited | none |
| properties func 3, 8, 22, 24 | many; D2R's own properties with the same codes use the same functions | none |

Server rules from #3, existence and effect (strings in ProjectDiablo.dll, plus data where it exists):

| Rule | Exists | Player sees it | Evidence |
|---|---|---|---|
| `.noexp` | yes | yes: "no longer gains experience" / "gains experience again" | chat strings |
| `.set-home` | yes | yes: starting town on load | chat strings |
| `.kills`, `.deaths`, `.played`, `.age`, `.allstats`, `.accountstats` | yes | yes: counters in chat | chat strings; per-boss kill types (dclone, rathma, lucion, ubers, maps, dungeons) |
| Teleport blocked | yes | yes | `TeleportBlocked`; PD2 levels.txt has the `Teleport` column D2R also reads |
| Corrupted zones | yes | yes: zone notification | `pGame->nCorruptedZone`, `CorruptedZoneNotify` |
| Map events, invaders | yes | yes | `MapEventNotify*` (Dark Wanderer, Gheed, Horazon, Mendeln, Invaders by class...), `AiThink4.cpp`, Invader monsters in monstats |
| Loot allocation | yes | multiplayer only | `pGame->nItemAllocation`, `LootAllocationTitle` |
| PvP modifiers, battle royale | yes | online only | `Normal/Nightmare/HellPVPModifiers`, `BRLevelCheck`, `BRGearCheck` |
| Phasing | yes (stat 278, state, property, Phasing SelfAura on Spectral Plate) | yes, effect unknown | `StrPhasingStatDesc` |
| Charm single-unique | yes | yes, which charms unknown | `CharmOnlySingleUniqueDesc` |
| Act 3 quest 5 | file name only | unknown | `a3q5.cpp` |
| Skill palette, player modes, NPC | file names only | unknown | `SkilPal.cpp`, `PlrModes.cpp`, `SUNITNPC.CPP` |
| Perfect shrine | name only | unknown | `PerfectShrine` |
| Extra unit events | yes | yes (cast and block procs) | `SUnitEvent.cpp`, the events.txt rows above |

## 3. Pass 2: per-rule notes

**Why so many collisions.** D2R's base bank stops at srvdofunc 152 and cltdofunc 97; D2R 3.x added 153-171 and 96-109 for RotW.
PD2 also numbered its additions from 153. Same-name comparison shows PD2 moved vanilla skills onto its own numbers: Leap 77 -> 153,
Double Throw 74 -> 154, Teleport 27 -> 155, Bash 2 -> 158, Static Field 20 -> 160, Poison Dagger 32 -> 164, Hunger 122 -> 165,
Firestorm 117 -> 166, Find Item 72 -> 168, the Assassin finishers 34/35 -> 170. In D2R as is, Leap would run Sigil Death
Explosion's server code and 19 Paladin auras would run Cleave's client code. D2R's same-name number is each rule's fallback if it is
not built (an approximated deviation, not silent breakage).

**Slots.** Free in D2R and kept: srvdofunc 173-182, 184-186, 188, 189; cltdofunc 110-114, 117-119; srvstfunc 71-79 (if the table is
94 entries, Q3); pSrvHitFunc 61-65; pCltHitFunc 66, 67 and 6 (empty in D2R); cube ops 30, 31 (D2R ignores unknown ops). Proposed
re-maps: srvdofunc 153 -> 172, 154 -> 183, 155 -> 187, 156 -> 190, then 157-170 (without 163) -> 191-203 through the overflow
table; cltdofunc 97 -> 115, 98 -> 116, 100-105 -> 120-125, 109 -> 126; srvstfunc 66 -> 80, 69 -> 81, 70 -> 82, 64 -> 67 (data,
per the toolkit notes); pSpell 12 -> 16, 13 -> 17; srvdofunc 109 -> 9 (data). Each re-map is a `merge/` rule logged on its row.

**Checked against the data guide (data, bucket a).** Cube op 16 = input item's current stat less than `value`, 18 = equal to `value`.
PD2's rows use exactly that shape (`param` 361 `corruptor` with thresholds, `param` 361 = 0 for "not corrupted"), so no code is
needed, but `param` is a stat id and PD2's 361, 276, 185 and 458 are different stats in D2R: the merge must remap them. Op 28 is
D2R's quest-item check, as PD2 uses it. `descfunc` 0-4, 6-9 and 20 are documented with the vanilla formats (D2R's own data barely
uses them because it moved most stats to 19, sprintf). `descfunc` 30 is "single skill random" in D2R; PD2's only user is
unreachable. `pSpell` 1-9 rows match D2R's rows for the same item code.

**Likely same, still verify.** Server hit 6: the toolkit notes it spawns the monstats row in `sHitPar1`, and all five PD2 users are
summon-on-hit missiles; the `sHitPar1` ids must be remapped (monstats ids collide). cltdofunc 15: D2R has a function there ("random
spray around the target"), PD2 uses it for one boss nova.

**Unknown, verify.** srvdofunc 41/143/163, cltdofunc 22/83, server hit 5/30, client hit 4/11/12/26, missile do 40/72/12, damage
18/19, aura event 32/3, state remove 12: D2R's data never uses these numbers, and D2R's tables live in `.data` past the file's
initialised part (srvdofunc at 0x238ea00 is beyond `.data`'s 0x3afa00 raw bytes), so the file can't show whether a slot is empty.
A one-time plugin log of each table's non-null slots answers it in one session (Q2).

**Effort.** A plain skill function was about one session in the spike, plus a client half. Most skill rows are M because each needs
both halves and at least one constant PD2's data doesn't hold (spread angles, timings): those are read from ProjectDiablo.dll, which
adds a reversing step per rule. Rough total: 78 bucket-c rows (38 S, 36 M, 4 L), on the order of 75-115 sessions, and the map
system (Q7) comes on top. Treat this as a guess until the first milestone measures it.

**Order hint for the work items (not decided here).** Plumbing first (installer, RVA header, cltdofunc client example, **srvdofunc
overflow table**, a slot-dump log). Then the rules that unlock most rows: cltdo 103 + cltst 54 (19 auras), skill on cast (62 rows),
melee splash (223 rows), the Assassin finishers (7 skills), hit 62 and 12 (many missiles).

## 4. Open questions

- **Q1. srvdofunc overflow.** 32 PD2 server do functions, 19 free slots. The dispatcher (`SrvDoDispatch`) rejects values over 190;
  the field is 16 bits. Proposal: detour the dispatcher and serve 191+ from a plugin table. Not tested; it is plumbing for work
  item 1. Alternative: build fewer rules and take D2R's fallback for the rest.
- **Q2. What D2R holds in slots its data never uses** (srvdofunc 41/143/163, cltdofunc 15/22/83, hit and missile do values above).
  Also whether `srvprgfunc`/`cltprgfunc` index the do-function tables: PD2 uses srvdofunc 143 and cltdofunc 83 for its Royal Fire
  bosses, which are D2R's vanilla prg numbers for Fists of Fire and Blades of Ice. If prg values index the same tables, PD2's prg 171
  collides with D2R's srvdofunc 171 (Korlic's Leap Attack).
- **Q3. Start-table sizes.** `srvstfunc` table at 0x238e710 sits 0x2f0 bytes before the srvdofunc table, which suggests 94 entries
  (inferred, not verified). The client start table is not located; PD2 needs 54-58 and D2R data stops at 61.
- **Q4. Did PD2 change vanilla function bodies?** Rows marked `data` assume a vanilla number does in D2R what it did in PD2. Strings
  can't tell; a balance difference would show only in the PD2 baseline test.
- **Q5. Rows picked by code.** splitthrowglaive/pilum (weapon-type choice, as in the spike) and blizzard2/3 have no data path. Check
  ProjectDiablo.dll / D2R's Blizzard code before calling them unreachable.
- **Q6. Effects not identified:** phasing, the charm single-unique rule, a3q5, SkilPal, PlrModes, SUNITNPC, PerfectShrine. Each
  needs a PD2 baseline observation or a ProjectDiablo.dll read.
- **Q7. Map system.** Map items (`pSpell` 12), the `map_mon_*` / `map_play_*` / `map_glob_*` stats, map recipes (op 30), map events
  and invaders are one large engine system. It needs its own plan; the rows here are its hook points only.
- **Q8. Cube op 30 meaning.** Guessed from the rows: it gates map recipes and the "BLOCK ... duplicates due to stackable maps" rows,
  so it probably checks a single, unstacked input. Op 31 rows have no output; the op probably rerolls the input in place. Both need
  the PD2 baseline.
- **Q9. Corrupted zones vs D2R's desecrated zones.** D2R's `pSpell` 15 and desecrated-zone config may carry most of it as data.
- **Q10. Corruption flow.** One cube press both rolls `corruptor` and applies an outcome recipe; whether PD2's cube code re-runs the
  recipe search after the first output is not known.

## 5. Second review: values the first pass read as vanilla or skipped

A second pass compared every PD2 function value with D2R's **same-name** rows in both banks (a value only counts as "same" when
PD2 and D2R use it for a skill or missile of the same name), and pooled columns that index one table (prg funcs with the do
tables, `auraeventfunc1-3`, `itemeventfunc1-2`, `func1-7`). It found these, now on the hook list:

| Value | PD2 rows | D2R rows on the same value | Change on the hook list |
|---|---|---|---|
| missiles `pSrvHitFunc` 60, `pCltHitFunc` 65 | 12 `tpot_*` throwing potions, sharptootharmageddoncontrol/rock | colossal throwing axe (60), miasmachains (65), both RotW | new row, re-map to 68 / 72 |
| `pSrvHitFunc` 34 | bladefragment1 | echoingstrike (RotW) | blade fragment row, 34 -> 69 |
| `pSrvDoFunc` 38, `pCltDoFunc` 70 | five splitthrow*return missiles | flamewave (38), vine beast death (70) | Split Throw row |
| `pCltDoFunc` 69 | fistsoffireexplode, bladesoficecubes | corpseexplosion | Assassin finisher row |
| `pSrvDmgFunc` 15, 16, 17 | psychichammer; doublethrow x3; multipleshotarrow/bolt | holybolt and fistoftheheavensbolt; echoingstrike; ringoffire | two new rows, Double Throw row |
| `pSrvDoFunc` 39, `pCltDoFunc` 71 | temp1 | abyss missiles; flamewave | new row, `unreachable` (nothing names temp1) |
| skills `srvstfunc` 67 | CorpseCycler, VineCycler | Frenzy (D2R moved it there from 1.13c's 64) | new row, verify |
| `srvstfunc` 68 | Poison Dagger, BaalMinionBossBerserk | Double Swing, Cleave (PD2's Double Swing uses 32) | was "vanilla, data"; now verify |
| `srvstfunc` 34, `cltstfunc` 28 | Grim Ward | Find Item (PD2's Find Item moved to 70 / 55) | data: Grim Ward borrows Find Item's vanilla start |
| `auraeventfunc1` 16 | Smite (`domeleedamage`) | used only in D2R's RotW bank | aura event row |
| properties `func1` 25 | `sorc-skill-rand-ctc` on Ormus' Robes (live, `enabled 1`) | `affix-rand` | new row, plugin |
| prg 106 (client) | Cobra Strike, RakanothStrike, Fists of Fire | cltdofunc 106 is Bind Demon | if prg indexes the do tables (Q2), cltdofunc needs 12 re-mapped slots for its 12 free ones (115, 116, 120-129) |

Two notes that support the first pass's open questions:

- **Q2, prg funcs.** D2R's data has no `srvdofunc` row on 39, 41 or 143, but Royal Strike and Fists of Fire use them as
  `srvprgfunc` values, and `cltdofunc` 15 and 83 are Royal Strike's `cltprgfunc3` / `cltprgfunc2`. That fits prg values indexing
  the do-function tables. It also explains PD2's bosses: WispBossNova (srvdo 41, cltdo 15) reuses Royal Strike's third-charge nova,
  and the Royal Fire bosses (srvdo 143, cltdo 83, param "radius of jump to next target") reuse its second-charge chain. Both are
  then likely data, still to verify in game.
- **Charm rule.** D2R groups carry-one uniques through uniqueitems `carry1` (Gheed's Fortune 1001, Annihilus 1002, Hellfire Torch
  1003, the crafted Rupture charms 1004). PD2 leaves `carry1` empty on every row and enforces the rule in ProjectDiablo.dll
  (`CharmOnlySingleUniqueDesc`). Its unique charms (Gheed's, Annihilus, Torch, the Dclone, Maxlevel and Rathma trophies, Dev
  Charm) can probably get `carry1` groups in data; the row moves to bucket (a), verify which charms and groups PD2 uses.

Source for both: the same PD2 and D2R tables as section 1.
