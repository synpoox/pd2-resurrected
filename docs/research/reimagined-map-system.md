# D2R Reimagined's map system

Research for issue #145 (map #143). Question: how D2R Reimagined builds its map system on D2R, under which licence,
which parts we could copy as they are, and which D2R or D2RLoader limits they hit and worked around.

Read on 2026-11-04. Nothing was built or run; sources were read as text only.

## Sources

| Tag | Source | Revision |
|---|---|---|
| MOD | https://github.com/D2R-Reimagined/d2r-reimagined-mod, branch `main` (released mod 3.1.0) | `e86baa2` |
| MAPS | Same repo, branch `feature/maps` (unreleased map system, mod 3.1.1) | `404dee9` |
| MAPS-README | `scripts/maps/README.md` on MAPS | `404dee9` |
| MAPS-CFG | `scripts/maps/maps_config.py`, `generate_maps.py`, `exterior_probe.py`, `boss_rooms.py` on MAPS | `404dee9` |
| LAB-NOTES | `lab-research-notes.md` on MAPS (their Forsaken Labyrinth debugging notes, 2026-10-15/16) | `404dee9` |
| WIKI | https://github.com/D2R-Reimagined/wiki-content, `maps/*.md`, `forsaken-labryinth.md` | HEAD 2026-11-04 |
| LAN | https://github.com/D2R-Reimagined/d2r-reimagined-LAN (unofficial D2RLoader repack) | `5d4e119f` |
| SITE | https://github.com/D2R-Reimagined/d2r-reimagined-website, `src/lib/server-builds.spec.ts` | HEAD 2026-11-04 |
| ORG | `gh repo list D2R-Reimagined` (licences as GitHub detects them) | 2026-11-04 |

## Answer in short

- Reimagined has two endgame area systems. The **Forsaken Labyrinth** (released) is pure data: 28 fixed levels and a
  reusable cube token. **Maps** (`feature/maps`, unreleased) are 30 map items (10 themes x 6 tiers) opened in the
  cube, 120 generated levels, and a closed-source D2RLoader plugin `d2rl-maps.dll` for rolled modifiers, events,
  treasure monsters and level fixes.
- Opening an area needs no plugin: a cube recipe with output `"Red Portal,lvl=<level id>"` opens a red portal to any
  level. This corrects our capability matrix, which says the cube portal output opens only the Cow Level (that is
  true of the plain `Cow Portal` output only).
- Licence: the mod repo is **GPL-3.0** (`LICENSE.txt`, the GNU GPL v3 text; GitHub detects `gpl-3.0`). The map
  plugin source is **not public**. The LAN repack ships plugin DLLs with no source and no licence.
- Copyable as is: the Python generator and its validators (`scripts/maps/`), the custom-arena builder, and the
  engine knowledge in their notes. Their table rows and maps are their own design, not PD2's, so they are templates
  for PD2 parity rather than parity data. Copying any GPL-3.0 file binds our release to GPL-3.0 (see Licence).

## 1. Forsaken Labyrinth (released, data only)

| Part | How | Source |
|---|---|---|
| Levels | 28 rows, ids 138-165, all `Act=4` (Act V), chained by `Vis`/`Warp`. Mix of maze (`DrlgType 1`) and preset (`DrlgType 2`) levels reusing stock tilesets; Hell `MonLvlEx` 100-110 | MOD `levels.txt` |
| Layouts | New DS1s in `data/global/tiles/labyrinth/` with HD scenes in `data/hd/env/preset/labyrinth/`; `lvlmaze` rows for the maze levels | MOD |
| Entry | Misc item `lab` (Forsaken Token). Cube recipe `input 1=lab`, `output="Red Portal,lvl=138,qty=1"`, `output b=lab` (token comes back), `min diff=2` (Hell) | MOD `misc.txt`, `cubemain.txt` |
| Waypoint | `actinfo` Act 5 `waypoint9=Forsaken Labyrinth 00`; the level row sets `Waypoint=40` (Act V stock uses 30-38) | MOD `actinfo.txt`, `levels.txt` |
| Warps | New lvlwarp 83 "Act 5 Town to Forsaken Labyrinth" reused for many exits | MOD `lvlwarp.txt` |
| Other portals | Colossal Summit (level 137) by the same `Red Portal,lvl=137` output from five uber materials | MOD `cubemain.txt` |
| Rewards | Token from Hell Baal / Hell Corpsefire; six new runes, a relic charm | WIKI `forsaken-labryinth.md` |

