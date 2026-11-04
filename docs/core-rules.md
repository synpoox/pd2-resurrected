# core-rules hook list

One row per engine rule (see `GLOSSARY.md`). Hand-edited. Built by research ticket #19; method, evidence and open questions are in
[docs/research/core-rules-inventory.md](research/core-rules-inventory.md).

Columns:

- **PD2 source**: table column and value (`srvdo` = skills `srvdofunc`, `cltdo` = `cltdofunc`, `srvst`/`cltst` = start functions,
  `prg` = `srvprgfunc1-3`/`cltprgfunc1-3` (`srvprg`/`cltprg` for one side), `shit`/`chit` = missiles `pSrvHitFunc`/`pCltHitFunc`, `sdo`/`cdo` = missiles
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

### M2: Amazon and Barbarian complete, the other classes on stand-ins

| # | Work item | Blocked by |
|---|---|---|
| W1 | Plumbing: installer, per-build RVA header, Split Throw server half (srvdo 189) moved into `core-rules` with its game check | none |
| W2 | Client-half example: Split Throw client half (cltdo 118), return missiles (sdo 38 -> 46, cdo 70 -> 78, shit 65), shared start srvst 79 | W1 |
| W3 | Stand-ins and data rows: `merge/` rules pointing each unbuilt rule's skill at the closest D2R function, "not yet" rows on the unported list, all `data` rows | P1 |
| W4 | Investigation: what PD2's `PlrModes.cpp` changes (PD2 baseline or ProjectDiablo.dll) | none |
| W5 | Amazon: Lightning Bolt, Lightning Fury (srvdo 188, cltdo 117); starts with the 188 check | W2, P2 |
| W6 | Amazon group: Fire/Exploding Arrow hit (shit 63), Multiple Shot dmg 17 -> 20, Power Strike client half (cltdo 22, a 1-byte patch), Immolation client hit (chit 12, data) | W2, P2 |
| W7 | Nova and orb hits (shit 5, chit 4) | W1, P2 |
| W8 | Barbarian: Leap, Leap Attack (srvdo 153 -> 172, cltdo 97 -> 115) | W2, P2 |
| W9 | Barbarian: Double Throw (srvdo 154 -> 183, cltdo 98 -> 116, shit 30 -> 66, dmg 16 -> 21) | W2, P2 |
| W10 | Barbarian group: Bash (srvdo router 190), Find Potion/Find Item (srvst 70 -> 82, cltst 55 -> 62), Frenzy start (srvst 78) | W1, P2 |

### Alpha: every PD2 class complete

Spec #85. Owners come from PD2's `skills.txt` `charclass` and `missiles.txt` `Skill`; a rule shared by two classes belongs to
the earliest in the order Necromancer, Paladin, Druid, Sorceress, Assassin, and the later class checks it.

| # | Work item | Blocked by |
|---|---|---|
| W11 | Necromancer: Poison Dagger (srvdo 164 -> router 190, cltdo 102 -> 122, srvst 68 -> 32 as data, shit 61) | none |
| W12 | Necromancer: Blood Warp (srvdo 157 -> router 190, srvst 66 -> 80, cltst 56 -> 63) | none |
| W13 | Necromancer: Desecrate (srvdo 161 -> router 190, cltdo 101 -> 121) | W10 |
| W14 | Necromancer: Golem start (srvst 74), Revive event 32; the get-hit/block window went to W34 | none |
| W15 | Paladin: aura client start and do: no port, the Left Click Auras companion covers them (owner 2026-11-04) | none |
| W16 | Paladin: Charge (srvdo 180, cltdo 112, srvst 72, cltst 57 -> free slot) and the Charge/Whirlwind rule | none |
| W17 | Paladin: Holy Light (srvdo 176, chit 66), Fist of the Heavens client hit (chit 26) | none |
| W18 | Paladin: Vengeance (srvdo 174), Joust (srvdo 162 -> 196) | none |
| W19 | Paladin: Holy Sword start (srvst 73), Smite start (srvst 75) and event 16, Run/Concentration window | none |
| W20 | Paladin: Blessed Hammer (sdo 40, cdo 72) | none |
| W21 | Druid: Arctic Blast and Inferno (srvdo 182, cltdo 111), channel take-over | none |
| W22 | Druid: Firestorm and the wall makers (srvdo 166 -> router 190, cltdo 119) | none |
| W23 | Druid: Gust (router 190), Hunger (srvdo 165 -> 198) | none |
| W24 | Druid: Fire Claws (srvdo 175, cltdo 110), Spirit Wolf/Fenris start (srvst 77) | none |
| W25 | Investigation: Assassin finisher hook points (prg slots, cdo 69, chit 67, charge build and release) | none |
| W26 | Sorceress: Teleport (srvdo 155 -> router 190), Teleport blocked areas | none |
| W27 | Sorceress: Static Field (srvdo 160 -> 194), Combustion and boss novas (srvdo 159 -> 193, cltdo 100 -> 120), Inferno check | W21 |
| W28 | Sorceress: Fire Bolt, Ice Bolt, Blizzard, Chilling Armor bolt hit (shit 62), Chilling Armor event 3 | none |
| W29 | Assassin: finisher server half (srvdo 170 -> router 190, srvprg 171 -> 184, 172 -> 185, 39 -> 186, 139 -> 181) | W25 |
| W30 | Assassin: finisher client half (cltprg 106 -> 127, 107 -> 128, chit 67, cdo 69 -> 79) | W29 |
| W31 | Assassin: Dragon Flight (srvdo 178), Psychic Hammer damage (dmg 15) | none |
| W32 | Assassin: Blade Fury fragment (shit 34 -> 69, chit 6, dmg 19), channel check | W21 |
| W33 | **Rush.** Melee splash (srvdo 163: proc_SplashDamage, Golem Splash, Skeleton Splash): item_splashonhit, Frenzy and Tiger Strike's splash radius (W10) and Revive's event (W14) cast it | none |
| W34 | Get-hit/block end window and cast/sequence delay (every class, PD2 gate B): find the writer of the cast stamp at PlayerData + 0x16c first ([necromancer-w14.md](research/necromancer-w14.md)) | none |
| W35 | Melee splash: the current skill's damage (Smite, kicks, Concentrate), the radius's item stat term and weapon -1, the room check ([#125](https://github.com/synpoox/pd2-resurrected/issues/125)) | W33 |

### After the cut, by rows unlocked per session

1. Router slot: built in W10 as srvdofunc 190 (dispatching by skill id; Bash first). Further rules join it.
2. The 4 dead item events and the remaining shared hit-function groups.
3. Class-themed item rules (Ormus' Robes), "not yet" on the unported list since the alpha (P12).
4. Monster and boss kits, then cube ops.
5. Investigations: phasing, a3q5, SkilPal/SUNITNPC, PerfectShrine, charm rule, cube op 30/31.

A rule that doesn't fit a slot uses a router on that table, keyed by the id its function receives; with no key it is cut
and listed. Slot sizes and free ranges per table: [skill-function-tables](https://github.com/synpoox/pd2-resurrected/blob/research/skill-function-tables/docs/research/skill-function-tables.md).

## Skill functions (server and client halves)

| Rule | PD2 source | Bucket | D2R slot | RVAs | Tier | Size | Status | Test | Work item |
|---|---|---|---|---|---|---|---|---|---|
| Split Throw | srvdo 189, cltdo 118, srvst 79 (shared, see below), cltst 9, shit 65; return missiles sdo 38, cdo 70 | c | srvdo 189, cltdo 118, shit 65; sdo 38 -> 46 (D2R 38 is flamewave), cdo 70 -> 78 (D2R 70 is vine beast death; missile cltdo has 92 slots, 78-91 free); cltst 9 is D2R's own (Dragon Tail's client start), set as data | SrvDoTable, CltDoTable, MissileSrvHitTable, MissileSrvDoTable, MissileCltDoTable, getSkillRecord, evalSkillCalc, skillTargetXY, createMissile, setUnitSkillFlag, PickSkillMissile, UnitDataFlag, ClientSkillTargetXY, ClientCreateMissile, SrvHitSubMissiles, MissileSrvDoDefault, MissileSrvEnd, MissileCltDoDefault, MissileOwner, ClientMissileOwner, MissileSrvFrames, MissileCltFrames, PathSetTargetUnit, PathSteer, UnitDistance | Exact | M | built (W2) with approximated deviations, owed to the unported list (`docs/unported.md`, W3): the spread is Multiple Shot's line, calc3 is read as the number of triggering axes, calc4 (ED%) is not applied; shit 65 runs D2R srvhit 4 on any collision; sdo 46 / cdo 78 home on the thrower like Guided Arrow and end within 2 subtiles (PD2's data has no homing column, so homing is a guess) | cast logs missile count = calc1 (`par1+min(lvl/5,par2)`); client draws them; out of town, 5 casts with PD2's splitthrowaxe (it pierces, so one axe can spawn several returns) logged 28 returns spawned and 28 caught; the axes show on screen once the fixture gives them an HD model (`missile_hd`; the owner saw them on Slot5) | W1, W2 |
| Lightning Bolt, Lightning Fury | srvdo 188, cltdo 117 | a | none, data only (`pd2_ids.MISSILE_FUNCS`, [lightning-javelins](research/lightning-javelins.md)) | none | Exact | S | data | one cast throws one `lightningjavelin` / `lightningfury` client missile (read from game memory), passed on Slot4 | W5 |
| Item casts of skills (hit skills and charges: Crackleshot, Thunderstroke, "of Lightning Fury", ...) | skills `ItemEffect` 183, 187, 188, `ItemCltEffect` 10, 116, 117 | c | 183/187/188 and 10/116/117 are empty in D2R: the dispatcher skips an empty item slot and runs the row's normal do function | SrvDoTable, CltDoTable | Exact | S | open | an item cast throws the skill's missile from the caster at the target unit (PD2's 187/116 set missile flag 0x10000) |  |
| Inferno, Arctic Blast | srvdo 182, cltdo 111 | c | 182, 111 | SrvDoTable, CltDoTable, CurrentSkill, MissileRecord, SetMissileAnimRange, UnitHasState, UnitFrame, SetUnitFrame, SetUnitFrames, SetUnitAnimSpeed, LocalPlayerSlot, ClientPlayer, UnitSeed | Exact | M | built (W21) with approximated deviations, owed to the unported list (`docs/unported.md`): PD2's re-timing of the caster's frame events after each cast is not ported, so the rows keep D2R's seqinput 10 (`merge/standins.tsv`); the client keeps D2R's local-player check; PD2's post-create call is taken to be D2R's anim-range set ([druid-w21](research/druid-w21.md)) | cast logs calc3 missiles of range calc1 (Inferno: 2 + lvl/4, max 5; Arctic Blast: 2); Slot7, Arctic Blast level 20 held out of town: 2 missiles of range 24, client drew 2 of 2 flames of range 24 (with PD2's seqinput 3 the client never reached cltdo 111) | W21 |
| Charge (Paladin) | srvdo 180, cltdo 112, srvst 72, cltst 57 | c | 180, 112, srvst 72, cltst 57 -> ? (Q3) | SrvDoTable, CltDoTable, SrvStTable, SkillStart, CltStTable ? | Exact | M | open | charge logs hit damage from calc | W16 |
| Dragon Flight | srvdo 178 | c | 178 | SrvDoTable, skillTargetXY | Exact | S | open | cast logs values from calc | W31 |
| Holy Light | srvdo 176, chit 66 | c | 176, chit 66 | SrvDoTable, CltHitTable | Exact | M | open | cast logs values from calc; client draws the delay missile | W17 |
| Fire Claws | srvdo 175, cltdo 110, srvst 79 (shared) | c | 175, 110 | SrvDoTable, CltDoTable | Exact | M | open | hit logs values from calc | W24 |
| Vengeance | srvdo 174 | c | 174 | SrvDoTable | Exact | S | open | hit logs values from calc | W18 |
| Firestorm (Druid) and wall makers (DiabWall, UberDiabWall, FingermageBossDiabWall, KanemithFirestorm) | srvdo 166, cltdo 119 (Firestorm only) | c | srvdo 166 -> router 190 (D2R fills 166), cltdo 119 | SrvDoTable, CltDoTable, MissileParams callback 1 (D2R's Firestorm maker callback), UnitSeed | Exact | M | built (W22) with an approximated deviation, owed to the unported list (`docs/unported.md`): each maker's seed is a roll of the caster's seed, where PD2 keeps a counter on the caster ([druid-w22](research/druid-w22.md)) | cast logs calc1 makers of srvmissilea (Firestorm: 3 + blvl/5), all created; the client logs the same count | W22 |
| Leap, Leap Attack, Leap Attack Korlic | srvdo 153, cltdo 97 | c | srvdo 153 -> 172, cltdo 97 -> 115 | SrvDoTable, CltDoTable, GetUnitSkill, LeapStart, LeapLanded, PathSetVelocity, AreaUnits, AreaKnockback, SkillMeleeHit, CltDoLeap, AddUnitEvent, RemoveUnitEvent, MonStatsRecord, UnitRoom, UnitId | Exact | M | built (W8) with approximated deviations, owed to the unported list (`docs/unported.md`): the client uses the server's speed, Leap's HitFlags 3 is not applied, each hit is D2R's melee hit (to-hit rolled, Korlic included), D2R's minimum airborne time gives way to PD2's speed. Re-map: srvdo 153 -> 172, cltdo 97 -> 115 (D2R's 153 / 97 are RotW functions), by value in `tools/pd2_ids.py` FUNCS. The leap distance is capped by aurarangecalc (D2R's landing finder reads it; empty means "Impossible") | landing logs radius = calc3 (Leap `min(par3+blvl*par4, 15)` = 12 at level 5, Leap Attack par5 = 8) and speed 0xb00 + calc2*0x50 (9216, 7616), client speed matches for both; out of town a Leap Attack landed with a monster in range (its hit rolled on it); Slot6, `tests/game/fixtures/leap` | W8 |
| Double Throw, UberMadawcDoubleThrow | srvdo 154, cltdo 98, shit 30, dmg 16, chit 16 | c | srvdo 154 -> 183, cltdo 98 -> 116, shit 30 -> 66 (D2R 30 is taken; PD2 30 is Chain Lightning's srvhit 12 changed), dmg 16 -> 21 (D2R 16 is echoingstrike); chit 16 is D2R's own (Chain Lightning's client hit), set as data | SrvDoTable, CltDoTable, MissileSrvHitTable, MissileSrvDmgTable, getSkillRecord, evalSkillCalc, createMissile, setUnitSkillFlag, UnitWeapon, ClientUnitWeapon, ItemMissileType, ThrowSkillMissile, ClientThrowSkillMissile, MissileBounces, SetMissileBounces, MissileSkill, MissileLevel, MissileRecord, NearestTarget, SetUnitStat, MissileOwner, UnitStateKey | Exact | M | built (W9) with shortcuts: PD2's no-bounce check in PvP maps (levels 157, 159, 166) is skipped (those maps are cut); dmg 21's fire to poison offsets assume 1.13c's +0x10 shift (PD2's rows have calc3 = 0, so it adds nothing); a monster throws column a; the client sets the skill flag before the missile check; D2R srvdo 74's other throw path (0x432f90) and Chain Lightning's extras are not copied; damagepercent = calc1 lands on the missile that hit, as PD2 does; dmg 21 is not reached because shit 66 never calls srvhit 1, the only reader of pSrvDmgFunc (PD2's calc3 = 0 makes it add nothing either way) | out of town on Slot7 with Deathbit (knife, column b): cast logs missile 743, damage% 217 = 45 + 14*8 + 6*10 synergy levels, bounces 8 = min(3 + 15/3, 8); client logs the same missile and bounces; a hit on a Fallen bounced with range 15, bounces 8 -> 7; the throws killed two Fallen and a zombie (Slot6, level 95) | W9 |
| Teleport, Blink, LucionBlink, Mon Bloodwarp | srvdo 155 | c | 155 -> 190 (router, by skill id 54, 429, 642, 682; D2R fills 155, and 187 is the merged Lightning Bolt/Fury `ItemEffect`), by value in `tools/pd2_ids.py` FUNCS | SrvDoTable, SrvDoTeleport, UnitStateList, UnitStat, createMissile, DirectionX/Y, NewStateList, SetListStat, SetUnitState | Exact | M | built (W26) with approximated deviations, owed to the unported list (`docs/unported.md`): PD2's teleport core (`0x1036cd20`, shared with Blood Warp) is D2R's srvdo 27 behind PD2's `disable_tele` check, without the `TeleportBlocked` chat line or the levels `Teleport` 3 path search (no merged level has 0 or 3), and an extended debuff list doesn't send the client its timer; no range rule exists ([sorceress-w26](research/sorceress-w26.md)) | cast logs teleportdebuff for 25 frames, each mastery -(r + mastery% of r), r = Param2 50 - calc1 `min(edmn, 50)` | W26 |
| Gust (Druid) | srvdo 156 | c | 156 -> router (srvdo 190, by skill id) | SrvDoTable | Exact | M | open | cast logs values from calc | W23 |
| Blood Warp (Necromancer) | srvdo 157, srvst 66, cltst 56 | c | srvdo 157 -> 190 (router, by skill id 439), srvst 66 -> 80, cltst 56 -> 63 (62 is Find's; D2R fills 157, 66 and 56), by value in `tools/pd2_ids.py` FUNCS | SrvDoTable, SrvStTable, CltStTable, SrvDoTeleport, UnitStat, SetUnitStat, UnitStateList, ListSkill, ListLevel, NewStateList, SetListStat, SetUnitState, SkillStateExpired | Exact | M | built (W12) with an approximated deviation, owed to the unported list (`docs/unported.md`): the teleport is the Teleport row's core (W26), and a recast that extends the running hpregen list doesn't send the client its new timer (PD2's timed-state helper `0x1036d7d0` sends packet `0x83`); the recast rule itself is D2R's NewStateList, the same as PD2's ([w12-blood-warp](research/w12-blood-warp.md)) | cast logs cost = max(calc1 12% of life, calc2 `min(ln34 * 256, 15360)`) and hpregen = cost / 125 + 1 for 125 frames | W12 |
| Bash, UberTalicBash | srvdo 158 | c | 158 -> 190 (router, by skill id 126/580; data set by `patches/020-skill-function-slots.json`) | SrvDoTable, SkillTargetUnit, QueuedHit, UnitSeed | Exact | S | built (W10) with an approximated deviation, owed to the unported list (W3): the 50% knockback roll is made before D2R's melee do function (srvdo 2) applies the hit, for every target; PD2 rolls after the apply, only for targets with flag 0x8000 | console counters: knockbacks / hits near 50% over 30+ hits | W10 |
| Combustion and boss novas (Fingermage, Rathma x3, Leoric, Zhar) | srvdo 159, cltdo 100 | c | srvdo 159 -> 193, cltdo 100 -> 120 | SrvDoTable, CltDoTable, SrvDoDispatch (overflow) | Exact | M | open | cast logs nova count and radius | W27 |
| Static Field (and Merc, Proc variants) | srvdo 160 | c | 160 -> 194 | SrvDoTable, SrvDoDispatch (overflow) | Exact | S | open | cast logs damage from calc | W27 |
| Desecrate | srvdo 161, cltdo 101 | c | srvdo 161 -> 190 (router, by skill id 83), cltdo 101 -> 121, by value in `tools/pd2_ids.py` FUNCS | SrvDoTable, CltDoTable, RoomAtXY, RoomLevelId, SpawnMonster, MonStatsRecord, DataTables, UnitSeed, NewStateList, SkillStateExpired, SetUnitState | Exact | M | built (W13) with approximated deviations, owed to the unported list (`docs/unported.md`): the 16 s corpse timer is a state list, not PD2's game event; PD2's fallback-level loop is bounded at 32 tries; only the six raise-list exclusions a ported level names are listed (`docs/research/necromancer-w13.md`) | cast in the Rogue Encampment (InTown set on the test slot only, so the fallback levels ran): 5 of 5 corpses (calc1), 3 client missiles, all 5 hid after 16 s; a cast outside town is not checked yet | W13 |
| Joust | srvdo 162 | c | 162 -> 196 | SrvDoTable, SrvDoDispatch (overflow) | Exact | M | open | cast logs hit from calc | W18 |
| Poison Dagger (and Spider Boss; BaalMinionBossBerserk shares srvst 68) | srvdo 164, cltdo 102, srvst 68, shit 61 | c | srvdo 164 -> 190 (router, by skill id 73/550; the table ends at 190, so not 197), cltdo 102 -> 122, srvst 68 -> 32 as data (PD2's 68 only calls slot 32, the generic melee start; D2R's 68 is Cleave's), shit 61 (D2R's is empty); all by `pd2_ids.FUNCS` ([necromancer-w11](research/necromancer-w11.md)) | SrvDoTable, CltDoTable, MissileSrvHitTable, SkillTargetUnit, ClientSkillTargetUnit, ElementalDamage, DirectionX/Y, createMissile, clientCreateMissile, UnitStat, SetUnitStat, MissileRecord, MissileOwner, MissileLevel, DataTables, RemoveSkillState, NewStateList, SetListStat, SetUnitState | Exact | M | built (W11) with approximated deviations, on the unported list: the clouds' poison is the fill's roll without PD2's length rescale (a factor of 1 unless a length bonus lands between the two), the melee hit is D2R's own srvdo 32, and shit 61 skips PD2's halving of negative resists on immune units | hit logs 11 poisondaggercloud missiles and the poison (at least PD2's EMin for the level); cloud hit logs poison_strike for 52 frames | W11 |
| Hunger | srvdo 165 | c | 165 -> 198 | SrvDoTable, SrvDoDispatch (overflow) | Exact | S | open | hit logs values from calc | W23 |
| Force Move (every class, charstats) | srvdo 167, srvst 69 | c | srvdo 167 -> 200, srvst 69 -> 81 | SrvDoTable, SrvStTable, SrvDoDispatch (overflow) | Function-equivalent | S | open | cast moves without attacking; log position | third-party plugin (move-only skill, owner 2026-11-03) |
| Find Potion, Find Item | srvdo 168, cltdo 104, srvst 70, cltst 55 | c | srvst 70 -> 82, cltst 55 -> 62 (PD2's owner guard, then D2R's srvst 33/34, cltst 26/28); srvdo 168 / cltdo 104 -> D2R's 69/72 and 38/40 as data; data set by `patches/020-skill-function-slots.json` | SrvStTable, CltStTable, SkillTargetUnit, ClientSkillTargetUnit | Exact | S | built (W10) with an approximated deviation and a not-yet part, owed to the unported list (W3): PD2's do functions run the start's guard a second time, D2R's do not (a refused start never reaches the do function and a corpse's owner does not change between the two frames, so the result is the same); PD2's 46 bosses whose corpses can't be searched (map, uber and PD2-only-area bosses) are not listed, they come with the map plan | log: Find ran on a monster corpse no player owns | W10 |
| Death novas (RathmaDeath, NihlathakMapDeath, LucionDeath1-4, DollMeteor) | srvdo 169, cltdo 105 | c | srvdo 169 -> 202, cltdo 105 -> 125 | SrvDoTable, CltDoTable, SrvDoDispatch (overflow) | Exact | M | open | boss death logs the nova |  |
| Assassin charge-up finishers (Tiger Strike, Fists of Fire, Cobra Strike, Claws of Thunder, Blades of Ice, Royal Strike, RakanothStrike) | srvdo 170, srvprg 39/139/171/172 (PD2's own; 36, 37, 40, 41, 143 vanilla), cltprg 106/107 (PD2's own; 9-15, 81, 83 vanilla), srvst 78, chit 67, cdo 69 (fistsoffireexplode, bladesoficecubes) | c | srvdo 170 -> 190 (router, by skill id 254, 259, 265, 269, 274, 280, 630); srvprg 171 -> 184, 172 -> 185, 39 -> 186 (D2R srvdo 38 then 39), 139 -> 181 (prg functions can't share the router with 170); cltprg 106 -> 127 (D2R 106 is Bind Demon), 107 -> 128; cdo 69 -> 79 (D2R 69 is corpseexplosion); chit 67; the rest are vanilla, set as data; by value in `tools/pd2_ids.py` FUNCS ([w25-assassin-finishers](research/w25-assassin-finishers.md)) | SrvDoTable, CltDoTable, MissileCltDoTable, CltHitTable, SrvPrgDispatch | Exact | L | open | each charge-up hit logs the charge count (shared, 3 clears all and restarts) and the prg slot it fired; a Dragon Talon release logs the stacked prg calls; the client logs each charge's missiles | W25, W29, W30 |
| PD2 monster: PoisonEggSpawn | srvdo 173, cltdo 109 | c | srvdo 173, cltdo 109 -> 126 | SrvDoTable, CltDoTable | Exact | S | open | cast logs spawn |  |
| Cold missiles (Baal Cold Missiles, RathmaColdMissiles, DemonPillar Lightning, WarlordMiniBoss1BaalFire) | srvdo 139 | c | 139 is D2R's vanilla; PD2 rewrote it to take the missile from a D2Game helper, not `srvmissile` ([w25-assassin-finishers](research/w25-assassin-finishers.md)); if they differ, -> router 190 | SrvDoTable | Exact | S | verify | cast logs the missile fired, compare with PD2's helper |  |
| PD2 monster: TorajanBossNest | srvdo 181 | c | 181 -> router 190 (181 holds the finishers' srvprg 139, W25) | SrvDoTable | Exact | S | open | cast logs spawn |  |
| PD2 monster: UberDiabSummon, UberDiabRun | srvdo 177, srvst 71 | c | 177, srvst 71 | SrvDoTable, SrvStTable | Exact | M | open | cast logs summon count |  |
| PD2 monster: RathmaSummon | srvdo 179 | c | 179 | SrvDoTable | Exact | M | open | cast logs summon count |  |
| PD2 monster: Lucion kit (Nova, Dash, Punish, Blink client) | srvdo 184/185/186, srvst 76, cltst 58, cltdo 113/114 | c | srvdo 184/185/186 -> router 190 (they hold the finishers' srvprg 171, 172, 39, W25), srvst 76, cltst 58 (verify table size), cltdo 113, 114 | SrvDoTable, CltDoTable, SrvStTable, CltStTable ? | Exact | L | open | each Lucion skill logs on cast |  |
| Melee splash (proc_SplashDamage via item_splashonhit, Golem Splash, Skeleton Splash) | srvdo 163 | c | 163 -> 190 (router, by skill id 430/461/465; D2R fills 163), by value in `tools/pd2_ids.py` FUNCS | SrvDoTable, SkillTargetUnit, PathTargetUnit, UnitRoom, AreaUnits, WeaponDamage, AvoidRoll, UnitHasState, DamageStep, DamageEffects, HitEvents, DealDamage, EngineAllocator, UnitStat | Exact | M | built (W33) with an approximated deviation, owed to the unported list: every unit within calc3 of the struck unit, that unit aside, takes the attacker's weapon damage at SrcDam with half the life leech (D2R's melee fill `0x44c030`, dealt by D2R's area apply `0x436e90` step by step with a melee hit's events 5 and 1, as PD2 fires them, no to-hit roll, no splash inside a splash); PD2 also skips skill-on-hit procs and item event 14 for the splashed units, adds the player's current skill's damage, adds an item stat sum / 20 to the radius, takes 1 off it for some weapons and checks the room first ([w33-melee-splash](research/w33-melee-splash.md)) | game check `melee-splash`: radius = calc3 (`max(6 - lvl + inc_splash_radius / 20, 0)`), units hit | W33 |
| WispBossNova | srvdo 41, cltdo 15 | c | 41, 15 if D2R's are empty or equal (D2R cltdo 15 is a "random spray" function) (equal: D2R's Phoenix Strike charge pair, see skill-function-tables) | SrvDoTable, CltDoTable | Exact | S | data | cast logs nova | W3 |
| Royal Fire (RadamentBossRoyalFire, LucionRoyalFire) | srvdo 143, cltdo 83 | c | srvdo 143 -> router 190, cltdo 83 -> free slot (PD2's boss branch in both slots; other callers get vanilla, [w25-assassin-finishers](research/w25-assassin-finishers.md)) | SrvDoTable, CltDoTable | Exact | S | open | cast logs fire missiles |  |
| Power Strike, Lightning Strike client half (and boss variants) | cltdo 22 | c | 22: D2R's is PD2's (1.13c D2Client 0x15660) plus a check that draws the strike only for the local player; `CltDoStrikeLocalOnly` jumps over it ([amazon-w6](research/amazon-w6.md)) | CltDoTable, CltDoStrikeLocalOnly | Exact | S | built (W6) | another player's, a Valkyrie's or a boss's strike draws on this client | W6 |
| Monster frenzy (MonFrenzy, BloodLordFrenzy) | srvdo 109, srvst 64 | a | srvdo 109 -> 9, srvst 64 -> 67 | none | Exact | S | data | thornhulk frenzies, no town portal | W3 |
| Vanilla functions reassigned to other skills (e.g. Magic Arrow srvdo 8, Berserk 9, War Cry 22, Psychic Hammer 26; Grim Ward srvst 34 / cltst 28 = D2R Find Item's start) | srvdo/cltdo/srvst/cltst values D2R's base bank also uses | a | same | none | Exact | S | data | assumes ProjectDiablo.dll did not change the vanilla function bodies (Q4) | W3 |
| Corpse/Vine Cycler start (CorpseCycler, VineCycler) | srvst 67 | c | 67 -> ? (D2R moved Frenzy's start from 1.13c's 64 to 67, so PD2's 67 is a different function) | SrvStTable | Exact | S | verify | cast logs start accepted |  |

## Start functions shared by several skills

| Rule | PD2 source | Bucket | D2R slot | RVAs | Tier | Size | Status | Test | Work item |
|---|---|---|---|---|---|---|---|---|---|
| Holy Sword start | srvst 73 | c | 73 | SrvStTable, SkillStart | Exact | S | open | cast logs start accepted | W19 |
| Golem start (Clay, Blood, Fire Golem) | srvst 74 | c | none: data | none | Exact | S | built (W14) as data with an approximated deviation: D2R's summon caps pets per pet type and replaces the oldest, so PD2's petmax (1 + Golem Mastery blvl / 5 + extra_golem) on the shared golem pet type is the rule; the Iron Golem keeps its own pet type and does not take a golem place, where PD2 subtracts it (unported list) | game check `golem-count`: 7 summons, at most 3 golems at Golem Mastery 10 | W14 |
| Smite start | srvst 75 | c | 75 | SrvStTable | Exact | S | open | cast logs start accepted | W19 |
| Spirit Wolf, Fenris start | srvst 77 | c | 77 | SrvStTable | Exact | S | open | summon logs start accepted | W24 |
| Frenzy, Tiger Strike start | srvst 78 | c | 78 | SrvStTable, DataTables, UnitStat, RemoveSkillState, NewStateList, SetListStat, SetUnitState, SkillStateExpired | Exact | S | built (W10) with an approximated deviation, owed to the unported list (W3): inc_splash_radius (Frenzy: calc2 * skill_frenzy / calc4; Tiger Strike: calc2 * charges under 3) goes on a temp_skill_buff list replaced each start and lasting 10 minutes, where PD2 keeps one permanent list; the replace also drops Split Throw's buff on the same state, and Split Throw's start drops the splash until the next Frenzy swing. The splash effect that reads the stat is not part of this rule | log: splash radius = PD2 dm34(lvl) * charges / 5 | W10 |
| Fire Claws, Split Throw start | srvst 79 | c | 79 | SrvStTable, RemoveSkillState, NewStateList, AddAuraStats, AddAuraStatsB, SetListStat, UnitStateKey, StateListKey, SendSkillState, SetUnitState, SkillStateExpired | Exact | S | built (W2) with an approximated deviation, owed to the unported list (W3): aurastate + aurastat1-6 for a fixed 25 frames (PD2's rows have no auralencalc). D2R has no temp_skill_buff; the merged data appends it as state 270 (checked in game, #45), the test fixture keeps quickness until the merged tables pass validation | cast logs the state, length and aurastat1 = attackrate +par4 (10) | W2 |
| Paladin aura client start (19 auras) | cltst 54 | c | none | none | Exact | S | cut | PD2's start/do only make auras left-button skills; the Left Click Auras companion covers that | third-party plugin (Left Click Auras companion, owner 2026-11-04) |
| Paladin aura client do (19 auras, Force Move, SelfAura skills) | cltdo 103 | c | none | none | Exact | M | cut | PD2's start/do only make auras left-button skills; the Left Click Auras companion covers that | third-party plugin (Left Click Auras companion, owner 2026-11-04) |
| Baal Corpse Explode client start | cltst 82 | a | none (past D2R's table end: use none, per toolkit notes) | none | Exact | S | data | check the cast still shows | W3 |

## Missile functions

| Rule | PD2 source | Bucket | D2R slot | RVAs | Tier | Size | Status | Test | Work item |
|---|---|---|---|---|---|---|---|---|---|
| Fire Bolt, Ice Bolt, Blizzard, Chilling Armor bolt hit | shit 62 | c | 62 | SrvHitTable | Exact | M | open | hit logs the hit effect | W28 |
| Fire Arrow, Exploding Arrow hit | shit 63 | c | 63 (D2R's is empty); PD2's 0x3a8800 read from ProjectDiablo.dll ([amazon-w6](research/amazon-w6.md)) | MissileSrvHitTable, SrvHitSubMissiles, MissileOwner, MissileRecord, MissileLevel, getSkillRecord, evalSkillCalc, createMissile, UnitSeed | Exact | M | built (W6) with an approximated deviation, owed to the unported list: the calc2 explosions are created at once with an activation delay where PD2 queues a timer each, their places roll on the owner's seed, and fireexplosion3 plays out where PD2 sends and removes it | hit logs calc2 explosions (Exploding Arrow: min(3 + lvl/5, 8)) | W6 |
| Uber immolation arrow hit | shit 64 | c | 64 | SrvHitTable | Exact | S | open | hit logs fire |  |
| Kanemith blaze dot hit (shares 61 with Poison Dagger) | shit 61 | c | 61 | MissileSrvHitTable | Exact | S | built (W11, Poison Dagger's shit 61; the merge gives sHitPar1 / sHitPar3 / cHitPar2 the mod's state and stat ids, `pd2_ids.HIT_PARAMS`) | hit logs state 44 with velocity and attackrate -60 | W11 |
| Nova and orb hits (thunderstorm1, powerstrikenova1, powerstrikenova1valk, shatteringarrow, voidbolt, rathmapoisonorb, leoricsummonnova, ZharNovaOrb, IskatuMagicOrb) | shit 5, chit 4 | a | 5/4: equal to D2R's ([nova-orb-hits](research/nova-orb-hits.md)) | SrvHitTable, CltHitTable | Exact | S | data | a Thunder Storm strike leaves 64 / max(`cHitPar1`, 1) client sub-missiles (read from game memory, not a log) | W7 |
| Summon-on-hit missiles (leoricsummonorbbolt, GuardianOfFateSummonHit, iskatubliz3, ...) | shit 6 | a | 6 (D2R: spawns monstats row `sHitPar1`) | SrvHitDispatch | Exact | S | verify | hit spawns the `sHitPar1` monster; remap `sHitPar1` ids |  |
| Blade fragment hit (bladefragment1) | shit 34, chit 6 | c | shit 34 -> 69 (D2R 34 is echoingstrike), chit 6 (empty in D2R) | SrvHitTable, CltHitTable | Exact | S | open | hit logs effect; client draws hit | W32 |
| Throwing potions (12 `tpot_*` rows, items `tpfs` etc.) and Sharptooth's armageddon control/rock | shit 60, chit 65 | c | shit 60 -> 68 (D2R 60 is colossal throwing axe), chit 65 -> 72 (D2R 65 is miasmachains) | SrvHitTable, CltHitTable | Exact | M | open | thrown potion logs its explode/nova sub-missiles (`sHitPar1`) |  |
| Psychic Hammer missile damage | dmg 15 | c | 15 -> ? (D2R 15 is holybolt / fistoftheheavensbolt) | SrvDmgTable ? | Exact | S | verify | hit logs damage | W31 |
| Multiple Shot arrow and bolt damage | dmg 17 | c | 17 -> 20 (D2R 17 is ringoffire; 18 and 19 stay free for PD2's); PD2's 0x3a6ea0 read from ProjectDiablo.dll ([amazon-w6](research/amazon-w6.md)) | MissileSrvDmgTable, MissileRecord, MissileOwner, EvalMissileCalc, UnitStat, SetUnitStat | Exact | S | built (W6) with an approximated deviation, owed to the unported list: hits are counted on pierce_count (merged 522), PD2 counts pierces, so the last (not piercing) hit takes one step less | each further unit hit logs 100 - (n-1)*20 percent, at least 20 | W6 |
| Placeholder missile `temp1` | sdo 39, cdo 71 | e | none | none | Exact | S | unreachable | no skill, missile, monster or item names it |  |
| Desert turret fireball client hit | chit 11 | c | 11 if equal, else -> 69 | CltHitTable | Exact | S | verify | client draws hit |  |
| Immolation, Blaze and Rathma fire client hits (17 missiles) | chit 12 | a | 12: equal to D2R's (1.13c D2Client 0xbb070, D2R 0x1acda0; ProjectDiablo.dll leaves it) ([amazon-w6](research/amazon-w6.md)) | CltHitTable | Exact | S | data | client draws hit | W6 |
| Fist of the Heavens delay client hit | chit 26 | c | 26 if equal, else -> 71 | CltHitTable | Exact | S | verify | client draws hit | W17 |
| Blessed Hammer | sdo 40, cdo 72 | c | 40, 72 if D2R's are empty | SrvMisDoTable, CltMisDoTable | Exact | M | verify | hammer logs path and hits | W20 |
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
| Random sorceress skill on striking (Ormus' Robes, prop `sorc-skill-rand-ctc`) | properties `func1` 25 | c | 25 -> 37+ (D2R 25 is `affix-rand`; table size unknown) | PropertyFuncTable | Exact | M | open | Ormus' Robes rolls one sorceress skill CTC within PD2's ranges | not yet (P12) |

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
| Teleport blocked areas | ProjectDiablo.dll `TeleportBlocked` | a | levels.txt `Teleport` | none | Exact | S | data (W26): the merged column equals PD2's for every merged level; PD2's Hell-only level set is never filled ([sorceress-w26](research/sorceress-w26.md)) | merged levels `Teleport` = PD2's | W26 |
| Corrupted zones (D2R's Terror Zones) | ProjectDiablo.dll `pGame->nCorruptedZone`, `CorruptedZoneNotify` | a | D2R Terror Zones, as they are | none | Function-equivalent | S | verify | zone rotation and bonuses compared with PD2; each difference is a deviation on the unported list |  |
| Map events and invaders (AiThink4, `MapEventNotify*`, Invader monsters) | ProjectDiablo.dll | c | none | ? | Exact | L | verify | part of the map system (Q7) | map plan |
| Corruption in one press (Q10: PD2 re-runs the recipe search after a roll row and marks a destroyed item; the alpha takes two presses and the merge marks it) | stat 361 `corruptor`, prop `corruptnum`, op 16 recipes, cubemain `ladder` | c | none | CubeExecutor | Exact | M | open | one cube press gives a PD2 outcome and the corrupted mark |  |
| Loot allocation | ProjectDiablo.dll `pGame->nItemAllocation`, `LootAllocationTitle` | c | none | ? | Function-equivalent | M | verify | multiplayer phase only | later release |
| PvP modifiers, battle-royale checks | ProjectDiablo.dll `*PVPModifiers`, `BRLevelCheck`, `BRGearCheck` | e | none | none | Function-equivalent | S | cut | online feature |  |
| Act 3 quest 5 patch | ProjectDiablo.dll `a3q5.cpp` | c | none | ? | Exact | S | verify | effect not identified (Q6) |  |
| Skill palette, NPC patches | ProjectDiablo.dll `SkilPal.cpp`, `SUNITNPC.CPP` | c | none | ? | Exact | M | verify | effects not identified (Q6) |  |
| Channel take-over: Inferno, Arctic Blast (srvdo 182) and Blade Fury (48) | ProjectDiablo.dll gate A, [plrmodes.md](research/plrmodes.md) | c | none | BusyModeGate (inline hook; D2R's gate `0x42bfb0` hands a busy player to it), CurrentSkill, UnitSkillId, UnitHasState | Exact | S | built (W21) with approximated deviations, owed to the unported list: only the server's gate is hooked, and the held skill itself goes to D2R's gate ([druid-w21](research/druid-w21.md)) | log: a skill requested mid-Inferno / Arctic Blast is allowed; a second Blade Fury is refused, another skill allowed; Slot7: Firestorm (F-key swap) took over a held Arctic Blast at once, a held Blade Fury's re-requests went to D2R's gate, which buffered them (4) and the channel went on (Attack mid-Blade Fury not seen in the log) | W21 |
| Charge (srvdo 180) in the Charge/Whirlwind rule, also for skill-less requests | ProjectDiablo.dll gate A, [plrmodes.md](research/plrmodes.md) | c | none | PlrModeChange | Exact | S | verify | Whirlwind and Charge take a skill-less mode change as in PD2 | W16 |
| Run and Concentration in the end-of-action window | ProjectDiablo.dll gates A and B, [plrmodes.md](research/plrmodes.md) | c | none | PlrModeChange | Exact | S | verify | run queued at the end of an attack; Concentration blocks get-hit, not the next attack | W19 |
| Get-hit/block end window, cast/sequence delay from neutral or run | ProjectDiablo.dll gate B, [plrmodes.md](research/plrmodes.md), decoded in [necromancer-w14.md](research/necromancer-w14.md) | c | none | PlrModeChange | Exact | S | verify; the cast stamp at PlayerData + 0x16c has no writer found yet | new mode taken in the last 2 frames of get-hit; cast 5 frames after the player-data stamp | W34 |
| Level life regen cap (30, `bloodwarp` lifts it) | ProjectDiablo.dll `0x102fc000`, [plrmodes.md](research/plrmodes.md) | c | none | ? | Exact | S | verify | part of the map system (Q7) | map plan |
| Player death handler and two more PlrModes hooks | ProjectDiablo.dll `0x1036fcd0`, `0x1036e950`, `0x10374020`, [plrmodes.md](research/plrmodes.md) | c | none | ? | Exact | ? | verify | not decoded yet |  |
| Perfect shrine | ProjectDiablo.dll `PerfectShrine` | c | none | ? | Exact | S | verify | effect not identified (Q6) |  |
| Stackable items / item quantity | ProjectDiablo.dll `HOTKEY_ItemQuantity`; cube rows note "stackable maps" | c | none | ? | Exact | M | verify | maps stack and split as in PD2 |  |
| Skill aura event 32 (Revive) | skills `auraeventfunc1` 32 | c | 32 (D2R's 0x42eea0 is reached by no row) and srvdo 58 wrapped | AuraEventTable, SrvDoTable, RegisterSkillEvents | Exact | S | built (W14, W33): D2R's Revive never registers a skill's aura events on the pet, so the srvdo 58 wrapper registers them (srvst 66's 0x42fa90) on the revived monster; the event runs proc_SplashDamage's do function directly at level 1, as PD2 does, around the unit it hit, where PD2's splash takes its path target (unported list) | game check `revive-event`: event 5 from skill 95 on a revived monster, splash 430 cast non-zero | W14 |
| Skill aura event 3 (Chilling Armor), 16 (Smite) | skills `auraeventfunc2` 3, `auraeventfunc1` 16 | c | 3 if D2R's is empty or equal; 16 -> ? (only D2R's RotW bank uses 16) | ? | Exact | S | verify | event fires on the aura owner | W19, W28 |
| State remove function 12 (`skilldelay`) | states `remfunc` 12 | c | 12 if empty | ? | Exact | S | verify | delay state ends as in PD2 |  |
