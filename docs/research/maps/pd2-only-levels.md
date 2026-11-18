# PD2-only levels and their tiles

Answers research ticket #146 (map #143). Inputs #151 (the level pipeline). Only names, counts and sizes are recorded
here; no PD2 or Blizzard file is copied into the repo.

## Answer in brief

- **74 PD2-only levels**, ids 137 to 210 in PD2's `levels.txt` (every row whose name D2R's `levels.txt` lacks). All are
  preset levels (`DrlgType` 2), built from **144 distinct DS1 files** (145 preset slots; `lvlprest` File1-6 are
  random alternatives) of 162.6 MB, all at new paths in `pd2maps.mpq` (`expansion/map` 138, `guild/pd2` 5,
  `act1/tristram` 1).
- Every tile they load exists: **205 vanilla DT1** that D2R ships, **180 PD2-only DT1** in `pd2assets.mpq`, and 3 DT1
  that PD2 ships over vanilla paths (`act4/fort/foregate.dt1`, `expansion/town/waypt.dt1`, `act1/barracks/warp.dt1`).
  No referenced DT1 is missing.
- **D2R has no HD art per DT1.** HD terrain is drawn from one HD preset per DS1 (`data/hd/env/preset/<ds1 path>.json`
  with its own `terrain_lod0.model` and `terrain.physics`, 2,100 of them), plus per-DT1 blend masks in
  `data/hd/env/tilemasks/*.json` (263 of D2R's 266 DT1). **None of the 144 PD2 DS1 has an HD preset**, and none of the
  180 PD2-only DT1 has a tile mask. All 205 vanilla DT1 they load do have one.
- **Import:** each level can be imported as its DS1 for layout, collision and classic graphics. No DS1 needs a rebuild
  for that: every DS1 is version 18 and every DT1 it loads exists. Importing them still takes table and engine work,
  not art: level ids shift (D2R uses 137 for Colossal Summit), 14 new level types sit past D2R's automap limit of 36,
  56 levels name `monpreset` rows D2R lacks, and 40 levels hold negative object ids, which no D2R DS1 uses. For HD,
  **all 74 must get a new HD preset** (built from scratch or stood in), since no PD2 DS1 has the same tile layers as a
  D2R DS1 whose preset could be reused.
- **HD stand-ins:** 29 levels use vanilla tiles only (counting the 3 overrides as vanilla), so their stand-in can draw
  on D2R's own biome and tile masks. 45 levels use PD2-only tiles: **180 DT1 files in 27 tile sets need an HD
  stand-in** (sets: levels that share PD2-only DT1 files are joined into one set).

## Method and sources

- PD2 tables: `levels.txt`, `lvlprest.txt`, `lvltypes.txt`, `monpreset.txt`, `objects.txt` from the local extract of
  `pd2data.mpq` (`tools/pd2_extract.py`, git-ignored `pd2-data/`). DS1 and DT1 from the live `pd2maps.mpq` and
  `pd2assets.mpq` in `C:\Games\PD2\ProjectD2`, read in memory with `tools/mpq.py` (nothing extracted to disk).
- D2R: file list and file contents of the installed game's CASC storage, read with the toolkit's `modkit/casc.py`
  (175,823 names); tables from `d2rAI-toolkit/d2r-data/93847`.
- A level's tiles are its `lvltypes` row's `File 1-32`, filtered by the OR of its presets' `Dt1Mask` (bit n = File
  n+1). These are the DT1 the game loads for the level, so they are counted here; the tile list inside each DS1 was
  not used.
- DS1 parsing (version 18): header, tile layers (wall and orientation layers, floors, shadow, tag), then the object
  list (type, id, x, y, flags). Type 1 ids index `monpreset` rows of the DS1's act, type 2 ids index `objpreset` rows.
- "Same tile layers" compares a SHA-1 of the tile layer block plus the size against all 2,277 D2R DS1. No PD2 DS1
  matched, neither the whole file nor the layers alone.