Every table change is mirrored into `data/global/excel/base/` (the other table bank) by
`data/global/excel/a_copy_excel_base.bat`, except `treasureclassex.txt`.

## 2. Maps (`feature/maps`, unreleased)

### Player-facing design (WIKI `maps/*.md`)

- Hell only; opened in Harrogath by putting one map alone in the cube. Tiers 1-6, area levels 100-105.
- 10 themes, each with a main body and a separate Warden arena (a superunique boss).
- Currencies: Horadric Orb (tier up to 5), Arcane Relic (reroll), Worldstone Shard (T5 to T6). The generator's
  comments use other names (Sextant, Prism, Astrolabe, Orb of Infusion for magic to rare), so naming is still moving.
- Modifiers 1-4 by tier: density/elite, player penalties (-res, -skills, -physical damage), monster auras, pack
  replacement, elemental pacts, reward bonuses; a combat Magic Find bonus up to +250%.
- Events (30% at defaults): Treasure Raider, Dark Challenger (sigil), Sealed Reliquary (three waves). Treasure
  monsters (8%): Jewel Hoarder, Amulet Collector.
- Each theme+tier can run once per game.

### How it is built

| Layer | What | Source |
|---|---|---|
| Single source | `maps_config.py` holds every theme, tier, affix, Warden and layout; `generate_maps.py` rewrites tagged blocks (`RMAP`, `rmap`, `RMap`, `rmapaffix`) in `levels`, `lvlmaze`, `lvlprest`, `monlvl`, `itemtypes`, `itemuicategories`, `misc`, `magicprefix`/`magicsuffix`, `cubemain`, strings, `treasureclassex`, monster rows, in both banks. `--check` diffs without writing. Python unit tests in `test_*.py` | MAPS-README, MAPS-CFG |
| Plugin header | The generator also writes `map_tables.gen.h` into a sibling `d2rl-plugins/plugins/maps/src/` checkout: map level ids, theme/tier, base density, world rectangles, Warden monprops, and calibration values the plugin uses to find fields in D2R's compiled Levels row at run time | MAPS-CFG `gen_plugin_header` |
| Levels | 120 rows, ids 166-285 (body + arena per theme and tier), all `Act=4`; level 286 is an Azmodan arena | MAPS `levels.txt` |
| Items | One misc item per theme+tier (`m<theme><tier>`, type `mapi`), plus `x*` variants; rare-capable like rings | MAPS `misc.txt`, MAPS-README |
| Open | Plain recipe per item: `output="Red Portal,lvl=<body id>,qty=1"`, `min diff=2` | MAPS `cubemain.txt` |
| Craft | 680 `rmap` cube rows: activate, upgrade, reroll, infuse, corrupt, all plain TXT that work without the plugin | MAPS-CFG `gen_cubemain` |
| Affixes | Map affix rows have `spawnable=0`, so offline D2R never rolls them; the plugin derives the roll from the item seed and applies it (rewrites the live Levels row for density/rarity; grants hostile auras for player penalties) | MAPS-CFG |
| Arenas | `arenas.py` builds each arena's DS1 and its HD scene JSON from one layout, from entities cloned from stock HD scenes; `boss_rooms.py` places Wardens; unmodified stock files it needs are committed under `scripts/maps/stock/` | MAPS-README "Custom arenas" |
| Plugin | `d2rl-maps.dll` (server build name per SITE). Rolled effects, events, treasure carriers, preset-construction hook, outdoor layout placement, reset of stored units in a map's world rectangle, "visited" protection. README reports a C++ suite (60,000 rolls, 128 tooltip capacities) but the source repo is not public | MAPS-README, SITE |

