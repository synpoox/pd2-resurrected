# Changelog

Player-facing changes, newest first. One section per version, heading `### <version> (<YYYY-MM-DD>)`. Until the first
private-multiplayer release a build may break characters; each break is noted here (ADR 0003).

### Unreleased

#### Items

- Items work more as in Project Diablo II: ethereal items keep their full durability, set items can drop ethereal,
  rare items get at least four affixes from item level 45 (five from 65, six from 85) and rare jewels always four,
  hiring a new mercenary drops the old one's gear, and shift-right-clicking a Tome of Identify identifies your
  inventory. Skill on cast and skill on block items proc through the Cast Triggers plugin (1.1.12); not yet confirmed in game.

#### Uber bosses

- The Diablo Clone, Rathma and Lucion keys open their areas. Use one at the Blood Fountain in Harrogath (Hell,
  level 80+, Rite of Passage done): everyone at the fountain who is level 80+ goes in, without their mercenary,
  summons or buffs. One map or uber area per game. The fights themselves come later.
- A tier-0 uber key also leaves three grey portals by the fountain. Each takes one player back in, once, by the same
  rules (no mercenary, summons or buffs), and heals the boss by 20% of its life. The uber areas have no waypoint.
- Uber fight monsters scale with the key's tier, as in PD2.

#### Maps

- Duplicated maps (`map_glob_extra_boss`) spawn their boss twice, as in PD2.

#### Maps

- PD2's wooded maps on D2R tiles (Jungle, Kurast, Graveyard, Siege, Necropolis Jungle and Swamp, Pandemonium Finale)
  draw tree lines along their borders in HD, and every map on D2R tiles uses whole D2R rooms, with their own ground,
  water and bridges, where its layout matches one.

#### Fixes

- The mercenary panel shows the mercenary's title (Rogue, Desert Mercenary, Barbarian, Ascendant) instead of
  "an evil force".
- PD2's own items (Worldstone Shards, maps, PD2 uniques) are visible on the ground in HD again.
- In legacy graphics mode, PD2's own missiles show again: Holy Nova, and the other missiles that used PD2-only art.
- Charged potions and other PD2-only items show their own pictures, in HD and legacy, instead of a hand axe.
- An item with Druid or Assassin class skills (Hellfire Torch, Spirit Ward) shows the number, e.g.
  "+2 to Assassin Skill Levels".
- Mercenaries already deal PD2's reduced damage to bosses in each difficulty; no change was needed.

### 0.1.0-alpha (2026-11-05)

The first alpha: every Project Diablo II (PD2) class plays as in PD2, offline single-player, with PD2's maps. The uber
bosses and the Warlock redesign come in a later alpha.

#### Getting started

- You need D2RLoader: characters use its extended save format and load only through it.
- Characters from test builds before this release don't load.
- Comes with a loot filter (with drop sounds), stash search, item roll ranges, a charm inventory, a buff bar,
  and Paladin auras on the left mouse button.
- PD2's names and descriptions for items, skills, monsters and areas (English).

#### Items

- PD2's items, affixes, runewords, cube recipes and treasure classes.
- Corruption: cube an item with a Worldstone Shard and press Transmute once. A corrupted item can't be corrupted
  again.
- Demonic Cube rerolls a unique or set item, keeping its base, sockets and ethereal state. Items with socketed items
  in them are refused for now.
- Cube recipes take their inputs one per cell (three T1 maps, three chipped gems, ...). Stacked inputs don't count
  yet.
- PD2's combat stats: critical and deadly strike, crushing blow, open wounds and deep wounds, and the life steal cap.
- Curse resistance and extra curse slots.
- Skill on cast procs for every item that has it; skill while equipped and Phasing work.
- Ormus' Robes roll a random Sorceress skill.
- The Warlock gets PD2's class item rules: Hellfire Torch and Spirit Ward can roll Warlock skills, and Warlock affixes
  roll as often as other classes'.

#### Monsters and areas

- PD2's monsters, mercenaries, areas, shrines and objects.
- Tyrael hires the Ascendant.
- Cycle of Life and Vines heal everyone nearby when they eat a corpse.
- Torajan's eggs burst in a poison nova as they hatch.
- Minions raised by boss missiles (Leoric's guards, Iskatu's vermin, ...) drop no loot and give no experience.

#### Skills

These now work as in PD2. Skills not listed still play like the closest D2R skill.

- **Every class:** melee splash (items, Frenzy, Tiger Strike, revived monsters), and another skill starts at once
  while Inferno or Arctic Blast channels. Hit recovery and block end on PD2's frame.
- **Amazon:** Lightning Bolt, Lightning Fury, Exploding Arrow, Multiple Shot, Power Strike (Valkyries too) and
  Shattering Arrow.
- **Assassin:** the charge-up skills (shared charges, each effect on the hit that builds it), Dragon Flight, Psychic
  Hammer and Blade Fury.
- **Barbarian:** Double Throw, Split Throw, Leap, Leap Attack, Bash, Find Potion and Find Item.
- **Druid:** Arctic Blast, Firestorm, Gust, Hunger, Fire Claws, Spirit Wolf and Fenris.
- **Necromancer:** Poison Dagger, Blood Warp, Desecrate and Golem Mastery.
- **Paladin:** Charge, Holy Light, Fist of the Heavens, Vengeance, Joust, Holy Sword, Smite and Blessed Hammer.
- **Sorceress:** Fire Bolt, Ice Bolt, Blizzard, Chilling Armor, Static Field, Combustion, Teleport, Inferno and
  Thunder Storm.

#### Interface and graphics

- Skill trees use PD2's layout and icons.
- Skill tooltips show the right numbers, with no stray format codes.
- PD2's monsters and effects show in HD.

#### Maps

- Maps drop. Open one in Harrogath in Hell (level 80+, Rite of Passage done) for a red portal to its area; one map
  per game.
- A map's stats apply: monster and player stats, and area stats (density, rarity, extra monster groups, fortify,
  area level).
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