- The tile-set count joins levels that share any PD2-only DT1 (union-find over levels).
- Rules on tiles and level types: `d2rAI-toolkit/knowledge/engine-limits.md` (level types fixed at 36 for the automap;
  classic tiles load in HD too), `tables.md` (`Layer` < 100, portal ids <= 255), `debugging.md`.

## D2R's HD terrain model

| Layer | Path | Keyed by | Count in D2R | PD2-only levels |
|---|---|---|---|---|
| Classic tiles | `data/global/tiles/**/*.dt1`, `*.ds1` | path | 266 DT1, 2,277 DS1 | needed, all present (D2R or `pd2assets`) |
| HD preset scene | `data/hd/env/preset/<ds1 path>.json` + `<ds1 path>/terrain_lod0.model`, `terrain.physics` | DS1 path | 2,100 json | **none** of the 144 DS1 |
| Biome (terrain texture layers, lighting, decorators) | `data/hd/env/biome/*.json`, named by each preset's `biomeFilename` | biome name | 58 | reusable: chosen per preset |
| Tile masks (terrain blend per DT1 tile sequence and style) | `data/hd/env/tilemasks/*.json` (`path_masks`, `wall_masks`, `filters`) | DT1 path | 263 DT1 | 205 vanilla DT1 named, 0 PD2-only |
| Special tiles | `data/hd/env/special/<act>/<type>/*.json` | tile | 51 | n/a |

Every D2R DS1 that a level uses has an HD preset. The 236 D2R DS1 without one belong to `lvlprest` rows with
`LevelId` 0 or are `lvlsub` rule files. **What D2R draws in HD for a level whose DS1 has no HD preset is not known**
(not tested here; see open questions). It decides whether "import the DS1, stand-in later" is playable in HD at all.

## The 74 levels

Columns: level type (`lvltypes` id; "new" = a row D2R lacks); DS1 count and names (all under
`data/global/tiles/expansion/map/` except Guilds `guild/pd2/` and 185 `act1/tristram/`); size in tiles; DT1 loaded,
vanilla / PD2-only; DS1 object findings (below); the HD stand-in each needs.

