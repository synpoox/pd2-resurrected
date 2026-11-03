# Spike: PD2 Split Throw in a free srvdofunc slot

Throwaway. Answers "Spike a PD2 skill function in a free srvdofunc slot"; never merged.

## What was built

- `plugin/`: the toolkit plugin template, plus `SrvDoSplitThrow` in srvdofunc slot 189 (PD2's own number for Split Throw).
  It's installed into `SrvDoTable` (RVA `0x238ea00`) with `PatchWriteU64`, using 8 zero bytes as the expected value.
- `mod/`: a modkit mod. Its patch turns vanilla Double Throw (id 140) into a stand-in. The patch sets srvdofunc 189, PD2's calc1 `par1+(min(lvl/5,par2))` (Param1=3, Param2=4), srvmissilea `throwaxe`, and cltdofunc 0.
- `pd2-spike.log`: the plugin log from the test run on build 93847.

## Result

- The slot installs cleanly: `skills 2/2 installed: srvdofunc 172 missile fan; srvdofunc 189 split throw (spike)`.
- Casting at level 10 runs the function server side. It logs `5 of 5 missiles (row 35), spread 30`, which is PD2's count (3 + min(10/5, 4) = 5).
- The missiles are not drawn on the client, even with `ClientSend=1` on throwaxe. The function and table side work, but what a player sees needs a client do function. PD2 pairs 189 with cltdofunc 118, and D2R's cltdofunc table is at RVA `0x235f7d0` (130 slots, 110-129 free, dispatcher `0x216d20`). The template has no example for this table yet.
- Not checked: whether the server missiles hit monsters. No monster was reached in the test.

## API used (build 93847 RVAs)

getSkillRecord 0x97790, evalSkillCalc 0x3b5160, skillTargetXY 0x432b70, createMissile 0x5371a0 (MissileParams 0xa0 bytes, flags 0x820), setUnitSkillFlag 0x34e790.

## Not matched

- **Missile choice:** PD2 picks misA/misB/misC (axe/knife/jav) by weapon type. That needs a weapon-in-hand item-type getter.
- **Spread angle:** not in PD2's data. Finding it needs ProjectDiablo.dll to be reversed.
- **Other PD2 functions:** srvstfunc 79, cltstfunc 9, cltdofunc 118, and missile pSrvHitFunc 65 (new) are not built. The return submissiles are not built either.
- **Calcs:** calc2 (activation frame), calc3 (triggered) and calc4 (ED%) are not used.
- **Aura:** the temp_skill_buff attack-rate aura is missing.
- **Item rules:** the `comb` weapon requirement and throw quantity use are not built.
- **New data rows:** PD2's own missile rows are not added.

## Effort

About one session. The plugin side was cheap: a wrapper around the fan example plus one table row.

Most of the time went to the test loop:
- casting is blocked in town;
- a reinstall resets the slot savepath, so `game_slots.py sync` must be run after each one;
- the missiles were invisible.
