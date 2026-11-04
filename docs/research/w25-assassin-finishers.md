# Assassin charge-up finishers: hook points (W25)

Answers work item W25 (#100): which table slots the charge-up skills (Tiger Strike, Fists of Fire, Cobra Strike,
Claws of Thunder, Blades of Ice, Royal Strike, RakanothStrike) take in D2R, and how PD2 builds and releases charges.
PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) in the PD2 Ghidra project, D2R side from the 93847 dump
(`ImageStamp 0x6ab3782c`); addresses are D2R RVAs unless they start with `0x10` or name their 1.13c DLL ("D2Game
`0x94e60`" is an RVA in PD2's 1.13c `D2Game.dll`). Read statically, not run in game.

## Where PD2 puts them

PD2's install routine `0x1036ba20` writes the D2Game do table (`[0x105de8fc]`, also reached as `[0x105e25d0]`) and the
D2Client do table (`[0x105de71c]`). prg values index those same tables ([skill-function-tables](skill-function-tables.md),
Q2). Of the values the seven skills use, PD2 writes:

| Table | PD2 writes (finisher values) | PD2 leaves vanilla |
|---|---|---|
| server do | 39 `0x103aee00`, 139 `0x103b0cf0`, 143 `0x103b0df0`, 170 `0x103b2c60`, 171 `0x103b30a0`, 172 `0x103b3360` | 36, 37, 40, 41 |
| client do | 83 `0x103ab070`, 106 `0x103ab920`, 107 `0x103aba80` | 9-15, 81 |
| missile cltdo (`[0x105e1240]`, `0x103a6330`) | 69 `0x103a60d0` | |
| missile clthit (`[0x105e1364]`, `0x103a6c50`) | 67 `0x103a6aa0` | |

Two of the rewritten slots keep vanilla behaviour for the finishers. PD2 reaches vanilla code through
version-indexed offset entries; column 0 is 1.13c (the entry `0x104a8e2c` holds D2Game `0x10da48` there). D2Game's srvdo table is at
file offset `0x1074a8`, D2Client's cltdo table at file offset `0xdea48`.

- **srvdo 143**: when the skill's own srvdofunc is 143 (the Royal Fire bosses) it runs PD2's fire path; otherwise it
  tail-calls vanilla srvdo 143 (D2Game `0x94e60`). Blades of Ice's prg3 and Royal Strike's prg2 get vanilla 143.
- **cltdo 83**: the same split on cltdofunc 83 (the bosses), else vanilla cltdo 83 (D2Client `0x415a0`).
- **srvdo 39** calls vanilla srvdo 38 (D2Game `0x93340`) and then vanilla srvdo 39 (D2Game `0x94970`): Fists of
  Fire's first charge runs both of vanilla's later fire charges. The port calls D2R's 38 and 39, assuming they match
  1.13c's, as the `data` rows do (core-rules inventory, Q4).

This corrects [skill-function-tables](skill-function-tables.md): the Royal Fire bosses (srvdo 143, cltdo 83) are not
data. Their PD2 branch is a monster rule.

## What each function does

The charge count is the skill's `aurastat1` value on its `aurastate` list (`0x1037ddf0`). D2R's add-charge
`0x56dae0` reads it the same way.

- **srvdo 170** (all seven skills; D2R uses 34 and 35, and 35 alternates hands before calling 34): one charge-up hit.
  1. If `0x1036ee80(unit)` returns 3, it removes every progressive state the unit has. The list of those states is
     at data tables `+0x16c`, count `+0x170`. Then it rolls the melee attack (`0x103048a0`).
  2. On a hit (bit 0 set, and either the target passes `0x1037c820` or none of `0x8390` is set), it adds charges
     (`0x1036efe0`) while this skill's charge count is at most that returned value. Charges are shared: a charge-up
     skill takes over the highest count, and a hit at 3 clears all states and restarts at 1.
  3. Cobra Strike and RakanothStrike (`aurastat1` 319, `progressive_steal`): at the 2nd charge it adds a 3-frame stat
     list with lifedrainmindam (60) and manadrainmindam (62) = calc1, and stat 25 = calc2.
  4. The melee damage (`0x1031b4a0`) follows. Then, on a hit, it calls srvprgfunc[min(charges, 3)] at once, bound
     191, with `(game, unit, skill, level)`. It sends the charge packet (`0x103a1c40`). A miss leaves the charges as
     they were, and an empty list is removed.
- **srvprg 171** (Fists of Fire 3, Cobra Strike 1, RakanothStrike 1): a spread of prgcalc[charge] missiles toward the
  target (`0x1037d780` steps the spread).
- **srvprg 172** (Fists of Fire 2, Cobra Strike 3, RakanothStrike 3): one missile call (`0x1036d250`) with
  prgcalc[charge]. All three users set that calc to 64 (a nova). 171 and 172 both have an extra branch for Cobra
  Strike (skill 265) that is not read yet.
- **srvprg 139** (Blades of Ice 2; also the srvdo of Baal Cold Missiles, RathmaColdMissiles, DemonPillar Lightning,
  WarlordMiniBoss1BaalFire): vanilla's cold-missile body, except that the missile comes from a D2Game helper through
  `[0x105e08b0]` (the helper is not resolved) instead of `srvmissile`. PD2's Blades of Ice row has no `srvmissile`,
  only `srvmissileb` (boiwavemaker), so the helper must pick the missile by charge. That is inferred from the data;
  W29 confirms it.
- **cltprg 106 / 107**: the client halves of 171 (spread) and 172 (nova), with cltcalc[charge] and cltmissile[charge].
  RakanothStrike's client prg3 is vanilla 11, not 107.
- **cdo 69** (fistsoffireexplode, bladesoficecubes): on the missile's last frame it creates its `CltSubMissile1`
  (fistsoffirefirewall, bladesoficeexplode) at the missile's position.
- **chit 67** (clawsofthunderbolt): on a client hit it creates `HitSubMissile1` (cotthunderstorm) at the hit point.
  The sub-missile gets a calc for its length and three params copied from the missile.

**Release.** A finisher (Dragon Talon and the other finishers, and plain Attack: D2R srvdo 1, 42, 46, 50, 52) calls
D2R's release dispatcher `0x56c3e0`. PD2 keeps vanilla's version of it. For each progressive state on the unit, it reads
the skill (stat 350) and level (stat 351) and takes n = min(max(charges, 1), 3). With `prgstack`, it first calls
srvprgfunc1..n-1, setting the list's charge stat to i before each call. Then it calls srvprgfunc n, with the do signature
and the charge-up skill's id, and sends the release packet (`0x5396d0`). The client side reads `cltprgfunc1-3`
(`0x216380`, `0x216920`, no bound).

## Slots in D2R

| PD2 | Users | D2R slot holds | Port |
|---|---|---|---|
| srvdo 170 | the seven skills | Korlic's Leap Attack | router 190, by skill id (254, 259, 265, 269, 274, 280, RakanothStrike 630) |
| srvprg 171 | FoF 3, Cobra 1, Rakanoth 1 | Charged Bolt Disk | -> 184 |
| srvprg 172 | FoF 2, Cobra 3, Rakanoth 3 | Leap (W8, 172) | -> 185 |
| srvprg 39 | FoF 1 | vanilla 39 (D2R's own FoF/BoI rows) | -> 186: call D2R srvdo 38, then 39 |
| srvprg 139 | BoI 2 | Baal Cold Missiles (vanilla) | -> 181 (the monster rows keep D2R's 139) |
| srvprg 143 | BoI 3, Royal 2 | vanilla 143 | data |
| srvprg 36, 37, 40, 41 | CoT, Royal | vanilla | data |
| cltprg 106 | FoF 3, Cobra 1, Rakanoth 1 | Bind Demon | -> 127 |
| cltprg 107 | FoF 2, Cobra 3 | `0x213410` (no D2R row uses it) | -> 128 |
| cltprg 9-15, 81, 83 | the rest | vanilla | data |
| cdo 69 | fistsoffireexplode, bladesoficecubes | corpseexplosion | -> 79 (78 is the Split Throw return) |
| chit 67 | clawsofthunderbolt | empty | 67 |
| srvst 23, 78 | all; Tiger Strike | vanilla; built (W10) | data; done |

The router here is the router slot (glossary): srvdofunc 190 holds core-rules' `SrvDoRouter`
(`plugin/core-rules/skill_functions.cpp`), which calls a PD2 do function by skill id. It is ours, not D2RLoader's.
The prg functions can't share it with srvdo 170. The release dispatcher calls them with the charge-up skill's
own id, which the router already maps to the 170 port. So they need real slots. D2R's free 172-190 are all used or
planned. 181 (TorajanBossNest) and 184-186 (the Lucion kit) are after-the-cut monster do functions, each with its own
skill id, so they are still built (after the cut), as router cases instead of in their own slots, and their slots
go to the four prg functions. Until then their stand-ins (`merge/standins.tsv`) keep those monsters' srvdofunc off
181 and 184-186. 188 is avoided: item casts need it empty (`ItemEffect`).

The merge moves the values by column in `tools/pd2_ids.py` FUNCS: `srvdofunc` 170 -> 190; `srvprgfunc1-3` 171 -> 184,
172 -> 185, 39 -> 186, 139 -> 181; `cltprgfunc1-3` 106 -> 127, 107 -> 128; missiles `pCltDoFunc` 69 -> 79. No other
PD2 row uses those values in those columns. Baal Cold Missiles' `srvdofunc` 139 is in another column, so it does not
move.

## Split

- **W29, server half**: the srvdo 170 port in the router (build, shared charges, the steal list, the immediate prg
  call through the table), the prg ports at 184, 185, 186 and 181, and the FUNCS entries. It is checked by charge-up
  hits on a monster (the charge count after each hit, the prg slot fired) and by a Dragon Talon release (the stacked
  prg calls). It also resolves the 139 missile helper and reads the Cobra Strike branch of 171/172, and narrows the
  finishers' stand-ins (`merge/standins.tsv`, Tiger Strike and RakanothStrike) and their unported row to the client
  columns. Size L.
- **W30, client half**: cltprg 127 and 128, cdo 79 and chit 67 (with their FUNCS entries), checked by the client
  missiles each charge creates. It removes what is left of the stand-ins and the unported row. Size M.

The split in core-rules stands. W30 needs W29 only for the charge packet that drives the client prg calls.

Owed to other rows: the Royal Fire bosses (srvdo 143 and cltdo 83, PD2's boss branch) become a monster row after the
cut (the after-the-cut entry on the unported list covers them). Their server half goes to the router, their client
half to a free cltdo slot.
