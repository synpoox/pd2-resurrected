# Changelog

Player-facing changes, newest first. One section per version, heading `### <version> (<YYYY-MM-DD>)`. Until the first
private-multiplayer release a build may break characters; each break is noted here (ADR 0003).

### Unreleased

- Fire Bolt, Ice Bolt, Blizzard and Chilling Armor's bolts work as in Project Diablo II: each hit splashes its damage
  onto every monster close to where it lands.
- Gust works as in Project Diablo II: it teleports you, then chills and staggers the monsters around where you land.
- Hunger works as in Project Diablo II: a werebear eats a corpse for a while of life steal, speed, open wounds and
  deep wounds.
- Firestorm works as in Project Diablo II: every one of its fire trails wanders (D2R sends the first one straight).
- Static Field works as in Project Diablo II: it takes a share of current life down to Project Diablo II's floor,
  ignores some map and Rathma monsters, and lowers lightning resistance for a while (half as much on a monster
  immune to lightning). Combustion and the boss novas that share it throw Project Diablo II's ring of missiles.
- Charge works as in Project Diablo II: it runs at Project Diablo II's speed, and the longer the charge, the wider
  its hit splashes with a splash-on-hit item.
- The Assassin's charge-up skills build charges as in Project Diablo II: charges are shared between them, and each
  charge's effect (Fists of Fire's fire, Cobra Strike's poison, Blades of Ice's wave) fires on the hit that builds
  it. What your client draws for each charge is still D2R's.
- Teleport works as in Project Diablo II: for a second after each teleport your elemental masteries are
  lowered, by less as the skill levels up.
- Holy Light works as in Project Diablo II: it heals allies and deals magic damage to enemies around its target,
  half as much to the side the target is not on, and knocks back undead. Fist of the Heavens' holy bolts reach
  every unit around the strike, the target included.
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
