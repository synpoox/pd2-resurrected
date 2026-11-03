# Inventory of PD2's data against D2R 3.3

Resolves research ticket #2 (child of map #1). Findings only: nothing here converts or ports anything, and no PD2 or Blizzard file content is copied into this repo (counts, ranges and short identifiers only).

## Answer in brief

- **Base game: 1.13c.** The shipped `Game.exe` reports file version 1.0.13.60. PD2 replaces the game DLLs and the data archive; the tables are classic 1.13c-layout tables with many added columns.
- **No new table formats.** PD2 has 93 `.txt` tables (each with a compiled `.bin`). 82 also exist in D2R 3.3 (build 93847). 11 are classic tables D2R does not ship; 9 are D2R-only tables PD2 lacks. The engine DLL does not read a private set of extra tables; the only text tables it opens by name are `sounds.txt` and `soundenviron.txt`.
- **Row counts are no obstacle for the 32,768 limit.** The largest PD2 table is 5,693 rows (17% of the limit). The pressure is on IDs (skill, stat, state, missile IDs collide with D2R's), not on table size.
- **Engine-level function numbers above D2R's used range exist in every column checked**, and ~45 distinct values need plugin code or a re-map (section 4).
- **Events: 5 extra rows** in `events.txt` (18 vs D2R's 17), and their IDs collide with D2R's own new rows (section 5).
- **Saves: classic `.d2s` version 96**, PlugY `.d2x` and `.sss` shared stash, plus newer PD2 stash/compendium files. D2R wants version 105 (section 6).
- **The older snapshot is not older.** `pd2-data/s14-alliance` is byte-identical to the current install for both DLLs, `pd2maps.mpq` and every data file both hold, so there is no season diff to report (section 1). A separate `Beta` folder in the install is a different, smaller build.

## 1. Sources and method

| Source | What it is | Used for |
|---|---|---|
| `C:\Games\PD2\ProjectD2` (`pd2data.mpq`, `Live\`, `Beta\`, engine DLLs, `Save\`) | Current install, read-only | Tables, DLL strings, save headers |
| `d2rAI-toolkit\pd2-data\s14-alliance` | Older-snapshot folder | Diff against the install |
| `d2rAI-toolkit\d2r-data\93847` | D2R 3.3 build 93847 tables | Comparison side |
| `d2rAI-toolkit\knowledge\porting-classic-mods.md` (section 1 inventory table, section 5 function numbers), `engine-limits.md`, `saves.md` | Port notes | Table layout of this report, function-number ranges, save facts |

Method: `pd2data.mpq` was read in memory with a throwaway script kept outside the repo (MPQ v1 with per-file encryption and PKWARE implode; 185 entries, all extracted without error except `sounds.bin` and `patchstring.tbl`, which are also unreadable by the script and so excluded). Tables were compared by header name and value sets. DLL claims come from printable strings in `ProjectDiablo.dll` and `PD2_EXT.dll`, not from disassembly.

**Snapshot finding.** MD5 of `ProjectDiablo.dll`, `PD2_EXT.dll` and `pd2maps.mpq` match between the install and the `s14-alliance` folder, and all 182 data files present in both are identical (the snapshot also holds `sounds.bin` and `patchstring.tbl`, which my script could not extract from the archive, and lacks one credits text file). The install's `Live\pd2data.mpq` is byte-identical to the top-level one. So "latest vs s14" is a null diff: the snapshot already is the latest. The `Beta\` folder is a different build: its `ProjectDiablo.dll` is 2.7 MB against 5.25 MB, and 45 tables differ, every one with equal or fewer rows (for example skills 579 vs 614, missiles 1,008 vs 1,067, monstats 1,141 vs 1,284, uniqueitems 446 vs 485). It looks like an earlier, smaller build; I did not establish which season it is.

## 2. Base version and the engine files

| Item | Finding |
|---|---|
| Game version | `Game.exe` file version 1.0.13.60 = 1.13c. D2R-side comparison is 3.3 build 93847 |
| Engine DLL | `ProjectDiablo.dll` (5.25 MB) and `PD2_EXT.dll` (87 KB) beside replaced classic `D2*.dll` files. The classic DLLs have no version resource, so I could not confirm whether they are stock 1.13c |
| Other injected code | `BH.dll` (UI/QoL overlay, 1.4 MB), `D2HD.dll` 2.0.0.4, `SGD2FreeRes`, `d2gl` (renderer), `PlugY.dll` 14.0.2.0 (shared stash, extra stat/skill points, etc.) |
| Source files named by assert strings in `ProjectDiablo.dll` | Client: `Skills.cpp`, `inv.cpp`, `panel.cpp`, `showitems.cpp`, `ui.cpp`, `Msg.cpp`, `SCmd.cpp`, `Gfx.cpp`, `SoundHdr.cpp`. Common: `ItemTbls.cpp`, `Items.cpp`, `StatsEx.cpp`. Game: `AiThink4.cpp`, `Clients.cpp`, `Game.cpp`, `ItemMode.cpp`, `PlrModes.cpp`, `PlrMsg.cpp`, `SkilPal.cpp`, `Skills.cpp`, `SUnitEvent.cpp`, `Quests/a3q5.cpp`. PD2's own: `CubeFunctions`, `D2CommonFunctions`, `D2NewFunctions`, `D2ServerFunctions`, `ItemFunctions`, `SaveFunctions`, `DrawHoverItem`, `Sound/*`, and a `guild/*` set (`GuildClient/Common/Game/Showcase`) |
| Triage use | These names are the starting list for the (a) data / (b) service / (c) hook / (d) UI / (e) cut sort in the port notes. Not done here. Guild code is online-feature and is probably map #1 out-of-scope |
| PD2_EXT.dll | 778 strings and no source paths, so I cannot say what it does from strings alone |

## 3. Tables: PD2 vs D2R

- PD2: 93 `.txt` tables. D2R 93847: 91.
- **In PD2, not shipped by D2R (11):** `AiParms`, `Arena`, `CharTemplate`, `CubeType`, `HireDesc`, `MiniSkillIcons`, `MonItemPercent`, `MonName`, `TreasureClass`, `UniqueTitle`, `WeaponClass`. Ten are classic-layout tables from the original game that D2R dropped. I did not have a vanilla 1.13c install to confirm which of these vanilla ships, so treat that as likely, not proven. `MiniSkillIcons` (532 rows) is the one I could not place; check what reads it before deciding.
- **In D2R, not in PD2 (9):** `ActInfo`, `ItemUICategories`, `LevelGroups`, `MonCalc`, `MonPet`, `ObjPreset`, `PropertyGroups`, `RunewordUICategories`, `WanderingMon`. These need rows authored for the port (`LevelGroups` 101 rows, `ObjPreset` 584, `MonPet` 179 in D2R).
- **Text tables the engine DLL opens by name:** `sounds.txt` and `soundenviron.txt` only (the DLL ships its own sound engine; D2R's `Sounds` is 11,913 rows against PD2's 4,760). Everything else goes through the game's own table loader, so no hidden extra tables beyond the 11 above.
- **Column shape.** Every shared table has column differences in both directions. Headline items: PD2 `ItemStatCost` has save-width columns that D2R lacks (`Save Bits`, `Save Add`, `Save Param Bits`, with `S12` and `S13` variants, and `1.09-` variants); PD2 has per-NPC `...MagicLvl/Max/Min` columns for the classic NPC names in `Armor`/`Weapons`/`Misc` while D2R renamed several (`Anya`/`Hratli` spellings differ); `CubeMain` has `ladder` where D2R has `firstLadderSeason`/`lastLadderSeason`; `Levels` has `Beta`, `EntryFile`, `MonLvl1-3`, `MonLvl1Ex-3Ex` where D2R has `LevelGroup`, `LevelEntry`, `MonLvl`, `MonLvl(H)` and similar; `MonStats` has `TreasureClass1-4` (+`(N)`/`(H)`) where D2R has a shorter set plus `CannotDesecrate`/`ShieldBlockOverride`; D2R `Runes`/`SetItems`/`UniqueItems` carry ladder/chronicle columns PD2 lacks. A full column-by-column map is a follow-up for the conversion tickets, not part of this inventory.

## 4. Function numbers above D2R's used range

"D2R used range" below is the set of values present in the 93847 data tables, with the free slots from `engine-limits.md` (table sizes found by reverse engineering). PD2 value lists are the distinct values present in the current `pd2data.mpq`.

| Column | PD2 range (distinct) | D2R data range | PD2 values D2R data does not use | Reading |
|---|---|---|---|---|
| skills `srvstfunc` | 1-79 (69) | 1-70 | 71-79 (9 values) | Beyond D2R's data range; 64 is also in use, which D2R reassigned to Bind Demon (note section 5) |
| skills `srvdofunc` | 1-189 (156) | 1-87 | 41, 143, 163, 173-182, 184-186, 188, 189 | 173-189 sit in D2R's free 172-190 slots: each needs plugin code (15 values). 41, 143, 163 are inside D2R's 1-171 but unused by its data, so D2R may run different code for them: check each |
| skills `cltstfunc` | 0-82 (54) | 0-61 | 58, 82 | 82 is past the end of D2R's client table; use none (per notes) |
| skills `cltdofunc` | 0-119 (87) | 0-102 | 15, 22, 83, 110-114, 117-119 | 110-119 are in D2R's free 110-129 slots (8 values): plugin. PD2's 94-109 range overlaps D2R's own skills 96-109, so each use needs a semantic check |
| skills `srvprgfunc1-3`, `cltprgfunc1-3` | up to 172 / up to 107 | smaller | 39, 139, 143, 171, 172 server; 9, 81, 83, 106, 107 client | Not covered by the notes' tables; flagged for the skill triage |
| missiles `pSrvHitFunc` | 1-65 | 0-60 | 5, 6, 30, 61-65 | 61-65 are in D2R's free 61-70: plugin (5 values). A stray value written as `*12` also appears (looks like a typo/comment; check) |
| missiles `pCltHitFunc` | 1-67 | 0-65 | 4, 6, 11, 12, 26, 66, 67 | 66, 67 are in D2R's free range 66-80 |
| missiles `pSrvDoFunc` | 1-40 | 1-45 | 40 | Within D2R's table; 40 unused by D2R's data |
| missiles `pCltDoFunc` | 1-72 | 1-77 | 12, 72 | Within D2R's range |
| misc `pSpell` | 1-13 | -1..15 | none | All inside D2R's set |
| cubemain `op` | 16, 18, 28, 30, 31 | 28 only | 16, 18, 30, 31 | D2R's switch handles 1-28, so 30 and 31 need a plugin; 16 and 18 hit D2R's vanilla ops, check they match PD2's intent |
| itemstatcost `descfunc` | 0-30 (26 values) | 5-29 | 0-4, 6-9, 20, 30 | 30 is outside D2R's set (plugin). 0-4, 6-9, 20 are not used by D2R's data and need checking |
| properties `func1` | 1-36 (23) | 1-25, 36 | 3 | Minor |
| properties `func2`, `func3` | up to 24 | 1,3,10,16 / 1,3,10,15,17 | 8, 22, 24 / 8 | Minor |

Rough total needing new code or a decision: 15 `srvdofunc` + 8 `cltdofunc` + 5 `pSrvHitFunc` + 2 `pCltHitFunc` + 2 cube ops (30, 31) + `descfunc` 30 + `cltstfunc` 82 + `srvstfunc` 71-79 + the program-function values = about 45 distinct values, before the semantic checks on values inside D2R's range. Each is one engine function PD2 added; identifying what each does is the job of the per-system tickets.

## 5. Events and ID collisions

**Events.** PD2 `events.txt` has 18 rows, D2R 17.

- The first 13 rows match D2R's first 13 (hit-by-missile through level-up).
- PD2 rows 13-17: `corpsefinish`, `dospellcast`, `doblock`, `docrit`, `dopierce`.
- D2R rows 13-16: `death`, `install`, `hextrigger`, `absorbresisteddamage`.
- So row indices 13-16 mean different events in the two games: an ID-based reference to the PD2 rows would fire the wrong D2R event. Port by name, not index.
- Four PD2 `itemstatcost` stats use the extra events (`item_skilloncast`, `item_skillonblock`, `item_skilloncrit`, `item_skillonpierce` via `itemevent1`). `corpsefinish` has no stat user in `itemstatcost`; no skill row references any extra event. D2R raises only its own events, so these four stats would be silently dead until a plugin raises them.

**ID collisions (important for saves and for table merging).** PD2 reuses or extends numbers D2R already assigned:

| Table | PD2 rows / max ID | D2R rows / max ID | Same ID and name | Same ID, different name |
|---|---|---|---|---|
| skills | 614 / 613 | 429 / 428 | 343 | 86 (first at ID 17) |
| states | 245 / 244 | 232 / 231 | 184 | 48 (first at 74) |
| itemstatcost | 511 / 510 | 368 / 367 | 315 | 52 (first at 183) |
| missiles | 1,067 / 1,066 | 742 / 741 | 671 | 71 (first at 526) |

D2R's newer content (Warlock skills, new stats, new missiles) sits in IDs PD2 also uses for its own content. Six skills share a name but have different IDs. Any approach that keeps PD2 IDs needs the "IDs that may move" rules of the port notes, and state/stat IDs are the sensitive ones because they are saved.

## 6. Row counts vs the 32,768 limit

Largest PD2 tables: `AutoMap` 5,693 (17.4% of limit), `Sounds` 4,760, `CubeMain` 2,288, `MonStats` 1,284, `MonSeq` 1,249, `LvlPrest` 1,167, `TreasureClassEx` 1,140, `Missiles` 1,067. Everything else is below 1,000. No table is near the limit. Against D2R, PD2 is larger in skills (614 vs 429), missiles (1,067 vs 742), monstats (1,284 vs 752), uniqueitems (485 vs 439), cubemain (2,288 vs 227), itemtypes (240 vs 110), automap (5,693 vs 3,287), and smaller in sounds, skillcalc, treasureclassex, setitems. States are 245 (D2R 232): well under the 511 / 1024 state caps discussed in the notes.

## 7. Save format

| Item | Finding |
|---|---|
| Character file | `.d2s`, signature `0xaa55aa55`, **version 96** in all 142 `.d2s` files in the install's `Save` folder (including backups). D2R 3.3 expects version 105 with the extended-format marker and a `.d2rl` sidecar (`saves.md`) |
| Shared stash | PlugY `.d2x` (header `CSTM01`) per character and `_LOD_SharedStashSave.sss` / `_LOD_HC_SharedStashSave.sss` (header `SSS` `01`). `PlugY.ini` has shared stash and multi-page stash on, big stash off |
| Newer PD2 files | `ProjectDiablo.dll` names `pd2_shared.stash`, `pd2_hc_shared.stash`, `pd2_shared.compendium`, `pd2_hc_shared.compendium`, a `/pd2_game_settings/stash_affinities` setting, and a guild inventory blob. The installed characters here predate these, so I saw no sample of the newer formats; their layout is unknown |
| Extra per-character files | `.ma0`-`.ma3`, `.map` (map/automap state), `.key` (hotkeys, BH) |
| Stat serialisation | PD2's `itemstatcost` has separate `Save Bits`/`Save Add`/`Save Param Bits` columns for `S12` and `S13` plus the plain and `1.09-` ones, which suggests PD2 changed stat bit widths between seasons and keeps old widths for old characters. Stat IDs go up to 510, inside the 9-bit retail limit of 511 but only just; D2R's extended save format uses 16-bit stat IDs |
| Skill points | 614 skill rows, but a `.d2s` stores a fixed per-class skill block; how PD2 maps its class skills into it is not verified here |

Open: reading the item bit layout of version 96 against 105 and the per-season stat widths needs sample files and is not done. I read only headers (signature, version) of the install's saves; nothing from them is stored here.

## 8. Limits of this inventory

- Function-number "D2R used range" comes from D2R's data tables. A number inside that range but unused by D2R's own data may still run D2R code with different meaning.
- Value lists are distinct values per column; I did not count rows per value or check which PD2 rows use a given function.
- No vanilla 1.13c install was available, so statements about what vanilla ships are inferred from D2R's layout and the port notes.
- The `Beta` folder's role and season were not established.
- DLL behaviour comes from strings only; nothing was disassembled.
