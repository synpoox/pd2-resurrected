# Inventory of PD2's strings against D2R's

Resolves research ticket [#28](https://github.com/synpoox/pd2-resurrected/issues/28) (child of map [#1](https://github.com/synpoox/pd2-resurrected/issues/1)). Findings only: nothing here converts or ships any text, and no PD2 or Blizzard text is copied into this repo (counts, key names and a few short markers only).

## Answer in brief

- **PD2 ships one string table: `patchstring.tbl`, English only.** It holds 3,278 entries (3,152 distinct keys). PD2 does not replace `string.tbl` or `expansionstring.tbl`; it overrides their keys from `patchstring.tbl`.
- **1,934 keys are new** (in no vanilla 1.13c table and not in D2R). **79 vanilla keys have changed text**; 34 of those change only colour codes (mostly rune names). The other 1,131 keys repeat vanilla text.
- **New keys land mostly in item and skill files:** skills 394, item-modifiers 268, item-names 237, item-nameaffixes 188, monsters 127, levels 99, item-runes 23, others 20. 320 are looked up by name from `ProjectDiablo.dll` (UI, guild, stash, hotkeys), and 256 have no reference I could find.
- **Collisions with D2R-only content are few:** 4 keys PD2 adds that D2R also added (`Runeword171` means a different runeword; 3 have the same text), plus 4 keys that differ from a D2R key only by letter case. No Warlock key collides.
- **The ID budget is ample.** 37,429 free IDs between 28,106 and 65,535 after skipping D2R's used low-16-bit values. All new keys use about 5% (IDs 28,106 to about 30,043).
- **Markup:** PD2 uses the same `ÿc` colour form as D2R (311 strings), plus three codes D2R's English text never uses (`ÿc;`, `ÿc:`, `ÿc/`). 242 strings are multi-line. D2R reversed the line order of multi-line skill text, so PD2's skill descriptions need their lines flipped. Placeholders are `%d`, `%s`, `%%` and 5 uses of `%S`.
- **The DLLs build a lot of text at run time.** `ProjectDiablo.dll` names 324 table keys and has about 140 English literals; `BH.dll` names only one table key and has about 140 English literals with inline colour codes. Both are plugin work, not data.

## 1. Sources and method

| Source | What it is | Used for |
|---|---|---|
| `d2rAI-toolkit\pd2-data\s14-alliance\data\global\local\LNG\ENG\patchstring.tbl` | PD2's string table, extracted earlier | All PD2 counts |
| `C:\Games\PD2\ProjectD2\pd2data.mpq` | Current PD2 archive, read-only | Check that the snapshot table is the current one; list of string tables PD2 ships |
| `C:\Games\PD2\d2data.mpq`, `d2exp.mpq`, `patch_d2.mpq` | The classic install PD2 sits beside; `Patch.txt` there lists 1.13c as the latest patch | Vanilla `string.tbl`, `expansionstring.tbl`, `patchstring.tbl` (the "changed" baseline) |
| `C:\Games\PD2\ProjectD2\patch_d2.mpq` | PD2's own copy of the patch archive | Cross-check of the vanilla patch table |
| `C:\Games\PD2\ProjectD2\ProjectDiablo.dll`, `BH.dll`, `PD2_EXT.dll` | PD2 engine DLLs | Printable strings only (ASCII and UTF-16), no disassembly |
| `s14-alliance\data\global\excel\*.txt` | PD2 tables | Which table column names each key (for the file it lands in) |
| `d2rAI-toolkit\d2r-data\93847\data\local\lng\strings\*.json`, `strings-legacy\`, `next_string_id.txt` | D2R 3.3 build 93847 strings | Comparison side, ID budget |
| `d2rAI-toolkit\knowledge\strings.md`, `porting-classic-mods.md` section 8, `d2r-93847-addresses.md`, `d2rloader.md` | Toolkit notes | ID rules, placeholder rules, colour-code facts |

Method: throwaway Python scripts outside the repo. The MPQ reader from the data inventory was reused with PKWARE implode support added. The toolkit has no `.tbl` reader, so a small one was written (header, index list, 17-byte hash entries; text is UTF-8). PD2's archive copy of `patchstring.tbl` now extracts: it is 197,784 bytes and its MD5 matches the snapshot file, so the snapshot is the current table.

Classification of each PD2 key, in this order: in vanilla (patch over expansion over string, the game's lookup order) with the same or different text; else in D2R only; else in vanilla or D2R under a different letter case; else new. For duplicate keys the first entry was used.

"Lands in" for a key D2R already has is the D2R file that holds it. For a new key it is the D2R file that most often holds the keys named by the same table column (for example `MonStats.NameStr` keys sit in `monsters.json` in D2R, so new `NameStr` keys land there).

## 2. What PD2 ships

| Item | Finding |
|---|---|
| String tables in `pd2data.mpq` | `data\local\LNG\ENG\patchstring.tbl` only. No `string.tbl`, no `expansionstring.tbl`, no other language folder. No string table in `pd2assets.mpq`, `pd2maps.mpq` or `pd2monchars.mpq` either |
| PD2 `patchstring.tbl` | 3,278 entries, 3,152 distinct keys. 13 keys repeat; 5 of those repeat with different text (`x`, `X`, `strModEnhancedDamage`, `StrSklTabItem6`, `merca239`). Longest string 884 characters, up to 23 lines |
| Vanilla 1.13c | `string.tbl` 5,391 entries (5,099 keys), `expansionstring.tbl` 2,818 (2,788), `patchstring.tbl` 1,179 (1,062); 8,683 distinct keys together |
| Vanilla patch table in PD2 | All 1,062 vanilla patch keys are in PD2's table. PD2's table is the vanilla one with entries added |
| PD2's own `patch_d2.mpq` | Its `patchstring.tbl` has the same 1,062 keys and texts as the classic install's, except the filler key `x` (10 fewer duplicate entries) |
| Loose copies | `data-S9`, `data-latest`, `data-patch4`, `data-s11-patch03`, `data-betapatch2` and `Beta\` hold older, smaller `patchstring.tbl` files (165-166 KB). They are not the current table and were not analysed |

## 3. Added and changed keys, and where they land

| Group | Keys | Notes |
|---|---|---|
| New (in no vanilla table, not in D2R) | 1,934 | Section 3.1 |
| Vanilla key, text changed | 79 | 7 from patch, 31 from string, 41 from expansion. 34 change only colour codes |
| Vanilla key, same text | 1,131 | 1,055 patch, 60 string, 16 expansion. Nothing to ship for these |
| In D2R only (D2R-only content) | 4 | Section 4 |
| Differs only by letter case from a vanilla or D2R key | 4 | Section 4 |

**Changed keys by D2R file** (all 79 exist in D2R): item-runes 35, ui 20, skills 13, item-names 6, item-nameaffixes 1, levels 1, item-modifiers 1, mercenaries 1, chinese-overlay 1. The runes are `r01`-`r33` (orange colour added). The ui ones are Battle.net, ladder and guild labels (`CapsBnet9`-`15`, `CapsGuildName`, `GuildText1`, `Ladder3`) and guild object names (`Guild Vault`, `Trophy Case`). The skill ones are a few descriptions (`skillld78`, `skillxld42`, `StrSkill110`).

### 3.1 New keys by D2R file

| Lands in | New keys | Main referencing columns |
|---|---|---|
| skills | 394 | `Skilldesc` `str name/short/long/alt`, `desctext*`, `dsc2text*`, `dsc3text*` |
| item-modifiers | 268 | `ItemStatCost` `descstrneg`, `descstr2`, `dgrpstr2`; `Misc`/`Weapons` `spelldescstr` |
| item-names | 237 | `UniqueItems.index`, `Misc` codes, `Weapons.namestr` |
| item-nameaffixes | 188 | `MagicPrefix.Name`, `MagicSuffix.Name` |
| monsters | 127 | `MonStats.NameStr`, `DescStr` |
| levels | 99 | `Levels.LevelName`, `LevelWarp` |
| item-runes | 23 | `Runes.Name` |
| objects 13, ui 4, mercenaries 2, npcs 1 | 20 | `Objects.Name`, `PetType.name`, `Hireling` |
| Named only in a comment column | 2 | `CubeMain.description` |
| Named by `ProjectDiablo.dll` (no table) | 320 | 283 as literal key names, 24 through key templates (`BHNotifyLevel%d`, `CreateGameGS%d`, `strGuildTierRewards%d`), 13 by literal prefix. Guild, stash tabs, Battle.net, map events, controller help |
| No reference found | 256 | Hotkey labels (`HOTKEY_*`, 61), stash categories, merc labels (`MercX*`), help pages (`NewHelp*`), PvP modifier text, a few skill and stat strings. They may be built by code in a way strings don't show, or be unused |

The 1,358 table-referenced keys are data. The 576 others are UI text that a plugin or a D2R UI layout would show.

PD2 table cells also name 33 `LevelWarp` keys, 41 object names and a few others that exist in neither PD2's table nor D2R. Classic shows such keys as raw text; D2R would too. They need a decision, not a key.

## 4. Collisions with D2R keys

| Key | D2R file and ID | Finding |
|---|---|---|
| `Runeword171` | item-runes, 27360 | Real collision. D2R uses it for one of its own runewords (D2R's `Runeword` keys run to 182); PD2 uses it for a different runeword name |
| `Large` | ui, 23968 | Same text both sides |
| `Virulent`, `of Conductivity` | item-nameaffixes, 27875, 27882 | Same text both sides |
| `of Darkness`, `of Searing`, `of Disease` | vanilla and D2R have `Of ...` (item-nameaffixes) | PD2 adds a lower-case key beside the vanilla one |
| `StrLevels` | D2R has `strLevels` (ui) | Not in vanilla; D2R added the other case |

No Warlock or Reign of the Warlock key collides by name. If D2R matches keys without regard to case, the four case-only keys override D2R text instead of adding new keys; I did not establish which.

One more table-side collision: PD2's `ItemTypes.ItemType` uses `Map`, which is a D2R-only key. That column is not shown to players, so it is minor.

## 5. ID budget

Rules from `strings.md`: IDs are matched by their low 16 bits, start at `next_string_id.txt` (28,106 in 93847), stay below 65,536, skip used low-16 values.

| Item | Value |
|---|---|
| Free IDs 28,106-65,535 (after skipping low-16 values used by D2R's `strings\` and `strings-legacy\`) | 37,429 |
| New keys | 1,934 (1,938 if the four case-only keys count as new) |
| IDs used, if numbered natively | 28,106 to about 30,043 |
| Left over | about 35,490 |

The table string pool (two-byte, 65,535) also holds: D2R has 12,245 strings, so adding about 1,940 is no problem.

## 6. Markup

| Item | PD2 | D2R 93847 enUS | Conversion |
|---|---|---|---|
| Encoding | UTF-8 in the `.tbl` | UTF-8 JSON with BOM | None beyond re-encoding |
| Colour codes | `ÿc` + code in 311 strings (272 new). Codes `0 1 2 3 4 5 8 9` plus `;` (6), `/` (3), `:` (1) | `ÿc` in 35 strings, codes `0-5 7-9 M U` | Same form. `;`, `/`, `:` are unused in D2R's English text. The toolkit notes say `ÿc;` is purple in D2R (`d2r-93847-addresses.md`); `/` and `:` are not verified |
| Newlines | 242 strings multi-line (216 new), up to 23 lines | Multi-line in 705 strings | See below |
| Line order | Classic stacks multi-line text bottom-up | D2R reversed every multi-line vanilla skill text it kept: 156 of 187 are the exact vanilla lines in reverse, the rest were also reworded. Gem texts kept their order (6 of 7). Item-modifier and NPC voice text became single-line | Flip lines in PD2's skill descriptions; keep item text order. 47 of the new multi-line keys are `SpellDescStr*` item text |
| Placeholders | `%d` 91, `%s` 37, `%%` 27, `%S` 5 | `%d`, `%s`, `%+d`, `%%`, `%u`, `%i`, `%02d`, `%.2f` | `%S` (wide string) is in 5 new guild and stats-command strings used by the DLL; it belongs with the plugin that formats them |
| Placeholder order | None of the 79 changed keys changes its placeholder list against vanilla | 91 PD2 keys differ from D2R's list because D2R changed the vanilla text (60 have fewer, 28 differ only in `%d` vs `%+d`, 3 differ in type) | None has more placeholders than D2R, so no crash case. Keep D2R's text for the 1,131 unchanged keys |
| PD2-only tokens | `<...>` (Battle.net help, inherited from vanilla), spacing placeholders in recipe help (`NewHelpRecipes*`) where icons are drawn | n/a | The recipe help assumes PD2's drawn icons; check before reuse |

## 7. Languages

English only. `pd2data.mpq` has only the `ENG` folder, the install's loose folders are only `ENG`, and no PD2 DLL names a language code or a language folder. D2R records carry 13 languages; PD2 gives text for `enUS` only.

## 8. Text built at run time by the DLLs

| DLL | Table keys it names | English literals (rough) | What they are |
|---|---|---|---|
| `ProjectDiablo.dll` | 324 (316 PD2-new, 8 vanilla such as `CapsBnet11`, `GuildText1`), plus 3 key templates | about 140 player-facing out of 418 text-like strings; 79 printf formats; 8 with colour codes | Zone group names for map messages (`the Cow Level`), hotkey load/save notices, act-access message, stats-command lines (`Player Kills: `), chat formats (`[Global] %s: %s`), guild and stash text |
| `BH.dll` | 1 | about 140 player-facing out of 471; 93 printf formats; 51 with colour codes | Stats panel lines with inline colours (`Gold Find:`, resist lines), skill tab names (`Amazon Skills`), loot-filter and config notices (`Copied item to clipboard`) |
| `PD2_EXT.dll` | 0 | none player-facing | Loader shim (as found in the DLL triage) |

These strings are hard-coded English with colour codes and printf formats. They belong to whichever plugin replaces the feature, not to the mod's string files. The "player-facing" counts are a filter over printable strings (drop library and error text), so treat them as estimates.

## 9. Facts the decision ticket needs

- Only one PD2 table to convert, English only; all 13 D2R language columns would carry English or be left out.
- About 1,358 new keys are data (named by tables), about 576 are UI text named by the DLL or by nothing found.
- Native loading means shipping complete copies of the D2R files touched: at least skills, item-modifiers, item-names, item-nameaffixes, monsters, levels, item-runes, objects, ui, mercenaries, npcs (and their `strings-legacy` twins where they exist). A D2RLoader namespace avoids that but needs `namespace:Key` in every table cell that names a new key.
- 79 overrides of vanilla keys, of which 34 are colour-only; `Runeword171` must be renamed or the D2R runeword loses its name.
- Skill descriptions need their line order flipped; item text does not.
- The ID budget is not a constraint.

## 10. Limits and open questions

- Whether D2R matches string keys case-sensitively (decides whether the 4 case-only keys add or override).
- Which entry wins for the 5 duplicate keys with different text, in classic and in D2R.
- Whether `ÿc:` renders in D2R. (`ÿc/` does not: D2R draws it as `*/`, so the strings rule drops a trailing one, #461.)
- The 256 unreferenced new keys: built by code or dead. Strings alone cannot tell; disassembly could.
- Whether the DLLs fetch strings by numeric ID (classic's other lookup path). Printable strings do not show numeric lookups.
- "Lands in" for new keys is a majority vote per column; a few columns had little D2R evidence (`Misc.spelldescstr` has 2 D2R examples).
- The classic install at `C:\Games\PD2` also holds `D2Sigma.dll` from another mod. `d2data.mpq` and `d2exp.mpq` are the base archives and its patch table matches PD2's copy, so I treat them as vanilla 1.13c, but I did not checksum them against a known release.
