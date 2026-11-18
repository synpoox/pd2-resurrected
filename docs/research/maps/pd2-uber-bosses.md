# Inventory of PD2's uber bosses

Resolves research ticket #147 (map #143). It covers PD2's three pinnacle bosses, Diablo Clone, Lucion and Rathma: how a fight
opens, its keys and cube recipes, areas, skills and AI, drops, and the Desecration that Lucion unlocks. Findings only: no PD2 or
Blizzard file content is in this repo. Table, row and string names below are short identifiers.

## Answer in brief

- **Data and DLL.** About half of each fight is data that PD2's tables already hold: monsters, skills, missiles, levels, items,
  cube recipes and chest treasure classes. The other half is code in `ProjectDiablo.dll`: the portal that opens at the Blood
  Fountain, the scripted phases, the tier scaling, the guaranteed drops, the key drop odds and the whole of Desecration. None of
  that code is reverse-engineered yet.
- **One shape for all three.** You cube three ingredients into an entry item: Vision of Terror (`dcma`), Voidstone (`rtma`) or
  Shadow of Hatred (`luca`). You cube the entry item alone to cycle its tier 0 -> 1 -> 2 -> 0 (stat 185 `uber_difficulty`), then
  use it at the Blood Fountain in Harrogath. That opens the fight's PD2-only area. Tier 0 gives three single-use portals; tiers 1
  and 2 allow no re-entry.
- **Their ingredients depend on other features.**
  - Dclone needs the map system (Pure Demonic Essence drops from map bosses) and Uber Tristram (Black Soulstone).
  - Rathma needs map events (Splinter of the Void comes from the Shadow of Mendeln event) and Uber Tristram (the Hellfire Torch).
  - Lucion needs PD2's corrupted zones (Talisman of Transgression), which the owner cut for D2R's Terror Zones. So the
    Talisman's source must be moved, a decision for the owner.
- **Desecration** is a second corruption on an amulet, made in Lucion's reward room, the Temple of the Triune. One attempt is
  allowed per kill. The success chance is 50% at tier 0 and 60% at tiers 1 and 2. On success it adds one mod from an
  eight-mod, high-rarity pool. On failure the amulet becomes a rare that keeps two random corruption mods (the bricked outcome).
  Desecration is in no cube recipe. It is DLL code with its own UI panel (`desecratepanel`) and stats 206 `desecrated` and 207
  `desecrator`, which mirror Corruption's 360/361. It is new work.
- **Existing rows** already cover the boss skill functions: unported rows UberDiabSummon/UberDiabRun, RathmaSummon, Lucion kit,
  death novas, Rathma bone spear and uber immolation arrow, and the matching core-rules rows. The entry items are already blocked
  from dropping (`merge/item-tables.txt`). The rest is new: the opening, the areas, the phase scripts, the tiers, the guaranteed
  drops and Desecration (section 7).

## 1. Sources and method

- **PD2 data:** `pd2-data/` from `tools/pd2_extract.py` (the baseline in `pd2-baseline.json`). I read it with throwaway scripts
  kept outside the repo, using `cubemain`, `misc`, `monstats`, `skills`, `levels`, `treasureclassex`, `uniqueitems`,
  `itemstatcost`, `properties`, `states` and `objects`, plus `patchstring.tbl` through `tools/pd2_strings.py`'s `read_tbl`.
  "Row N" means the Nth data row, counted from 0 after the header. Monster HP is the Hell base, before PD2's player and tier
  scaling.
