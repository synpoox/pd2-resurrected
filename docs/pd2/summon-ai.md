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
| 106 ShadowMaster (Valkyrie, Decoy `dopplezonnew`, Shadow Master) | think kept, 7 patches (below); init replaced by `0x10360af0` | think: yes (core-rules `summons`); init: no |
| 105 ShadowWarrior | init and think replaced (`0x10360a10`, `0x10360240`) | no |
| 110 Vines (Poison Creeper) | 30 bytes NOPed at `+0xab494`: its "target already poisoned / poison immune" skip | yes (core-rules `summons`, below) |
| 111 CycleOfLife (Carrion Vine, Solar Creeper) | one byte at `+0xab26e` (a flag stored 0 becomes 1) | yes (core-rules `summons`, below) |
| 61 Hireable (mercenaries) | think replaced (`0x1035a270`) | no |
| 67 NecroPet, 107 Raven, 108 DruidWolf, 109 Totem, 112 DruidBear, 86 Hydra, 103 InvisoPet | none | nothing to port |

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

## The vine thinks (#1802)

AI table rows 110 and 111 give D2R's thinks `0x5d0630` (Vines) and `0x5c83c0` (CycleOfLife). core-rules' `summons` part
patches one site in each, by a unique signature, through the MutationService (status entry `vines`).

| monai row | 1.13c | PD2 | D2R 93847 | Effect |
|---|---|---|---|---|
| 110 Vines | `+0xab494`-`+0xab4b1`: `push 2; call` (state 2, poison) `jnz`, `push 0x2d; call` (stat 45, poison resist) `cmp 0x64; jz` to "walk away" | 30 bytes NOPed | `0x5d07ad` `8d 50` (`lea edx,[rax+2]`) -> `eb 28` (`jmp 0x5d07d7`, past the same two tests) | the Poison Creeper attacks a target that is already poisoned or has 100 poison resist |
| 111 CycleOfLife | `+0xab26a` `mov dword [esp+0x1c], 0`, reached when the owner is at full life (class 426) or mana (class 427) | the stored 0 becomes 1 | `0x5c86a0` `74 52` (`je` on `r15d`, set only when the owner is below max life or mana) -> `66 90` (nop) | Carrion Vine and Solar Creeper eat a corpse in range (their skill, on their cast timer) at full life or mana too |

D2R's CycleOfLife tests the owner's life and mana through two skill fields (`+0x190`, `+0x194`) instead of 1.13c's
class ids; skipping the flag's `je` gives PD2's "always 1" for both.
