# Changelog

Player-facing changes, newest first. Entries follow PD2 unless noted. Until multiplayer, a build may break characters (ADR 0003).

### 0.2.2-alpha (2026-11-21)

- Invaders now wear their real player-class art in legacy and HD, and drop the weapon they carry. (#1215)

#### Features

- Controller: LB + RB + A sends a whole stash or stack-tab stack to the Cube.
- Pad: RB + A on a stack-tab stack moves 5 to the inventory, LB + RB + A sends it to the Cube (#1515).
- Corrupted Annihilus and other dyed items now show a red-tinted picture in the inventory. (#1529)
- The zone info now shows Monsters remaining: N (N+ until every room is visited). (#1534)
- Bind and Summon plugin included: summons and Bind Demon return after a rejoin, and skeletons rise without a corpse (#1535).
- Map tooltips list the immunities their monsters have, like "Immunities: C120, F120" (#1552).
- An unidentified map gives +30% Item Quantity: kills roll the monster's drops again more often (#1596).
- Mercenaries wear an amulet, two rings, gloves, a belt and boots, and gain their stats (#1627).
- The mercenary panel lays out its gear like the inventory, HD and legacy, in a taller panel (#1651).
- The controller mercenary panel lays out its gear like the controller inventory, in a taller panel (#1657).
- Controller: the Lock Inventory padlock shows at the inventory grid's bottom right and works with the pad.
- Controller: on the Maps stash tab, LB/RB pick the base and the left stick click the tier (#1669).
- The Lock Inventory padlock is larger, just under an inventory cell, in HD and legacy.
- Controller: the stash Maps tab shows a legend for LB/RB (base) and the left stick click (tier).
- Controller: a "LB + A Split Stack" legend shows in the inventory while a stack is held.

#### Fixes

- Show a Sacrifice tooltip when hovering over the altar button (#1530).
- Fixed the All Attributes bonus text on item tooltips (#1531).
- Restored the Palace Cellar Level 1 waypoint name.
- Fix dropped Worldstone Shards to look like crystals in HD. (#1533)
- Stacks of gems, runes and other materials keep their full count in the Gems, Materials and Runes stash tabs (#1537).
- Restored the Cairn Stone order on the deciphered Scroll of Inifuss (#1538).
- Throwing weapon stacks and gamble prices now cost what they do in PD2 (#1539).
- Fix starting equipment and inventory sizing for new Classic and Expansion characters (#1543).
- Act 1, 2 and 5 mercenaries no longer crash the game when you change area after they die.
- Restored the original D2R inventory settings for mercenaries and the Ancients.
- Fixed the display of Zinthros's name for the Act 3 mercenary (#1571).
- The Horadric Navigator and Almanac can be put on the skill bar like the Tome of Town Portal (#1591).
- Larzuk's Puzzlebox, Puzzlepiece and five other rare items now drop from monsters (#1600).
- Map events show their cross on the minimap again (#1601).
- Big fights no longer stutter from log writes on every hit, like Multiple Shot's arrows; those lines now need PD2R_HOT_LOGS (#1609).
- Superior items no longer count their stats in vendor prices, and repair costs cap at 250000, as in PD2 (#1625).
- Healers (Akara, Drognan, Fara, Ormus, Jamella, Malah) cure poison again.
- Monsters Remaining is a zone info row (hides with TAB, not in towns); 25+ above 25 or under 90% explored.
- Controller: the stash's Maps and Bossing tabs and page stop work with a rebound LT/RT, pads 2-4, DS4 and Steam Input.
- Controller: the stash Personal tab reads "Personal" in full.
- Hide the Pyre Stash Search box in the controller stash (#1684).
- Controller stash: Maps and Bossing use the Mats tab frame, with no extra borders or shift.
- Inventory hotkeys: drop and Cube move work with any panel open, Identify uses any ID item, all play sounds (#1698).
- The Cube grid and Transmute now work on the Maps and Bossing stash tabs; their unused cells are gone. (#1699)

#### Getting started

- See the [Linux setup guide](docs/linux.md) to play with Wine or Proton (#1541).
- Ctrl+clicking a map, uber item or map crafting item into the stash files it on its own shared stash tab. (#1553)
- The stash gets Maps and Bossing tabs: maps sort by tier and base, boss items by boss, round the cube. (#1553)
- PD2 inventory hotkeys: Ctrl/Shift+right click drop, stash, Cube; configurable. (#1567)

#### Changes

- Updated the bundled Pyre Loot Filter to 1.3.1+rev.7.
- Added +35% movement speed to Stamina Shrines (#1572).
- A padlock in the inventory locks your and your mercenary's equipment against clicks and hotkeys. (#1578)
- Fixed map portals failing to open on some D2R 3.4 setups.
- Spire of Darkness shadows can drop Keys of Terror, Hate and Destruction again. (#1582)
- Redesign six unique quivers and use base artwork for other equipment (#1584).
- Refresh miscellaneous item icons and restore the organ portal to Uber Tristram (#1585).
- Removed 40 retained PD2 map-item icons; map uniques use stock or Cartographer icons (#1587).
- Classic missile art of #1588 keeps its HD stand-in models, now checked by a test.
- Fix a stall when a map opens by writing its one-time log lines only with PD2R_HOT_LOGS=1. (#1593)
- Leoric's Passage maps spawn undead again instead of packs of cows (#1595).
- Arrows, bolts and throwing weapons never run out, as in PD2 (#1598).
- Fixed missiles stopping at invisible walls over the void in Horazon's Memory.
- Map tuning: two renames and new sizes, shapes, densities and PD2 monster lists (#1599).
- Fixed fights freezing when Cartographer logged every kill and drop.

#### Bug fixes

- Quest rewards Potion of Life, Scroll of Resistance and Book of Skill can be used again (#1643).
- The Maps stash tab shows its label, and the Shared tab no longer pages into the Maps and Bossing pages. (#1653)
- Lock Inventory shows its `Lock Inventory` hover in HD and now has the padlock in legacy graphics too. (#1659)
- Pad players get the Maps and Bossing stash tabs, and pad paging on Shared stops at page 15. (#1661)

#### Bug Fixes

- On a controller, the Lock Inventory padlock keeps focus after A, so A again unlocks it.
- Pyre Stash Search updated to 1.3.1+rev.7: controller tab flyout, item hover cards and a duplicates filter. (#1691)
- Item Roll Ranges updated to 1.3.1+rev.61: faster hover tooltips and more range breakdowns. (#1692)
- Controller inventory shows the charm-row tint on rows 5-8, as the mouse layout does. (#1701)
- Invaders now wear the gear they will drop, and roll their weapon set with PD2's odds. (#1708)
- Invaders roll their gear when they spawn and drop exactly that gear on death. (#1708)
- A closed invader portal now disappears in HD, as it does in legacy. (#1711)

### 0.2.1-alpha (2026-11-18)

#### Maps

- The uber areas, Throne, Pandemonium, Realm of Terror and Kanemith draw HD floors, and walls where D2R has a match.
- Ten more PD2-only areas draw walls and props in HD, among them Ruined Cistern, Ureh City and Black Abyss.
- Mesa, Lava and Fortress maps draw ground beside copied D2R rooms instead of black gaps.
- Demon Road, Bitter Depths and Dark Temple draw walls and props in HD.
- Dark Wanderer bosses drop 4-6 Worldstone Shards.
- Each native map has its PD2 map boss: King Leoric, The Guardian, Nathkill The Numb, The Great Worm, Shattered Soul.
- The Shadow of Mendeln drops its loot again when it fades.
- The Treasure Fallen drops its loot as it is hit and on death, ten times in all.
- The Diablo Clone arena now holds four trapped souls, N/E/S/W around him.
- The Temple of the Triune now holds six chests.
- 20 new maps, one for each Cartographer tileset, each with its own icon.
- Four Cartographer maps now carry PD2 map names: Cathedral of Light, Ancestral Trial, Torajan Jungle and Canyon of Sescheron.
- Splashing map affix rolls on maps (monsters splash on melee hits).
- Each Cartographer map now spawns monsters from the matching PD2 map area, using the map monster variants.
- Map recipes take stacked normal maps: one stack of 3 or more upgrades or merges, and the leftovers keep their Area Level.
- The five original maps now have their own icons instead of the shared scroll picture.
- Every map's icon colour now matches its tier: T1 white, T2 gold, T3 red.
- The map line "Extra Monster Groups" now names the group it adds, for example "+1 Extra Monster Group: Hell Temptresses".
- A map with Smith, Armorer, Crafter or Jeweler affixes now says which kind of bonus drop each gives, instead of identical "Chance of a Bonus Drop" lines.
- Using a stack of maps opens one map and leaves the rest of the stack.
- Ruins of Viz'jun and River of Blood get their PD2 bosses; Johnathan, Indio and Rotting Horror return.
- Maps have more monsters and uniques; six maps are now PD2 maps with their monsters and bosses.
- The invaders event is back: its portals open and corrupted invaders step out, dropping their class ears and gear.
- Siege maps (Arreat Battlefield) end in a cliff and a ravine instead of a black field at their west and east ends.
- A native Tunneling Depths Map opens a Cartographer area in Hell, with its own native properties.
- Native maps for the Crypt, Ice caves, Jungle and Snow tilesets drop beside Tunneling Depths from one shared pool.
- Wanderer, Mendeln, Treasure Fallen and Gheed events run on native maps.
- Uber keys open their areas again with Cartographer installed; unfinished uber areas refuse without using the key.
- A Catalyst Shard cubed with a native map forces its event again.
- The Void/Tomb map event opens its portal to the Bitter Depths again, a square arena built by Cartographer.
- PD2's map items, affixes, recipes and drops are removed; Cartographer's maps replace them (old map items are not converted).
- Altar of the Catalyst stays PD2's own event beside Cartographer, so its Catalyst drops differ from Cartographer's.
- `core-maps` is now `core-events` (events and the uber fights); maps are Cartographer's.
- Updating by hand: delete `mods\PD2Resurrected\d2rloader\plugins\core-maps.dll`, it is now `core-events.dll`.
- Native map items now use the codes m01 to m05 (was cmc, cmr, cmi, cmj, cms).
- Map events fire on every Cartographer map, and `core-events event <name>` forces the next one for tests.
- Lucion spawns in his arena again (the key opens it, and he and his controller appear).

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
- Monsters cleared from Chaos Sanctuary when Diablo arrives drop nothing.
- Nova item casts (on striking, charges) burst around the target, and no longer misfire.
- The Horadric Almanac identifies again on right-click, beside Cartographer, through item spell use slot 14.

#### Maps

- Jungle and Necropolis maps draw water in their streams beyond the copied D2R rooms.
- Jungle, Kurast and Necropolis maps grow tree walls on the near side of paths, closing most gaps in their borders.
- Ashen Plains and other outdoor maps grow trees on the blocked ground past their edge walls.
- Frozen Forest and other outdoor maps close more gaps in the tree lines beyond their paths.
- Fortress Map draws the walls PD2 added inside copied D2R rooms, closing most of its border gaps.
- Tree tiles no longer draw a stone wall or cliff, which hid Fortress Map's waypoint court.
- Copied D2R rooms paste only where the map's walkable floor matches theirs, so Fortress Map draws its own walls.
- Halls of Torture Map draws stock temple pillars and cages where PD2 drew its own pillars.
- PD2 areas show "Entering <area>" on arrival instead of "Missing string".
- Dungeon boss barriers show as iron gates instead of invisible walls.
- Map waypoints draw their platform, and no stray waypoints stand on plain ground.
- Westmarch's flat ground no longer draws Harrogath stair pieces.
- The Sewers map no longer grows Kurast's jungle trees and vines on its floor.

#### Uber bosses

- Dclone's trapped-soul boulders hurt only through the fire they leave, not on impact.
- Uber fight monsters are tier-scaled again with Cartographer loaded.
- Desecration works again in the new Temple of the Triune after a Lucion kill.
- Diablo Clone fights in a desert arena of his own.
- Lucion fights in a Kurast arena of his own.
- Dclone's summon waves spawn around his arena's centre and come at you.
- Open wounds on one of Rathma's clones now splits to the other clone, like PD2.
- Rathma's key opens his three stages again, in new square arenas.
- Lucion's key opens a new square arena, and his Temple has its chests and altar again.
- Uber ingredients drop again from Hell Mephisto, Diablo and Baal (also in Desecrated) and from map bosses (map bosses about 1 in 9 with one player).

#### Fixes

- PD2 misc items (maps, stacks, essences) no longer show a potion on the ground.
- Lucion now shows his nameplate when hovered.
- Rune and gem stacks show their own rune or gem on the ground, not El or a chipped gem.
- Installing into a test slot keeps `cartographer.dll` instead of moving it away.
- Rathma's arena rooms spawn none of their own monsters again next to Cartographer.
- Characters saved on the earlier alpha load again with no "Character environment mismatch".
- Fixed a crash entering the Cow Level and other levels, in the uber monster scaling.
- Multiple Shot's missile damage bonus is back, and Demon Machine's proc arrows trigger nothing.
- The Sacrifice Altar shows in HD.
- Rathma's bone form, the Demon Pillar, Zhar and Lucion show in legacy view and can be hovered.
- The Diablo Clone, Lucion and Temple arenas no longer hold stray Act IV monsters.
- Dire Wolf, Fire Golem and other summons and bosses now cast their intended hit and death skills.
- Shrines work as in Project Diablo 2: a gem shrine upgrades one gem off a stack, nearby players share boost shrines, and resist shrines add +10 to the max resist.
- Shadow of Mendeln's wave champions and uniques now spawn as champions and uniques.
- Telekinesis, Confuse, Blessed Hammer and the monster hydras work again.
- Berserk and Blade Dance now play their body motion in HD.
- Treasure Fallen no longer mixes up its drop count with an earlier game when it dies right away.
- Cast Triggers (skill on cast and on block) now starts on every launch (#1180).
- Our plugins now load from the mod folder; updating moves the old game-wide copies out (#1180).
- Runes inside runewords show their names instead of Missing String.
- The Sacrifice Altar opens a panel: place a corrupted amulet and confirm to Desecrate it.
- Using the Sacrifice Altar after Lucion's death now runs Desecration on the amulet in the cube.
- Rathma's stage portals to the next level now open where they can be reached, not behind a pillar.
- Entering an arena shows its own name instead of Missing String.
- No more random shrines, wells, chests or barrels in the arenas.
- Invaders are visible and animate in both graphics modes.
- The Invader portal shows as a red portal in HD.
- Dclone now plays his run animation in HD.
- Lucion now plays cast and run animations in HD.
- Lucion no longer rises off screen when he dashes.
- Both of Rathma's clones now spawn in the last stage.
- The Sacrifice Altar no longer risks a crash with no game.
- Lucion's dash no longer misbehaves after an earlier dash was cut short.
- tools/install.ps1 refuses a test slot the run has not locked.
- Dark Pact's damage synergies from curses work again.
- Arriving in the Diablo Clone, Lucion and Rathma arenas no longer puts you on top of the boss.
- Diablo Clone's fire nova now draws as a fire nova in HD.
- Rathma's first stage no longer opens a portal until the boss dies.
- Diablo Clone's soul pillars can be targeted and block missiles.
- Diablo Clone's soul pillars can no longer be killed.
- The map tooltip shows Tier 1, 2 or 3 correctly on every class.
- A new map dropped onto a stack of maps you saved earlier now merges with it (#1340).
- Corrupted, Area Level, density, magic find and experience list first; Corrupted no longer recolours lines after it.
- The map info panel no longer lists the Corrupted and Corruptor marks under "Players have".
- Desecration and the corruption roll write the right properties, not a map stat.
- The Sacrifice Altar panel shows one blank slot and a bigger close button.
- The Sacrifice Altar can be used once per game, as in PD2.
- Charms in inventory rows 1-4 are tinted red, like PD2.
- Two same-name unique charms in the lower charm rows now both show red, as they give no stats.
- Legacy: the "of the Adventurer" map affix now has its name, and the Fortified, Imbued, Scouring and Upgrade-map orb descriptions match the HD text.
- The Large map prefix now shows its name in legacy graphics.
- Magic item prefixes and suffixes that showed no name in legacy graphics now show it.
- Monsters in Horazon's Memory and other native maps now draw in HD.
- Orb of Fortification works on a normal map or a stack of normal maps, and leaves the rest of the stack.
- Maps no longer drop as magic from monsters.
- A map boss now drops the loot of the map's tier (T1, T2 or T3), not one fixed tier per boss.
- The Sacrifice Altar can be opened and closed with a controller from the START menu.
- With a controller, the Sacrifice Altar panel now takes the D-pad and closes with B.
- Holy Sword with a two-handed sword now parries attacks like it does in Project Diablo 2.
- Mass Identify and Item Durability are no longer bundled.
- The Potion Counter now shows each belt potion picture above its belt slot on the controller HUD.
- Ethereal items now keep their full maximum durability instead of half.
- The Sacrifice Altar panel opens only from the altar in the Temple. The controller inventory tab bar no longer shows an altar tab.
- Assassin claws now block while walking and running, as in Project Diablo 2.
- Throwing weapons keep their stack when their durability runs out, and repair to full.
- Dodge and avoid now roll while walking and running, after a failed evade, as in Project Diablo 2.
- The Horadric Cube in the stash's Gems, Materials and Runes tabs shows its full 4x4 grid.
- A map split off a stack stacks again right away, without a new game.
- Mercenary skills with no icon (Vigor, Meditation, Meteor, Blizzard, Whirlwind, Strafe and more) now show HD icons.
- A map used from a stack opens at its real Area Level, not double.
- Cast Triggers now loads alongside Controller QoL (QoL's cast observer is off).
- The Ascendant mercenary shows its own legacy sprites and a Zakarum Council HD model.
- Skill-on-cast and skill-on-block procs (Ormus' Robes, Dragonscale) work again.
- Socketed gems and runes no longer apply their bonuses twice.
- Bone Spear, Teeth and other multi-missile skills no longer hit monsters off the visible missiles.
- Move Only keeps its own icon on the skill button instead of another skill's.
- Frozen Orb plays its cast animation again.
- Hydra and Lesser Hydra last their full duration again.
- The Warlock can now use Move Only like the other classes.
- The Scroll of Inifuss can be right-clicked and read again.
- The gate no longer leaves a skipped `table-diff` check on a PR when a label is added or the PR closes (#897)
- The Altar guardian's death is now heard on native maps, so its clones clear and the shards unlock.
- Map events (Spire, Gheed, Mendeln and the rest) now finish on native maps.
- The Almanac identifies Cube items; use it from the inventory, since D2R can't use items in the Cube.
- Restore PD2 item names ("an evil force") on the Cartographer build.

#### Features

- The Dark Wanderer map event spawns a buffed boss a few seconds after the Wanderer dies.
- Horazon's death opens a red portal to a second map at the source map's level.
Local install now includes Cartographer and the companion plugins.
- The download now includes the license and notice files.
- Maps now come in Tier 1, 2 and 3 (area level 87-89) and never roll magic.
- Rare maps roll 27 of PD2's map affixes (9 prefixes, 18 suffixes) in their tier bands: monster life, damage and speed, player stats, density and exp.
- Rare maps now roll many more of the map modifiers: monster elemental damage, attack and defence modifiers, extra monster groups and an extra boss, more monster rarity, and bonus item drops (weapons, armor, crafting items, jewelry). Only the Splashing modifier is still missing.
- Fortified maps now also give +100% chance for monsters to drop extra items.
- Corrupting a map can now also give deadly strike, cannot-be-frozen, more monster rarity, or bonus jewelry drops.
- Anya in Hell also gives you 3 random Tier 1 maps (item level 99) with her reward.
- Normal maps with the same stats now stack up to 50; rare maps and maps with a different Area Level or event do not.
- Cube a rare map with a class invader ear: +50% chance for monsters to drop extra items, that class's ear drops, and two class modifiers (some weaken the player). One ear per map (#1347).
- The charm area (inventory rows 5-8) has a slight tint in HD.
- Controller QOL Updates (PyreFly) now comes with the mod, for smoother controller play.
- After a block, parry, dodge or evade a player cannot avoid again for 4 frames, as in PD2.
- Zhar, Leoric, Na-Krul and the Butcher use PD2's own voice and combat sounds.

#### Changes

- The uber arenas are now small enclosed rooms; Diablo Clone fights in a swamp.
- The legacy inventory now shows PD2's whole panel: helm slot, I/II tabs, 8 rows, gold and close strip.
- The legacy inventory's equipment slots and grid now sit exactly inside the frames PD2's art draws.
- You can carry a second unique charm of the same name; two of them in inventory rows 5-8 both give no stats until one is moved out.
- The HD inventory's 10x8 grid now has full-size cells and fills the panel width.
Cartographer maps get PD2's map cube recipes, and the Cartographer's and Fortify orbs are back.
- Maps drop in Hell as a T1 pool; map drop tables are PD2's.
- Diablo Clone's nova-phase missiles show in HD again.
- Dclone's ring of fire draws its tall flame pillars in HD, and his fire wall uses Diablo's HD flames.
The Sacrifice Altar panel uses the inventory's amulet slot art, drops its inner X button and centers its contents.
Cubing a unique, rare or crafted quiver with PD2's tier recipe upgrades it and keeps its affixes.
Unique, rare and crafted quivers can now be upgraded to their next tier in the cube.
- With a controller, all 8 rows of the inventory can now be seen and used.
- With a controller, hold LB (or your Controller QoL modifier) and press A to put down one item of a held stack.
Includes a Potion Counter that shows PD2-style potion pictures above the belt.
- Runs on D2R 3.4 (build 95032) as well as 3.3 (#1509).
- Charms work only in inventory rows 5-8, as in PD2. The launcher removes the old Charm Inventory plugin; a hand install must delete d2rl-charm-inv.dll and .mpq.
- The inventory is now 10x8 as in PD2, and the Charm Inventory is gone: empty it into your inventory or stash before updating.
- The Uber Tristram key opens D2R's Tristram arena, fought with PD2's primes, minions and drops.
- Horazon drops six native maps and opens no portal for now (#959, portal in #1028).

#### Tools

- install.ps1 -Data installs into a test slot again, through the toolkit's slot_install.py.
- pd2_merge waits for one of 4 machine-wide slots (PD2R_MERGE_SLOTS) to cap RAM use.
- Add an orchestrator skill and a game-check recipe doc for agents.
- `tools/worktree_remove.ps1` removes a worktree without deleting through its cache junctions.
- A merge rule's cache key now covers only the d2rAI-toolkit modules it imports, so a toolkit commit rebuilds fewer rules.
- Add screenshot rules to the game-check recipe.
- Orchestrator skill: stop finished workers; workers do not wait on CI.
- The CI gate replays unchanged merge rules from the shared rule cache; PRs keep only in a per-PR scratch folder.
- Two merges that miss one rule key run it once; the second waits and replays (single-flight).
- The gate skips docs-only changes and cancels the run of a merged or closed PR.
- Merge slots: the last PD2R_CI_RESERVED (2) are the CI gate's; agent merges can't take them.
- plugin\build.ps1 initialises an empty PluginSDK submodule itself in a fresh worktree.
- Gate: fork PRs fail table-diff, and label events rerun only the open PR's current head.
- CI cancels a superseded push-to-main gate run, so only the newest main commit gates.

#### Docs

- One gate rule in AGENTS.md; agent worktree and merge safety notes; stale doc facts fixed.
- Orchestrator merges without `--delete-branch`, which removed worktrees through the `pd2-data` junction.

#### Fixed

- Diablo Clone's key opens his arena again, a square arena built by Cartographer.

#### Changed

- The build drops PD2's converted level files and HD presets; Dclone, Rathma, Lucion, the Temple and Bitter Depths stay refused.
- CI gate drops the HD preset checks and artifacts, now covered by Cartographer.

#### Notes

- Alpha: characters saved with PD2's old maps may lose map items; start fresh characters for this build.
- Multiplayer is not verified with Cartographer maps and the new events plugin; test in single player.

#### Removed

- PD2's old map items, map affixes, map recipes and map-opening code are gone; Cartographer maps replace them.

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