- **ProjectDiablo.dll:** printable strings only (ASCII and UTF-16), no disassembly.
- **PD2 wiki:** [Lucion](https://wiki.projectdiablo2.com/wiki/Lucion) (it holds the shared pinnacle page: tiers, Dclone,
  Rathma, Lucion), [Rathma](https://wiki.projectdiablo2.com/wiki/Rathma) (a stub marked possibly outdated),
  [Desecration](https://wiki.projectdiablo2.com/wiki/Desecration),
  [Vision of Terror](https://wiki.projectdiablo2.com/wiki/Vision_of_Terror),
  [Voidstone](https://wiki.projectdiablo2.com/wiki/Voidstone),
  [Shadow of Hatred](https://wiki.projectdiablo2.com/wiki/Shadow_of_Hatred) and
  [Annihilus](https://wiki.projectdiablo2.com/wiki/Annihilus). The wiki is a secondary source. Where its numbers and the data
  disagree, the data wins and the disagreement is noted. Phase scripts and DLL-only odds come from the wiki alone, because the
  data has no trace of them.

## 2. Opening a fight

| | Diablo Clone | Rathma and Mendeln | Lucion |
|---|---|---|---|
| Entry item | `dcma` Vision of Terror ("Uber Map") | `rtma` Voidstone ("Rathma Map") | `luca` Shadow of Hatred ("Lucion Map") |
| Recipe (cubemain) | row 198: `dcbl` + `dcho` + `dcso` | row 1478: `rtmv` + Hellfire Torch + `rtmo`; row 1962: the same with `cm2f` (Torch Fragment, the wiki's "Hellfire Ashes") in place of the Torch | row 2138: `lucb` + `lucc` + `lucd` |
| Tier cycle (cubemain) | rows 2048-2050 | rows 2051-2053 | rows 2139-2141 |
| Where it is used | Blood Fountain, Harrogath (strings `DCloneDesc`, `rtmaDesc`, `lucaDesc`) | same | same |

- The tier rows are op 18 on stat 185 (`uber_difficulty`, shown as "Tier:" by `MapTier`). They add +1, +1, then -2. That is
  cube op 18 (already a data row in core-rules) plus the `uber-difficulty` property.
- The entry items use misc `pSpell` 12, the map-item spell. Core-rules already has that row (remap 12 -> 16), and so does
  unported "Map items" (`misc.dcma.pSpell`, `misc.rtma.pSpell`, `misc.luca.pSpell`). What the spell does at the fountain (it
  checks the place, opens a portal to the tier's area and records the tier on the game) is DLL code. **New.**
- Entering removes pre-buffs and kills the mercenary. Leaving is blocked until the stage is cleared (the save-and-exit or death
  rule). Tier 0 gives three single-use town portals with invulnerability on entry. Source: the wiki only. **New** (an area rule).
- The wiki says only one boss area or map can be opened per game.
- Rathma's legacy path: row 1479 cubes 2 `rtmf` Ancient Bone Fragments + Hellfire Torch into a Jawbone. The wiki says fragments
  stopped spawning in season 5 (`misc.rtmf` spawnable 0).

### Ingredients and where they drop

| Item | Code | Source (wiki unless noted) | Depends on |
|---|---|---|---|
| Pure Demonic Essence | `dcbl` | map bosses, "about 2%" (unconfirmed); map stat 211 `map_glob_boss_dropubermats` ("Map bosses drop an Uber Material") | map system |
| Black Soulstone | `dcho` | Uber Tristram reward, with the Hellfire Torch | Uber Tristram (`ubtm`, row 182: Horn + Eye + Brain) |
| Prime Evil Soul | `dcso` | Hell Mephisto, Diablo (1:21) and Baal (1:13); the uber Tristram primes' TC is named "Uber Soul" | none (vanilla bosses) |
| Splinter of the Void | `rtmv` | Shadow of Mendeln map event (`nihlathakMap`), "40%" (unconfirmed) | map events |
| Trang-Oul's Jawbone | `rtmo` | undead in Hell, 1:22,500; TCs "Rathma Jawbone N/NM/H" (Unique 0/0/2250) look like the DLL's odds table | none |
| Hellfire Torch / Torch Fragment | `cm2` unique / `cm2f` | Uber Tristram; rows 1955-1961 and 2260-2266 break a Torch into 1-7 fragments | Uber Tristram |
| Demonic Insignia | `lucb` | Act 5 red-portal zones (Abaddon, Pit of Acheron, Infernal Pit); TCs `lucbN/NM/H` | none (vanilla areas) |
| Talisman of Transgression | `lucc` | enemies in corrupted zones; TC `lucc` (Unique 4000) | **corrupted zones: cut**, Terror Zones stand in |
| Flesh of Malic | `lucd` | demon enemies; TCs `lucdN/NM/H` | none |

- The key TCs (`lucb*`, `lucc`, `lucd*`, `Rathma Jawbone *`) hold only a Unique/Set number and no items. They are no ordinary
  treasure class. My guess is that the DLL reads them as odds for a code-driven drop (the DLL names `rtmo`, `lucb`, `lucc` and
  `lucd` near each other). Unconfirmed. **New:** a drop rule per monster class or area, by difficulty.

## 3. Areas

| Fight | Levels (id: name, string) | Notes |
|---|---|---|
| Diablo Clone | 137 `UberDiabloLvl` (shown as "The Pandemonium Fortress") | one arena; the monster is `uberdiablonew` ("Diablo Clone") |
| Rathma | 161 `RathmaMap2` "Ruins of Necropolis", 162 `RathmaMap1` "Decaying Swamplands", 163 `RathmaMap3` "The Void" | six stages on the wiki: clear, Mendeln, clear, Rathma, clear (the portal is in one of 3 places), both; the wiki calls the last area "Decayed Highlands", the string says "The Void" |
| Lucion | 188 `LucionArenaMap` "Veiled Realm", 189 `LucionVaultMap` "Temple of the Triune" | the vault holds `LucionChest` objects (treasure classes `LucionChestLow`/`LucionChestHigh`) and the Desecration panel |

The new PD2-only levels, their presets, warps and objects fall under unported "New PD2-only areas" (`levels.*.[*]` and
similar), which waits on the level pipeline (map #143's first milestone). They need no new row, only this list of levels.
Desecrate's never-raised list (unported, Desecrate rows) already names the Rathma monsters (`*Rat`, `*Rat2`, `void*`).

## 4. Monsters, skills and AI

### Diablo Clone (`uberdiablonew`, Hell level 110, HP 10530 base, resists 30/30/30/30)

- **Skills:** UberDiabWall (srvdo 166), UberDiabCold (100), UberDiabFire and UberDiabSuperFire (22, missiles `uberdiabfire` and
  `ubernova`), UberDiabLight (srvst 53 / srvdo 152), UberDiabSummon (srvdo 177, summons `dcloneskele`), UberDiabRun (srvst 71),
  MonTeleport.
- **AI:** `Trap-Melee`, a vanilla AI. The phases are scripted elsewhere: skeleton waves below 85, 60 and 35% life (immune while
  they live), and cold-nova sequences below 75, 50 and 25% (six novas each, the cold-resist cut rising from -50 to -175). Below
  that, meteors, bone spirits and burning ground. He is immune when no player is within about 10 yards. Source: the wiki.
- **Adds:** `dcloneskele`, `dcloneskelearcher`, `dcloneskmage_*` and `dclonebloodlord` (Hell level 110, 92 for the blood lord).
  `ubertrappedsoul1-5` cast UberDiabloMeteor and UberDiabloBoulder (missile `uberimmolationarrow`).
- **Wiki disagreement:** the wiki gives poison resist 50, but the data says 30. The wiki also gives PLR 50/53/56 by tier.
- `diabloclone` (D2R's and vanilla's own Dclone, row 333) and `uberdiablo` (Uber Tristram) are separate rows. PD2's Dclone is
  `uberdiablonew`.

### Rathma (`rathmaBone`, HP 6975) and Mendeln (`rathmaPoison`, HP 8550), Hell level 110

- **Rathma:** RathmaBoneSpear (srvdo 8, missiles `rathmabonespear*`), RathmaTeethNova (159), RathmaPrison (104), Teeth Rathma (8),
  RathmaSummon (srvdo 179), Blink.
- **Mendeln:** RathmaPoisonOrb (missile `rathmapoisonorb`), RathmaColdMissiles (139), LowRes, Poison Nova Rathma (22),
  RathmaPacman and RathmaOuterNova (159), RathmaSummon, Blink.
- **Clones:** `rathmaBoneClone` and `rathmaPoisonClone` (HP 3900 each) are the final pair, probably.
- **Adds:** `rathmaVoidGolem` (Void Walker), `rathmaBloodGolem` (Sanguine Bearer, which heals Mendeln on reach),
  `rathmaTotem` (Conjured Soul, RathmaFissure) and `voidKnight{Cold,Fire,Poison,Phys}`.
- **AI:** `Summoner`, also vanilla. The life-threshold script is on the wiki: Mendeln's healing waves at 90/70/50/30% and poison
  sequences at 80/60/40%; Rathma's Void Knight waves of 4, 5 and 6 at 70/50/30%; both retreat at 15%. In the final fight the pair
  share life (damage is halved and applied to both, and so is poison length), and RathmaDeath buffs the survivor.
- **Wiki disagreement:** the wiki's resists (Rathma 75/50/50/0, Mendeln 65/40/40/0) match the data.

### Lucion (`Lucion`, Hell level 110, HP 24000, resists 30/30/30/30)

- **Skills:** LucionBlink (srvdo 155, cltdo 113), LucionNova (184), LucionBoneWave (missile `lucionbonefingers`), LucionPunish
  (186), LucionDash (srvst 76 / srvdo 185 / cltst 58 / cltdo 114). LucionCharge (67) is in the table but no monster uses it.
- **Arena controller:** `LucionControl` (LucionRoyalFire, srvdo 143 / cltdo 83).
- **Adds:** `LucionSpawner` (Veiled Portal), `LucionSpawn` (Shivering Child), `LucionSpawnRanged` (Smouldering Defiler, imp
  kit) and `LucionSpawnTank` (Anguished Mother, MonSmiteLucion srvdo 150).
- **AI:** `Overseer`, also vanilla. On the wiki the fight repeats portal phases (he vanishes and cannot be hurt) and
  lightning-nova phases. LucionDeath1-4 (srvdo 169) are his death novas.

### States

`dcloneset`/`dclone8set`, `rathmaset`/`rathma8set`, `lucionset`/`lucion8set` and `uberscaling` (ids 209-241). The DLL names
`dclone_aura`, `rathma_aura` and `lucion_aura`. The `uberscaling` state is probably the tier scaling. Unchecked.

## 5. Drops

| Fight | Guaranteed | Rolls (wiki) |
|---|---|---|
| Diablo Clone | one Annihilus (`cm1` unique), not through a monstats TC (the TC columns are empty) | tiers 1-2: 1 in 200 for Overlord's Helm, Dark Abyss, Itherael's Path, Hadriel's Hand or Aidan's Scar (uniqueitems 402-406, level 131) |
| Rathma | a Tainted Worldstone Shard (`cwss`), which corrupts an Annihilus (cubemain rows 287 and 395-401: 20% rare, otherwise one of four added mods; row 2154 rerolls it with a Demonic Cube) | tiers 1-2: 1 in 200 for The Third Eye, Band of Skulls or Cage of the Unsullied (uniqueitems 417-419); all tiers: Skeleton Key, Horadric Almanac or Horadric Navigator at 1 in 50/44/38 |
| Lucion | entry to the Temple of the Triune: one Desecration and the gold chests (`LucionChestLow`/`High`, one more item per tier) | Demonic Cube (`imrn`): none at tier 0, 1 in 75 at tier 1, 1 in 50 at tier 2 |

- Every guaranteed drop and every 1-in-N roll is DLL code. The monster rows have no treasure class. **New.**
- Season trophies and "of Clout" uniques (`Dclone Trophy`, `Rathma Trophy`, `DcloneClout`, `RathmaClout`) and the `.kills`
  counters are ladder and multiplayer extras. I suggest cutting them with the multiplayer milestone.
- The Annihilus corruption rows ride on Corruption (P9). The Demonic Cube reroll rides on the core-rules row "Demonic cube
  reroll" (cube op 31).

## 6. Desecration

Source: the wiki, plus the data's stats and strings. The DLL holds the rule itself, and it is not reverse-engineered.

- **Items:** amulets only. The amulet must already be corrupted. Desecration adds a second corruption mod and changes the
  amulet's look.
- **Where:** the Temple of the Triune after a Lucion kill, one attempt per kill, through PD2's own panel
  (`DATA\GLOBAL\ui\menu\desecratepanel`). No cubemain row matches it.
- **Success chance:** 50% at tier 0, 60% at tier 1, 60% at tier 2.
- **Success:** one mod from the pool +1 to All Skills, +10% Faster Cast Rate, +10% Faster Run/Walk, +10% Curse Resistance,
  +30-40% Enhanced Damage, Cannot Be Frozen, +2% to all maximum resistances, All Resistances +7-10.
- **Failure (bricked):** the amulet becomes rare and keeps two random corruption mods.
- **Stats:** 206 `desecrated` (shows `StrDesecrated` "Desecrated") and 207 `desecrator` (the hidden roll), with properties
  `desecrate` and `desecratenum`. This mirrors Corruption's 360 `corrupted` / 361 `corruptor`.
- **D2R:** no such panel or stats. The UI and the rule are both new work.

## 7. Mapped to the existing lists

| Piece | Row | Status |
|---|---|---|
| UberDiabSummon, UberDiabRun | unported "UberDiabSummon, UberDiabRun"; core-rules "PD2 monster: UberDiabSummon, UberDiabRun" | existing |
| RathmaSummon | unported "RathmaSummon"; core-rules "PD2 monster: RathmaSummon" | existing |
| Lucion kit (Nova, Dash, Punish, Blink) | unported "Lucion kit", "Teleport, Blink, LucionBlink, Mon Bloodwarp"; core-rules "Lucion kit", the Teleport row (built) | existing |
| Death novas (RathmaDeath, LucionDeath1-4, NihlathakMapDeath) | unported "Death novas"; core-rules "Death novas" | existing |
| Rathma bone spear, blade fragment | unported and core-rules "Rathma bone spear, blade fragment damage" | existing |
| Uber immolation arrow (trapped souls) | unported and core-rules "Uber immolation arrow hit" | existing |
| UberDiabWall | core-rules "Firestorm ... wall makers" (built, W22) | existing |
| Rathma novas (TeethNova, OuterNova, Pacman) | core-rules "Combustion and boss novas" (built, W27) | existing |
| RathmaColdMissiles | core-rules "Cold missiles" (data) | existing |
| LucionRoyalFire | core-rules "Royal Fire" | existing |
| rathmapoisonorb hit, Rathma fire client hits | core-rules "Nova and orb hits", "Immolation, Blaze and Rathma fire client hits" (data) | existing |
| Entry items' spell | core-rules "Map items" (`pSpell` 12 -> 16); unported "Map items" | existing |
| Entry items blocked from dropping | unported "Map items never drop" (`merge/item-tables.txt`) | existing |
| Tier cycle and recipes | core-rules "Item stat less than / equal" (op 16/18, data) | existing |
| Annihilus corruption | unported "Corruption"; core-rules "Corruption in one press" (P9) | existing |
| Demonic Cube reroll | core-rules "Demonic cube reroll" (op 31) | existing |
| Areas 137, 161-163, 188-189 and their presets | unported "New PD2-only areas" | existing |
| Desecrate's never-raised uber and Rathma monsters | unported "Desecrate: monsters never raised" | existing |
| `difficultylevels` UberCodeOdds/UltraCodeOdds | unported "PD2 columns read by PD2's DLL" (item base upgrades, not these bosses) | unrelated |
| Blood Fountain opening: portal, tier, one area per game | none | **new** |
| Area rules: buffs stripped, mercenary killed, exit blocked, tier-0 portals | none | **new** |
| Boss phase scripts (life thresholds, immunity, waves, shared life, Lucion's portal phases) | none | **new** (the largest piece) |
| Tier scaling (stats per tier, `uberscaling`) | none | **new** |
| Guaranteed drops and 1-in-N rolls (Annihilus, Shard, uniques, utility items, Demonic Cube) | none | **new** |
| Key drop rule (the odds-only TCs `lucb*`, `lucc`, `lucd*`, `Rathma Jawbone *`) | none | **new** |
| Talisman of Transgression's source (corrupted zones are cut) | none | **new**, needs an owner decision |
| Desecration (panel, roll, mod pool, brick, stats 206/207) | none | **new** |
| Season trophies, Clout uniques, `.kills` | none | **new**, suggested cut (multiplayer) |

## 8. Open questions

- What does the DLL do for each new row? Each needs Ghidra on `ProjectDiablo.dll` (`PD2.gpr`), starting from the strings named
  here (`desecratepanel`, `lucion_aura`, the item codes near 0x378e40).
- The wiki pages are partly outdated (the Rathma page is marked so; its unique lists conflict). The phase thresholds and odds
  need a game check on the PD2 install before they count as the port's baseline.
- Does D2R's own Dclone (the world event, `diabloclone`) stay next to PD2's? It is D2R-only content, so this is not a deviation.
  Still, it shares the boss's name.
