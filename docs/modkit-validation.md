# modkit validation errors

What modkit's validation (`python -m modkit build .`) reports on the committed `files/data`, and what is left alone. `modkit build` prints only the first 15; `out/validation.md` groups them with 3 examples per kind.

**Rule:** a PR that fixes any of these is not merged until an in-game test shows the fix works and breaks nothing else (Hell, a level 90+ character). Tool tests, the diff gate and CI don't show what D2R does with the data.

Counts, 2026-11-16 (main at 80cd1575, then [#171](https://github.com/synpoox/pd2-resurrected/issues/171)'s first PR):

| | Errors |
|---|---|
| On `main` | 76 |
| Fixed by direct edits of `files/data` (in-game test pending) | 22 |
| Left alone | 54 |

## Fixed

Blank where PD2 names a row no table defines (1.13c finds no row, so the game behaves as before), or holds a note in a number column (1.13c reads 0). Two property codes are lowercased (1.13c looks codes up in any case). One deviations row covers them.

| Table | Rows | Column | Was |
|---|---|---|---|
| missiles | leapattacknova | ExplosionMissile | `doubledamage1` |
| missiles | sharptoothchainpoison | ProgOverlay | `sharptoothchainpoisontrail` |
| monprop | fenris, firegolem | prop2, prop2 (N), prop2 (H) | `inc_splash_radius` |
| properties | phasing | set3 | `phasing` |
| runes | Ferocity | T1Code6 | `Thorns` (now `thorns`) |
| setitems | Naj's Circlet | prop3 | `Light` (now `light`) |
| skills | Bash | srvmissilea, srvmissileb | `bashgroundspike`, `bashgroundsmoke` |
| skills | Holy Light | srvmissilea | `holylight` |
| skills | Whirlwind, Blade Dance | aurastat2 | `item_attackrate` |
| skills | proc_SplashDamage, Skeleton Splash | skilldesc | `proc skill (special, melee splash)` |
| skills | RathmaSummon | summon | `rathmaBoneGolem` |
| skills | RathmaBoneSpear | castoverlay | `sorceress_cast_cold` |
| uniqueitems | Skullder's Ire | prop7, prop8 | `*vit`, `*enr` (commented-out codes) |

## Left alone (54)

| Errors | Where | Why |
|---|---|---|
| 18 | itemstatcost `descstrpos`/`descstrneg`, the 9 `cartographer_*` stats | false positive: the string keys `cartographer:*` come from the Cartographer plugin at run time, not from the string tables |
| 9 | monstats `TreasureClass(H)`, `TreasureClassChamp(H)`, `TreasureClassUnique(H)` on ubermephisto, uberdiablo, uberbaal: `Uber Soul` | PD2 names a TC no table defines. An empty TC may not mean "no drop" in D2R, so blanking could change drops; needs a game check |
| 3 | monstats tcextradrop TreasureClass, Champ, Unique: `Special` | same as above |
| 2 | monstats NameStr chainlightningsentry, cantorboss | no such key in PD2's strings (1.13c shows "An evil force"); the fix is a name the owner picks |
| 1 | monpreset row 3 `Place` `rathma` | names a monster id nobody defines; a spawn place, so blanking could change what spawns |
| 3 | skills ItemTgtDo `10` (FingermageBossNova, RathmaTeethNova, LeoricSummonNova) | D2R allows 0 or 1; how 1.13c reads 10 as a flag is unchecked |
| 2 | skills ItemTarget `5` (Frost Nova, Nova) | outside D2R's list; behaviour of 5 unchecked |
| 2 | states restrict `2` (delerium, cowset); 1 states colorshift `115` (black) | outside D2R's range; behaviour unchecked |
| 1 | uniqueitems String of Ears nolimit `6` | D2R allows 0 or 1; unchecked |
| 2 | skills EType `magic` (IskatuMagicOrb, SharptoothUnholyOrb) | an empty EType means physical, which may not match 1.13c's "no row" |
| 2 | skills aurastatcalc1/2 Cyclone ArmorMon | formula has an unmatched `)`; what 1.13c computes is unchecked |
| 1 | skills calc2 Bone Wall `par34 in par34` | skills have no parameter 34; unchecked |
| 2 | skills DmgSymPerCalc and EDmgSymPerCalc MegademonArmaBoss | names skill `Fissure`, which PD2 lacks |
| 1 | skills EDmgSymPerCalc Dark Pact | formula over D2R's 255-byte limit; needs a shorter equal formula or a plugin |
| 2 | treasureclassex MapStat DropCrafting Item2, InvaderBarbarian1h Item4 | TC defined further down; fixing means moving rows (ids, order) |
| 2 | treasureclassex Act 1 (H) Weap A / Armo A Item9 | names `Act 5 (N) Weap C` / `Armo C`, which no table defines; a drop-list change |

The 258 "need a plugin" findings (function numbers above D2R's, free slots, extended saves) are not errors; the plugins provide them.
