# PD2's uber bosses in ProjectDiablo.dll

Resolves UB1 (#226) in [uber-bosses.md](../uber-bosses.md) (#153). It reads, with Ghidra, the parts of PD2's uber bosses that
the tables do not hold: the fountain opening, the area rules, the tier scaling, the phase scripts, the guaranteed drops and
rolls, the key drop odds and Desecration. The table side is in the inventories
([#147](https://github.com/synpoox/pd2-resurrected/blob/research/pd2-uber-bosses/docs/research/pd2-uber-bosses.md),
[#185](pd2-pandemonium-uber-tristram.md)). Findings only: no PD2 or Blizzard file content is in this repo.

## Answer in brief

- **The wiki's drop odds and Desecration numbers are in the DLL, and they all match.** That covers the 1-in-200 uniques, Rathma's 1 in 50/44/38,
  the Demonic Cube's 1 in 75/50, Desecration's 50/60/60% and its eight mods, and the Jawbone's 1 in 22,500. So the numbers
  are now checked against the PD2 install's DLL and become the baseline (#153). Dclone's skeleton waves at 85/60/35 and
  Rathma's shared life are read too.
- **Much more is data than we thought.** Each uber key's misc `len` is the level it opens. Its `missiletype` is the portal
  object only for maps; uber keys warp the group in instead (section 2). The key odds are the odds-only TCs read as a 1-in-N roll. The Desecration pool is the amulet corruption
  cube rows. The phase scripts take their thresholds and counts from the bosses' monstats `aip` and skill columns.
- **The phase scripts are AI overrides.** PD2 swaps its own functions into about 30 vanilla AI slots. Dclone is the
  Trap-Melee slot, Rathma and Mendeln the Summoner slot, and Lucion with his controller and spawner the Overseer slot. Each
  fight keeps its state in a few game fields: a stage byte, the boss pointers, and the timer stats 446/447.
- **The opening is one function shared with the maps.** It checks the gates (Hell, level 80, Ancients, Harrogath, one
  area per game), checks the distance to the Blood Fountain and reads the tier from stat 185. Then it warps every
  eligible player at the fountain straight into the area. At tier 0 it adds three single-use portals for re-entry.
- **Gaps** (section 9): the exit block is not in the DLL's readers of the area type or the uber levels, and Rathma's
  scaling input after the group moves on is unchecked.

## 1. Method

- Ghidra 12.2 headless on `C:\ghidra-work\pd2-project\PD2.gpr`, program `ProjectDiablo.dll` (image base `0x10000000`;
  addresses below are Ghidra addresses). I used throwaway scripts kept outside the repo: string and item-code xrefs,
  displacement and immediate searches, decompile and disassembly dumps.
- Leads came from item codes stored as dwords (`dcma`, `lucb`, ...), the monster-death drop switch, writes to the game
  fields named below, and PD2's AI table override.
- Ids are runtime ids. In monstats, properties and uniqueitems the runtime id is one less than the row index for rows
  after the `Expansion` row. For example uberdiablonew is 789 (0x315), rathmaBone 933, rathmaPoison 934, the clones 935/936
  and Lucion 1112 (0x458).
- Not done: a game check on the PD2 client itself. Every number here comes from the PD2 install's own DLL and tables.

### Game fields PD2 adds

| Field | Meaning |
|---|---|
| `game+0x1df4` | the active map or uber area type: 0x29 Uber Tristram, 0x2a Dclone, 0x2b Rathma, 0x2c Uber Ancients, 0x2d Lucion (0x1e-0x24 unique maps, 9-0x15 map events). Any value > 0 blocks another opening: the one-per-game gate |
| `game+0x2628` | the number of entries into the uber area (+1 per portal use). Uber Tristram and the map events use it for other things |
| `game+0x2629` | the uber tier (0-2), copied from the uber key's stat 185 |
| `game+0x262a` | the fight's stage byte (Rathma 1-7, Lucion 1-7, then 8 when Lucion dies, then 9 after the Desecration attempt) |
| `game+0x262b` | Rathma's and Lucion's sub-step counter |
| `game+0x2600`, `+0x2604` | the boss pointers (Dclone or Lucion or Rathma, and Mendeln) |
| `game+0x261c`-`+0x2624` | the three tier-0 portals |
| `game+0x26fe` | a 15-tick window during which the boss is immune and idle (see 5) |

## 2. Opening at the Blood Fountain (`0x10383060`)

The map-item use (misc `pSpell` 12) for maps and uber keys. In order:

1. It fails if the player is busy, or if `game+0x1df4` > 0 (an area is already open). The exception is 0x28, which
   allows only levels 136 and 185.
2. Level = the item's misc `len` (dcma 137, rtma 161, luca 188, ubtm 185). Maps open a portal whose object is misc
   `missiletype` (584 red for maps, dcma and ubtm, 585 ghostly for rtma, 622 dark grey for luca), or 60 when the column
   is empty.
3. Gates: Hell only, character level 80 or more, Rite of Passage done (quest flag 0x27), and the player stands in
   Harrogath (level 109). Levels 0x98, 0x99 and 0xff are refused.
4. **Uber keys only** (levels 136, 137, 161, 168, 185, 188; 168 is PD2's Uber Ancients, cut in the port): `0x1037d840` must report the player at most 0x95 from
   (5074, 5114), the Blood Fountain. The tier is the item's stat 185 (`uber_difficulty`). For levels 137, 161 and 188 it
   is stored in `game+0x2629`.
5. **Uber keys warp the group straight in. No portal is opened.** The code places object 587 (invisible) at the use
   spot. `0x1038a680` then warps every player in Harrogath who is within 0x96 of the fountain and level 80 or more. Each
   player goes through the portal-entry code in section 3. The count is stored in `game+0x2628`, and the opening fails
   if it is 0. So the uber keys' `missiletype` is not used here. For 585 and 622 it matches the portals the fights open
   later (sections 5-6).
6. It sets `game+0x1df4` to the area type (table above). For Dclone, Rathma and Lucion it also clears the stage bytes.
7. **Tier 0:** it spawns three object-595 portals (`xactportal_grey`) at (5076, 5119), (5068, 5108) and (5079, 5110)
   around the fountain. These are the re-entries. Tiers 1 and 2 get none.

So a key's tier and target are data, and the fountain check is one distance test. UB2 builds this on MS2's map-use hook.

## 3. Area rules (portal entry, `0x103a9780`)

When a player enters an uber level (137, 161-163 from Harrogath, or 188), by the opening's warp or a tier-0 portal:

- **Entries are counted** in `game+0x2628`.
- **The boss heals 20% of its maximum life** on each entry (Dclone, Lucion, and both Rathma and Mendeln). This is the
  entry code's own heal; Dclone's AI has a second, separate one (section 5).
- **Pets are removed** (`0x1037fb10` with flag 0). This covers every pet type except type 3 (the Iron Golem), and the
  hireling (type 7) is killed. This is the "mercenary killed" rule.
- **37 states are stripped** (`0x1037f4a0` with flag 1): enchant, coldenchant, venomclaws, shout, battleorders,
  battlecommand, wolf, bear, fade, quickness, energyshield, shiverarmor, chillingarmor, bonearmor, cyclonearmor,
  bladeshield, holyshield, holysword, thunderstorm, thawing, antidote, every shrine state (128-137), monsterset and
  blood_mana. Battlecry, openwounds, freeze and cold are always stripped. These are the "pre-buffs removed".
- **Tier-0 portals are single use.** Taking an object-595 portal removes it.
- **Rathma tier 0 re-entry** comes out at the current stage's spot: (0x476b, 0x35d3) after stage 1, (0x47d7, 0x3961)
  after stage 3, and (0x4e98, 0x1419) after stage 5.
- On arrival in levels 136, 137, 161-163 or 185, the level's `Waypoint` objects are removed.
- The **exit block** is not in this function. Tiers 1-2 open no way back, and the three tier-0 portals are the only
  re-entry. Where save-and-exit or death is refused was not found (section 9).

## 4. Tier scaling (`0x10381160`, state 241 `uberscaling`)

`0x10371a70` runs it once on each monster in a PD2 level (levels 0x89-0xd2 except 0xca-0xce), with the tier from
`game+0x2629`. It then sets state 241 so the scaling is not applied twice. With *e* = max(entries − 1, 0):

- **Max life:** at tier 0, ×1.5. At tiers 1-2, ×4 for the bosses (the set at `0x105deec4`: Dclone 789, Rathma
  and Mendeln 933/934, their clones 935/936, Lucion 1112) or ×3 for the rest. Then +20% of that per tier. So a boss has ×4.8 at tier 1 and ×5.6 at tier 2.
- **+4 to all skills** (stat 127) per tier.
- **The bosses only:** fire, lightning and cold absorb % (stats 142/144/148) +7×tier + 2×e, magic absorb % (146) +3×tier
  + 2×e, damage resist (36) +3×e, poison length resist (110) +3×tier + 3×e.

The tier scales every monster's life (×3 or ×4, then +20% per tier). Only the bosses get the absorbs. Monsters in
maps get ×1.5 too, because `game+0x2629` stays 0 there.
That this applies to maps is a reading of the code, not checked in game.

## 5. Phase scripts

`0x10361150` writes PD2's AI functions into D2Game's AI table (`DAT_105ddeb0`, 16 bytes per monai row). The rows that
matter here:

| monai row | PD2 function | Uber monsters that use it |
|---|---|---|
| 87 Trap-Melee | `0x1035b3f0` | `uberdiablonew` (Dclone) |
| 99 TrappedSoul | `0x1035bc60` | `ubertrappedsoul1-5` (also map boss 0x39a) |
| 53 Summoner | `0x10358510` | rathmaBone, rathmaPoison and both clones |
| 120 Overseer | `0x1035dca0` | Lucion, LucionControl, LucionSpawner |
| 7 Brute, 74 OblivionKnight, 141 BaalMinion | `0x103578f0`, `0x1035b0a0`, `0x103580a0` | void golem, void knights, blood golem |
| 50 Mephisto, 51 Diablo, 135 BaalCrab | `0x10357ba0`, `0x10357d50`, `0x10357f00` | the Uber Tristram primes (UB3) |

The scripts read their numbers from the boss's monstats record: `aip1`-`aip8` per difficulty at +0x56, skills `Skill1-8`
at +0x170 with modes at +0x180. They use stats 446/447 (`mon_cooldown1/2`) as timers and phase counters. Shared by all
three: while `game+0x26fe` > 0 the boss is immune (all six resists set to 200 and curses removed, `0x10380770`) and idles.
The counter goes down only while a player is in the level. A hook at `0x102fc6c0`, on D2Game's room-release pass
(`D2Game+0x2cf30`, called when a room has gone more than 10 ticks without a player), starts that 15-tick window when the
room holds a registered boss (`game+0x2600` to `+0x2624`) from the fight-monster set at `0x105dc078`. That set holds 30
ids: Dclone, his adds and trapped souls, the Rathma group with its golems, totem and void knights, and Lucion with his
controller, spawner and adds. So a boss left alone, or pulled out of every player's range, goes immune and idle.

### Diablo Clone, "Dclone" (`0x1035b3f0`)

- It registers itself as the boss (`game+0x2600`). Each tick it counts the players in its level and keeps the count. When
  the count drops (a player leaves or dies), it heals 20% of max life, capped at max.
- **Cold-nova phases:** below 75%, 50% and 25% life (doubles 100 − (phase/2)×25), with stat 447 = 2, 4 or 6, its life is
  reset to that percentage. It then walks to the arena centre (15129, 6548), killing any monster standing on the centre
  spot (skill slot 8, MonTeleport). There it casts skill slot 5 (UberDiabSuperFire), counting stat 446 up to `aip5` (Hell:
  7, so six novas). Each cast *k* sets stat 331 (`passive_cold_mastery`) to (*k* + 1) × `aip6` (Hell: 25, so 50 up to 175).
- **Skeleton waves** at stat-447 phases 1, 3 and 5. They fire when life drops below Param4, Param5 and Param6 of skill slot
  6 (UberDiabSummon) in `skills.txt`: 85, 60 and 35, the wiki's numbers.
- **Immunity:** each tick it checks whether any of monsters 886-892 (Dclone's adds) are alive in the level, and calls the
  immunity toggle (`0x10380770`, section 5 intro). The boss is immune while any add lives, and also while no player is
  within 0x17c of it; otherwise its resists are restored from `monstats`.
- **Trapped souls** (`0x1035bc60`, monsters 790-794) die when Dclone is dead or dying. Their cast delay is the `aidel`
  minus 4×tier (at least 20) and their spread shrinks with the tier.

### Rathma and Mendeln (`0x10358510`)

- They register as `game+0x2600` (Rathma, the bone clone) and `+0x2604` (Mendeln, the poison clone). Stage byte 0 -> 1 on
  the first tick.
- **Life thresholds** every 10% from 90% down to 20%, with stat 446 as the event index. Mendeln alone gets 90, 70, 50 and
  30 (the healing waves), and both get 80, 60, 40 and 20. Each event casts skill slot 7 or 8.
- **Retreat below 15%:** stage 1 -> 2 or 3 -> 4. A ghostly portal (object 585) opens at the boss, and an invisible object
  587 marks it. While the Rathma area is active, entering level 162 or 163 sets `game+0x2628` to 2 or 4. Here the field
  reads as the furthest area reached.
- **Death:** Rathma (`0x103767d0`) and Mendeln (`0x10376760`) each open their stage portal once, at (18387, 14667) and
  (18284, 13759).
- **Final pair:** when the first clone dies (stage 5 -> 6), `0x10376840` makes the surviving clone cast a skill through
  `0x1036d7d0`. That is PD2's general cast helper (nine callers, other AIs among them), so it is not a shared-life
  mechanic. This is probably RathmaDeath's buff. From stage 6 the survivor's AI repeats the cast while it lacks state 103
  (`monfrenzy`). When the second clone dies (6 -> 7),
  every void knight, golem and totem in the level is killed.
- **Shared life:** in PD2's damage code (`0x10302ca0`), when a clone (935 or 936) takes damage and both boss slots are
  set, the other clone loses the same life (stat 6), unless it is dead or dying or has state 8. Life-drain damage (stat
  74, `0x103055b0`) is mirrored the same way through `0x10305460`. The damage is copied, not halved: the wiki's
  "halved" is not in this code.
- Adds: each tick it checks whether void knights (968, 969, 1106, 1107) and the blood golem (938) are alive. That gates
  which skills are used.

### Lucion (`0x1035dca0`)

- **Stages** go up as Lucion's life falls below these thresholds:

  | Stage | Tier 0 | Each tier adds |
  |---|---|---|
  | 2 | 85% | +5 |
  | 3 | 80% | +5 |
  | 4 | 75% | +4 |
  | 5 | 70% | +4 |
  | 6 | 55% | +3 |
  | 7 | 35% | +3 |

- **Stage 3, the portal phase:** Lucion stops (mode 10, a 25-tick delay). Four units spawn at the arena corners
  (x 0x622d/0x6217 by y 0x17eb/0x17d5) through the same spawn call the spawner uses, probably Veiled Portals (1141).
- **LucionControl** (1113) and **LucionSpawner** (1141) spawn adds on a timer from stage 3 or 4. Shivering Child (1114),
  Smouldering Defiler (1115) and Anguished Mother (1140) are picked by the `aip` odds and capped by the `aip` counts.
  Their timers shorten with the tier and the stage. While Lucion's own AI is in its mode 5, the controller's and
  spawner's timers are halved.
- **Death** (`0x10376560`): it spawns object 622 and a portal at (0x6221, 0x17df) to level 189, the Temple. It sets stage
  8 and kills the remaining adds in level 188.

## 6. Guaranteed drops and rolls

They come from the monster-death switch (`0x10378d80`, inside PD2's death-drop chain `0x1037a330`). Item level 130 unless
noted.

| Fight | Handler | Guaranteed | Rolls |
|---|---|---|---|
| Dclone (789) | `0x103763d0` | Annihilus (`cm1`, unique). A gold portal (object 586) to Harrogath opens at the body | tiers 1-2: 1 in 200, then one of five evenly: Itherael's Path (`utb`), Overlord's Helm (unique id 402), Dark Abyss (`uth`), Aidan's Scar (`7qr`), Hadriel's Hand (`7bs`), item level 131 |
| Rathma pair (935/936, last death) | `0x10376840` | Tainted Worldstone Shard (`cwss`). Portal 586 to Harrogath | all tiers: 1 in (50 − 6×tier) for one of `rid`/`rtp`/`rkey`; tiers 1-2: 1 in 200 for The Third Eye (`ram`), Cage of the Unsullied (`rar`) or Band of Skulls (`rbe`) |
| Lucion (1112) | `0x10376560` | the Temple portal (section 5) | tiers 1-2: Demonic Cube (`imrn`) at 1 in (4 − tier)×25 = 75 or 50 |
| Uber Tristram primes | `0x10377710` | when Mephisto, Diablo and Baal (704/705/709) are all dead: Hellfire Torch (`cm2`) and Black Soulstone (`dcho`), and a town portal (object 60) | none |
| Uber Izual (706) | `0x10377800` | `mbr` (the other mini-ubers have their own handlers at `0x10377830`/`0x10377860`, for UB3) | none |

Solo tier-2 kills also set trophy and season flags (`0x10380a10`, states 209-240). They are cut, like the `.kills` counters.

## 7. Key drop rule (`0x103799d0`)

This runs for every monster death, after the drop chain. Each roll is `0x10375ee0(tc, mult, players)`: it takes the
odds-only TC row *tc* + difficulty and drops on `rand % (Unique × max(Set, 1)) < mult`. A Unique of 0 means no drop.
With more than one player (`game+0x90`, where values above 8 count as 1), Unique is scaled by
(127 − 2×(players − 1))/127. *mult* is 1, or 2 for monsters with stat 493 (`map_glob_skirmish_mode`).

| Item | Condition | TC rows (N/NM/H) | Hell odds |
|---|---|---|---|
| Trang-Oul's Jawbone `rtmo` | undead monster | Rathma Jawbone 1022-1024 | 1 in 22,500 |
| Flesh of Malic `lucd` | demon monster | lucd 1032-1034 | 1 in 26,000 |
| Demonic Insignia `lucb` | levels 125-127 (Abaddon, Pit of Acheron, Infernal Pit) | lucb 1028-1030 | 1 in 2,000 |
| Talisman of Transgression `lucc` | in PD2's corrupted zone | lucc 1031 | 1 in 4,000 |
| Worldstone Shard `wss` | in PD2's corrupted zone | Corrupted Zone 1019 | 1 in 200 |
| Demonic Cube `imrn` | any monster | Demonic Cube 1039-1041 | 1 in 1,000,000 |

The same function rolls the other odds-only TCs: lbox, lpp, rkey, rtp, rid, llmr, lsvl, the Uber Ancients keys, map drops
in PD2 levels, the map-stat item drops and corrupted-zone maps. Those belong to the map system and the items.

For the port, the Talisman's corrupted-zone condition becomes "in an active Terror Zone" at the same TC (#153).

## 8. Desecration (`0x103961b0`, roll `0x103729e0`)

- **Where:** object 621 (`Sacrifice Altar`) in the Temple. Using an item on it runs the handler. PD2 has no cube row for it.
  PD2's panel only shows this.
- **Conditions:** the item passes a type check (`0x1030cc10`; the brick below is an amulet) and two flag checks. It must
  be corrupted (stat 360 ≠ 0), not desecrated (stat 206 = 0) and not mirrored (stat 486 = 0). Lucion's stage byte must be
  8, so the altar only works after Lucion dies. Otherwise the item is returned and nothing happens.
- **One attempt per kill:** the roll sets the stage to 9. After the attempt the altar switches to mode 1 and may be
  removed (`0x1037ff80`).
- **Success:** `rand % 100 < 50`, or `< 60` when the tier is 1 or 2. The amulet then gets a roll from 851-999, applied
  through the amulet corruption cube rows (op 16 on stat 361, ordered by `value`). That band hits only rows 682-689: +10%
  FRW, +30-40% ED, +1 all skills, +10% curse effectiveness, +2% max resists, +10% FCR, +7-10 all resists, cannot be frozen.
  Each has about 1 in 8 (18 or 19 of 149). The row's `corrupt`/`corruptnum` mods are written as properties 425/426
  (`desecrate`/`desecratenum`), which set stats 206/207.
- **Failure (the brick):** a new rare amulet (`amu`, item level 99) replaces the old one. `0x10375340` hands it over
  (an engine call, then a refresh of every client); nothing is read from the old amulet. The new amulet then gets two
  rolls from `0x103649d0`:
  - flag 0, a normal Corruption: a roll from the first corruption row's value up to 999, written as the usual
    `corrupt`/`corruptnum` stats (270/271);
  - flag 1, the Desecration roll above (851-999, written as 425/426).

  So the brick ends with one Corruption mod and one Desecration mod, both fresh. The wiki's "keeps two random corruption
  mods" means these two rolls, not the old amulet's mods. Its affixes are fresh rolls too.

For UB7 this means the roll is a handful of lines over cube rows we already have. Only the port's trigger (a cube row
in the Temple, an approximated row) is new.

## 9. Open points

- **Exit block:** no reader of `game+0x1df4` refuses a leave or a death. They are the opening, the area rules, the AIs,
  the drop handlers and a state dump (`0x102fcd60`). The checks on the uber level ids (161-163, 188-189) don't refuse one
  either. If PD2 blocks exits, it does so through something else (level data or the client). UB2 checks in game whether
  the block exists.
- **`game+0x2628` in Rathma:** it counts entries (and so feeds the tier scaling's *e*), but entering level 162 or 163 sets
  it to 2 or 4. Whether Rathma's scaling input jumps when the group moves on is unchecked. UB5 checks it.

## 10. What this means for the work items

| Item | Now known | Still to read |
|---|---|---|
| UB2 | opening and its warp (2), gates, fountain distance, tier-0 portals, area rules (3), tier scaling (4), key drop rule (7) | whether the exit block exists |
| UB3 | prime and mini-uber drop handlers, Torch and Soulstone (6), level 185 opening (2) | the primes' AI overrides (`0x10357ba0`, `0x10357d50`, `0x10357f00`) |
| UB4 | AI `0x1035b3f0`, nova phases and counts, skeleton waves, immunity rule, Annihilus and the 1-in-200 roll | none |
| UB5 | AI `0x10358510`, thresholds, retreat, stage portals, shard and rolls | the per-event skill choices in detail |
| UB6 | AI `0x1035dca0`, stage thresholds, portal phase, spawner, Temple portal, cube roll | the stage-5 and stage-7 skill choices in detail |
| UB7 | the whole roll and the brick (8) | none |
