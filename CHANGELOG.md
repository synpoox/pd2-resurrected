# Changelog

Player-facing changes, newest first. Entries follow PD2 unless noted. Until multiplayer, a build may break characters (ADR 0003).

### Unreleased

#### Getting started

- Includes a DPS counter above the life globe.

#### Items

- Rare items roll an even spread of affix counts above their item level's minimum.
- Open wounds stacks up to three hits, and deep wounds adds to its damage.
- The Horadric Navigator and Horadric Almanac are no longer used up.
- Items with replenish charges (Enigma, Oath and others) regain their used charges over time.
- Throwing potions burst into their explosion and fire, gas, cold or lightning ring.
- Weapons lose durability half as often.
- Class items roll skill bonuses from the whole class tree, gated by each skill's required level.
- Nova item casts (on striking, charges) burst around the target, and no longer misfire.

#### Maps

- Jungle and Necropolis maps draw water in their streams beyond the copied D2R rooms.
- Jungle, Kurast and Necropolis maps grow tree walls on the near side of paths, closing most gaps in their borders.
- PD2 areas show "Entering <area>" on arrival instead of "Missing string".

#### Uber bosses

- Dclone's trapped-soul boulders hurt only through the fire they leave, not on impact.

### 0.2.0-alpha (2026-11-08)

#### Items

- Rare-only affixes (Visceral, +2 class skill helms, +3 skill trees) no longer roll on magic or crafted items.
- Larzuk's Malus shows a malus in HD, not an elixir.
- Crossbows and claws use PD2's attack speeds and breakpoints.
- Crushing blow rolls on normal hits.
- Purgatory gains damage per worn ethereal item, updated as they are put on or taken off.
- Spear and two-hand masteries boost crits and crushing blow; Smite's crushing blow hits harder.
- Ethereal items get +25% defense or damage, not +50%.
- Ethereal durability, rare affixes, mercenary gear drops, Tome of Identify and skill on cast/block follow PD2.
- A unique charm sold by mistake can be bought back.
- Quivers roll automagic again: arrows +2/10/18 max damage, bolts -2/4/6% enemy physical resistance.
- The Horadric Cube has a 4x4 grid.
- Stacks show their quantity on the picture (tomes excepted).
- Ctrl+left-clicking a held stack onto a matching stack adds one item.
- Curses follow duration resistance (75 cap on players), land on attracted units and fail when fully resisted.
- of Anima rolls on amulets, cold prefixes tint items dark blue, and potion texts are fixed.
- D2R's organ portal is off: the three organs make the Pandemonium Talisman.

#### Skills

- Dark Pact consumes a curse and explodes for magic damage that grows with the curses.

#### Uber bosses

- The Diablo Clone, Rathma and Lucion keys open their areas at the Blood Fountain (Hell, level 80+).
- The Pandemonium Talisman opens Uber Tristram at the Blood Fountain.
- Cubing the three Pandemonium keys in Hell opens a mini-uber portal again.
- Lilith, Uber Duriel and Uber Izual drop the organs again.
- Uber areas have no waypoint, and their monsters scale with the key's tier.
- A tier-0 key leaves three grey portals for one re-entry each, healing the boss 20%.
- Uber bosses left alone go idle and immune.
- Uber Mephisto, Diablo and Baal summon their minions; Conviction and Lower Resist work.
- Uber Tristram's last prime drops the Hellfire Torch and Black Soulstone and opens a town portal.
- Diablo Clone fights in phases: nova rounds at the arena centre, skeleton waves, immune while his adds live.
- Diablo Clone's trapped souls rain meteors and boulders by tier and die with him.
- Diablo Clone drops Annihilus, and at tiers 1-2 has a 1 in 200 chance at a Dclone unique.
- Mendeln and Rathma spawn in their arenas, reached by ghostly portals.
- Rathma fights by script: life events, held life, immunity and retreat portals below 15%.
- Rathma summons golems, a totem and void knights; his bone spear takes a share of max life.
- Rathma's last two clones share life; the survivor casts RathmaDeath and the drops are item level 130.
- Rathma and Mendeln drop the Tainted Worldstone Shard and leave a gold portal to Harrogath.
- Lucion fights in stages: blinks, Nova, a Punish frenzy, and spawners for his adds.
- Lucion's Nova, Punish and Dash show their effects, on the client too.
- Lucion's frenzy darkens the act and plays its sounds; his immunity strips poison and Open Wounds.
- Lucion's death opens portals to the Temple and Harrogath, ends his adds and drops a Demonic Cube at tiers 1-2.
- The Temple's chests drop Lucion loot, one more item per tier.
- In the Temple, cube a corrupted amulet to try a Desecration (50%, 60% at tiers 1-2); failure bricks it.
- New monsters (Lucion, Na-Krul, Iskatu and others) finish their attacks and keep fighting.
- Cross-act portals are visible, drawn as D2R's red portal.
- Royal Fire casts its ring of fire.

#### Maps

