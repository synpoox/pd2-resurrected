# Cheat commands

Every cheat command D2RLoader's in-game console accepts, from D2RLoader's own list (157 commands). Type them in the console as `cheat <command> <arguments>`, after `debug_mode` turns cheats on. Agents: use these to set up a test fast instead of playing to the spot (AGENTS.md, "Testing in-game").

Notes from our own runs, which override the descriptions below where they differ:

- Spawn items with `cheat spawn <code>,inv 0`, and a unique with `cheat uniques <row>` (AGENTS.md has the details). `cheat item` does nothing.
- `cheat gold <n>` did not add gold in our tests, although the list says it does.
- `cheat stat name,value`, `cheat idall` and `cheat waypoints` work as described.
- `expansion` can change the character's version on save: do not use it on a test character you keep.
- `debugbreak` executes an `int3` and kills the game without a debugger attached. Do not run it.
- Item-quality toggles (`sio`, `uio`, `mio`, `rio`, `normalitemsonly`, ...) stay on until set back to 0.

| Command | Arguments | Description |
|---|---|---|
| `bug` | `<summary>` | Legacy bug-report helper: captures a JPEG screenshot and bundles it with the supplied summary. |
| `verifyitems` | `—` | Logs all known item units with code/name, ids, mode, owner, NPC owner, and hand/owner state. |
| `debugpanel` | `—` | Toggles Blizzard's debug registry, initializing its mode-specific entries when enabled and clearing them when disabled. |
| `log` | `—` | Toggles Blizzard's AllLog window/open flag. |
| `itemspawnlevel` | `<level>` | Sets the global default item-spawn level; accepts 1 through 120. |
| `isl` | `<level>` | Alias for itemspawnlevel. |
| `clearinventory` | `—` | Removes carried/equipped items through native item-location validation and destruction. |
| `cli` | `—` | Alias for clearinventory. |
| `savecheatitems` | `<0\|1>` | Sets whether cheat-spawned items remain saveable; prefer the ImGui Cheat Settings toggle. |
| `experience` | `<amount>` | Adds the signed amount to the player's Experience stat. |
| `waypoints` | `—` | Unlocks every valid waypoint row for the current game version/difficulty. |
| `ospawn` | `<Objects.txt row\|name>[,<count>] [@ <x>,<y>]` | Spawns a valid Objects.txt row at the selected location; names are resolved to rows and count defaults to 1. |
| `mspawn` | `<MonStats.Id\|row>[,<count>[,<ai>]] [@ <x>,<y>]` | Spawns monsters using an exact MonStats.Id or numeric row id. |
| `stat` | `<statName>,<amount>` | Sets an ItemStatCost stat with layer/parameter 0; HP/mana stats 6–9 are shifted by 8. |
| `sklvl` | `<skillName>,<level>` | Looks up a skill by name and sets/adds it at the requested level. |
| `skadd` | `<skillName>[,<level>]` | Looks up a skill, adds it, and optionally sets its level. |
| `skdel` | `<skillName>` | Looks up a skill and sends the native level-0 delete update. |
| `fstorm` | `—` | Creates 25 hard-coded Firestorm-style missile/effect units in a 5×5 pattern around the player. |
| `pcloud` | `—` | Creates 49 hard-coded poison-cloud missile/effect units in a 7×7 pattern around the player. |
| `sockets` | `<0-6>` | Sets the forced socket count for newly generated eligible items. |
| `ethereal` | `<0\|1>` | Toggles the forced ethereal override for newly generated eligible items. |
| `nameit` | `—` | Marks matching inventory items personalized and copies the player's name onto them. |
| `testtreasure` | `mon=<id>,num=<count>[,...]` | Runs treasure-drop tests, optionally dumps treasure records, and writes tab-separated results to a file. |
| `uniques` | `[destroy,][start][,end]` | Spawns forced unique items over an optional UniqueItems range. |
| `sets` | `[!setIndex\|destroy,][start][,end]` | Spawns forced set items over an optional SetItems range. |
| `champions` | `—` | Toggles forced champion modifiers for eligible monsters. |
| `expansion` | `<0\|1\|2>` | WARNING: appears to persistently change character version on save; 0=Classic, 1=LoD, 2=RotW. |
| `durability` | `<amount>` | Sets carried inventory-grid item durability, clamped by max durability. |
| `quantity` | `<amount>` | Sets carried stack quantity, clamped by max stack size, and sends an update. |
| `maxvar` | `<amount>` | Stores the item-roll test maximum variance value. |
| `findaffixes` | `<name>` | Finds exact case-insensitive magic affix name matches and reports ids/levels. |
| `testmagicsuffixes` | `<start>[,<end>]` | Creates magic test items using suffix indexes start+1 through exclusive end. |
| `testmagicprefixes` | `<start>[,<end>]` | Creates magic test items using prefix indexes start+1 through exclusive end. |
| `spawnallmisc` | `—` | Spawns every misc item at the default item-spawn level. |
| `objectstats` | `—` | Reports per-group object placement attempts plus room failure and no-object totals. |
| `spawnallweapons` | `—` | Spawns every weapon item at the default item-spawn level. |
| `spawnallarmor` | `—` | Spawns every armor item at the default item-spawn level. |
| `debugbreak` | `—` | Legacy debugger trap: immediately executes an int3 breakpoint. |
| `objectpopulate` | `<count>` | Populates nearby room objects when count is in the valid object-row range. |
| `objectdensity` | `<density>` | Sets object population density; accepts byte values 31–127. |
| `objectgroup` | `<group>` | Sets/populates the current room's object group when group is positive. |
| `normalitemsonly` | `<0\|1>` | Toggles normal-quality-only item generation. |
| `lowqualityitemsonly` | `<0\|1>` | Toggles low-quality-only item generation. |
| `hiqualityitemonly` | `<0\|1>` | Toggles superior/high-quality-only item generation. |
| `sio` | `<0\|1>` | Toggles set-only item generation. |
| `uio` | `<0\|1>` | Toggles unique-only item generation. |
| `mio` | `<0\|1>` | Toggles magic-only item generation. |
| `rio` | `<0\|1>` | Toggles rare-only item generation. |
| `invincible` | `<0\|1>` | Sets the raw player no-damage/invincibility helper. |
| `mongod` | `<0\|1>` | Sets the monster receive-no-damage flag. |
| `monfreeze` | `<0\|1>` | Sets the monster freeze flag. |
| `monai` | `[0-3]` | Sets AI debug mode to 0–3, or toggles between 0 and 1 without an argument. |
| `rain` | `<0\|1>` | Sends the client weather/debug packet; only first char 1 enables rain. |
| `goto` | `<level>[,<x>[,<y>]]` | Warps to a level by name/id, optionally resolving the room from x/y. |
| `spawn` | `<code>[,<option>...]` | Spawns items by Inventory.txt code; supports num, qty, lvl, nid, typ, pre/suf, seeds, inv, and spc. |
| `spawnmode` | `—` | Sets native spawn-mode so subsequent cheat input is parsed directly as item-spawn arguments. |
| `multiunique` | `<count>` | Stores the duplicate-unique allowance used by item uniqueness tests. |
| `diabloclone` | `—` | Reads Diablo Clone/world-event state and forwards it to the event update helper. |
| `idall` | `—` | Identifies carried inventory items in native inventory modes 1 or 3. |
| `object` | `<objectId> [@ <x>,<y>]` | Clamps the selected spawn location to a valid nearby room and spawns one object by id. |
| `monster` | `[player] [dead] <monster\|!id\|unique:id\|super:id\|form:id> [@ <x>,<y>]` | Spawns monsters with player/dead and unique/super/form test branches. |
| `mspawnwithstats` | `<monster>[;option...][;stat=value...] [@ <x>,<y>]` | Spawns custom-stat monster/dummy tests. |
| `gold` | `<amount>` | Adds signed gold, normalizing through native cap/validity checks when needed. |
| `spawngoldpiles` | `<count>` | Spawns count gold piles with hard-coded amount 100. |
| `Player` | `<a\|b\|d\|i\|n\|p\|s\|w>` | Legacy player class/test helper. |
| `playerspawn` | `<a\|b\|d\|i\|n\|p\|s\|w>` | Runs the native player-spawn test near player x+5/y. |
| `spawnrand` | `<a\|h\|l\|m\|r\|s\|u>` | Spawns a random item category: any, high, low, magic, rare, set, or unique. |
| `freeze` | `<0\|1\|2>` | Sets the global freeze mode to 0, 1, or 2. |
| `item` | `<miscId>` | Looks up a decimal misc.txt row id and prints item code/name. |
| `allskills` | `<count>` | Repeats the all-skills grant helper count times. |
| `skillpoints` | `<amount>` | Grants unspent skill points when amount is positive. |
| `skillbonus` | `<amount>` | Applies an item-style all-skills stat bonus and sends a stat update. |
| `resetskills` | `—` | Calls the native player skill reset helper. |
| `resetstats` | `—` | Calls the native player stat reset helper. |
| `skillmax` | `<skillId> <maxLevel>` | Writes a new Skills.txt max level when valid and maxLevel > 20. |
| `questdebug` | `<0\|1>` | Legacy raw quest debug toggle. |
| `save` | `<anything>` | Saves all players when any non-empty argument is supplied. |
| `easykill` | `<0\|1>` | Toggles the native kill-cheat flag. |
| `nokillbydamage` | `<0\|1>` | Toggles the raw no-kill-by-damage debug flag. |
| `nogethit` | `<0\|1>` | Toggles the raw no-get-hit reaction debug flag. |
| `questcontrol` | `<command>` | Runs the raw quest debug/control parser; e.g. a5q6, e4, xN. |
| `quests` | `—` | Completes quest progress through Act 5 Quest 6 for the current difficulty. |
| `texttest` | `—` | Sends a fixed test server-system-message packet to the client. |
| `questalert` | `—` | Legacy quest alert test scheduling a delayed SCMD_QUESTALERT notification. |
| `plants` | `<0\|1>` | Calls the plant/object decoration helper. |
| `video` | `<id>` | Sends a video/cinematic id to the client helper. |
| `level` | `<level>` | Raises the player up to the class-clamped level through level-up side effects. |
| `stamina` | `—` | Sets the infinite stamina cheat flag. |
| `god` | `[0\|1]` | Sets player no-damage, stamina cheat, and mana cheat; no argument toggles. |
| `speedup` | `<amount>` | Sets VelocityPercent, AttackRate, and FasterCastRate to amount. |
| `attackinfo` | `<level>` | Sets the Combat Log attack-info debug level. |
| `statsup` | `—` | Runs the stat-up helper for stats 0–3 using 25% of stat 4. |
| `itemgridclear` | `[bodyLocation]` | Removes inventory-mode 0 items, optionally filtered by body location. |
| `itemgroundclear` | `—` | Legacy raw handler that removes nearby ground items. |
| `corpseclear` | `—` | Legacy raw handler that removes nearby corpses/dead-unit test data. |
| `pickup` | `—` | Attempts native pickup for nearby room item units. |
| `endinteraction` | `—` | Calls the native end-interaction helper with zeroed interaction arguments. |
| `ear` | `—` | Spawns a PvP ear item near the player's position. |
| `questfx` | `<0-19>` | Plays quest effect ids 0–19. |
| `townshrine` | `—` | Toggles a debug flag forcing subsequently initialized shrines to type 17. |
| `hireup` | `<xp>` | Adds parsed experience to the player's mercenary. |
| `hirelevel` | `<level>` | Sets/raises mercenary level; valid range 1–98. |
| `killme` | `—` | Kills the player unit through the native unit death helper. |
| `killhirable` | `—` | Finds and kills the player's mercenary. |
| `killmonster` | `—` | Finds a nearby monster and kills it. |
| `shrine` | `<id> [@ <x>,<y>]` | Spawns object id 86, maps/clamps shrine type, and attaches Shrines.txt data. |
| `cubestuff` | `<recipe> [@ <x>,<y>]` | Legacy raw recipe-input spawner at the selected location. |
| `cubemake` | `<recipe>[,<count>]` | Creates Horadric Cube recipe output items directly; refuses recipes manipulating inputs. |
| `deadfill` | `—` | Spawns dead-unit test data in a 20×20 square around the player. |
| `serverstate` | `—` | Broadcasts raw cheat state lines: cheats enabled, spawn mode, and save-cheat-items. |
| `setdupeall` | `—` | Walks duplicate-eligible inventory items and marks/fixes them as duplicated. |
| `nominions` | `<count>` | Sets the no-minions override and records whether the previous override was nonzero. |
| `resetnominions` | `—` | Restores the saved 0-or-1 no-minions state. |
| `fakehostile` | `<0\|1>` | Sets the fake-hostile warning flag. |
| `widgetdebug` | `<0\|1>` | Sets the widget placement helper flag. |
| `mapdebug` | `<0\|1>` | Sets the automap tile highlight/debug flag. |
| `inifussanimreplay` | `—` | Sets the Scroll of Inifuss animation replay flag. |
| `clearspritecache` | `<0\|1>` | Sets the clear-sprite-cache flag. |
| `advancedstatsdebug` | `—` | Toggles advanced-stat tooltip debug lines. |
| `useoscursor` | `<0\|1>` | Sets the OS cursor fallback flag. |
| `crash` | `—` | Calls the native intentional crash helper. |
| `vfxlabels` | `<0\|1>` | Passes the parsed value to the VFX label toggle/helper. |
| `vfxslowmissile` | `<0\|1>` | Sets the raw slow-missile VFX debug flag. |
| `setmaxmana` | `<amount>` | Sets MaxMana to amount << 8, then fills current Mana to the new max. |
| `depletemana` | `<amount>` | Sets current Mana to amount << 8, clamped between 0 and max Mana. |
| `setmana` | `<amount>` | Alias for depletemana. |
| `depletehealth` | `<amount>` | Sets current HitPoints to amount << 8, clamped between 0 and max HitPoints. |
| `sethealth` | `<amount>` | Alias for depletehealth. |
| `heal` | `—` | Legacy raw heal helper: sets current HitPoints to max HitPoints. |
| `seed` | `<seed>` | Sets game initSeed via atoi(arg), then writes the player save to a local buffer. |
| `perfteststart` | `<type>` | Legacy performance-test helper: starts capture and passes the raw argument pointer. |
| `perfteststop` | `—` | Legacy performance-test helper: stops/finalizes capture. |
| `camerashakestart` | `<0\|1>` | Sets the raw camera-shake debug flag. |
| `monsterinfo` | `<0\|1>` | Sets target/monster debug tooltip output. |
| `pathcorrections` | `<0\|1>` | Sets the raw path-correction debug output flag. |
| `debugdamage` | `<damage>[,<type>[,<flags>]]` | Applies parsed debug damage/type/flags to the player/current unit. |
| `debugdamageunit` | `<unitType>,<unitId>[,<damage>[,<type>[,<flags>]]]` | Finds a server unit by type/id and applies debug damage; damage defaults to 13. |
| `desecrate` | `<level>[,<param1>...[,<flag>]]` | Builds debug level/encounter settings when needed, then starts a Desecrate encounter. |
| `treasureinfo` | `[level]` | Stores treasure-drop info level or clears the override with no argument. |
| `maxMissileDamage` | `<0\|1>` | Clears min-missile override and sets max-missile damage rolls from atoi(arg) != 0. |
| `minMissileDamage` | `<0\|1>` | Clears max-missile override and sets min-missile damage rolls from atoi(arg) != 0. |
| `skillTestCleanup` | `—` | Removes current-room units/items matched by the skill-test cleanup filter. |
| `chronicleFill` | `[percent]` | Runs unique, runeword, and set Chronicle discovery fillers. |
| `chronicleFillUniques` | `[percent]` | Marks unique Chronicle entries discovered with optional percent chance. |
| `chronicleFillSets` | `[percent]` | Marks set Chronicle entries discovered with optional percent chance. |
| `chronicleFillRunewords` | `[percent]` | Marks runeword/rune Chronicle entries discovered with optional percent chance. |
| `chronicleWipe` | `—` | Calls the client Chronicle wipe/reset helper. |
| `chronicleRemoveUnique` | `<uniqueId>` | Sends a Chronicle remove request for unique discovery type 1. |
| `chronicleRemoveSet` | `<setId>` | Sends a Chronicle remove request for set discovery type 0. |
| `chronicleRemoveRune` | `<runeId>` | Sends a Chronicle remove request for rune/runeword discovery type 2. |
| `setbinddemonchance` | `<chance>` | Stores the raw Bind Demon chance override value. |
| `heraldsettokens` | `<tokens>` | Sets Herald tokens, clamped to the current cap. |
| `heraldspawn` | `<monsterId>` | Validates level/monster state and spawns a Herald monster at the player. |
| `setheraldtier` | `<tier>` | Clamps tier to 1..current Herald tier cap and writes it to the game field. |
| `treasureroller` | `mon=<id>,rol=<rolls>,ech=<echo>[,...]` | Runs controlled treasure-roll tests and stores per-client roller results. |
| `stashfill` | `[count]` | Asynchronously fills stash space with random valid stashable weapon/armor items. |
| `stashwipe` | `[stashIndex]` | Clears body-location 4 items, then wipes all or one 1-based advanced stash proxy. |
