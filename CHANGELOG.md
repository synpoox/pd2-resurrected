# Changelog

Player-facing changes, newest first. One section per version, heading `### <version> (<YYYY-MM-DD>)`. Until the first
private-multiplayer release a build may break characters; each break is noted here (ADR 0003).

### Unreleased

#### Items

- Purgatory's damage follows ethereal items put on or taken off while it is worn.
- D2R's organ portal to its Uber Tristram is off: the organs make PD2's Pandemonium Talisman.
- Item crushing blow rolls on normal hits, and Purgatory gains damage per worn ethereal item, as in PD2.
- Spear and two-hand masteries boost crits and crushing blow, and Smite's crushing blow hits harder, as in PD2.
- Ethereal items get Project Diablo II's +25% defense or damage, not +50%.
- Items follow PD2 more closely: ethereal durability, rare affixes, mercenary gear drops, Tome of Identify, skill on cast/block.
- A unique charm sold to a vendor by mistake can be bought back, as in PD2.
- Quivers roll PD2's automagic again: arrows +2/10/18 max damage, bolts -2/4/6% enemy physical resistance.
- The Horadric Cube has PD2's 4x4 grid.
- Stacks in the inventory, cube and stash show their quantity on the picture, as in PD2 (tomes excepted).
- Curses follow PD2's duration resistance (75 cap on players), land on attracted units, and fail when fully resisted.
- Ctrl+left-clicking a held stack onto a stack of the same item adds one item to it, as in PD2.
- of Anima rolls on amulets, cold prefixes tint items dark blue, and potion texts are fixed.

#### Uber bosses

- Rathma left alone in his arena idles and turns immune for a short while, as in PD2.
- Uber Mephisto, Diablo and Baal summon PD2's minions: skeletons, Pit Lords, vampires and wraiths.
- Uber Mephisto's Conviction and Uber Baal's Lower Resist work as in PD2.
- PD2's new monsters (Lucion, Na-Krul, Iskatu and others) finish their attacks and casts and keep fighting.
- The Pandemonium Talisman opens Uber Tristram at the Blood Fountain, like the other uber keys.
- Cubing the three Pandemonium keys in Hell opens a red portal to a mini-uber area again.
- Lilith, Uber Duriel and Uber Izual drop Diablo's Horn, Baal's Eye and Mephisto's Brain again.
- The Diablo Clone, Rathma and Lucion keys open their uber areas at the Blood Fountain (Hell, level 80+); the fights come later.
- The uber areas have no waypoint, also after a key's warp.
- A tier-0 uber key leaves three grey portals for one re-entry each, healing the boss 20%.
- Lucion fights in PD2's stages: blinks, Nova, a frenzy of Punish, and spawners that call his adds.
- Lucion's blink and dash show their novas on the client too.
- Rathma and Mendeln fight by PD2's script: life events, held life, immunity, and retreat portals below 15%.
- Mendeln, Rathma and their clones spawn in their arenas, each reached by a ghostly portal, as in PD2.
- Diablo Clone fights in PD2's phases: three nova rounds, three skeleton waves, immune while his adds live.
- Diablo Clone runs to the arena centre for his novas, and uber bosses left alone go idle and immune, as in PD2.
- Diablo Clone's trapped souls rain meteors and boulders, faster and tighter by tier, and die with him.
- Uber fight monsters scale with the key's tier, as in PD2.
- PD2's cross-act portals (UB2 re-entry, Lucion's Temple) are visible, drawn as D2R's red portal.
- Lucion's death opens a portal to the Temple, ends his adds and drops a Demonic Cube at tiers 1-2.
- Lucion's death also opens a portal in Harrogath back into his arena.
- The death novas of Rathma, the Nihlathak map boss and DollMeteor show, as in PD2.
- After Lucion's death, cube a corrupted amulet in the Temple to try a Desecration (50%, 60% at tiers 1-2).
- Diablo Clone drops Annihilus, and at tiers 1-2 rolls 1 in 200 for one of PD2's five Dclone uniques.
- Rathma and Mendeln drop the Tainted Worldstone Shard and PD2's rolls, and leave a portal to Harrogath.
- Diablo Clone's skeleton summon raises PD2's three waves around his arena's centre.
- Uber Tristram's last prime drops the Hellfire Torch and Black Soulstone and opens a town portal.
- Royal Fire (Lucion's controller, Radament's map boss) casts PD2's ring of fire, aimed into his arena for Lucion's.
- Lucion's Nova leaves PD2's gaps, Punish rings its target, and his Dash leaps with a trail and a landing nova.
- The Temple's Lucion chests show and drop PD2's LucionChest loot, one more item per uber tier.

#### Maps

- The Gheed map event spawns Gheed in tiered maps, as in PD2.
- The Horazon map event spawns Horazon in tiered maps; only that Horazon opens his portal, as in PD2.
- Unique map bosses leave a portal to Harrogath (or onward) and drop PD2's charms, facets and puzzle pieces.
- The Warlord of Blood and Ureh's boss drop PD2's random uniques, and the Warlord's arena starts empty.
- The Outer Void has PD2's three random bosses, and its portals lead deeper, then home with uber parts.
- Monsters in maps can drop unique maps, and Horazon has a 10% chance to drop one, as in PD2.
- Fortified maps no longer double key and unique map rolls on map bosses, as in PD2.
- A map event's spawn sends PD2's notify line ("Evil beckons you from the Void...") in red chat.
- Fortified, drop-bonus and item-drop maps give their extra and stat drops on kills, as in PD2.
- Map tooltips show the map's tier, and map stats read without raw format codes.
- Killing Horazon in a tiered map drops maps and opens a red portal to another map of that tier or the next.
- Duplicated maps (`map_glob_extra_boss`) spawn their boss twice, as in PD2.
- Map events Shadow of Mendeln, Dark Wanderer and Treasure Fallen spawn in tiered maps, as in PD2.
- Shadow of Mendeln's Nihlathak sends mob waves, then casts his death nova and dies, as in PD2.
- A dungeon or unique map's area stats (density, rarity, added monsters) now apply on all of its levels.
- Teleport blockers in Realm of Terror, Sanctuary of Sin and the Imperial boss map stop teleports past them, as in PD2.
- Wooded PD2 maps draw HD tree borders, and D2R-tile maps use whole D2R rooms.
- Eight more PD2 maps, among them Ashen Plains and Descent, draw walls and props in HD.
- Wooded maps in HD fill the gaps in their tree lines and no longer plant trees on walkable ground.
- Wooded maps in HD keep tall trees off the near side of paths, so they no longer hide the character.

#### Sounds

- Zhar the Mad attacks with his own PD2 voice.

#### Fixes

- Rathma's portal to Harrogath is gold, and the shard names lose a stray `*/`, as in PD2.
- Split Throw's weapons show, split and come back to the Barbarian as in PD2.
- Legacy graphics no longer draws the ground in D2R's own areas as red and cyan noise.
- Legacy graphics draws PD2's maps (Mesa, Tomb Map, Graveyard Map and others) in their own colours.
- The Ascendant mercenary equips staves only, as in PD2.
- The mercenary panel shows the mercenary's title instead of "an evil force".
- PD2's own items (Worldstone Shards, maps, PD2 uniques) show on the ground in HD again.
- Legacy graphics shows PD2's own missiles again (Holy Nova and others).
- Charged potions and other PD2-only items show their own pictures instead of a hand axe.
- Druid and Assassin class skills on items show their number ("+2 to Assassin Skill Levels").
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
