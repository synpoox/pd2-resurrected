# Unported list

Every deviation from the PD2 baseline, marked approximated, cut or not yet (`GLOSSARY.md`). Hand-edited. W3 seeded it
with the engine rules from [core-rules.md](core-rules.md) that are not built; P5 adds the gaps its table-diff gate finds.
Delete a row when its rule is built and its game check passes.

## Engine rules

**Stand-in** is what the port runs meanwhile. "Closest D2R function" rows have lines in `merge/standins.tsv`: each skill
or missile that would point at PD2's function number uses a D2R function that plays alike, or none. "Same number" rows
keep PD2's number, which already holds a D2R function (a `verify` row: it may be the same rule). "D2R's own behaviour"
rows have no table hook: D2R does what it does.

| Rule | Status | Stand-in | Work item |
|---|---|---|---|
| Split Throw | Not yet | closest D2R function (`merge/standins.tsv`); server half built (W1) | W1, W2 |
| Lightning Bolt, Lightning Fury | Not yet | closest D2R function (`merge/standins.tsv`) | W5 |
| Inferno, Arctic Blast | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Charge (Paladin) | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Dragon Flight | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Holy Light | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Fire Claws | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Vengeance | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Firestorm and wall makers | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Leap, Leap Attack | Not yet | closest D2R function (`merge/standins.tsv`) | W8 |
| Double Throw | Not yet | closest D2R function (`merge/standins.tsv`) | W9 |
| Teleport, Blink | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Gust | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Blood Warp | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Bash | Not yet | closest D2R function (`merge/standins.tsv`) | W10 |
| Combustion and boss novas | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Static Field | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Desecrate | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Joust | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Poison Dagger | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Hunger | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Force Move | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Find Potion, Find Item | Not yet | closest D2R function (`merge/standins.tsv`) | W10 |
| Death novas | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Assassin charge-up finishers | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| PoisonEggSpawn | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| TorajanBossNest | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| UberDiabSummon, UberDiabRun | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| RathmaSummon | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Lucion kit | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Melee splash | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Power Strike, Lightning Strike client half | Not yet | closest D2R function (`merge/standins.tsv`) | W6 |
| Corpse/Vine Cycler start | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Holy Sword start | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Golem start | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Smite start | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Spirit Wolf, Fenris start | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Frenzy, Tiger Strike start | Not yet | closest D2R function (`merge/standins.tsv`) | W10 |
| Fire Claws, Split Throw start | Not yet | closest D2R function (`merge/standins.tsv`) | W2 |
| Paladin aura client start | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Paladin aura client do | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Fire Bolt, Ice Bolt, Blizzard, Chilling Armor bolt hit | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Fire Arrow, Exploding Arrow hit | Not yet | closest D2R function (`merge/standins.tsv`) | W6 |
| Uber immolation arrow hit | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Kanemith blaze dot hit | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Nova and orb hits (thunderstorm1, powerstrikenova1, voidbolt, rathmapoisonorb, leoricsummonnova, ZharNovaOrb, IskatuMagicOrb) | Not yet | D2R's function at the same number | W7 |
| Summon-on-hit missiles (leoricsummonorbbolt, GuardianOfFateSummonHit, iskatubliz3, ...) | Not yet | D2R's function at the same number | after the alpha |
| Blade fragment hit | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Throwing potions | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Psychic Hammer missile damage | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Multiple Shot arrow and bolt damage | Not yet | closest D2R function (`merge/standins.tsv`) | W6 |
| Desert turret fireball client hit | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Immolation, Blaze and Rathma fire client hits (17 missiles) | Not yet | D2R's function at the same number | W6 |
| Fist of the Heavens delay client hit | Not yet | D2R's function at the same number | after the alpha |
| Blessed Hammer | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Siege beast stomp client do | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Rathma bone spear, blade fragment damage | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Skill on cast (`item_skilloncast`, 62 live item rows) | Not yet | D2R's own behaviour | after the alpha |
| Skill on block (`item_skillonblock`, 5 live rows) | Not yet | D2R's own behaviour | after the alpha |
| Skill while equipped (`item_skillonequip`, prop `equipped-skill`: Tempest, Blackoak Shield, Spectral Plate) | Not yet | D2R's own behaviour | after the alpha |
| Phasing (stat `phasing` + state, from Phasing SelfAura) | Not yet | D2R's own behaviour | after the alpha |
| Charm "only one unique" rule | Not yet | D2R's own behaviour | after the alpha |
| Map items | Not yet | closest D2R function (`merge/standins.tsv`) | map plan |
| Unlimited TP and ID books | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Random sorceress skill on striking | Not yet | closest D2R function (`merge/standins.tsv`) | after the alpha |
| Item stat less than / equal (corruption outcomes, map recipes; 1,126 enabled rows) | Not yet | D2R's own behaviour | P9, map plan |
| Map recipes (heroic, corrupt, fortify, ears, map events; 26 rows) | Not yet | D2R's own behaviour | map plan |
| Demonic cube reroll of unique and set items (16 rows) | Not yet | D2R's own behaviour | after the alpha |
| `.noexp` toggle | Not yet | D2R's own behaviour | after the alpha |
| `.set-home` start town | Not yet | D2R's own behaviour | after the alpha |
| Character stat commands (`.kills`, `.deaths`, `.played`, `.age`, `.allstats`, `.accountstats`) | Not yet | D2R's own behaviour | after the alpha |
| Teleport blocked areas | Not yet | D2R's own behaviour | after the alpha |
| Corrupted zones (D2R's Terror Zones) | Not yet | D2R's own behaviour | after the alpha |
| Map events and invaders (AiThink4, `MapEventNotify*`, Invader monsters) | Not yet | D2R's own behaviour | map plan |
| Corruption | Not yet | D2R's own behaviour | P9 |
| Loot allocation | Not yet | D2R's own behaviour | later release |
| PvP modifiers, battle-royale checks | Cut | none: the rule is not reproduced | - |
| Act 3 quest 5 patch | Not yet | D2R's own behaviour | after the alpha |
| Skill palette, NPC patches | Not yet | D2R's own behaviour | after the alpha |
| Channel take-over: Inferno, Arctic Blast (srvdo 182) and Blade Fury (48) | Not yet | D2R's own behaviour | after the alpha |
| Charge (srvdo 180) in the Charge/Whirlwind rule, also for skill-less requests | Not yet | D2R's own behaviour | after the alpha |
| Run and Concentration in the end-of-action window | Not yet | D2R's own behaviour | after the alpha |
| Get-hit/block end window, cast/sequence delay from neutral or run | Not yet | D2R's own behaviour | after the alpha |
| Level life regen cap (30, `bloodwarp` lifts it) | Not yet | D2R's own behaviour | map plan |
| Player death handler and two more PlrModes hooks | Not yet | D2R's own behaviour | after the alpha |
| Perfect shrine | Not yet | D2R's own behaviour | after the alpha |
| Stackable items / item quantity | Not yet | D2R's own behaviour | after the alpha |
| Skill aura event 32 (Revive), 3 (Chilling Armor), 16 (Smite) | Not yet | D2R's own behaviour | after the alpha |
| State remove function 12 (`skilldelay`) | Not yet | D2R's own behaviour | after the alpha |

## Data rows

Hook-list rows that need no plugin. Checked in W3 (93847, PD2 baseline in `pd2-data/baseline.json`):

- **Exact by `merge/standins.tsv`**: Monster frenzy (srvdo 109 -> 9, srvst 64 -> 67, by `=`),
  Baal Corpse Explode client start (cltst 82 blanked, past D2R's table end) and the `*12` server hit (blanked: PD2 reads
  `*12` as no hit function, and a blank means the same without relying on D2R's parse). These are not deviations.
- **Equal numbers**: WispBossNova (srvdo 41, cltdo 15) and Royal Fire (143, 83) keep D2R's functions at the same
  numbers. The vanilla functions reassigned to other skills keep their numbers; the merge guard passes them.
- **Tables merged in P3**, checked against D2R's rows by key:
  - misc `pSpell` 1-9: 21 rows equal D2R's, 1 PD2-only row uses a value D2R uses. Herb (`hrb`) differs: PD2 9, D2R 10.
  - properties `func` 3, 8, 22, 24: 35 equal, 63 PD2-only properties use D2R's functions. `fireskill` adds `func2` 3.
  - cubemain `op` 28: PD2's 8 rows set `param` blank, 1 or 2; D2R's 6 rows leave it blank. Same function.
  - itemstatcost `descfunc`: D2R moved nearly every stat to 19, which formats the stat's string itself; PD2 keeps
    0-4, 6-9 and 20. P3 must merge each stat's `descfunc` with the strings written for it, then check the tooltips.