The static population, baseline Magic Find, drops and recipes work without the plugin; rolled effects need it
(MAPS-CFG docstring).

## 3. Limits they hit and how they got around them

| Limit | Workaround | Source |
|---|---|---|
| Red portals and town portals must stay within one act | Every Labyrinth and map level is `Act=4` (Act V) whatever its tileset, opened from Harrogath | MAPS-README "Themes and layouts" |
| Levels of one act must not overlap in world space; the server stores an unloaded room's units by world position, so overlapping tiers swapped Wardens | Each map level gets its own rectangle (`MAP_WORLD_ORIGIN` 400,4300; 220-tile body cells, 130-tile arena cells), checked by `check_world_layout`; positions stay well under 13,107 tiles (16-bit subtiles) | MAPS-CFG, MAPS-README |
| Automap `Layer` indexes a fixed 100-entry directory; 100+ reads past it and can hang on save | Layers stay < 100; bodies and arenas keep their template's layer (shared exploration accepted) | MAPS-CFG `AUTOMAP_LAYER_COUNT` |
| Level ids above 255 | Ids run to 286. The generator warns past 254 ("headroom is unverified"); `exterior_probe.py` exists to test "level IDs above 255" with a probe DLL; recipes already target `lvl=256`+ | MAPS-CFG |
| Act 5 preset slots are 8-bit | `boss_rooms.py` raises when slots pass 256 | MAPS-CFG |
| Some maze routines place their stairs-down room only for hardcoded level ids (Act 2 Tomb 55-58, Durance 100-101, sewers) | Bodies clone only tilesets that place stairs for any id (Catacombs, Maggot Lair, Flayer Dungeon, Swampy Pit, Ice Caves, Baal Temple) | MAPS-CFG theme docs |
| Abaddon's maze routine ignores `lvlmaze` Rooms | Infernal bodies use LevelType 28 (River of Flame); the plugin adds the stairs rooms | MAPS-CFG |
| A red portal lands on the first special room the maze routine assigned, often beside the exit | A self-linked `body_entry` warp per theme; a plugin preset-construction hook swaps rooms where needed; outdoor bodies get an `entry_preset` placed first | MAPS-README "Where the portal lands" |
| Outdoor bodies have no maze routine and were left unsized | The plugin writes `ExteriorLayouts` x/y; mis-placed layouts overlapped stock levels until 2026-10-17 | MAPS-README |
| A second way into an arena asserts `hDungeonRoomOther` in the client on mouse-over | Exactly one body exit per theme | MAPS-CFG |
| `minion1/minion2` never spawn for a DS1-preset monster; a skill-slot aura in mode `NU` does not activate | Wardens are superuniques with escorts via `MinGrp/MaxGrp` and auras/procs in a dedicated monprop row | MAPS-README |
| Travincal stairs are `NoInteract` unless an OpenWarp object sits over them | Keep the object as UnfoldingStairRight (objects 195), clickable without the Compelling Orb | MAPS-README |
| `Position=1` with no position-marker tile crashes on waypoint/teleport arrival | `Position=0` on new levels | LAB-NOTES section 2 |
| Exit markers stacked on a wall tile are not drawn on the automap; `wre`/`wle` rows must sit inside the level type's contiguous automap block | Put markers alone in their cell; add rows per level type | LAB-NOTES section 3 |
| Maze levels' stairs direction comes from the stock DS1's main index, not from `Warp` | Wire `Vis0`/`Vis1` as vanilla does | LAB-NOTES section 4 |
| `Portal=1` gives a bit in a 32-bit mask (1.10 asserts above 32) | Trim `Portal=1` rows (unverified on D2R) | LAB-NOTES section 2 |
| No "increased damage taken" or "reduced aura effect" stat | Negative `red-dmg%`; `-2 allskills` | MAPS-CFG affixes |
| Town ids are hardcoded (1, 40, 75, 103, 109) | Rows are only appended; the probe refuses shifted towns | LAB-NOTES, MAPS-CFG `validate_town_boundaries` |
| Plugin reads compiled Levels fields with no SDK accessor | Generated calibration values locate the fields in the row at run time | MAPS-CFG `gen_plugin_header` |

