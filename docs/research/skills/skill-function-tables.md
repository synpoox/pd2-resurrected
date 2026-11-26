# D2R 93847 skill and missile function tables

Answers "Probe D2R's skill function tables from a plugin" (Q1-Q3 of the core-rules inventory). Read from the runtime
dump `C:\ghidra-work\D2RLoader_6ab3782c_dump.exe` (memory layout, base 0x140000000) and the toolkit's Ghidra project. Not
run in game. Rerun after a patch: new dump, then `python -I <toolkit>/tools/probe_tables.py --dump <dump.exe>`. Re-find the
dispatchers first (the table lists below say where).

## Tables and readers

The table RVAs, sizes and empty slots, and who reads the do and start tables (including the unbounded ItemEffect path),
are in the toolkit: `knowledge/d2r-93847-addresses.md` (*D2R server skill functions*) and `knowledge/engine-limits.md`.
Image `0x6ab3782c`, D2R 3.4.0 build 95032. The answers below are the PD2 port's use of them.

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