- Map events spawn in tiered maps: Altar of the Catalyst, Spire of Darkness, Void/Tomb, Gheed, Horazon, Shadow of Mendeln, Dark Wanderer, Treasure Fallen.
- A map event's spawn sends its notify line in red chat.
- Killing Horazon drops maps and opens a portal to a map of the same or next tier.
- Leoric, the Siege Beast, Sharptooth, Zhar and the other map bosses spawn, run their AI and summon adds.
- Unique map bosses leave a portal onward and drop charms, facets and puzzle pieces.
- The Outer Void has three random bosses, and its portals lead deeper, then home with uber parts.
- Zhar's Sanctum has seal portals and tomes; Zhar's charms get a skill tab prefix.
- Monsters in maps can drop unique maps.
- Fortified, drop-bonus and item-drop map stats apply on kills; fortified maps no longer double key rolls.
- Duplicated maps spawn their boss twice.
- A map's area stats apply on all its levels.
- Teleport blockers stop teleports past them.
- Map tooltips show the tier, without raw format codes.
- More maps draw walls, props and tree borders in HD; trees no longer block paths or hide the character.

#### Sounds

- Zhar the Mad has his own voice.

#### Fixes

- Uber, map and skill states (immunity, Hunger, Desecrate, Vengeance) are real states, so their checks fire.
- Druid vines eat each corpse once.
- Split Throw's weapons show, split and return.
- Legacy graphics draws D2R's ground and PD2's maps in the right colours, and shows PD2's missiles again.
- The Ascendant equips staves only; its title and skill icons show.
- PD2's own items show on the ground in HD again and keep their own pictures.
- Druid and Assassin class skills on items show their number.
- Leoric's taunt no longer crashes the game.
### 0.1.0-alpha (2026-11-05)

The first alpha: every Project Diablo II (PD2) class plays as in PD2, offline single-player, with PD2's maps. The uber
bosses and the Warlock redesign come in a later alpha.

#### Getting started

- You need D2RLoader: characters use its extended save format and load only through it.
- Characters from test builds before this release don't load.
- Includes a loot filter, stash search, item roll ranges, a charm inventory, a buff bar and left-click Paladin auras.
- PD2's names and descriptions for items, skills, monsters and areas (English).

#### Items

- PD2's items, affixes, runewords, cube recipes and treasure classes.
- Corruption: cube an item with a Worldstone Shard, once per item.
- Demonic Cube rerolls a unique or set item, keeping its base, sockets, socketed items and ethereal state.
- Cube recipes count a stack as its quantity (a stack of 3 El makes Eld); unused units stay in the cube.
- PD2's combat stats: critical and deadly strike, crushing blow, open and deep wounds, life steal cap.
- Curse resistance and extra curse slots.
- Skill on cast procs for every item that has it; skill while equipped and Phasing work.
- Ormus' Robes roll a random Sorceress skill.
- The Warlock gets PD2's class item rules (Hellfire Torch, Spirit Ward, affix rates).

#### Monsters and areas

- PD2's monsters, mercenaries, areas, shrines and objects.
- Tyrael hires the Ascendant.
- Cycle of Life and Vines heal everyone nearby when they eat a corpse.
- Torajan's eggs burst in a poison nova as they hatch.
- Minions raised by boss missiles drop no loot and give no experience.

#### Skills

These now work as in PD2. Skills not listed still play like the closest D2R skill.

- **Every class:** melee splash, skills start during Inferno and Arctic Blast, hit recovery and block end on PD2's frame.
- **Amazon:** Lightning Bolt, Lightning Fury, Exploding Arrow, Multiple Shot, Power Strike (Valkyries too) and Shattering Arrow.
- **Assassin:** the charge-up skills, Dragon Flight, Psychic Hammer and Blade Fury.
- **Barbarian:** Double Throw, Split Throw, Leap, Leap Attack, Bash, Find Potion and Find Item.
- **Druid:** Arctic Blast, Firestorm, Gust, Hunger, Fire Claws, Spirit Wolf and Fenris.
- **Necromancer:** Poison Dagger, Blood Warp, Desecrate and Golem Mastery.
- **Paladin:** Charge, Holy Light, Fist of the Heavens, Vengeance, Joust, Holy Sword, Smite and Blessed Hammer.
- **Sorceress:** Fire Bolt, Ice Bolt, Blizzard, Chilling Armor, Static Field, Combustion, Teleport, Inferno and Thunder Storm.

#### Interface and graphics

- Skill trees use PD2's layout and icons.
- Skill tooltips show the right numbers, with no stray format codes.
- PD2's monsters and effects show in HD.

#### Maps

- Maps drop; open one in Harrogath in Hell (level 80+) for a red portal, one map per game.
- A map's monster, player and area stats apply.
- A map can't be cubed with a recipe PD2 forbids for it.
- Maps built on D2R's tiles draw in HD; many walls, streams and bridges are still missing.
- Map walls show on the automap.
- A map's corrupted, socketed and ethereal drop chances apply.

#### Bugfixes

- Right-clicking an Identify scroll or tome enters identify mode again.
- Wands, staves and scepters show their class skill instead of "an evil force".
- Item stat lines no longer show their value twice.
- Runes and gem stacks show their own pictures instead of an Elixir's.
- Legacy graphics no longer crash with a PD2 skill on a skill button.