LAN (unofficial, by another author, D2RLoader 1.1.0 metadata) also ships a `RedPortalOriginTown.dll` whose config
maps a red-portal destination level to the act it may be opened from, and JSON byte patches (`op: nop`, `rva`,
`expected` bytes) for D2RLoader's patch loader. No source.

## 4. Licence

| Repo | Licence | Note |
|---|---|---|
| d2r-reimagined-mod | GPL-3.0 | `LICENSE.txt` is the unmodified GNU GPL v3 (29 June 2007). No per-file headers. README: "Everything we do ... is open source." `compatibility/unhoarder/` on MAPS carries its own GPL-3.0 `LICENSE` |
| wiki-content, website, launcher | none detected | Not needed |
| reimagined-level-editor, reimagined-d2r-mod-studio, doc-generator | MIT | Tools (DS1/level editing) |
| d2r-dotnet-tools, d2r-multi-export-tool | GPL-3.0 | Tools |
| d2r-reimagined-LAN, d2r-reimagined-online | none | Unofficial repacks; plugin DLLs without source |
| `d2rl-plugins` (map plugin) | not public | Not found on GitHub under the org or its committers (CollinHerber, Delegus, The-Honorable); D2RLoader/D2RL-Plugins is empty |

What GPL-3.0 means for us: anything copied from the mod repo keeps its licence and notices, and a release that
contains it must be offered under GPL-3.0 with its source (GPL-3.0 sections 4-6). Our repo has no licence file yet.
Copying is therefore an owner decision about the port's own licence, not only an attribution line in `third_party/`.
The tables, DS1s and HD scenes are derived from Blizzard's game data, as ours are; the GPL covers only Reimagined's
own contribution to them.

## 5. What we could copy as is

| Part | Copy as is? | Why |
|---|---|---|
| `Red Portal,lvl=<id>` cube recipe pattern | Yes (an engine fact, not code) | Opens any same-act level with data only; PD2's map opening can start without a plugin |
| `scripts/maps/generate_maps.py` structure: tagged blocks, both-bank write, `--check`, generated plugin header | Yes, then retarget at PD2's map list | GPL-3.0; idempotent generator that keeps TXT and plugin tables in step |
| `arenas.py` (DS1 + HD scene from one layout), `boss_rooms.py`, `build_sprites.py` | Yes, if we need new arenas | GPL-3.0; PD2 maps reuse PD2 levels, so likely only for rooms D2R lacks |
| World-rectangle allocation and `check_world_layout`, automap layer < 100, town-id and maze-routine checks | Yes | Engine limits that apply to any new Act V level |
| LAB-NOTES engine findings | As knowledge | Their own 1.10f (D2MOO) reading, tagged confirmed/inferred |
| Their map rows, themes, affixes, Wardens, currencies | No | Reimagined's design; PD2's maps, affixes and drops are the parity target |
| Map plugin | Not possible | Source not public; the README only describes what it does |
| LAN plugins and patches | No | No licence, no source, unofficial |

## 6. Open questions

- Whether D2R handles red portals to level ids above 255 from the recipe alone. Reimagined has not confirmed it
  (generator warning, probe DLL); our matrix says the portal setter clears the high byte.
- Whether a red portal opened from another act's town fails or misbehaves (LAN's origin-act plugin hints at a rule).
- Whether `Waypoint=40` (past stock 38) saves and shows correctly; Reimagined ships it on `main`, not checked here.
- The map plugin's hook list and D2RLoader version are not visible without its source.
