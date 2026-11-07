# PD2 art, audio and map asset inventory

Answers map issue [#1](https://github.com/synpoox/pd2-resurrected/issues/1), research ticket [#5](https://github.com/synpoox/pd2-resurrected/issues/5). Only counts, formats, sizes and names are recorded here. No PD2 or Blizzard
asset is copied into this repo.

## Answer in brief

- PD2's own non-table content is small except for two archives: **`pd2monchars.mpq` (373 MB) is not monsters**. It holds
  14,421 DCC + 757 COF for the seven classic player classes, used by PD2's *Invader* monsters (player-shaped enemies).
  **`pd2maps.mpq`** is 453 DS1 files, of which 162 "expansion/map" files are 190 MB unpacked.
- Everything else is modest: 277 item DC6, 213 DT1 tilesets (98 MB packed / 183 MB unpacked), about 260 monster DCC, 37
  missile DCC, 40 overlay DCC, 92 object DCC, 46 WAV, about 190 UI DC6, 10 palettes.
- D2R still needs the classic formats for maps (DS1/DT1) and for timing (COF/DCC, `animdata.d2`), so **maps, tiles and
  COF/DCC port as-is**. What needs real conversion is the HD side: item pictures, skill icons and panels (to `.sprite`),
  and a model mapping for every new monster, missile, overlay and object (stand-ins first).
- Audio: only 46 WAV are PD2's own. D2R stores its sound effects as FLAC under `data/global/sfx`; the conversion path is
  not covered in `hd-graphics.md` and is an open item (see Open questions).

## Method and sources

- PD2 install read-only at `C:\Games\PD2\ProjectD2` (live folder; `Live\` holds copies of the same size). Archives were **listed, not unpacked**: a small script read each MPQ's header, hash and block tables and its
  `(listfile)` to get names, packed size and unpacked size per file. Nothing was extracted.
- "New vs vanilla" compares each PD2 path against the file list of a retail D2R install's CASC storage (names only,
  35,003 files under `data/global`, read through the toolkit's `modkit/casc.py`). A PD2 path absent from D2R's legacy
  tree is called **new**; a path present is an **override** (PD2 changed a vanilla file). Monster, item, missile,
  overlay and object checks also use the table snapshot in `d2rAI-toolkit/pd2-data/s14-alliance` against D2R's tables.
- Caveat: the s14 table snapshot is the toolkit's older snapshot. Archive sizes in `s14-alliance` equal the live
  archives, but its MonStats has no row for code `87` while the live `pd2assets.mpq` ships 129 files for it, so table
  based "new" counts are lower bounds. Path based counts come from the live archives and are exact.
- Conversion rules come from `d2rAI-toolkit/knowledge/hd-graphics.md`.

## Archives

| Archive | Size | Files | Contents |
|---|---|---|---|
| `pd2assets.mpq` | 143.9 MB (243.5 MB unpacked) | 1,411 | items, monsters, objects, missiles, overlays, tiles (DT1), UI, WAV, palettes |
| `pd2monchars.mpq` | 373.6 MB (380.5 MB unpacked) | 15,178 | 14,421 DCC + 757 COF, seven player classes only (see below) |
| `pd2maps.mpq` | 5.2 MB (201.8 MB unpacked) | 453 | DS1 map files only |
| `pd2data.mpq` | 2.2 MB | 186 | 94 txt, 90 bin, 1 tbl (tables, covered by other tickets) |
| `patch_d2.mpq` | 2.1 MB | 209 | stock 1.14 patch content, not PD2 art |
| `d2gl.mpq`, `SGD2FreeRes.mpq`, `D2MultiRes.mpq`, `SlashDiabloHD.mpq` | 10.2 / 1.3 / 0.5 / 0.7 MB | n/a | renderer and resolution helpers, not game content; not listed |

Total game art in PD2's three asset archives: about 522 MB packed, about 826 MB unpacked, 17,042 files.
(A `Beta\` folder holds an older, smaller `pd2assets.mpq` of 72 MB; it is ignored.)

## Per category: counts, formats, new vs vanilla

Sizes are packed MB (unpacked in brackets) from the MPQ block tables.

| Category | Format | Count | Size | New vs vanilla (D2R legacy path) |
|---|---|---|---|---|
| Player class art (`pd2monchars`) | DCC + COF | 14,421 + 757 | 355.6 (361.7) + 0.1 | all new: none of these paths exist in D2R. Classes: barbarian 2,582, assassin 2,470, druid 2,214, paladin 1,963, amazon 1,855, sorceress 1,669, necromancer 1,668 DCC. No warlock |
| Monster art (`pd2assets`) | DCC + COF + DAT | 260 + 118 + 35 | 21.8 (23.1) | 14 monster codes new (see below); 15 codes override or extend vanilla folders |
| Objects | DCC + COF | 92 + 58 | 1.9 (2.1) | 12 new object folders, 6 existing; 84 DCC and 54 COF new |
| Missiles | DCC | 37 | 1.5 (1.6) | all 37 new; PD2 missiles.txt has 36 CelFile values not in D2R |
| Overlays | DCC | 40 | 1.4 (1.6) | all new; 29 new Filename values |
| Items (inventory pictures) | DC6 | 277 | 0.2 (0.3) | 276 new, 1 override. 219 distinct invfile names in PD2 tables are not in D2R, and all 219 have a shipped DC6; the other 385 invfiles are vanilla pictures |
| Tilesets | DT1 | 213 | 98.1 (183.0) | 195 under `tiles\PD2assets`, 13 `guild`, 2 `act2`, 3 override vanilla (act1, act4, expansion town) |
| Maps | DS1 | 453 | 5.0 (201.8) | 336 new paths, 117 override vanilla paths (see below) |
| UI | DC6 | 67 under `global\ui`, 155 under `local\ui\eng` | 5.6 + 0.5 | 190 new, 32 override vanilla; 26 panel, 17 spell sheets, compendium, controller, character-select, bigmenu |
| Sound effects | WAV | 46 | 3.3 (3.7) | all new: `pd2\dropsounds` 16, `pd2\zhar` 10, `d1roster\j6` 8, `pd2\nakrul` 5, `pd2\leoric` 3, one each holy sword parry, butcher, puzzlebox, barbarian leap land |
| Palettes | PL2 + DAT | 10 + 1 | 2.6 (4.2) | 5 act palettes are overrides, 5 `_e` are new |
| Animation timing | `animdata.d2` | 1 | 0.6 unpacked | override of vanilla |
| Music, video | none | 0 | 0 | PD2 ships none; D2R's own are used |
| Fonts | DC6 | 1 | tiny | new |

### Monsters and objects

- New monster codes with their own art: `!3` (Na-Krul boss), `!8` (Iskatu), `!9` (Leoric map boss), `$1` (Rathma bone),
  `$2` (act 4 hireling), `$3` (demon pillar), `$4` (Zhar the Mad), `$7` and `$8` (Lucion, spawner, invader portal),
  `0n` (Warlord of Blood), `87` (129 files, not in the s14 table), `j6` (poisoned well boss), `lm` (lightning beast),
  `vi` (Imperial Palace viper boss). 21 PD2 MonStats codes are not in D2R; 20 have art here, `bu` (butcher map) has none
  and reuses vanilla art.
- The seven class folders in `pd2monchars` back PD2's `InvaderAmazon`, `InvaderAssassin`, `InvaderBarbarian`,
  `InvaderDruid`, `InvaderNecromancer`, `InvaderPaladin`, `InvaderSorceress` monsters. Each class has many armor-type
  layers (light, medium, heavy; helm and shield types) times modes (a1, a2, rn, kk, sc, wl, gh, nu, s1, tw, tn, bl, th,
  s1 to s4, dd, dt) times weapon classes, which is why 14,421 files exist. Average DCC is 26 KB.
- Existing vanilla folders PD2 overrides or extends: `0d 0e 21 fd g2 hp hx hz io je mo ox rd re xu` (monsters), `60 61
  62 ao xo y4` (objects). They are mostly palette shift files and COFs.
- Sample new object folders: `#1`..`#6`, `#a`, `#b`, `#c`, `4y`, `4z`, `zz`.

### Maps and tiles

- `pd2maps.mpq` DS1 by folder: `act2/arcane` 218 (PD2 reuses act 2 slots for its custom areas), `expansion/map` 162,
  `act2/maggot` 58, `guild/pd2` 7, then one each in act1 crypt, tristram, act3 kurast, travincal, act4 mesa,
  expansion town and two act 2 town files.
- The 162 `expansion/map` files are the big ones: 4.6 MB packed, **190 MB unpacked**. The act 2 DS1 are tiny.
- PD2 `Levels.txt` has 212 rows against D2R's 138 (74 new names): Pandemonium, uber and map-run areas (Ashen Plains,
  Black Abyss, Caldeum, Crypts, Dark Temple, Descent, Desert, Diamond Gate, Djinn's Domain, Fortress, Frozen Forest,
  Graveyard, Halls of Torture, Imperial Palace, Kyovoshad, Monastery, Na-Krul's Abyss, Necropolis, Outer Void,
  Poisoned Well, Ruined Cistern, Sanctuary of Sin, Skovos, Spider, Throne, Tomb, Ureh City, Westmarch, Zhar areas), five
  guild halls, three arenas, PvP maps. `LvlPrest.txt` has 1,167 rows against D2R's 1,093; `LvlTypes` references 416 tile
  files against D2R's 230.
- DT1 by folder (`tiles\PD2assets`): root 92, `descend` 28, `dtprivate` 23, `guilds` 23, `psnwell\used` 9, `s13` 9,
  `torturehalls` 4, `nakrulabyss` 3, `act2`/`tombs` 4; guild: outdoors 6, house1/house2/cottages 7.

### Audio

PD2 audio is almost entirely vanilla. `Sounds.txt` has 4,760 rows (D2R 11,895); only 46 sound files are shipped, all of
them boss/NPC voice lines, drop "tink" sounds and a handful of effects. Everything else reuses Blizzard's own sound
archives, which are not in these MPQs. PD2 ships **no music**.

## Conversion paths into D2R HD

Per `knowledge/hd-graphics.md`. D2R draws HD from `data/hd/...` and classic from `data/global/...`, and both are loaded.

| PD2 asset | Path into D2R | Ship as stand-in? | Effort driver |
|---|---|---|---|
| DS1, DT1 | Copy unchanged to `data/global/tiles`; needed even in HD, a missing tile gives a black screen or crash. DS1/DT1 are the same format D2R reads | Not needed | Wiring (`LvlTypes`, `LvlPrest`, `Levels`) and tile id collisions; volume is 456 files, 285 MB unpacked, no art work |
| Monster DCC/COF/`animdata.d2` | Copy to `data/global/monsters/<code>/cof/...`; COF is used in HD for timing, DCC for classic mode. Merge PD2 records into D2R's `animdata.d2` (must contain every Code+mode+weapon class) | Yes: map each new monstats `Id` to the closest D2R HD model in `data/hd/character/monsters.json`, or reuse a D2R monster `Code` | About 14 new monsters need an HD model choice each; 20 PD2-only codes need `monsters.json` entries or they have no HD model. A missing COF crashes |
| Invader class DCC (14,421) | Same copy path for classic mode | Yes: map the seven Invader `Id`s to D2R's player HD models | Large in bytes (356 MB) but zero conversion; hard part is equipment display in HD, which is state-machine driven |
| Missile DCC | Classic mode needs the DCC (crashes if missing); HD needs a `missiles.json` entry mapping the missile name to a D2R model | Yes: model with the same element and shape; automatic fallback is the model of the vanilla missile with the same `CelFile` | 383 new missiles in the merge (`tools/pd2_missiles_hd.py`; 53 with PD2-only art picked by hand in `merge/missiles-hd.tsv`; the Rathma and Lucion missiles stay on stand-ins only until PD2's art is merged); a missile missing from the map is invisible in HD |
| Overlay DCC | Classic copy; HD needs overlay.txt `Character` set and `data/hd/overlays/<Character>/<name>.json`, empty `Character` means no HD effect | Yes: copy a D2R overlay json or reuse a D2R overlay row | 29 new overlays |
| Objects DCC | Classic copy; HD needs a line in `data/hd/objects/objects.json` plus `data/hd/objects/<folder>/<name>.json`; a copy of a stock object's json reuses its model | Yes: stock object json | 12 new object folders; without both files the object spawns but draws nothing |
| Item DC6 (277) | Classic: copy to `data/global/items/<invfile>.dc6`. HD: build `.sprite` + `.lowend.sprite` at 98 px per inventory cell (49 px lowend), place under `data/hd/global/ui/items/<weapon\|armor\|misc>/<folder>/`, add an entry to `data/hd/items/items.json` / `uniques.json` / `sets.json` | Yes: D2R picture of the same base type, or a bilinear upscale of the classic DC6 (x3.5). Uniques also need a non-blank `invfile` | 219 new invfile names. Every item code, unique and set needs a valid sprite entry or the client crashes (vendor screen, Item Spawner). Scripting an upscale step is cheap; art quality is the later cost |
| Skill icons | DC6 sheets to `.sprite` strips: 130 px icons, 132 px stride, two frames per icon (normal, pressed), max 16,384 px wide so 62 icons per strip | Yes: upscale of the classic icon | The class-less shared sheet holds one frame per icon and is capped at 62; PD2's item skills compete for slots. Read each sheet's real layout before converting |
| Panels (26 DC6 under `ui\panel`) | Upscale to 1162 x 1507 `.sprite` under `data/hd/global/ui/panel/` (x2.2695 across, x2.1406 down from 512 x 704) | Yes: upscale | Panel art may contain things D2R draws itself; layout JSON is separate work (`ui-layouts.md`) |
| Other UI DC6 (`global\ui`, `local\ui\eng`) | Classic mode only needs the DC6; HD needs per-screen `.sprite` | Mostly skip | Mostly English text labels for options and expansion screens that D2R's own UI replaces |
| WAV (46) | Convert to D2R's container, expected FLAC at the mirrored path under `data/global/sfx`; add rows to `sounds.txt` | n/a | Small; blocked only by confirming the D2R sound format and `sounds.txt` handling (see Open questions) |
| PL2 palettes | Classic mode only | Skip for HD | None in HD; 5 act overrides alter classic colours |

New monster models and textures at new paths additionally need entries in `model_lod_desc.bin` and
`texture_desc_cache.json`; that is only relevant once PD2 art is converted to real HD models, not for stand-ins.

## What can ship as stand-in art

Everything with an HD counterpart can ship as stand-in on day one, in this order of priority:

1. Maps, tiles: no stand-in needed, copy as-is.
2. Items: map the 219 new invfile items to D2R sprites of the same base type (`items.json`), upscale classic DC6 later.
3. Monsters, Invaders, bosses: map to the closest D2R HD model in `monsters.json`.
4. Missiles, overlays, objects: map to D2R models and overlay rows.
5. Skill icons and panels: bilinear upscale of the classic art.
6. Sound: keep Blizzard's own sounds; PD2's 46 files are optional.

Keep a list of every stand-in (policy in `porting-classic-mods.md`).

## Effort drivers, ranked

1. **Map wiring**, not art: 336 new DS1 paths, 74 new level names, 186 new tile type references and tile collisions. This
   is the parity-bar "must" item and also the biggest data volume (190 MB of DS1).
2. **Monster HD mapping**: each new `Id` needs a model choice; the Invader player-class monsters need the equipment
   display question solved (HD player state machines, not DCC layers).
3. **Item sprite pipeline**: one scripted upscale + sprite writer covers 219 items; correctness of every item map entry
   is a crash gate.
4. **Skill icon and panel pipelines**: scripted, but the 62-icon shared sheet cap needs decisions.
5. **Missile/overlay/object map entries**: dozens, each needs a D2R model pick.
6. **Audio**: tiny volume, one-time format and `sounds.txt` investigation.
7. **Disk size**: shipping all classic art is about 826 MB unpacked, of which the 356 MB of Invader DCC is the avoidable
   part if the first milestone only needs one class in Act 1.

## Open questions

- D2R's sound format handling for mods: sound effects are FLAC under `data/global/sfx` (2,955 files) and
  `data/hd/global/sfx` (4,748); whether WAV works as a mod file at the same path, or needs conversion and a
  `sounds.txt` filename change, is untested. Test with one file.
- Whether the Invaders' classic DCC set is needed at all if they are drawn with D2R's HD player models (the DCC would
  serve classic mode only).
- Table-based "new" counts use the s14 text snapshot, which lacks at least monster code `87`. Re-run them once the live
  tables are extracted (table-inventory ticket).
- Beta folder (`Beta\pd2assets.mpq`, 72 MB, older) not analysed.
