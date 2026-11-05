# Changelog

Player-facing changes, newest first. One section per version, heading `### <version> (<YYYY-MM-DD>)`. Until the first
private-multiplayer release a build may break characters; each break is noted here (ADR 0003).

### 0.1.0-alpha (2026-11-05)

The first alpha: every Project Diablo II (PD2) class plays as in PD2, offline single-player. The map system, the uber
bosses and the Warlock redesign come in a later alpha.

#### Getting started

- You need D2RLoader: characters use its extended save format and load only through it.
- Characters from test builds before this release don't load.
- Comes with a loot filter (with drop sounds), stash search, item roll ranges, a charm inventory, a buff bar,
  and Paladin auras on the left mouse button.
- PD2's names and descriptions for items, skills, monsters and areas (English).

#### Items

- PD2's items, affixes, runewords, cube recipes and treasure classes.
- Corruption: cube an item with a Worldstone Shard. For now it takes two tries: the first hands the shard back, the
  second corrupts the item. A corrupted item can't be corrupted again.
- The Warlock gets PD2's class item rules: Hellfire Torch and Spirit Ward can roll Warlock skills, and Warlock affixes
  roll as often as other classes'.
- Wands, staves and scepters show their class skill instead of "an evil force".

#### Monsters and areas

- PD2's monsters, mercenaries, areas, shrines and objects.
- Cycle of Life and Vines heal everyone nearby when they eat a corpse.
- Torajan's eggs burst in a poison nova as they hatch.
- Minions raised by boss missiles (Leoric's guards, Iskatu's vermin, ...) drop no loot and give no experience.

#### Skills

These now work as in PD2. Skills not listed still play like the closest D2R skill.

- **Every class:** melee splash (items, Frenzy, Tiger Strike, revived monsters), and another skill starts at once
  while Inferno or Arctic Blast channels.
- **Amazon:** Double Throw, Lightning Bolt, Lightning Fury, Exploding Arrow, Multiple Shot, Power Strike (Valkyries
  too) and Shattering Arrow.
- **Assassin:** the charge-up skills (shared charges, each effect on the hit that builds it), Dragon Flight, Psychic
  Hammer and Blade Fury.
- **Barbarian:** Leap, Leap Attack, Bash, Find Potion and Find Item.
- **Druid:** Arctic Blast, Firestorm, Gust, Hunger, Fire Claws, Spirit Wolf and Fenris.
- **Necromancer:** Poison Dagger, Blood Warp, Desecrate and Golem Mastery.
- **Paladin:** Charge, Holy Light, Fist of the Heavens, Vengeance, Joust, Holy Sword, Smite and Blessed Hammer.
- **Sorceress:** Fire Bolt, Ice Bolt, Blizzard, Chilling Armor, Static Field, Combustion, Teleport, Inferno and
  Thunder Storm.

#### Interface and graphics

- Skill trees use PD2's layout.
- Skill tooltips show the right numbers, with no stray format codes.
- Item stat lines no longer show their value twice.
- PD2's monsters and effects show in HD.
- Legacy graphics no longer crash with a PD2 skill on a skill button.

#### Maps (groundwork, no map items drop yet)

- Maps built on D2R's tiles draw in HD; many walls, streams and bridges are still missing.
- Map walls show on the automap.
- A map's corrupted, socketed and ethereal drop chances apply.
