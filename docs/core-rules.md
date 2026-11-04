# core-rules hook list

One row per engine rule (see `GLOSSARY.md`). Hand-edited. Built by research ticket #19; method, evidence and open questions are in
[docs/research/core-rules-inventory.md](research/core-rules-inventory.md).

Columns:

- **PD2 source**: table column and value (`srvdo` = skills `srvdofunc`, `cltdo` = `cltdofunc`, `srvst`/`cltst` = start functions,
  `prg` = `srvprgfunc1-3`/`cltprgfunc1-3`, `shit`/`chit` = missiles `pSrvHitFunc`/`pCltHitFunc`, `sdo`/`cdo` = missiles
  `pSrvDoFunc`/`pCltDoFunc`, `dmg` = `pSrvDmgFunc`), or the DLL source for server rules.
- **Bucket**: (a) data only, (b) D2RLoader service, (c) native hook, (d) UI rebuild, (e) cut.
- **D2R slot**: PD2's number where that slot is free in D2R. `->` marks a re-map (collision or past the table end), logged here.
  Slots 191+ need the srvdofunc overflow table (plumbing, see the research file, Q1).
- **RVAs**: names only; the values live in the per-build header. `?` = not located yet.
- **Tier**: Exact or Function-equivalent (glossary).
- **Size**: sessions. S under 1, M 1-2, L 3+. Every size here is an estimate from the spike, not measured.
- **Status**: `spiked` (proven in a spike), `open` (to build), `verify` (a value inside D2R's range, or a rule whose effect is not
  yet confirmed: treat as different from D2R until checked), `data` (no plugin code expected; listed so the check is not lost),
  `unreachable` (no row reaches it in play; not added to the unported list), `cut`.
- **Work item**: the build slice that carries the row (see Work items below); empty = after the alpha cut, in the order
  below. `map plan` rows are sliced by the PD2 map system plan; `later release` rows wait for multiplayer.
- **Test**: the log assertion the `tests/game/` check makes. Expected values come from PD2's data unless noted.

## Work items