| Id | Level | Type | DS1 | Size | DT1 vanilla / PD2 | DS1 objects needing table work | HD stand-in |
|---|---|---|---|---|---|---|---|
| 137 | UberDiabloLvl | 26 | 1: uberdiablolvl.ds1 | 48x24 | 6 / 0 (+act4/fort/foregate.dt1 override) | monpreset past D2R | from D2R tiles |
| 138 | Mesa | 27 | 6: mesa1-6.ds1 | 151x151 | 9 / 0 | monpreset past D2R | from D2R tiles |
| 139 | Siege Map | 30 | 2: siege1, siege2.ds1 | 200x200 | 11 / 0 | neg. object ids, act 5 monpreset 51+ | from D2R tiles |
| 140 | Palace | 15 | 6: palace1-6.ds1 | 151x151 | 3 / 0 | - | from D2R tiles |
| 141 | Sewers | 25 | 3: sewermap1-3.ds1 | 180x180 | 3 / 0 | neg. object ids, monpreset past D2R | from D2R tiles |
| 142 | Arcane | 19 | 2: arcanemap, arcanemap2.ds1 | 180x180 | 2 / 0 | monpreset past D2R | from D2R tiles |
| 143 | Kurast | 22 | 3: kurastopenwide, 2, 3.ds1 | 151x151 | 14 / 0 | monpreset past D2R | from D2R tiles |
| 144 | River Of Blood | 22 | 4: riverofblood, 2-4.ds1 | 241x241 | 4 / 0 | neg. object ids, monpreset past D2R | from D2R tiles |
| 145 | Lava | 28 | 3: lava1, lava3, lava5.ds1 | 250x250 | 11 / 0 | monpreset past D2R | from D2R tiles |
| 146 | Ice Map | 33 | 3: frozenriver1-3.ds1 | 200x200 | 1 / 0 | neg. object ids, act 5 monpreset 51+ | from D2R tiles |
| 147 | Desert Map | 13 | 1: market2.ds1 | 250x250 | 18 / 0 | monpreset past D2R | from D2R tiles |
| 148 | Jungle Map | 21 | 3: jungle1-3.ds1 | 251x251 | 9 / 0 | monpreset past D2R | from D2R tiles |
| 149 | Fortress Map | 30 | 3: fortress1-3.ds1 | 181x106 | 26 / 0 (+expansion/town/waypt.dt1 override) | neg. object ids, act 5 monpreset 51+ | from D2R tiles |
| 150 | Throne Map | 34 | 3: throne1-3.ds1 | 150x150 | 5 / 1 | neg. object ids, act 5 monpreset 51+ | PD2 tiles |
| 151 | Tomb Map | 17 | 4: tomb1-4.ds1 | 250x250 | 11 / 0 | monpreset past D2R | from D2R tiles |
| 152 | Monastery Map | 4 | 1: monastery.ds1 | 191x241 | 15 / 6 | objpreset past D2R, monpreset past D2R | PD2 tiles |
| 153 | Monastery Map Basement | 10 | 1: monasterybase.ds1 | 23x32 | 4 / 0 | monpreset past D2R | from D2R tiles |
| 154 | Graveyard Map | 2 | 3: tristmap, tristmapv2, v3.ds1 | 191x241 | 27 / 0 | monpreset past D2R | from D2R tiles |
| 155 | Caldeum Map | 12 | 3: caldeum, 2, 3.ds1 | 180x160 | 13 / 2 | neg. object ids, monpreset past D2R | PD2 tiles |
| 156 | Pandemonium Map | 27 | 3: pandemonium, 2, 3.ds1 | 180x180 | 12 / 0 (+act4/fort/foregate.dt1 override) | monpreset past D2R | from D2R tiles |
| 157 | Pvp Map | 12 | 1: pvpmap_palace.ds1 | 180x160 | 13 / 2 | neg. object ids | PD2 tiles |
| 158 | Spider Map | 24 | 3: spider_dungeon1-3.ds1 | 250x250 | 1 / 1 | monpreset past D2R | PD2 tiles |
| 159 | Pvp Map 2 | 2 | 1: pvpmap1.ds1 | 191x241 | 27 / 0 | objpreset past D2R, monpreset past D2R | from D2R tiles |
| 160 | Frozen Forest Map | 31 | 3: frozenforestv1-3.ds1 | 150x150 | 27 / 0 (+expansion/town/waypt.dt1 override) | act 5 monpreset 51+ | from D2R tiles |
| 161 | Necropolis Jungle | 24 | 1: necropolis_2.ds1 | 180x160 | 16 / 0 | monpreset past D2R | from D2R tiles |
| 162 | Necropolis Swamp | 24 | 1: necropolis_1.ds1 | 180x160 | 16 / 0 | - | from D2R tiles |
| 163 | Necropolis Void | 27 | 1: necropolis_3.ds1 | 151x151 | 12 / 1 (+act4/fort/foregate.dt1 override) | neg. object ids | PD2 tiles |
| 164 | Realm of Terror | 27 | 3: realmoftorment1-3.ds1 | 202x201 | 12 / 1 (+act4/fort/foregate.dt1 override) | neg. object ids, monpreset past D2R | PD2 tiles |
| 165 | Hole of Terror | 27 | 1: holeofterror.ds1 | 68x92 | 12 / 1 (+act4/fort/foregate.dt1 override) | monpreset past D2R | PD2 tiles |
| 166 | BR Arena | 2 | 1: brarena.ds1 | 191x241 | 27 / 0 | monpreset past D2R | from D2R tiles |
| 167 | Library Map | 10 | 3: library, library_v2, v3.ds1 | 125x125 | 18 / 0 | monpreset past D2R | from D2R tiles |
| 168 | Uber Ancients | 31 | 1: uberancients.ds1 | 21x29 | 5 / 0 | - | from D2R tiles |
| 169 | Westmarch Map | 36 (new) | 3 slots, 2 files: shadowsofwestmarchv1, v3.ds1 (v3 twice) | 160x150 | 20 / 0 (+expansion/town/waypt.dt1 override) | neg. object ids, act 5 monpreset 51+ | from D2R tiles |
| 170 | Crypts Map | 3 | 3: royalcrypts, v2, v3.ds1 | 150x150 | 14 / 0 | monpreset past D2R | from D2R tiles |
| 171 | Sanctuary Of Sin Map | 28 | 3: sanctuaryofsin, v2, v3.ds1 | 170x200 | 14 / 5 | neg. object ids, monpreset past D2R | PD2 tiles |
| 172 | Black Abyss Map | 28 | 1: blackabyss.ds1 | 150x150 | 14 / 5 | monpreset past D2R | PD2 tiles |
| 173 | Butcher Event Map | 22 | 1: butcher1.ds1 | 60x60 | 4 / 0 | - | from D2R tiles |
| 174 | Ruined Cistern Map | 13 | 3: ruinedcistern, v2, v3.ds1 | 150x190 | 9 / 2 | monpreset past D2R | PD2 tiles |
| 175 | Ashen Plains Map | 37 (new) | 3: ashenplains1-3.ds1 | 201x201 | 0 / 13 | monpreset past D2R | PD2 tiles |
| 176 | Zhar Library | 15 | 1: zharlibrary.ds1 | 50x50 | 2 / 1 | neg. object ids, monpreset past D2R | PD2 tiles |
| 177 | Zhar Rivers | 34 | 1: zharrivers.ds1 | 170x200 | 7 / 13 | neg. object ids, monpreset past D2R | PD2 tiles |
| 178 | Zhar Ice | 33 | 1: zharice.ds1 | 149x149 | 3 / 3 | neg. object ids, monpreset past D2R | PD2 tiles |
| 179 | Zhar Kurast | 22 | 1: zharkurast.ds1 | 150x150 | 20 / 1 | neg. object ids, monpreset past D2R | PD2 tiles |
| 180 | Outer Void Map | 27 | 1: voideventmap.ds1 | 201x201 | 12 / 1 (+act4/fort/foregate.dt1 override) | neg. object ids, monpreset past D2R | PD2 tiles |
| 181 | Hellcaves | 38 (new) | 1: hellcaves.ds1 | 151x151 | 12 / 3 | monpreset past D2R | PD2 tiles |
| 182 | Hellcave Fortress | 38 (new) | 1: hellcavefortress.ds1 | 151x76 | 12 / 3 | monpreset past D2R | PD2 tiles |
| 183 | Fallen Gardens | 34 | 1: fallengardens.ds1 | 151x151 | 2 / 5 | neg. object ids, monpreset past D2R | PD2 tiles |
| 184 | Diamond Gate | 27 | 1: diamondgates.ds1 | 76x76 | 2 / 2 (+act4/fort/foregate.dt1 override) | monpreset past D2R | PD2 tiles |
| 185 | PD2 Pandemonium Finale | 11 | 1: act1/tristram/pd2_ubertrist.ds1 | 44x49 | 6 / 0 | monpreset past D2R | from D2R tiles |
| 186 | Kanemith Outside | 39 (new) | 1: dakenshar.ds1 | 150x150 | 12 / 6 | neg. object ids, monpreset past D2R | PD2 tiles |
| 187 | Kanemith Dungeon | 39 (new) | 1: tombsofkanemith.ds1 | 150x150 | 3 / 1 | neg. object ids, monpreset past D2R | PD2 tiles |
| 188 | Lucion Arena | 38 (new) | 1: lucionarena.ds1 | 50x50 | 12 / 3 | monpreset past D2R | PD2 tiles |
| 189 | Lucion Vault | 22 | 1: lucionvault.ds1 | 61x61 | 20 / 2 | neg. object ids | PD2 tiles |
| 190 | Skovos Stronghold Map | 2 | 2: skovosstrongholdv1, v2.ds1 | 180x150 | 25 / 2 | monpreset past D2R | PD2 tiles |
| 191 | Demon Road Map | 40 (new) | 4: demonroad1-4.ds1 | 201x201 | 0 / 7 | neg. object ids, monpreset past D2R | PD2 tiles |
| 192 | Imperial Palace Map | 15 | 1: imperial_palace.ds1 | 150x150 | 2 / 4 | neg. object ids, monpreset past D2R | PD2 tiles |
| 193 | Halls of Torture Map | 42 (new) | 3: torturehalls, 2, 3.ds1 | 150x150 | 0 / 4 | neg. object ids, monpreset past D2R | PD2 tiles |
| 194 | Imperial Boss Map | 17 | 1: imperialpalacebossroom.ds1 | 26x26 | 8 / 0 | neg. object ids | from D2R tiles |
| 195 | Ureh City Map | 22 | 1: urehcity.ds1 | 175x175 | 8 / 3 | neg. object ids, monpreset past D2R | PD2 tiles |
| 196 | Nemyr Monastery Map | 22 | 1: nemyrmonastery.ds1 | 33x33 | 4 / 1 | neg. object ids, monpreset past D2R | PD2 tiles |
| 197 | Djinns Domain Map | 43 (new) | 1: djinnsdomain.ds1 | 125x125 | 10 / 3 | neg. object ids | PD2 tiles |
| 198 | Djinns Domain Red Map | 43 (new) | 1: djinnsdomainred.ds1 | 125x125 | 10 / 3 | neg. object ids, monpreset past D2R | PD2 tiles |
| 199 | Djinns Domain Boss Map | 43 (new) | 1: djinnsdomainboss.ds1 | 20x20 | 10 / 3 | neg. object ids, monpreset past D2R | PD2 tiles |
| 200 | Na-Krul's Abyss Map | 44 (new) | 2: nakrulabyss1, 2.ds1 | 150x150 | 0 / 3 | neg. object ids, monpreset past D2R | PD2 tiles |
| 201 | Kyovoshad Map | 45 (new) | 2: kyovoshad1, 2.ds1 | 201x201 | 1 / 9 | neg. object ids, monpreset past D2R | PD2 tiles |
| 202 | Guild 1 | 46 (new) | 1: guild1.ds1 | 40x40 | 0 / 9 | neg. object ids | PD2 tiles |
| 203 | Guild 2 | 46 (new) | 1: guild2.ds1 | 50x50 | 0 / 10 | neg. object ids, objpreset past D2R | PD2 tiles |
| 204 | Guild 3 | 46 (new) | 1: guild3.ds1 | 55x52 | 0 / 12 | neg. object ids, objpreset past D2R | PD2 tiles |
| 205 | Guild 4 | 50 (new) | 1: guild4.ds1 | 55x55 | 0 / 14 (+act1/barracks/warp.dt1 override) | neg. object ids, objpreset and monpreset past D2R | PD2 tiles |
| 206 | Guild 5 | 50 (new) | 1: guild5.ds1 | 60x68 | 0 / 17 (+act1/barracks/warp.dt1 override) | neg. object ids, objpreset and monpreset past D2R | PD2 tiles |
| 207 | Tomb Event Map | 18 | 1: bitterdepths.ds1 | 101x51 | 9 / 2 | objpreset past D2R, monpreset past D2R | PD2 tiles |
| 208 | Poisoned Well Map | 47 (new) | 3: poisonedwell, v2, v3.ds1 | 150x150 | 0 / 9 | neg. object ids, monpreset past D2R | PD2 tiles |
| 209 | Dark Temple Map | 48 (new) | 3: darktemple, v2, v3.ds1 | 175x175 | 0 / 22 | neg. object ids, monpreset past D2R | PD2 tiles |
| 210 | Descent Map | 49 (new) | 4: descend, descend_2-4.ds1 | 150x150 | 0 / 28 | neg. object ids, monpreset past D2R | PD2 tiles |

