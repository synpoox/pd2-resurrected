# Changelog

Player-facing changes, newest first. One section per version, heading `### <version> (<YYYY-MM-DD>)`. Until the first
private-multiplayer release a build may break characters; each break is noted here (ADR 0003).

### Unreleased

- The automap draws the walls Project Diablo II draws with its own automap pictures: Hellcaves, Djinns Domain and
  Na-Krul's Abyss now show their walls on the automap.
- Monster kits work as in Project Diablo II: Cycle of Life and Vines heal everyone nearby at once when they eat a
  corpse, Torajan's eggs burst in a poison nova as they hatch and his nest lays at most 35, and the minions boss
  missiles raise (Leoric's guards, Iskatu's vermin, ...) are the right monsters and drop no loot and give no
  experience.
- Project Diablo II's class item rules reach the Warlock: Hellfire Torch and Spirit Ward can roll Warlock skills,
  Warlock skill and skill-tree affixes roll as often as other classes', and the Warlock gets the affixes Project
  Diablo II added for the other classes: +1 and +2 class skills on body armor, helms and shields, and +3 to a skill
  tree on grimoires and daggers.
- Melee splash works more as in Project Diablo II: it carries the damage of the skill you hit with (Frenzy's
  damage bonus and elemental damage, a kick's damage), One and Two Hand Mastery widen it, a normal-tier weapon
  narrows it. As in Project Diablo II, on-hit procs and freezes don't fire on the monsters it splashes onto.
- Blade Fury's blades work as in Project Diablo II: a blade that hits a wall bounces off it as three blades, and a
  blade that hits a monster can split in two, up to three times over.
- Wands, staves and scepters with a class skill show it ("+1 to Raise Skeleton Warrior (Necromancer Only)") instead
  of "an evil force".
- Blessed Hammer works as in Project Diablo II: hammers fly through walls.
- Fire Bolt, Ice Bolt, Blizzard and Chilling Armor's bolts work as in Project Diablo II: each hit splashes its damage
  onto every monster close to where it lands.
- Dragon Flight works as in Project Diablo II: it teleports you and kicks in the same moment, and its damage bonus
  is Project Diablo II's. Psychic Hammer's hammer loses damage with each monster it jumps to, as in Project Diablo II.
- Holy Sword and Smite work as in Project Diablo II: Holy Sword casts only with a two-handed sword, and Smite
  adds its crushing blow chance to your hits while you smite (the crushing blow itself is still Diablo II
  Resurrected's).
- Gust works as in Project Diablo II: it teleports you, then chills and staggers the monsters around where you land.
- Hunger works as in Project Diablo II: a werebear eats a corpse for a while of life steal, speed, open wounds and
  deep wounds.
- Fire Claws works as in Project Diablo II: each claw sends a trail of fire waves (more with the skill's level).
- Summoning a Spirit Wolf or Fenris at the limit replaces the oldest one, as in Project Diablo II.
- Firestorm works as in Project Diablo II: every one of its fire trails wanders (D2R sends the first one straight).
- Static Field works as in Project Diablo II: it takes a share of current life down to Project Diablo II's floor,
  ignores some map and Rathma monsters, and lowers lightning resistance for a while (half as much on a monster
  immune to lightning). Combustion and the boss novas that share it throw Project Diablo II's ring of missiles.
- Charge works as in Project Diablo II: it runs at Project Diablo II's speed, and the longer the charge, the wider
  its hit splashes with a splash-on-hit item.
- The Assassin's charge-up skills build charges as in Project Diablo II: charges are shared between them, and each
  charge's effect (Fists of Fire's fire, Cobra Strike's poison, Blades of Ice's wave) fires on the hit that builds
  it, and your client draws each charge as Project Diablo II's does (Fists of Fire's fire and nova, Cobra Strike's
  poison bolts and cloud, Claws of Thunder's falling storm bolts).
- Teleport works as in Project Diablo II: for a second after each teleport your elemental masteries are
  lowered, by less as the skill levels up.
- Holy Light works as in Project Diablo II: it heals allies and deals magic damage to enemies around its target,
  half as much to the side the target is not on, and knocks back undead. Fist of the Heavens' holy bolts reach
  every unit around the strike, the target included.
- Vengeance works as in Project Diablo II: a hit bursts fire, cold and lightning from your own elemental damage
  (Holy auras and Enchant aside) on the enemies around the target, and the burst chains on from enemy to enemy.
- Joust works as in Project Diablo II: you teleport to the target and strike it with extra damage and critical strike
  chance, then run faster for two seconds.
- Melee splash works as in Project Diablo II: items that splash on hit, Frenzy and Tiger Strike's splash radius,
  and revived monsters' hits damage the monsters around the one struck.
- Poison Dagger works as in Project Diablo II: each stab leaves a ring of poison clouds around the target.
- Blood Warp works as in Project Diablo II: it costs a share of your life (never all of it), then regenerates life
  for five seconds.
- Inferno and Arctic Blast send Project Diablo II's line of flames (more with Inferno's level).
- Another skill starts at once while Inferno or Arctic Blast channels, and a second Blade Fury does not cut the first short, as in Project Diablo II.
- Desecrate raises corpses around its cloud as in Project Diablo II.
- Skill trees show their boxes and arrows where Project Diablo II's skills and prerequisites are.
- Golem Mastery adds a golem every 5 base levels, as in Project Diablo II; a new golem replaces the oldest.
- Double Throw works as in Project Diablo II: one throw that bounces between monsters.
- Leap and Leap Attack work as in Project Diablo II: faster leaps, and Leap Attack hits every monster where it lands.
- Lightning Bolt and Lightning Fury work as in Project Diablo II.
- Exploding Arrow spreads extra explosions, Multiple Shot loses damage per pierce, and Valkyries' Power Strike shows.
- Skill tooltips no longer show raw format codes ("Duration: %d seconds40 seconds") or a stray "an evil force" line;
  the Mana Cost and damage lines are back ("Magic Damage: 3-5"). Percent values read "+110%" where PD2 says "+110 percent".
- Bash knocks back half the time, and Find Potion and Find Item skip minions' corpses.
- Item stat lines no longer show the value twice ("+1 %+d to All Skills").
- Corruption: cube an item with a Worldstone Shard. It takes two presses for now: the first rolls the hidden
  corruption value and hands the shard back, the second gives one of PD2's outcomes. A corrupted item can't be
  corrupted again.
- Thunder Storm, Power Strike, Shattering Arrow and boss orbs burst into Project Diablo II's novas when they hit.
- PD2's monsters, mercenaries, areas, shrines and objects are in. PD2's map areas and bigger inventory are not yet.
- PD2's items, affixes, runewords, cube recipes and treasure classes are in. Map items do not drop yet. Breaks
  characters from earlier builds.
- Project Diablo II's names and descriptions for items, skills, monsters and areas (English).
- The mod uses D2RLoader's extended save format: characters load only with D2RLoader.
- Comes with a loot filter (with drop sounds), stash search, item roll ranges, a buff bar and an XP bar.
- Skills not rebuilt yet play like the closest D2R skill.