Ordered by "Order and slice core-rules into work items" (#25); placed into milestones, next to the P items, by [roadmap.md](roadmap.md). A work item is about one agent session. A `verify` check is
the first step of the item that builds its row; when D2R already matches, the item shrinks to data. Each build PR merges
when its game check, the table-diff gate and a code-review agent pass; the owner reviews P1, W1, W2 and the alpha release.
Target: D2R 3.3 build 93847 + D2RLoader 1.3.1 (a frozen install copy); 3.4 is its own item once D2RLoader supports it.

### Alpha: Amazon and Barbarian complete, the other classes on stand-ins

| # | Work item | Blocked by |
|---|---|---|
| W1 | Plumbing: installer, per-build RVA header, Split Throw server half (srvdo 189) moved into `core-rules` with its game check | none |
| W2 | Client-half example: Split Throw client half (cltdo 118), return missiles (sdo 38 -> 46, cdo 70 -> 78, shit 65), shared start srvst 79 | W1 |
| W3 | Stand-ins and data rows: `merge/` rules pointing each unbuilt rule's skill at the closest D2R function, "not yet" rows on the unported list, all `data` rows | P1 |
| W4 | Investigation: what PD2's `PlrModes.cpp` changes (PD2 baseline or ProjectDiablo.dll) | none |
| W5 | Amazon: Lightning Bolt, Lightning Fury (srvdo 188, cltdo 117); starts with the 188 check | W2, P2 |
| W6 | Amazon group: Fire/Exploding Arrow hit, Multiple Shot dmg 17 -> 20, Power Strike client half (cltdo 22), Immolation client hit (chit 12) | W2, P2 |
| W7 | Nova and orb hits (shit 5, chit 4) | W1, P2 |
| W8 | Barbarian: Leap, Leap Attack (srvdo 153 -> free slot, cltdo 97) | W2, P2 |
| W9 | Barbarian: Double Throw (srvdo 154 -> 183, cltdo 98 -> 116, shit 30, dmg 16 -> free slot) | W2, P2 |
| W10 | Barbarian group: Bash, Find Potion/Find Item (cltst 55 -> 62), Frenzy start (srvst 78) | W1, P2 |

### After the cut, by rows unlocked per session

1. Router slot (one free srvdofunc slot dispatching by skill id), built with the first rule past the free slots.
2. The 4 dead item events and the remaining shared hit-function groups.
3. Remaining classes, fewest rules first: Necromancer, Paladin, Druid, Sorceress, Assassin (its L finisher split by hook point).
4. Monster and boss kits, then cube ops.
5. Investigations: phasing, a3q5, SkilPal/SUNITNPC, PerfectShrine, charm rule, cube op 30/31.

A rule that doesn't fit a slot uses a router on that table, keyed by the id its function receives; with no key it is cut
and listed. Slot sizes and free ranges per table: [skill-function-tables](https://github.com/synpoox/pd2-resurrected/blob/research/skill-function-tables/docs/research/skill-function-tables.md).

## Skill functions (server and client halves)

| Rule | PD2 source | Bucket | D2R slot | RVAs | Tier | Size | Status | Test | Work item |
|---|---|---|---|---|---|---|---|---|---|
| Split Throw | srvdo 189, cltdo 118, srvst 79 (shared, see below), cltst 9, shit 65; return missiles sdo 38, cdo 70 | c | srvdo 189, cltdo 118, shit 65; sdo 38 -> 46 (D2R 38 is flamewave), cdo 70 -> 78 (D2R 70 is vine beast death; missile cltdo has 92 slots, 78-91 free); cltst 9 is D2R's own (Dragon Tail's client start), set as data | SrvDoTable, CltDoTable, MissileSrvHitTable, MissileSrvDoTable, MissileCltDoTable, getSkillRecord, evalSkillCalc, skillTargetXY, createMissile, setUnitSkillFlag, PickSkillMissile, UnitDataFlag, ClientSkillTargetXY, ClientCreateMissile, SrvHitSubMissiles, MissileSrvDoDefault, MissileSrvEnd, MissileCltDoDefault, MissileOwner, ClientMissileOwner, MissileSrvFrames, MissileCltFrames, PathSetTargetUnit, PathSteer, UnitDistance | Exact | M | built (W2) with approximated deviations, owed to the unported list (`docs/unported.md`, W3): the spread is Multiple Shot's line, calc3 is read as the number of triggering axes, calc4 (ED%) is not applied; shit 65 runs D2R srvhit 4 on any collision; sdo 46 / cdo 78 home on the thrower like Guided Arrow and end within 2 subtiles (PD2's data has no homing column, so homing is a guess) | cast logs missile count = calc1 (`par1+min(lvl/5,par2)`); client draws them; out of town, 5 casts with PD2's splitthrowaxe (it pierces, so one axe can spawn several returns) logged 28 returns spawned and 28 caught; the axes show on screen once the fixture gives them an HD model (`missile_hd`; the owner saw them on Slot5) | W1, W2 |
| Lightning Bolt, Lightning Fury | srvdo 188, cltdo 117 | c | 188, 117 | SrvDoTable, CltDoTable, createMissile | Exact | M | verify | first check what srvdo 188 changes (Lightning Bolt has no calc columns); data only if nothing visible | W5 |
| Inferno, Arctic Blast | srvdo 182, cltdo 111 | c | 182, 111 | SrvDoTable, CltDoTable | Exact | M | open | cast logs values from PD2's calc columns |  |
| Charge (Paladin) | srvdo 180, cltdo 112, srvst 72, cltst 57 | c | 180, 112, srvst 72, cltst 57 -> ? (Q3) | SrvDoTable, CltDoTable, SrvStTable, SkillStart, CltStTable ? | Exact | M | open | charge logs hit damage from calc |  |
| Dragon Flight | srvdo 178 | c | 178 | SrvDoTable, skillTargetXY | Exact | S | open | cast logs values from calc |  |
| Holy Light | srvdo 176, chit 66 | c | 176, chit 66 | SrvDoTable, CltHitTable | Exact | M | open | cast logs values from calc; client draws the delay missile |  |
| Fire Claws | srvdo 175, cltdo 110, srvst 79 (shared) | c | 175, 110 | SrvDoTable, CltDoTable | Exact | M | open | hit logs values from calc |  |
| Vengeance | srvdo 174 | c | 174 | SrvDoTable | Exact | S | open | hit logs values from calc |  |
| Firestorm (Druid) and wall makers (DiabWall, UberDiabWall, FingermageBossDiabWall, KanemithFirestorm) | srvdo 166, cltdo 119 (Firestorm only) | c | srvdo 166 -> 199, cltdo 119 | SrvDoTable, CltDoTable, SrvDoDispatch (overflow) | Exact | M | open | cast logs wall missiles from calc |  |
| Leap, Leap Attack, Leap Attack Korlic | srvdo 153, cltdo 97 | c | srvdo 153 -> 172, cltdo 97 -> 115 | SrvDoTable, CltDoTable | Exact | M | open | leap logs landing point and hit | W8 |
| Double Throw, UberMadawcDoubleThrow | srvdo 154, cltdo 98, shit 30, dmg 16 | c | srvdo 154 -> 183, cltdo 98 -> 116, shit 30 -> 66 if different (verify), dmg 16 -> ? (D2R 16 is echoingstrike) | SrvDoTable, CltDoTable, SrvHitTable | Exact | M | open | cast logs both throws and their hits | W9 |
| Teleport, Blink, LucionBlink, Mon Bloodwarp | srvdo 155 | c | 155 -> 187 | SrvDoTable | Exact | M | verify | teleport logs range rule; compare with D2R Teleport (srvdo 27) |  |
| Gust (Druid) | srvdo 156 | c | 156 -> 190 | SrvDoTable | Exact | M | open | cast logs values from calc |  |
| Blood Warp (Necromancer) | srvdo 157, srvst 66, cltst 56 | c | srvdo 157 -> 191, srvst 66 -> 80, cltst 56 -> ? (Q3) | SrvDoTable, SrvStTable, CltStTable ?, SrvDoDispatch (overflow) | Exact | M | open | cast logs values from calc |  |
| Bash | srvdo 158 | c | 158 -> 192 | SrvDoTable, SrvDoDispatch (overflow) | Exact | S | open | hit logs values from calc | W10 |
| Combustion and boss novas (Fingermage, Rathma x3, Leoric, Zhar) | srvdo 159, cltdo 100 | c | srvdo 159 -> 193, cltdo 100 -> 120 | SrvDoTable, CltDoTable, SrvDoDispatch (overflow) | Exact | M | open | cast logs nova count and radius |  |
| Static Field (and Merc, Proc variants) | srvdo 160 | c | 160 -> 194 | SrvDoTable, SrvDoDispatch (overflow) | Exact | S | open | cast logs damage from calc |  |
| Desecrate | srvdo 161, cltdo 101 | c | srvdo 161 -> 195, cltdo 101 -> 121 | SrvDoTable, CltDoTable, SrvDoDispatch (overflow) | Exact | M | open | cast logs cloud count |  |
| Joust | srvdo 162 | c | 162 -> 196 | SrvDoTable, SrvDoDispatch (overflow) | Exact | M | open | cast logs hit from calc |  |
| Poison Dagger (and Spider Boss; BaalMinionBossBerserk shares srvst 68) | srvdo 164, cltdo 102, srvst 68 (verify: D2R 68 is Double Swing's and Cleave's start, but PD2's Double Swing uses 32), shit 61 | c | srvdo 164 -> 197, cltdo 102 -> 122, shit 61 | SrvDoTable, CltDoTable, SrvHitTable, SrvDoDispatch (overflow) | Exact | M | open | hit logs cloud missile and poison from calc |  |
| Hunger | srvdo 165 | c | 165 -> 198 | SrvDoTable, SrvDoDispatch (overflow) | Exact | S | open | hit logs values from calc |  |
| Force Move (every class, charstats) | srvdo 167, srvst 69 | c | srvdo 167 -> 200, srvst 69 -> 81 | SrvDoTable, SrvStTable, SrvDoDispatch (overflow) | Function-equivalent | S | open | cast moves without attacking; log position |  |
| Find Potion, Find Item | srvdo 168, cltdo 104, srvst 70, cltst 55 | c | srvdo 168 -> 201, cltdo 104 -> 124, srvst 70 -> 82, cltst 55 -> ? (Q3) | SrvDoTable, CltDoTable, SrvStTable, CltStTable ? | Exact | S | open | corpse use logs the result | W10 |
| Death novas (RathmaDeath, NihlathakMapDeath, LucionDeath1-4, DollMeteor) | srvdo 169, cltdo 105 | c | srvdo 169 -> 202, cltdo 105 -> 125 | SrvDoTable, CltDoTable, SrvDoDispatch (overflow) | Exact | M | open | boss death logs the nova |  |
| Assassin charge-up finishers (Tiger Strike, Fists of Fire, Cobra Strike, Claws of Thunder, Blades of Ice, Royal Strike, RakanothStrike) | srvdo 170, prg 139/171/172 (server), prg 81/106/107 (client), srvst 78, chit 67, cdo 69 (fistsoffireexplode, bladesoficecubes) | c | srvdo 170 -> 203; prg slots depend on Q2 (if prg indexes the do tables, client prg 106 collides with D2R cltdo 106 Bind Demon -> 127, and 107 -> 128 unless empty); cdo 69 -> ? (D2R 69 is corpseexplosion) | SrvDoTable, CltDoTable, SrvPrgTable ?, SrvDoDispatch (overflow) | Exact | L | open | each finisher logs its charge release values |  |
| PD2 monster: PoisonEggSpawn | srvdo 173, cltdo 109 | c | srvdo 173, cltdo 109 -> 126 | SrvDoTable, CltDoTable | Exact | S | open | cast logs spawn |  |
| PD2 monster: TorajanBossNest | srvdo 181 | c | 181 | SrvDoTable | Exact | S | open | cast logs spawn |  |
| PD2 monster: UberDiabSummon, UberDiabRun | srvdo 177, srvst 71 | c | 177, srvst 71 | SrvDoTable, SrvStTable | Exact | M | open | cast logs summon count |  |
| PD2 monster: RathmaSummon | srvdo 179 | c | 179 | SrvDoTable | Exact | M | open | cast logs summon count |  |
| PD2 monster: Lucion kit (Nova, Dash, Punish, Blink client) | srvdo 184/185/186, srvst 76, cltst 58, cltdo 113/114 | c | 184, 185, 186, srvst 76, cltst 58 (verify table size), cltdo 113, 114 | SrvDoTable, CltDoTable, SrvStTable, CltStTable ? | Exact | L | open | each Lucion skill logs on cast |  |
| Melee splash (proc_SplashDamage via item_splashonhit, Golem Splash, Skeleton Splash) | srvdo 163 | c | 163 if D2R's slot is empty, else -> overflow | SrvDoTable, castSkillEvent | Exact | M | verify | melee hit logs splash targets and radius (`inc_splash_radius`) |  |
| WispBossNova | srvdo 41, cltdo 15 | c | 41, 15 if D2R's are empty or equal (D2R cltdo 15 is a "random spray" function) (equal: D2R's Phoenix Strike charge pair, see skill-function-tables) | SrvDoTable, CltDoTable | Exact | S | data | cast logs nova | W3 |
| Royal Fire (RadamentBossRoyalFire, LucionRoyalFire) | srvdo 143, cltdo 83 | c | 143, 83 if D2R's are empty or equal (Q2) (equal: D2R's Phoenix Strike charge pair, see skill-function-tables) | SrvDoTable, CltDoTable | Exact | S | data | cast logs fire missiles | W3 |
| Power Strike, Lightning Strike client half (and boss variants) | cltdo 22 | c | 22 if D2R's is empty or equal | CltDoTable | Exact | S | verify | client draws the strike | W6 |
| Monster frenzy (MonFrenzy, BloodLordFrenzy) | srvdo 109, srvst 64 | a | srvdo 109 -> 9, srvst 64 -> 67 | none | Exact | S | data | thornhulk frenzies, no town portal | W3 |
| Vanilla functions reassigned to other skills (e.g. Magic Arrow srvdo 8, Berserk 9, War Cry 22, Psychic Hammer 26; Grim Ward srvst 34 / cltst 28 = D2R Find Item's start) | srvdo/cltdo/srvst/cltst values D2R's base bank also uses | a | same | none | Exact | S | data | assumes ProjectDiablo.dll did not change the vanilla function bodies (Q4) | W3 |
| Corpse/Vine Cycler start (CorpseCycler, VineCycler) | srvst 67 | c | 67 -> ? (D2R moved Frenzy's start from 1.13c's 64 to 67, so PD2's 67 is a different function) | SrvStTable | Exact | S | verify | cast logs start accepted |  |

## Start functions shared by several skills

| Rule | PD2 source | Bucket | D2R slot | RVAs | Tier | Size | Status | Test | Work item |
|---|---|---|---|---|---|---|---|---|---|
| Holy Sword start | srvst 73 | c | 73 | SrvStTable, SkillStart | Exact | S | open | cast logs start accepted |  |
| Golem start (Clay, Blood, Fire Golem) | srvst 74 | c | 74 | SrvStTable | Exact | S | open | summon logs golem count rule |  |
| Smite start | srvst 75 | c | 75 | SrvStTable | Exact | S | open | cast logs start accepted |  |
| Spirit Wolf, Fenris start | srvst 77 | c | 77 | SrvStTable | Exact | S | open | summon logs start accepted |  |
| Frenzy, Tiger Strike start | srvst 78 | c | 78 | SrvStTable | Exact | S | open | cast logs start accepted | W10 |
| Fire Claws, Split Throw start | srvst 79 | c | 79 | SrvStTable, RemoveSkillState, NewStateList, AddAuraStats, AddAuraStatsB, SetListStat, UnitStateKey, StateListKey, SendSkillState, SetUnitState, SkillStateExpired | Exact | S | built (W2) with an approximated deviation, owed to the unported list (W3): aurastate + aurastat1-6 for a fixed 25 frames (PD2's rows have no auralencalc). D2R has no temp_skill_buff; the merged data appends it as state 270 (checked in game, #45), the test fixture keeps quickness until the merged tables pass validation | cast logs the state, length and aurastat1 = attackrate +par4 (10) | W2 |
| Paladin aura client start (19 auras) | cltst 54 | c | 54 -> ? (Q3) | CltStTable ? | Exact | S | verify | aura start draws on client |  |
| Paladin aura client do (19 auras, Force Move, SelfAura skills) | cltdo 103 | c | 103 -> 123 | CltDoTable | Exact | M | open | aura graphic draws; D2R would run Cleave's client code |  |
| Baal Corpse Explode client start | cltst 82 | a | none (past D2R's table end: use none, per toolkit notes) | none | Exact | S | data | check the cast still shows | W3 |

## Missile functions

| Rule | PD2 source | Bucket | D2R slot | RVAs | Tier | Size | Status | Test | Work item |
|---|---|---|---|---|---|---|---|---|---|
| Fire Bolt, Ice Bolt, Blizzard, Chilling Armor bolt hit | shit 62 | c | 62 | SrvHitTable | Exact | M | open | hit logs the hit effect |  |
| Fire Arrow, Exploding Arrow hit | shit 63 | c | 63 | SrvHitTable | Exact | S | open | hit logs explosion | W6 |
| Uber immolation arrow hit | shit 64 | c | 64 | SrvHitTable | Exact | S | open | hit logs fire |  |
| Kanemith blaze dot hit (shares 61 with Poison Dagger) | shit 61 | c | 61 | SrvHitTable | Exact | S | open | hit logs dot |  |
| Nova and orb hits (thunderstorm1, powerstrikenova1, powerstrikenova1valk, shatteringarrow, voidbolt, rathmapoisonorb, leoricsummonnova, ZharNovaOrb, IskatuMagicOrb) | shit 5, chit 4 | a | 5/4: equal to D2R's ([nova-orb-hits](research/nova-orb-hits.md)) | SrvHitTable, CltHitTable | Exact | S | data | a Thunder Storm strike leaves 64 / max(`cHitPar1`, 1) client sub-missiles (read from game memory, not a log) | W7 |
| Summon-on-hit missiles (leoricsummonorbbolt, GuardianOfFateSummonHit, iskatubliz3, ...) | shit 6 | a | 6 (D2R: spawns monstats row `sHitPar1`) | SrvHitDispatch | Exact | S | verify | hit spawns the `sHitPar1` monster; remap `sHitPar1` ids |  |
| Blade fragment hit (bladefragment1) | shit 34, chit 6 | c | shit 34 -> 69 (D2R 34 is echoingstrike), chit 6 (empty in D2R) | SrvHitTable, CltHitTable | Exact | S | open | hit logs effect; client draws hit |  |
| Throwing potions (12 `tpot_*` rows, items `tpfs` etc.) and Sharptooth's armageddon control/rock | shit 60, chit 65 | c | shit 60 -> 68 (D2R 60 is colossal throwing axe), chit 65 -> 72 (D2R 65 is miasmachains) | SrvHitTable, CltHitTable | Exact | M | open | thrown potion logs its explode/nova sub-missiles (`sHitPar1`) |  |
| Psychic Hammer missile damage | dmg 15 | c | 15 -> ? (D2R 15 is holybolt / fistoftheheavensbolt) | SrvDmgTable ? | Exact | S | verify | hit logs damage |  |
| Multiple Shot arrow and bolt damage | dmg 17 | c | 17 -> ? (D2R 17 is ringoffire) | SrvDmgTable ? | Exact | S | verify | Multiple Shot hit logs damage from PD2's calc | W6 |
| Placeholder missile `temp1` | sdo 39, cdo 71 | e | none | none | Exact | S | unreachable | no skill, missile, monster or item names it |  |
| Desert turret fireball client hit | chit 11 | c | 11 if equal, else -> 69 | CltHitTable | Exact | S | verify | client draws hit |  |
| Immolation, Blaze and Rathma fire client hits (17 missiles) | chit 12 | c | 12 if equal, else -> 70 | CltHitTable | Exact | S | verify | client draws hit | W6 |
| Fist of the Heavens delay client hit | chit 26 | c | 26 if equal, else -> 71 | CltHitTable | Exact | S | verify | client draws hit |  |
| Blessed Hammer | sdo 40, cdo 72 | c | 40, 72 if D2R's are empty | SrvMisDoTable, CltMisDoTable | Exact | M | verify | hammer logs path and hits |  |
| Siege beast stomp client do | cdo 12 | c | 12 if empty | CltMisDoTable | Exact | S | verify | client draws stomp |  |
| Rathma bone spear, blade fragment damage | dmg 18, dmg 19 | c | 18, 19 (D2R's table size unknown) | SrvDmgTable ? | Exact | S | verify | hit logs damage |  |
| `*12` server hit (royalstrikechainlightning, boichaosice) | shit `*12` | a | none | none | Exact | S | data | the text parses as 0 (no hit function); confirm D2R reads it the same | W3 |

## Item events and item rules

| Rule | PD2 source | Bucket | D2R slot | RVAs | Tier | Size | Status | Test | Work item |
|---|---|---|---|---|---|---|---|---|---|
| Skill on cast (`item_skilloncast`, 62 live item rows) | events.txt `dospellcast`, itemeventfunc 33 | c | event by name; itemeventfunc 33 not in D2R | UnitEventDispatch, SkillStart, castSkillEvent | Exact | M | open | cast with Bane Ash logs the proc at its chance |  |
| Skill on block (`item_skillonblock`, 5 live rows) | events.txt `doblock`, itemeventfunc 30 | c | event by name | UnitEventDispatch, block hook ? | Exact | M | open | blocked hit logs the proc |  |
| Skill on crit (`item_skilloncrit`) | events.txt `docrit` | e | none | none | Exact | S | unreachable | no item or monster row carries the stat |  |
| Skill on pierce (`item_skillonpierce`) | events.txt `dopierce` | e | none | none | Exact | S | unreachable | no item or monster row carries the stat |  |
| Corpse finish event | events.txt `corpsefinish` | e | none | none | Exact | S | unreachable | no stat or skill listens to it |  |
| Skill while equipped (`item_skillonequip`, prop `equipped-skill`: Tempest, Blackoak Shield, Spectral Plate) | itemstatcost stat, properties func 22 | c | none | ? | Exact | M | verify | equip logs the SelfAura skill running |  |
| Phasing (stat `phasing` + state, from Phasing SelfAura) | ProjectDiablo.dll `StrPhasingStatDesc` | c | none | ? | Exact | M | verify | effect not identified yet (Q6) |  |
| Charm "only one unique" rule | ProjectDiablo.dll `CharmOnlySingleUniqueDesc`; PD2 uniqueitems `carry1` is empty on every row | a | uniqueitems `carry1` groups (D2R: Gheed's 1001, Annihilus 1002, Torch 1003, crafted Rupture charms 1004) | none | Exact | S | verify | second unique charm is refused; which PD2 charms (trophies, Dev Charm, ...) and whether one shared group is unknown |  |
| Map items (48 items) | misc `pSpell` 12 | c | 12 -> 16 (D2R 12 is the Resist Scroll) | ItemSpellTable, ItemSpellCheck, ItemSpellUse | Exact | L | open | using a map logs the map's area and modifiers (part of the map system, Q7) | map plan |
| Unlimited TP and ID books | misc `pSpell` 13 | c | 13 -> 17 (D2R 13 is the respec token) | ItemSpellTable, ItemSpellUse | Exact | S | open | use logs a portal and the book keeps its charges |  |
| Other item spells (potions, books, herb) | misc `pSpell` 1-9 | a | same | none | Exact | S | data | PD2 rows use them as D2R does (by item code) | W3 |
| Item stat descriptions | itemstatcost `descfunc` 0-4, 6-9, 20 | a | same | none | Exact | S | data | D2R's data guide documents each with the vanilla meaning; check tooltip text | W3 |
| `item_shiny_appearance` description | itemstatcost `descfunc` 30 | e | none | none | Exact | S | unreachable | no row carries the stat; D2R's 30 means "single skill random" |  |
| Property functions | properties `func` 3, 8, 22, 24 | a | same | none | Exact | S | data | D2R's own properties with the same codes use the same functions | W3 |
| Random sorceress skill on striking (Ormus' Robes, prop `sorc-skill-rand-ctc`) | properties `func1` 25 | c | 25 -> 37+ (D2R 25 is `affix-rand`; table size unknown) | PropertyFuncTable | Exact | M | open | Ormus' Robes rolls one sorceress skill CTC within PD2's ranges |  |

## Cube ops

| Rule | PD2 source | Bucket | D2R slot | RVAs | Tier | Size | Status | Test | Work item |
|---|---|---|---|---|---|---|---|---|---|
| Item stat less than / equal (corruption outcomes, map recipes; 1,126 enabled rows) | cubemain `op` 16, 18 | a | 16, 18 | none | Exact | S | data | recipes match D2R's data-guide meaning; the merge remaps the `param` stat id of ops 3-26 (361 `corruptor` -> 414, 185, 276, 458) | P9, map plan |
| Quest item difficulty check | cubemain `op` 28 | a | 28 | none | Exact | S | data | same as D2R's own op 28 rows | W3 |
| Map recipes (heroic, corrupt, fortify, ears, map events; 26 rows) | cubemain `op` 30 | c | 30 | CubeRecipeSearch, CubeInputMatch | Exact | M | verify | meaning guessed (Q8); recipe accepts only what PD2 accepts | map plan |
| Demonic cube reroll of unique and set items (16 rows) | cubemain `op` 31 | c | 31 | CubeRecipeSearch, CubeInputMatch, CubeExecutor | Exact | M | open | reroll logs new stats inside the item's ranges |  |

## Server rules from the DLL triage

| Rule | PD2 source | Bucket | D2R slot | RVAs | Tier | Size | Status | Test | Work item |
|---|---|---|---|---|---|---|---|---|---|
| `.noexp` toggle | ProjectDiablo.dll `PlrMsg.cpp` strings | c | none | experience gain ?, chat command ? | Function-equivalent | S | open | `.noexp` logs no experience on a kill |  |
| `.set-home` start town | ProjectDiablo.dll strings | c | none | ? | Function-equivalent | S | open | reload logs the chosen town |  |
| Character stat commands (`.kills`, `.deaths`, `.played`, `.age`, `.allstats`, `.accountstats`) | ProjectDiablo.dll strings | b | none | UnitDeath | Function-equivalent | M | open | kill a boss, `.kills` logs +1; counters need saved state |  |
| Teleport blocked areas | ProjectDiablo.dll `TeleportBlocked` | a | levels.txt `Teleport` | none | Exact | S | verify | teleport refused in the same areas as PD2 |  |
| Corrupted zones (D2R's Terror Zones) | ProjectDiablo.dll `pGame->nCorruptedZone`, `CorruptedZoneNotify` | a | D2R Terror Zones, as they are | none | Function-equivalent | S | verify | zone rotation and bonuses compared with PD2; each difference is a deviation on the unported list |  |
| Map events and invaders (AiThink4, `MapEventNotify*`, Invader monsters) | ProjectDiablo.dll | c | none | ? | Exact | L | verify | part of the map system (Q7) | map plan |
| Corruption in one press (Q10: PD2 re-runs the recipe search after a roll row and marks a destroyed item; the alpha takes two presses and the merge marks it) | stat 361 `corruptor`, prop `corruptnum`, op 16 recipes, cubemain `ladder` | c | none | CubeExecutor | Exact | M | open | one cube press gives a PD2 outcome and the corrupted mark |  |
| Loot allocation | ProjectDiablo.dll `pGame->nItemAllocation`, `LootAllocationTitle` | c | none | ? | Function-equivalent | M | verify | multiplayer phase only | later release |
| PvP modifiers, battle-royale checks | ProjectDiablo.dll `*PVPModifiers`, `BRLevelCheck`, `BRGearCheck` | e | none | none | Function-equivalent | S | cut | online feature |  |
| Act 3 quest 5 patch | ProjectDiablo.dll `a3q5.cpp` | c | none | ? | Exact | S | verify | effect not identified (Q6) |  |
| Skill palette, NPC patches | ProjectDiablo.dll `SkilPal.cpp`, `SUNITNPC.CPP` | c | none | ? | Exact | M | verify | effects not identified (Q6) |  |
| Channel take-over: Inferno, Arctic Blast (srvdo 182) and Blade Fury (48) | ProjectDiablo.dll gate A, [plrmodes.md](research/plrmodes.md) | c | none | PlrModeChange | Exact | S | verify | another skill starts at once mid-channel; a second Blade Fury is refused |  |
| Charge (srvdo 180) in the Charge/Whirlwind rule, also for skill-less requests | ProjectDiablo.dll gate A, [plrmodes.md](research/plrmodes.md) | c | none | PlrModeChange | Exact | S | verify | Whirlwind and Charge take a skill-less mode change as in PD2 |  |
| Run and Concentration in the end-of-action window | ProjectDiablo.dll gates A and B, [plrmodes.md](research/plrmodes.md) | c | none | PlrModeChange | Exact | S | verify | run queued at the end of an attack; Concentration blocks get-hit, not the next attack |  |
| Get-hit/block end window, cast/sequence delay from neutral or run | ProjectDiablo.dll gate B, [plrmodes.md](research/plrmodes.md) | c | none | PlrModeChange | Exact | S | verify | new mode taken in the last 2 frames of get-hit; cast 5 frames after the player-data stamp |  |
| Level life regen cap (30, `bloodwarp` lifts it) | ProjectDiablo.dll `0x102fc000`, [plrmodes.md](research/plrmodes.md) | c | none | ? | Exact | S | verify | part of the map system (Q7) | map plan |
| Player death handler and two more PlrModes hooks | ProjectDiablo.dll `0x1036fcd0`, `0x1036e950`, `0x10374020`, [plrmodes.md](research/plrmodes.md) | c | none | ? | Exact | ? | verify | not decoded yet |  |
| Perfect shrine | ProjectDiablo.dll `PerfectShrine` | c | none | ? | Exact | S | verify | effect not identified (Q6) |  |
| Stackable items / item quantity | ProjectDiablo.dll `HOTKEY_ItemQuantity`; cube rows note "stackable maps" | c | none | ? | Exact | M | verify | maps stack and split as in PD2 |  |
| Skill aura event 32 (Revive), 3 (Chilling Armor), 16 (Smite) | skills `auraeventfunc` 32, `auraeventfunc2` 3, `auraeventfunc1` 16 | c | 32, 3 if D2R's are empty or equal; 16 -> ? (only D2R's RotW bank uses 16) | ? | Exact | S | verify | event fires on the aura owner |  |
| State remove function 12 (`skilldelay`) | states `remfunc` 12 | c | 12 if empty | ? | Exact | S | verify | delay state ends as in PD2 |  |