Which of these are map areas, uber areas, events or multiplayer areas is read off the names only. PvP Map, Pvp Map 2,
BR Arena and Guild 1-5 (157, 159, 166, 202-206) look multiplayer-only; whether they are cut is #151's or the map's
call. #147 inventories the uber bosses and their areas.

## Tile sets needing an HD stand-in

27 sets, 180 PD2-only DT1 files (paths under `data/global/tiles/pd2assets/`):

| Levels | DT1 | Files |
|---|---|---|
| Throne Map (150), Zhar Rivers (177), Kanemith Outside (186), Kanemith Dungeon (187), Skovos Stronghold (190) | 18 | a1_ruinwall, a5_a1barracks_objects, a5_a1grass_objects, a5_floor, a5_huts, a5_market, a5_palm, a5_prickly, a5_river, a5_rogue_encampment_objects, a5_rotatedriver, a5_shack, a5_stones, a5_trees, kanemith, new_ground, triune_floor, xxthrone |
| Monastery Map (152) | 6 | archwallalt, barflooralt, fac5pd2, flooralt, pboxwallalt, stainedalt |
| Caldeum Map (155), Pvp Map (157) | 2 | aer_canal, aer_palace |
| Spider Map (158) | 1 | lair_mishift |
| Necropolis Void (163), Outer Void (180), Hellcaves (181), Hellcave Fortress (182), Lucion Arena (188) | 3 | hellcaves_base, hellcaves_obj, void |
| Realm of Terror (164), Hole of Terror (165), Diamond Gate (184) | 2 | diamondgates, flip |
| Sanctuary Of Sin (171), Black Abyss (172) | 5 | a4_throne, a4_underflr, a4_worldstone, new_floornew, rotatedthronearch |
| Ruined Cistern (174) | 2 | new_chamb, oasiswalkable |
| Ashen Plains (175) | 13 | cairnstones2, final_cliff1, final_cliff2, final_corner, final_fallen, final_floor, final_objects, final_rocks1, final_shrub, final_stonewall, final_treegroups, final_trees, final_walls |
| Zhar Library (176) | 1 | sanctuaryyellow |
| Zhar Ice (178) | 3 | a5_cliff_parts, sanctuaryadds, sanctuaryreindexed |
| Zhar Kurast (179) | 1 | zharboat |
| Fallen Gardens (183) | 5 | fallengardenfloor, -stairs, -trees, -wall, -water |
| Lucion Vault (189) | 2 | a3_altar, a3_objects |
| Demon Road (191) | 7 | a5_chain_walls, a5_head, borq, borq1, borq2, borq4, borq_floor |
| Imperial Palace Map (192) | 4 | cellar_white, cellflr_white, palace_white, wall_white |
| Halls of Torture (193) | 4 | torturehalls/file1, file2, file3, trees |
| Ureh City (195) | 3 | ext, recoloured_tombcor, rotated_ext |
| Nemyr Monastery (196) | 1 | nemyrmonastery |
| Djinns Domain, Red, Boss (197-199) | 3 | aer_palace2, djinnfloor, djinnfloorbloody |
| Na-Krul's Abyss (200) | 3 | nakrulabyss/l5_addon, l5_floor, l5_walls |
| Kyovoshad (201) | 9 | s13/boxes, brick_wall_floor, brick_walls, building_floor, buildings, inv_wall, mesafloor, outwall, surf_wall |
| Guild 1-5 (202-206) | 21 | guilds/act3_kurast_huts, altar, cliff, cottwalls, courtyard, cust_int, floor, fortress, house1_ext, house1_int, house2_ext, house2_int, house2_roof, props, river_bridge, stones, stonewall, tavern, treegroups, trees, woodfloor |
| Tomb Event Map (207) | 2 | tombs/centre-piece, things |
| Poisoned Well (208) | 9 | psnwell/used/a3houses, bridgefill, den, psndetail, rivbank, sewerchamb, seweritems, sewerradament, sewerwalls |
| Dark Temple (209) | 22 | dtprivate/archwall, bone, desertground, floor, ground, negradient, nwgradient, oasis, outwall, palm, pboxwall, pentagram, rock, segradient, swgradient, throne, underflr, underwall, village, walls, worldstone, worldstonegray |
| Descent (210) | 28 | descend/a4_throne, a5_rotatedriver, basewall, bridge, camp, change-me, cliff1, cliff2, corner, entrance, extwalls, fence, file2, floor, hellcaves_base, hellgate, new_floors, new_kanemith, objects, objects_outdoors, pond, river, stone_fence, stonewall, torture, town_reindexed, treegroups, trees |

