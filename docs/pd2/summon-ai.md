# PD2's summon AI changes

Answers [#1790](https://github.com/synpoox/pd2-resurrected/issues/1790). Static reads of the PD2 baseline install
(`C:\Games\PD2\ProjectD2`: `ProjectDiablo.dll`, its 1.13c `D2Game.dll`, base `0x6fc20000`) against the D2R 93847 dump.
D2Game addresses are RVAs; ProjectDiablo addresses are the installed file's (PD2.gpr's copy sits `0x70` lower in code).

## Where PD2 changes pet AI

The AI table (1.13c `D2Game +0x10e788`, 16-byte rows `{kind, init, think, next}`; D2R `0x2396e90`, 32-byte rows) is
indexed by the `monai.txt` row. PD2 writes its own functions into it (`0x10361150`) and patches 1.13c's code through its
patch table (20-byte records at file offset `0x49b028`-`0x4a0744`, [plrmodes](plrmodes.md)). For the player's pets:

| monai row | PD2 change | Ported |
|---|---|---|
| 106 ShadowMaster (Valkyrie, Decoy `dopplezonnew`, Shadow Master) | think kept, 7 patches (below); init replaced by `0x10360af0` | think: yes (core-rules `summons`); init: yes ([#1801](https://github.com/synpoox/pd2-resurrected/issues/1801), below) |
| 105 ShadowWarrior | init and think replaced (`0x10360a10`, `0x10360240`, PD2.gpr's build) | yes (core-rules `summons`, [#1800](https://github.com/synpoox/pd2-resurrected/issues/1800): init wrapped, think replaced; [core-rules](../features/core-rules.md)) |
| 110 Vines (Poison Creeper) | 30 bytes NOPed at `+0xab494`: its "target already poisoned / poison immune" skip | yes (core-rules `summons`, below) |
| 111 CycleOfLife (Carrion Vine, Solar Creeper) | one byte at `+0xab26e` (a flag stored 0 becomes 1) | yes (core-rules `summons`, below) |
| 61 Hireable (mercenaries) | think `0x1035a270` runs PD2's rules, then 1.13c's think (`+0x1dd40`), which 8 patch-table records patch | yes: Battle Orders, the aura restart, party/pet Battle Orders, Whirlwind, A4 AmpDmg and the target-search roll (core-rules `mercai`, [#1803](https://github.com/synpoox/pd2-resurrected/issues/1803), [#1925](https://github.com/synpoox/pd2-resurrected/issues/1925)); the five follow patches with the pre-think leash (core-rules `pet-leash`, [#1926](https://github.com/synpoox/pd2-resurrected/issues/1926), below) |
| 67 NecroPet, 107 Raven, 108 DruidWolf, 109 Totem, 112 DruidBear, 86 Hydra, 103 InvisoPet | none | nothing to port |

First think, outside the AI table (#1835, ported in core-rules): a new Valkyrie's AI event is queued at frame + 8
(1.13c `+0x4d879` `14` -> `08`; D2R srvdo 16 `0x558000`), a revived monster's at + 8 (`+0x50ad1` `0f` -> `08`; D2R
srvdo 58 `0x55e7e0`), and a revived monster's size is capped at 2 (`0x102fb2b0`).

Every ShadowMaster, NecroPet, DruidWolf, DruidBear and vine monstats row equals PD2's column for column, so no
table edit reproduces PD2 here.

## The ShadowMaster think (1.13c `+0xab580`, D2R `0x5cd740`)

The two builds run the same code: the leash gate `aip2(H)` squared (D2R `+0x9a`), target max range `aip2(N)`
(`+0x98`), the basic-attack chance `aip3 - 2 x skill level` (`+0x9c`). PD2's patches, and the D2R site each maps to:

| 1.13c | PD2 | D2R 93847 | Effect |
|---|---|---|---|
| `+0xab66e`, `+0xab939` | call `0x1e140` (leash) -> `0x103a2540` -> `0x10360d40` | calls at `0x5cd84f`, `0x5cdc19` of `0x5bd1e0` | PD2's leash (below) |
| `+0xab865` `jne` | `jmp` | `0x5cdafb` `75 41` | the buff branch always casts |
| `+0xab8e2` `je` | `jmp` | `0x5cdbbf` `75 17` | the pet never takes its owner's target |
| `+0xab99d` `je` | `jmp` | `0x5cdc7e` `74 73` | no basic-attack shortcut |
| `+0xabb48` `jle` | `jmp` | `0x5cde54` `7e 6e` | no step back to the owner, or aside, in a crowd of more than 3 |
| `+0xac337` `je` | `jmp` | `0x5ceb5c` `74 45` | no fallback attack when no skill scored |

1.13c's leash (D2R `0x5bd1e0`), called once the pet is more than 13 subtiles from its owner, walks it back when it is
more than about 6 subtiles away, target or not: the Valkyrie ran at a far monster, then turned back (#1790). PD2's
(`0x10360d40`): only a player's pet; squared owner-to-pet distance against `1.0 x 361` (`0x1044fb10`, `0x1044fc50`), or
`3.2 x 361` (`0x1044fb98`) while the pet has a target (the call's, or its path target within `1155.2`, `0x1044fc58`);
beyond it, warp at `0x900` and over, else walk back; with a target, nothing; else step aside within 5, or wander one
think in 12. core-rules' `summons` part hooks D2R's leash and applies this rule to the two think calls only (by return
address), with D2R's mover kinds (1 walk back, 3 warp, 4 wander, 5 step aside); PD2's own walker and its
owner-path-end distance are not ported.

## The pre-think leash (#1989, #1926)

1.13c's AI dispatcher (`+0x9d3b0`) runs a pre-think (`+0x9cd90`) before each think; non-zero skips the think. PD2 swaps its
last step, the minion-to-leader walk (`+0x9cb30`, called at `+0x9cdc5`), for `0x103606e0` (PD2.gpr `0x10360670`). For a
monster whose AI owner (AI control `+0x2c` id, `+0x30` type) is a player, other than AI 106 (returns 0: its think
leashes) and 102 (1.13c's step): nothing without a walk mode (monstats2 mWL, `0x103a2240(class, 2)`, the test 1.13c's
step makes), at velocity 0 (monstats `+0x32`) or with a dead owner; else the ShadowMaster leash's rule above (the
tick's target is still empty, so only the path target counts as one). Mercenaries skip it on PD2's own game flag
`+0x2702` (`0x1037f720`). This is the mover that replaces the Hireable think's follow: PD2 patches that think's warp
past 100, run, walk, step aside and wander away (`+0x1deb3`, `+0x1ded6`, `+0x1defb`, `+0x1e0eb`, `+0x1e11c`).

D2R's pre-think (`0x4a3a20`) inlines the leader walk (`0x4a3b50`; leader `+0x3c`/`+0x40` of the AI control) after a
step of its own, so core-rules' `pet-leash` redirects the AI tick's one call of it (`0x4a2bbb`): D2R's pre-think first,
then, when it lets the think run, the leash for a player's pet (owner `+0x68`/`+0x6c`, monstats AI `+0x52`, velocity
`+0x68`, monstats2 modes `+0xf0`). The Hireable think's sites are `0x5bec54`, `0x5bec7b`, `0x5becab`, `0x5befe9` and
`0x5bf043`, patched only once the redirect is in.

## The ShadowMaster init (#1801; 1.13c `+0xa4320`, D2R `0x5cec80`)

D2R's row-106 init is 1.13c's code and is the one the live AI table points at (`0x2397bd8` in the 93847 dump). PD2's
`0x10360af0` (PD2.gpr's build; installed `0x10360b60`) keeps 1.13c's steps: the owner's level in the
summoning skill (monstats `aip8`: Valkyrie 32, Dopplezon 28, Shadow Master 279) into the AI block, then every skill of
the owner's class list (D2Common's list, not PD2's own `0x1037fd70`) through 1.13c's filter `+0xa3d60` (PD2 calls it
through `0x103a2490`; D2R `0x5cc810`: not the skill that summons this pet, pet type in range) at 1.13c's level
`+0xa3d00` (through `0x103a24b0`: owner's level / 2 + summon level / 2, 1 to 24; inlined in D2R). It adds:

| PD2 (`0x10360af0`) | D2R (`0x5cec80`) | Pets | Effect |
|---|---|---|---|
| left and right skill = the pet's first skill, if it has one (`0x10360b9c`-`0x10360bfa`) | both Attack (`0x33ec70`, `0x33ef10` with 0) | all three | the aura slot the think fills (aitype 6) starts on the monstats skill; no aura pet here, so no visible change |
| sumskill1-5 of the summoning skill's skills.txt row (`0x1037c110`: `+0xb98` record, `+0xc4` ids, `+0xd0` calcs), each at its sumskcalc for the owner (`0x1030ad70`), after the class copy | none in the init; srvdo 16 applies them for the Valkyrie (`0x55ef80`, loop `0x55f446`); srvdo 49 (`0x5710c0`) has no sumskill loop | Decoy (DoppleZonStrafe at its level, Critical Strike, Pierce, Penetrate at the owner's base level); Valkyrie (same skills either way); Shadow Master: none (skill 279 has no sumskills) | the Decoy shoots PD2's strafe and gets the owner's passive levels |

core-rules' `summons` part wraps D2R's init (`ShadowMaster init`): it reads the pet's first skill before D2R's init
runs, then sets both slots to it and applies the five sumskills with D2R's calc (`0x3b5160`).

## The vine thinks (#1802)

AI table rows 110 and 111 give D2R's thinks `0x5d0630` (Vines) and `0x5c83c0` (CycleOfLife). core-rules' `summons` part
patches one site in each, by a unique signature, through the MutationService (status entry `vines`).

| monai row | 1.13c | PD2 | D2R 93847 | Effect |
|---|---|---|---|---|
| 110 Vines | `+0xab494`-`+0xab4b1`: `push 2; call` (state 2, poison) `jnz`, `push 0x2d; call` (stat 45, poison resist) `cmp 0x64; jz` to "walk away" | 30 bytes NOPed | `0x5d07ad` `8d 50` (`lea edx,[rax+2]`) -> `eb 28` (`jmp 0x5d07d7`, past the same two tests) | the Poison Creeper attacks a target that is already poisoned or has 100 poison resist |
| 111 CycleOfLife | `+0xab26a` `mov dword [esp+0x1c], 0`, reached when the owner is at full life (class 426) or mana (class 427) | the stored 0 becomes 1 | `0x5c86a0` `74 52` (`je` on `r15d`, set only when the owner is below max life or mana) -> `66 90` (nop) | Carrion Vine and Solar Creeper eat a corpse in range (their skill, on their cast timer) at full life or mana too |

D2R's CycleOfLife tests the owner's life and mana through two skill fields (`+0x190`, `+0x194`) instead of 1.13c's
class ids; skipping the flag's `je` gives PD2's "always 1" for both.
