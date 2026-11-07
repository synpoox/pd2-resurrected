# PD2's uber bosses in ProjectDiablo.dll

Resolves [UB1](https://github.com/synpoox/pd2-resurrected/issues/226) ([#226](https://github.com/synpoox/pd2-resurrected/issues/226)) in [uber-bosses.md](../uber-bosses.md) ([#153](https://github.com/synpoox/pd2-resurrected/issues/153)). It reads, with Ghidra, the parts of PD2's uber bosses that
the tables do not hold: the fountain opening, the area rules, the tier scaling, the phase scripts, the guaranteed drops and
rolls, the key drop odds and Desecration. The table side is in the inventories
([#147](https://github.com/synpoox/pd2-resurrected/blob/research/pd2-uber-bosses/docs/research/pd2-uber-bosses.md),
[#185](pd2-pandemonium-uber-tristram.md)). Findings only: no PD2 or Blizzard file content is in this repo.

## Answer in brief

- **The wiki's drop odds and Desecration numbers are in the DLL, and they all match.** That covers the 1-in-200 uniques, Rathma's 1 in 50/44/38,
  the Demonic Cube's 1 in 75/50, Desecration's 50/60/60% and its eight mods, and the Jawbone's 1 in 22,500. So the numbers
  are now checked against the PD2 install's DLL and become the baseline ([#153](https://github.com/synpoox/pd2-resurrected/issues/153)). Dclone's skeleton waves at 85/60/35 and
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
- **No exit block:** PD2 does not block save-and-exit or death exits from the uber areas, and the DLL has no code for one.

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
   (5074, 5114), the Blood Fountain. `0x1037d840` returns the squared distance, so that is within about 12 subtiles (read for [UB2](https://github.com/synpoox/pd2-resurrected/issues/227)). The tier is the item's stat 185 (`uber_difficulty`). For levels 137, 161 and 188 it
   is stored in `game+0x2629`.
5. **Uber keys warp the group straight in. No portal is opened.** The code places object 587 (invisible) at the use
   spot. `0x1038a680` then warps every player in Harrogath whose squared distance to the fountain is under 0x96 and who is level 80 or more. Each
   player goes through the portal-entry code in section 3. The count is stored in `game+0x2628`, and the opening fails
   if it is 0. So the uber keys' `missiletype` is not used here. For 585 and 622 it matches the portals the fights open
   later (sections 5-6).
6. It sets `game+0x1df4` to the area type (table above). For Dclone, Rathma and Lucion it also clears the stage bytes.
7. **Tier 0:** it spawns three object-595 portals (`xactportal_grey`) at (5076, 5119), (5068, 5108) and (5079, 5110)
   around the fountain. These are the re-entries. Tiers 1 and 2 get none.

So a key's tier and target are data, and the fountain check is one distance test. [UB2](https://github.com/synpoox/pd2-resurrected/issues/227) builds this on [MS2](https://github.com/synpoox/pd2-resurrected/issues/217)'s map-use hook.

## 3. Area rules (portal entry, `0x103a9780`)

When a player enters an uber level (137, 161-163 from Harrogath, or 188), by the opening's warp or a tier-0 portal:

- **Entries are counted** in `game+0x2628`.
- **The boss heals 20% of its maximum life** on each entry (Dclone, Lucion, and both Rathma and Mendeln). This is the
  entry code's own heal; Dclone's AI has a second, separate one (section 5).
- **Pets are removed** (`0x1037fb10` with flag 0). This covers every pet type except type 3 (`golem` in pettype.txt; the Iron Golem is type 22 and goes too), and the
  hireling (type 7) is killed. This is the "mercenary killed" rule.
- **37 states are stripped** (`0x1037f4a0` with flag 1): enchant, coldenchant, venomclaws, shout, battleorders,
  battlecommand, wolf, bear, fade, quickness, energyshield, shiverarmor, chillingarmor, bonearmor, cyclonearmor,
  bladeshield, holyshield, holysword, thunderstorm, thawing, antidote, every shrine state (128-137), monsterset and
  blood_mana. Battlecry, openwounds, freeze and cold are always stripped. These are the "pre-buffs removed".
- **Tier-0 portals are single use.** Taking an object-595 portal removes it.
- **Rathma tier 0 re-entry** comes out at the current stage's spot: (0x476b, 0x35d3) after stage 1, (0x47d7, 0x3961)
  after stage 3, and (0x4e98, 0x1419) after stage 5.
- On arrival in levels 136, 137, 161-163 or 185, the level's `Waypoint` objects are removed.
- **No exit block.** PD2 does not refuse save-and-exit or a death exit, and no reader of `game+0x1df4` or of the uber
  level ids does. Tiers 1-2 open no way back, and the three tier-0 portals are the only re-entry.

## 4. Tier scaling (`0x10381160`, state 241 `uberscaling`)

PD2's monster spawn hook (`0x10371c30`) runs the map stats (`0x1038af90`), then `0x10371a70`. That runs the scaling on
each monster spawning in a PD2 level (levels 0x89-0xd2 except 0xca-0xce) without state 241, with the tier from
`game+0x2629`. **The scaling only takes the 30 monsters of the uber fights** (the set at `0x105def0c`: Dclone 789, his
skeletons 886-891 and trapped souls 790-794, Rathma and Mendeln 933-936, their golems, totem and spire 937-939 and 966,
the void knights 968, 969, 1106 and 1107, Lucion 1112 with 1113-1115, 1140 and 1141). It puts its stats in a stat list
(flags 8) under state 241, attaches it, sets the new maxhp as the base stat and sets state 241, so no monster is scaled
twice. With *e* = max(entries − 1, 0), entries being the byte at `game+0x2628`:

- **Max life:** from the total maxhp, at tier 0, ×1.5 (×3 / 2, 64-bit). At tiers 1-2, ×4 for the bosses (the set at
  `0x105deec4`: Dclone 789, Rathma and Mendeln 933/934, their clones 935/936, Lucion 1112) or ×3 for the rest. Then
  + tier × trunc(that × 20 / 100), capped at 0x7fffffff. So a boss has ×4.8 at tier 1 and ×5.6 at tier 2.
- **+4 to all skills** (stat 127) per tier.
- **The bosses only:** fire, lightning and cold absorb % (stats 142/144/148) +7×tier + 2×e, magic absorb % (146) +3×tier
  + 2×e, damage resist (36) +3×e, poison length resist (110) +3×tier + e.
- Then it calls `0x1030bd80` (a D2Common thunk on the unit, not identified) and gives the list a removal callback
  (`0x10312350`).

So monsters in maps are not scaled: they are not in the set. [#347](https://github.com/synpoox/pd2-resurrected/issues/347)
corrected three points of the first reading: the set, the poison length resist (+e, not +3×e) and the maps (the
issue's "maps get ×1.5 too" is wrong; the game check on Slot5 showed a zombie in level 138 left unscaled).

The Ghidra project's `ProjectDiablo.dll` is not byte for byte the baseline's (`pd2-baseline.json`): the same code
sits at shifted addresses and reads shifted constants. The numbers above (20.0 at `0x1044fbe8`, the sets) were read
from the baseline file with capstone, at the addresses its own instructions name.

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
| 50 Mephisto, 51 Diablo, 135 BaalCrab | `0x10357ba0`, `0x10357d50`, `0x10357f00` | the Uber Tristram primes ([UB3](https://github.com/synpoox/pd2-resurrected/issues/228)) |

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
  587 marks it. While the Rathma area is active, entering level 162 before stage 2 sets `game+0x2628` to 2, and entering
  163 before stage 4 sets it to 4 (section 9).
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

- **Stage 3, the portal phase:** Lucion goes immune and stops (mode 10, a 25-tick delay). Four LucionSpawners (1141,
  the `ECX` of each spawn call) spawn at the arena corners (x 0x622d/0x6217 by y 0x17eb/0x17d5), with no treasure and
  no experience. Then his state machine enters state 4 (below).
- **LucionControl** (1113) and **LucionSpawner** (1141) are read below ([#437](https://github.com/synpoox/pd2-resurrected/issues/437)).

#### Lucion's state machine (read for #437)

The `aip` columns are read as one int16 list *p*0-*p*23 (aip1, aip1(N), aip1(H), aip2, ...; 1.13c monstats `+0x56`),
whatever the difficulty, so each column is just one more number. The AI keeps three params: the next state, a counter,
and the state it just ran (*last*). Skills are monstats `Skill1-5`: LucionBlink, LucionNova, LucionBoneWave,
LucionPunish and MonCharge (the dash), each cast in its `SkNmode`. *aidel* is the difficulty's `aidel` minus the tier.

Each tick, before the state runs: the stage goes up by at most one (table above); the sub-step `game+0x262b` counts
down unless the state is 5; stat 446 counts down unless the state is 7 or 8. If the stage just became 3, the portal
phase runs and the next state is 4. In state 1, a player farther than √700 subtiles (or √500, 1 time in 5) makes him
blink at the target.

| State | What it does | Next |
|---|---|---|
| 0 | idle 40; stage at least 1 | 6 |
| 1 | immunity off; roll *r* < 100. *r* < *p*0: in melee range a swing (state kept); else, within 10 subtiles, 3 in 10 walk at the target and delay 15; else 9. *p*0 ≤ *r* < *p*1: 12. Else 7 if *last* ≠ 8, *r* < *p*2, stage ≥ 2 and stat 446 ≤ 0; else 4 (3 while the sub-step runs) if *last* ≠ 6, *r* < *p*3 and stage ≥ 3; else 3 (9 below stage 2 when *r* < *p*2). Idles *aidel* | as rolled |
| 2, 11 | idle *aidel* | 1 |
| 3 | attack in mode A2 | 1 |
| 4 | counter 0: the portal phase again (immune, mode 10, delay 25). Counter 1: state 146 (`invis`) with light radius −100 (`0x10380950`), `0x103a4c70`, sub-step 0, the level darkened for its players (packet 0x53, `0x1037dc70`) and sound 0xa78 (`0x1037f240`); delay *aidel* | 5, counter 6 + rand 6 |
| 5 | the frenzy, one step per tick while the counter lasts: 6 in 10 a swing (A1); else Punish at the target three times running (the sub-step 1-3), then a swing. Each swing uses up one count | 5, then 6 with the sub-step set to *p*6's low byte (Lucion: 50 ticks before the next frenzy) |
| 6 | counter 0: darkening off, sound 0x26d, idle. Counter 1: state 146 off, `0x103a4c90`, mode 11, delay 10. Then immunity off and idle | 6, 6, 1 |
| 7 | blink to the arena centre (0x6223, 0x17e1), delay 35 − 4×tier; then Nova at himself (the counter becomes 1 + rand 10000), delay 65 − 8×tier; then Nova again in mode 9 | 7, 7, 8 |
| 8 | idle; stat 446 = rand(2×(3 − tier)) − stage/2 + *p*7 (Lucion: 30) | 1 |
| 9 | Bone Wave at the target's spot | 1 |
| 10 | blink at the target, 0 or −1 subtile off on each axis | 1 |
| 12 | dash (MonCharge) to 3 subtiles past the target | 1 |
| > 12 | idle *aidel* | 1 |

So the frenzy (states 4-6) waits for stage 3 and the Nova (7-8) for stage 2.

#### Lucion's skill functions (read for [#487](https://github.com/synpoox/pd2-resurrected/issues/487))

The install routine `0x1036ba20` writes srvdo 184 `0x103b5110`, 185 `0x103b52b0`, 186 `0x103b56d0` and srvst 76
`0x103b6aa0`. Missiles are made with flags 3 (`0x10311b20`) and sent to every client (`0x1037fe90`).

- **LucionNova (184)**, monsters only: srvmissileb when the caster's AI param 0 is 8 (the second Nova), else
  srvmissilea. A ring over D2's 64 directions (D2Game `0xfb870` / `0xfb770`) from the caster, starting at direction
  *s* & 63, where *s* = `0x10380670`(AI param 1), the cast-sequence step of `0x103805f0`. Before each missile, while
  fewer than 5 gaps were made and 8 missiles have been fired since the last gap, the next step *s* rolls *s* % 100 <
  15 (2 less per gap): then it skips 6 directions. Both Novas of state 7 share param 1, so they leave the same gaps.
- **LucionPunish (186)**: calc1 srvmissilea, with no target offset, at the skill's target + (cos *a*, sin *a*) × calc2,
  *a* = 2 × 3.2 × *i* / calc1 (the constant `0x1044fb98` is 3.2, not π). For Lucion (0x458), sub-step 2 doubles the
  count and adds 3 to the radius; sub-step 3 triples it and adds 6.
- **LucionDash (srvst 76, srvdo 185)**: the start picks a free spot at the target unit (else the skill's point), stores
  it and the distance in the unit skill, and sets flag 0x80. The do function is a leap with no attack. With 0x80 it
  sets 0x1101, collision off, path type 9 to the spot at a monster's monstats Run (1.13c `+0x34`) << 9 or a player's
  0xb00 + calc2 × 0x50, and a ring (`0x1036d250`) of calc1 srvmissilea. In flight (0x100) it lays srvmissilec at the
  caster (1.13c D2Game `0xa28f0`, flags 0x21). Within 2 subtiles of the spot it puts the collision back, ends the mode
  a frame later and makes a ring of calc2 srvmissileb.

#### LucionControl and LucionSpawner (`0x1035dca0`, monsters 0x459 and 0x475)

Both idle 10 while `game+0x26fe` > 0. "In the frenzy" means Lucion (`game+0x2600`) is in state 5.

- **LucionControl**, from stage 3: while stat 446 < 1 and Lucion is in the frenzy, it counts the LucionSpawners in the
  level and, below *p*2 (6) of them and on a roll < (tier − *p*0) × count + 100, spawns one at a random arena spot
  (0x620d + rand 44, 0x17cb + rand 44), with stat 446 = rand(3 − tier) − stage/2 + *p*1 (*p*1 − 3 in the frenzy).
  Otherwise stat 446 counts down. **From stage 4** it casts Royal Fire (Skill1) at its target when stat 447 < 1, and
  sets stat 447 = rand(3 − tier) − stage/2 + *p*3 − tier (+2 in the frenzy). It idles its `aidel` (halved in the
  frenzy).
- **LucionSpawner**, from stage 3, when stat 446 < 1, spawns one add at itself: Shivering Child (1114) while fewer than
  *p*3 − stage/2 live; at stage 5 instead Smouldering Defiler (1115) on a roll < *p*0 below *p*4 − stage/3 of them; from
  stage 6 that, or else Anguished Mother (1140) on a roll < *p*1 below *p*5. Stat 446 = rand(3 − tier) − stage/2 + *p*6
  (halved in the frenzy). It idles its `aidel` (one less in the frenzy).
- Monsters 0x451 and 0x49e (a map invader portal) share the function; every other Overseer gets vanilla's AI.
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
| Uber Izual (706) | `0x10377800` | `mbr` (the other mini-ubers have their own handlers at `0x10377830`/`0x10377860`, for [UB3](https://github.com/synpoox/pd2-resurrected/issues/228)) | none |

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

For the port, the Talisman's and the Worldstone Shard's corrupted-zone condition becomes "in an active Terror Zone" at
the same TC ([#153](https://github.com/synpoox/pd2-resurrected/issues/153), [#348](https://github.com/synpoox/pd2-resurrected/issues/348)).

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

For [UB7](https://github.com/synpoox/pd2-resurrected/issues/232) this means the roll is a handful of lines over cube rows we already have. Only the port's trigger (a cube row
in the Temple, an approximated row) is new.

## 9. Open points

Nothing is left to read in the DLL. What stays is the game check: these readings are from Ghidra, not from play.

One reading is worth checking in game first:

- **`game+0x2628` in Rathma:** the tier scaling reads its low byte as the entry count, once per monster when it spawns.
  Portal entries into levels 161-163 add 1. On the normal path the group follows the retreat portal, so the stage is
  already 2 or 4 and the field keeps the entry count. Only someone entering 162 before stage 2, or 163 before stage 4,
  overwrites it with 2 or 4. That sets *e* to 1 or 3 for the monsters that spawn after, which can lower it as well as
  raise it. [UB5](https://github.com/synpoox/pd2-resurrected/issues/230) checks it.

## 10. What this means for the work items

| Item | Now known | Still to read |
|---|---|---|
| [UB2](https://github.com/synpoox/pd2-resurrected/issues/227) | opening and its warp (2), gates, fountain distance, tier-0 portals, area rules (3), tier scaling (4), key drop rule (7) | none |
| [UB3](https://github.com/synpoox/pd2-resurrected/issues/228) | prime and mini-uber drop handlers, Torch and Soulstone (6), level 185 opening (2) | the primes' AI overrides (`0x10357ba0`, `0x10357d50`, `0x10357f00`) |
| [UB4](https://github.com/synpoox/pd2-resurrected/issues/229) | AI `0x1035b3f0`, nova phases and counts, skeleton waves, immunity rule, Annihilus and the 1-in-200 roll | none |
| [UB5](https://github.com/synpoox/pd2-resurrected/issues/230) | AI `0x10358510`, thresholds, retreat, stage portals, shard and rolls | the per-event skill choices in detail |
| [UB6](https://github.com/synpoox/pd2-resurrected/issues/231) | AI `0x1035dca0` with its full state machine, stage thresholds, portal phase, controller and spawner, Temple portal, cube roll | `0x103a4c70` / `0x103a4c90` (D2Game thunks around the frenzy) |
| [UB7](https://github.com/synpoox/pd2-resurrected/issues/232) | the whole roll and the brick (8) | none |
