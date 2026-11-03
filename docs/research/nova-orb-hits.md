# Nova and orb hits (shit 5, chit 4)

Answers work item W7 (#50): do D2R's missile hit functions 5 (server) and 4 (client) do what PD2's do? They do, so the row
is data only and needs no plugin. Read from the PD2 baseline install `C:\Games\PD2\ProjectD2`: `ProjectDiablo.dll` and its
1.13c `D2Game.dll` / `D2Client.dll`. D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). Static disassembly with
capstone, then one game run. Addresses are RVAs.

## The functions

| | 1.13c | D2R 93847 |
|---|---|---|
| Server hit 5 | D2Game `0x3c700` | `0x45fd20` |
| Client hit 4 | D2Client `0xb5f00` | `0x1ae5d0` |

Both sides do the same thing: fill a MissileParams with flags 2, then loop over the 64 directions, stepping
`max(par, 1) * 4` while below `0x100`, and spawn one sub-missile per step. The server uses `HitSubMissile1` and `sHitPar1`;
the client uses `CltHitSubMissile1` and `cHitPar1`. So one hit makes `64 / max(par, 1)` sub-missiles (rounded up).

1.13c's server hit 5 also checks unit `+0xc8` bit 10 (`UNITFLAGEX 0x400`, owner info stored). D2R makes the same check
inside its owner lookup `0x490300` (`CheckFlagEx(unit, 0x400)` at `0x34fb00`); the client owner lookup `0x9a7b0` has it
too.

## PD2 does not change them

`ProjectDiablo.dll` reaches vanilla tables through version-indexed offset entries. The entries for the two hit tables are
`0x4a8e2c` (D2Game `0x10da48`, DLL index 6) and `0x4a9934` (D2Client `0xdda10`, DLL index 0). Their getters (`0x31d990`,
`0x31db90`, via the resolver `0x3a5f20`) are called only from static-initializer thunks (for example `0x66e0`, `0xb60f0`)
listed in the `.rdata` initializer tables, and nothing reads the resolved pointers (`0x105eb260`, `0x105eaffc`) after
that. None of its call rewrites target either function. The entry `0x3c7e0` that looks close belongs to D2COMMON (index 1).

DLL index order in ProjectDiablo: 0 D2CLIENT, 1 D2COMMON, 2 D2GFX, 3 D2LANG, 4 D2WIN, 5 D2NET, 6 D2GAME.

## Rows

The merged missiles table already carries PD2's hit functions, pars and sub-missiles for all nine rows (only the row ids
are renumbered). Ring size per hit, from PD2's data:

| Missile | Sub-missile | sHitPar1 | Server ring | cHitPar1 | Client ring |
|---|---|---|---|---|---|
| thunderstorm1 | thunderstormnova | | 64 | | 64 |
| powerstrikenova1 | powerstrikenova2 | | 64 | | 64 |
| powerstrikenova1valk | powerstrikenova2valk | | 64 | | 64 |
| shatteringarrow | shatteringarrowexp | 20 | 4 | 20 | 4 |
| voidbolt | voidbolthitmissile | 4 | 16 | 8 | 8 |
| rathmapoisonorb | rathmapoisonorbhitmissile | 4 | 16 | 4 | 16 |
| leoricsummonnova | leoricsummonorb | 90 | 1 | 90 | 1 |
| ZharNovaOrb | ZharNovaOrbHitMissile | | 64 | | 64 |
| IskatuMagicOrb | IskatuNovaOrbHitMissile | 4 | 16 | 8 | 8 |

D2R's own `thunderstorm1` has no hit functions, so on vanilla D2R data a Thunder Storm bolt makes no nova.

## Game check

`tests/game/nova_orb_checks.py` (`thunder-storm-nova`) casts Thunder Storm next to monsters and counts the
`thunderstormnova` client missiles each bolt leaves, read from game memory by `tests/game/missile_probe.py` (the client
missile hash table at `0x2a25d10`, 128 buckets keyed `guid & 0x7f`, next pointer at unit `+0x158`). The expected count
comes from PD2's `cHitPar1`.

One manual run on Slot6 (2026-11-03) saw one strike make exactly 64 `thunderstormnova` missiles, matching PD2. The probe
sees client missiles only, so it checks chit 4; the server side rests on the code comparison above.
