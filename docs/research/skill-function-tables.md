# D2R 93847 skill and missile function tables

Answers "Probe D2R's skill function tables from a plugin" (Q1-Q3 of the core-rules inventory). Read from the runtime
dump `C:\ghidra-work\D2RLoader_6ab3782c_dump.exe` (memory layout, base 0x140000000) and the toolkit's Ghidra project. Not
run in game. Rerun after a patch: new dump, then `python -I skill-function-tables/probe_tables.py`. Re-find the
dispatchers first (the table lists below say where).

## Tables (RVA, size, D2R's empty slots)

| Table | RVA | Size (bound) | Empty slots |
|---|---|---|---|
| srvstfunc | `0x238e710` | 91 (immediate `0x5a`) | 71-90 |
| srvdofunc | `0x238ea00` | 191 (immediate `0xbe`, prg path dword `0x1d1945c` = 191) | 172-190 |
| cltstfunc | `0x235f590` | 70 (dword `0x235f7c0`) | 62-69 |
| cltdofunc | `0x235f7d0` | 130 (dword `0x235f7c4`) | 110-129 |
| missile srvdo | `0x2390e80` | 54 (no bound; srvhit table follows) | 46-53 |
| missile srvhit | `0x2391030` | 71 (dword `0x2391404`) | 61-70 |
| missile srvdmg | `0x2391270` | 31 (dword `0x2391408`); read by srvhit 1 `0x45bc40` | 18-30 |
| missile cltdo | `0x2358430` | 92 (no bound; clthit table follows) | 78-91 |
| missile clthit | `0x2358710` | 81 (dword `0x2358a64`) | 6-8, 11, 15, 17, 20-23, 27, 35, 45, 49, 58, 59, 66-80 |
| property funcs | `0x2386ab0` | 37 (dword `0x2386bd8`) | 26-35 |

Not located: srvstopfunc/cltstopfunc tables, skill aura event and item event function tables (`0x44d570` dispatches
through a further indirection).

## Who reads the do tables

- srvdofunc: skill dispatcher `0x43acb0` (field +0x4e, bound 190; when cast from an item it uses field +0x20a,
  `ItemEffect`, with **no** bound), prg dispatcher `0x56c3e0` (`srvprgfunc1-3` at +0x50, bound 191),
  `0x435490` (call by number, bound 190), `0x435520` (a states-like record field +0x36, bound 190).
- cltdofunc: dispatcher `0x216d20` (field +0x14a, `ItemCltEffect` +0x20c, bound 130), prg `0x216380`/`0x216920`
  (`cltprgfunc1-3` at +0x14c, no bound), `0x215180` (a 0x44-byte record field +0x34, bound 130).
- srvstfunc: `0x43b3e0`, `0x43acb0`, `0x438670`, `0x433910`. cltstfunc: `0x218030` (field +0x148).

## Answers

- **Q1 (overflow).** A ">190" detour has to change 5 code sites with 3 different bound styles, including the
  unbounded ItemEffect path. A **router slot** needs no code patch: one free srvdofunc slot holds a plugin function that
  looks up the skill id in a plugin table and calls that PD2 function, using the spike's existing `PatchWriteU64`
  table write. The router dispatches on the skill id, so each skill gets at most one routed function. A skill that needs two
  overflow functions (do plus prg) takes a real free slot for one of them. All 32 PD2 server do functions fit.
  Not proven in game; that proof is the first plumbing work item.
- **Q2 (unused slots, prg).** prg values index the do tables (server and client). D2R's "unused" slots hold real functions
  reached by prg: srvdo 41 `0x56dd60` and 143 `0x56e240` (Phoenix Strike charges, Fists of Fire), cltdo 15 and 83. PD2's
  WispBossNova (41 + 15) uses exactly D2R's Phoenix Strike pair, so it is data, not code. Royal Fire (143 + 83) is
  not: PD2 rewrote both slots with a boss branch ([w25-assassin-finishers](w25-assassin-finishers.md)).
  srvdo 163 `0x520a00` and cltdo 22 `0x2aed10` are filled, but no D2R data reaches them (code-only callers), so PD2's
  splash (163) and the Power Strike client half (22) still need checking against PD2. srvdo 171 is D2R's Charged Bolt Disk, so PD2's prg 171 collides.
- **Q3 (start tables).** srvstfunc has 91 slots (71-90 free). cltstfunc has 70 (62-69 free, 8). PD2's cltst 54-58
  collide with D2R's 54-58 and go to 62-66.

## What changes in the hook list

- dmg 18, 19 fit as-is (table is 31, 18-30 free); dmg 15, 17 go to 20+.
- chit 11 fits as-is (D2R slot empty). chit 12, 26 and shit 5, 6 are taken (check they match PD2).
- cdo 70 (Split Throw return) and cdo 12, 72 are taken; cdo goes to 78-91. sdo 38, 40 are taken; sdo goes to 46-53.
- srvst 72, 76, 79 fit as-is. srvst 67, 68 are taken (check they match PD2).
- property func 22 is taken; free 26-35.
