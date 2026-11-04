# The remaining PD2 items and item rules

Resolves research ticket [#148](https://github.com/synpoox/pd2-resurrected/issues/148) (child of map
[#143](https://github.com/synpoox/pd2-resurrected/issues/143)). Findings only: nothing here ports anything, and no PD2 or
Blizzard file content is copied (counts, row keys and stat names only).

## Answer in brief

- **The item tables are in.** P3 merged PD2's 460 enabled uniques, 128 set items in 33 sets, 119 runewords, the affixes,
  the cube rows and the new base items. An item whose stats are all D2R stats, stats a skill formula reads, or stats
  built from a D2R `op` works today. What is left is the rules behind the stats and recipes, listed below.
- **Rows already in core-rules with no work item (13):** skill on cast, skill on block, skill while equipped, phasing,
  the one-unique-charm rule, unlimited TP/ID books, Ormus' Robes' random sorceress skill, the demonic cube reroll (op 31),
  stackable items, item casts of skills (`ItemEffect`), throwing potions, corruption in one press, and the dead
  `docrit`/`dopierce` events (unreachable, no work).
- **New engine code with no core-rules row (the main finding):** 14 PD2-only item stats have no `op`, no item event and
  no skill or missile formula that reads them. Only ProjectDiablo.dll gives them an effect, so in the port they show on the
  tooltip and do nothing. They are critical strike chance and multiplier, deadly strike multiplier and maximum, crushing
  blow efficiency, deep wounds, curse effectiveness, max curses, replenish charges, the life-steal cap, splash radius per
  missing life, damage per ethereal item, and the dye. #19's inventory missed them because it counted function columns, not
  stats. The PD2 wiki adds about ten general item rules that no table holds (ethereal durability, the open wounds rework,
  the minimum rare affix count, ...).
- **Table data only:** a few open unported rows whose work item (P3) is closed (quiver automagic, the weapon spell
  columns, the unread item columns, `cubetype`/`treasureclass`), and game checks for cube items the merge already ships
  (Lilith's Mirror, Vial of Lightsong, Larzuk's items, crafting infusions, jewel fragments).
- **Overlaps left to other tickets:** map currency, the scarab and map recipes (#144, #152); uber keys, organs, maps,
  `uber_difficulty` and the clout stats (#147, #153); item art and the shared skill icon sheet (#143, under "not yet
  specified"). PvP items are cut with PvP.

## Sources and method

| Source | Used for |
|---|---|
| `pd2-data/data/global/excel/*.txt` (the git-ignored baseline from `tools/pd2_extract.py`, PD2 install `C:\Games\PD2\ProjectD2`) | PD2's items, properties, stats, cube rows |
| `d2rAI-toolkit/d2r-data/93847/data/global/excel/*.txt` | D2R's stats, properties and item-event functions |
| `generated/data/global/excel/misc.txt` (the local merge output) | What the merge ships today (stack sizes, `pSpell`) |
| PD2's `patchstring.tbl`, read with `tools/pd2_strings.read_tbl` | What a stat or rule's tooltip says it does |
| [docs/core-rules.md](../core-rules.md), [docs/unported.md](../unported.md), [docs/roadmap.md](../roadmap.md), `merge/standins.tsv`, `merge/item-tables.txt` | What has a row or a work item |
| PD2 wiki: [General Changes](https://wiki.projectdiablo2.com/wiki/General_Changes), [New Items](https://wiki.projectdiablo2.com/wiki/New_Items), [Game Mechanics](https://wiki.projectdiablo2.com/wiki/Game_Mechanics) | Rules no table holds |

Method (throwaway scripts in the session scratchpad, not committed):

1. PD2-only stats = PD2 `itemstatcost` rows whose name D2R lacks (map stats `map_*` set aside for #144).
2. For each one: the properties that set it (`stat1-7`), and every enabled unique, set item, set bonus, complete runeword,
   spawnable affix, gem and enabled cube row that carries those properties.
3. Whether data gives it an effect: an `op`/`op base` (D2R's stat engine applies it), an `itemevent1-2` with a function D2R
   also has, or a reference by name in `skills`, `missiles`, `states`, `monstats` or `skilldesc` formulas (the merge remaps
   those). A stat with none of the three is read only by ProjectDiablo.dll. That last step is an inference from the tables;
   no stat was traced in Ghidra.
4. PD2-only base items (codes D2R lacks) by type, and the cube rows that take each.
5. The wiki's item rules against the list above; a rule found only on the wiki is marked so.

## 1. Rows already in core-rules, with no work item

Each is a core-rules row with an empty **Work item** (after the cut) and a "not yet" row on the unported list. The
carriers are what the player loses until it is built.

| core-rules row | Status there | Items that need it | Notes |
|---|---|---|---|
| Skill on cast (`item_skilloncast`, event `dospellcast`, itemeventfunc 33) | open | 16 uniques (Bane Ash, Arcanna's Deathwand, Lazarus Spire, The Third Eye, Warpspear, ...), 2 set items (Cathan's Rule, ...), 8 runewords (Destruction, Doom, Enlightenment, Infinity, Obsession, Asylum Staff, Phoenix Staff, ...), 9 magic suffixes (of Nova, of Frost Nova, of Fireball, of Chain Lightning, of Lower Resist, ...) | |
| Skill on block (`item_skillonblock`, `doblock`) | open | Dragonscale, Ephemeral, Firelizard's Talons, Mirror Shield, Skywarden | itemeventfunc 30 is D2R's (skill on death/level-up); only raising the event is missing |
| Skill while equipped (`item_skillonequip`, `equipped-skill`, func 22) | verify | Blackoak Shield (Chilling Armor SelfAura), Tempest and Uldyssian's Awakening (Energy Shield SelfAura), Spectral Plate (Phasing SelfAura), Skyfall, Innocence, Hustle, Epiphany (`es-efficiency-and-es-when-equip`) | the row names 3 items; 8 carry it |
| Phasing (stat `phasing` + state) | verify, effect unknown (Q6) | Spectral Plate only, through its equipped skill | rides on the row above; the wiki has no page for it |
| Charm "only one unique" rule | verify | Annihilus, Hellfire Torch, Gheed's Fortune (PD2's `carry1` is empty on all three) | PD2's tooltip says one unique charm of each type; the wiki says only one of each charm is active, and that active charms sit in their own inventory zone. The zone goes with the cut inventory layouts; the one-of-each part may be D2R's `carry1` (data) |
| Unlimited TP and ID books (`pSpell` 13) | open | Horadrim Navigator (`rtp`), Horadrim Almanac (`rid`); merged today on D2R's `pSpell` 2 and 1 | the wiki adds that the Almanac identifies every item at once. Both drop from Rathma: overlaps #147 |
| Random sorceress skill on striking (`sorc-skill-rand-ctc`, func 25) | open | Ormus' Robes | P12 (#108) only adds its unported row; nothing builds it |
| Demonic cube reroll (cube op 31) | open | Demonic Cube (`imrn`), 16 recipe rows (unique and set items by type) | |
| Stackable items / item quantity (`HOTKEY_ItemQuantity`) | verify | 33 rune stacks (`r01s`-`r33s`) and 14 gem stacks (flawless/perfect `*s`): no cube row makes or takes them, so only the DLL turns runes and gems into stacks; the "upgrade 3 to 1 / 15 to 5 / 150 to 50" clicks on stacked currency (PD2's tooltip); shift-click identify and the move/stack shortcuts (wiki) | the merge already ships PD2's stack sizes (runes, gems, keys and shards 50, tomes 80), which D2R's own stacking may cover; the check says how much is left |
| Item casts of skills (`ItemEffect` 183/187/188, `ItemCltEffect` 10/116/117) | open | the hit and charge procs of Lightning Bolt and Fury, Fire Bolt, Ice Bolt, Fire Ball, Lightning, Glacial Spike, Frozen Orb, the novas (Crackleshot, Thunderstroke, "of Lightning Fury", ...) | |
| Throwing potions (shit 60 -> 68, chit 65 -> 72) | open | 12 `tpot_*` items | also needs the unported row "PD2 weapon spell columns" (section 3) |
| Corruption in one press | open | every corruptible item | the alpha's two-press flow (P9) works; this only makes it one press |
| Skill on crit / pierce (`docrit`, `dopierce`) | unreachable | none | no work |

Item skills on a stand-in class rule (Dragon Flight on Mage Slayer and Stalker's Cull, Vengeance, Joust on Zerae's
Resolve, the Spirit Wolf and Fenris starts, Tiger Strike) come with their class work items (W18, W24, W29-W31). They need
no item work.

## 2. New engine code: no core-rules row yet

### 2a. PD2-only stats that only the DLL reads

None of these has an `op`, an item event or a formula that reads it. In the port each shows its tooltip line and has no
effect.

| Stat (property) | What PD2 does with it (tooltip, wiki) | Carriers |
|---|---|---|
| `item_crit_chance` (`crit-strike-chance`), `item_crit_multiplier` (`crit-strike-multiplier`) | Critical strike reworked: 75% cap, 2x on the physical part only, rolled before deadly strike (wiki) | Deception, Insight; Stalker's Cull, The Gladiator's Bane, Warshrike, Deathbit; the Throwing Knife automagic |
| `item_ds_multiplier` (`deadly-strike-multiplier`), `item_maxdeadlystrike` (`max-deadly`) | Deadly strike 1.5x, multiplier bonuses add, cap 75% and raised by items (wiki) | Deathcleaver, Fleshripper, Lance Guard, Wraithskin; Doom's Finger, Headstriker, Wraithskin |
| `item_crushingblow_efficiency` (`crush-efficiency`) | Scales crushing blow damage; Prime Evils also take less per missing life (wiki) | Giant Maimer, Ribcracker, Shatterhead, Steelrend |
| `deep_wounds` (`deep-wounds`) | Raises open wounds damage | Gutsiphon, Messerschmidt's Reaver, The Gnasher, The Meat Scraper, Malice; magic prefixes Bloodthirster's, Cultist's, Gorelust's |
| `curse_effectiveness` (`curse-effectiveness`) | Curse strength on the target (PD2 strings name it with curse resistance) | 18 uniques (Bonehew, Headhunter's Glory, Wraithskin, Templar's Might, ...), Gloom, 4 cube "added mod" rows |
| `max_curses` (`maxcurse`) | One more curse on a target | Darkforge Spawn, Ebonbane, Raekor's Virtue, Gloom, Last Wish |
| `item_replenish_charges` (`rep-charge`) | Gives back a charge every few seconds | Enigma, Oath, Stone, Naj's Puzzler, Bloodraven's Charge, Demonlimb, The Gavel of Pain |
| `lifedrain_percentcap` (`lifesteal-cap`), `inc_splash_radius_permissinghp` (`splash%/missinghp%`) | No life steal above a life fraction; splash radius per missing life | Nightmare's Feast |
| `item_dmgpercent_pereth` (`dmg%/eth`) | Enhanced damage per ethereal item worn: an `op 4` on `equipped_eth`, which nothing sets but the DLL | Purgatory |
| `item_dmgpercent_permissinghppercent` (`dmg%/missinghp%`) | The same on `missing_hp` | none today (left for completeness) |
| `transform_dye` (`dye`) | Tints an item; set by 35 cube rows | the dye recipes (needs client drawing code) |

Engine-internal PD2 stats with no item carrier (`openwounds_stack`, `curse_slots`, `equipped_eth`, `missing_hp`,
`previous_hitpoints`) belong to the rules above.

### 2b. Rules found only on the PD2 wiki

No table holds these; each is unchecked against ProjectDiablo.dll.

- Open wounds rework: stacks up to three times, damage by the attacker's level, applies through reflect.
- Ethereal items keep full durability; set items can roll ethereal.
- Rare items get a minimum affix count that rises with item level; staffmods have no upper item level.
- Loot from flying monsters over unreachable tiles lands on the nearest walkable tile; monsters killed by Chaos Sanctuary
  seals drop nothing.
- A mercenary drops its gear when another is hired.
- Unique charms sold by mistake can be bought back; some special items can't be sold.
- The gamble window has a refresh button (UI).
- Antidote, thawing and stamina potions go to the belt (may be data: itemtypes `beltable`, unchecked).
- Stash search and gold caps go with the cut stash layouts (unported list, map #143 notes).

## 3. Table data only

Open unported rows whose work item (P3) is closed. Each needs a fold into a D2R column or a cut, and none has a work item:

- Quivers lose their automagic mods (misc `auto prefix`; Arrows2/3, Bolts2/3 and the 2H Phase Blade use it).
- PD2 weapon spell columns `stat1-3`, `calc1-3`, `spelldesc*` (the throwing potions).
- PD2 item price and quality columns (affix transform/divide/add/multiply, itemtypes Charm, Gem, CostFormula).
- Other unread item columns (armor absorbs, misc special, `normcode`/`ubercode`/`ultracode`, gems nummods, sets level, runes
  server, ladder flags).
- Missing tables `cubetype` and `treasureclass`.

Merged items that need only a game check (cube rows on D2R's own ops):

- Lilith's Mirror (30 rows, `useitem` with the `mirrored` mark), Vial of Lightsong (2 rows, makes an item ethereal).
- Larzuk's Puzzlebox (9 rows), Puzzlepiece (8), Malus (5): sockets.
- Crafting infusions (7 items, 238 rows), jewel fragments (`jewf,qty=N` inputs), Torch fragments (1 row).
- `item_healafterhit` (Pus Spiter, a cube mod) uses itemeventfunc 28, D2R's heal-after-kill function, on the hit events:
  data if the function reads the stat it is given.

Stats a skill or `op` already gives an effect (data, no work): the summon counts (`extra_*`, `no_wolves`), the joust, gust
and dragon flight reductions, `item_leap_speed`, `passive_phys_pierce`, `es_efficiency`, `inc_splash_radius`,
`eaglehorn_raven`, `blood_warp_life_reduction`, `corpseexplosionradius`, `item_mindamage_energy` (`op 4` on energy), and the
split elemental skill stats (`coldskill` and the others set D2R's `item_elemskill` with func 21, as `fireskill` does).
`item_numsockets_textonly` and `mirrored` are tooltip marks.

## 4. Overlaps with other tickets (left out)

- **Map system (#144, #152):** map items and orbs (`upma`, `upmp`, `rera`, `scou`, `imma`, `imra`, `irma`, `irra`, `urma`,
  `rrra`, `fort`), Horadrim Scarab (`scrb`), Catalyst Shard (`iwss`), invader ears and `treacherous`, `heroic`, the
  `*_ear_drops` stats, cube op 30, the map rows of ops 16/18, Gheed's event inventory, and the map-only parts of Find Item
  and Desecrate.
- **Uber bosses (#147, #153):** the uber maps and keys (`dcma`, `luca`, `rtma`, `uba`, `ubtm`, `ubaa-c`, `lucb-d`), the
  organs and essences, Rathma's ingredients and splinters, the Tainted Worldstone Shard (`cwss`), `uber_difficulty`, the
  clout stats (`dclone_clout`, `rathma_clout`, `maxlevel_clout`, `dev_clout`: no carrier), and the Rathma-dropped tomes
  of section 1.
- **Art (#143, not yet specified):** PD2's item sprites (today each PD2-only item borrows a D2R picture) and the shared
  skill icon sheet for item skills.
- **Cut:** the PvP charms (`cm1p`-`cm3p`), the PvP mana potion and the PvP maps go with PvP; the active-charm zone and
  the stash layouts go with the cut inventory layouts.

## Open questions

- Is each stat in 2a really read only by ProjectDiablo.dll? Tracing one (`item_crit_chance`) in Ghidra would back the
  inference for the group.
- How much of the stackables row does D2R's own stacking already give, now that the merge ships PD2's stack sizes? The
  row's game check answers it.