Without the multiplayer-looking levels, 26 sets and 159 files remain (the Guilds' set drops; Caldeum keeps `aer_*`). Many PD2 DT1 are
recolours or rotations of vanilla art (`*alt`, `*_white`, `recoloured_*`, `rotated*`, `*reindexed`, `a5_*` copies of
act 1 to 3 tiles) going by their names, so a D2R biome can likely stand in for several sets. That was not checked
tile by tile.

The 3 vanilla-path overrides (`foregate`, `waypt`, `warp`) also change vanilla areas if shipped as they are. 12
PD2-only levels load them.

## DS1 contents that need table or engine work

These decide whether a DS1 imports "as is". None of them is about art.

| Finding | Levels | Detail |
|---|---|---|
| Level ids shift | all 74 | D2R uses id 137 for Colossal Summit; PD2's 137-210 must move to 138 or later. Ids stay <= 255 and every `Layer` is < 100 (87 to 99, one 25) |
| New level types 36-50 | 22 levels on 14 types (36-40, 42-50) | Past the automap's fixed 36 types (`engine-limits.md`: hook `0x32bdf0`). D2R's `lvltypes` is kept today (unported row "PD2 level types table") and must take PD2's rows. 17 vanilla type rows also name DT1 that PD2 ships (new or override) |
| Negative object ids (type 2) | 40 | 194 distinct ids from -7 to -600. No D2R DS1 uses one. The names line up with `objects.txt` rows (-130 Well, -600 ZharTomeObject, -565 strlastcinematic), so it is likely a PD2 DLL convention, "negative id = objects.txt row". Not yet confirmed in `ProjectDiablo.dll` |
| `objpreset` ids past D2R's rows | 7 (152, 159, 203-207) | act 1 ids 139-141, act 2 773, act 5 490 and 780 |
| `monpreset` ids past D2R's rows | 56 | D2R has 47/59/39/28/57 rows per act, PD2 60/80/60/53/71. PD2's acts 1-4 extend D2R's in place; act 3 id 168 and act 5 id 147 are past even PD2's rows |
| Act 5 `monpreset` rows 51+ differ | 6 (139, 146, 149, 150, 160, 169) | PD2's act 5 rows from 51 are its own; D2R's 51-56 are other monsters (matches the unported row on `monpreset`) |

## Open questions

- **What D2R draws in HD for a DS1 without an HD preset** (black floor, nothing, a crash, or the biome's terrain
  alone). Test with one small level, for example Uber Ancients (168, 21x29, vanilla tiles), in HD and in classic mode.
  The answer decides whether an imported DS1 is playable before its HD stand-in exists.
- Whether an HD preset can be written for a new DS1 without Blizzard's tools: copying a D2R preset json gives the
  wrong layout, and `terrain_lod0.model` is baked per preset. D2R Reimagined (#145) may show how it adds areas.
- The negative object id convention: confirm in `ProjectDiablo.dll` (Ghidra, `PD2.gpr`) how the DS1 loader resolves
  them, and the monpreset ids past PD2's own rows.
- `pd2maps.mpq` also holds 192 new DS1 paths that no PD2-only level uses (165 `act2/arcane`, 23 `expansion/map`, 2
  `guild/pd2`, 1 `act4/mesa`, 1 named by a `LevelId` 0 preset) and 117 DS1 over vanilla paths (113 named by `LevelId`
  0 presets, 4 by Act 2 Town, Mephisto 3, Act 5 Town). They concern vanilla areas, not this ticket.
