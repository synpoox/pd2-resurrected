# Changelog

Player-facing changes, newest first. One section per version, heading `### <version> (<YYYY-MM-DD>)`. Until the first
private-multiplayer release a build may break characters; each break is noted here (ADR 0003).

### Unreleased

- Inferno and Arctic Blast send Project Diablo II's line of flames (more with Inferno's level).
- Desecrate raises corpses around its cloud as in Project Diablo II.
- Skill trees show their boxes and arrows where Project Diablo II's skills and prerequisites are.
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
