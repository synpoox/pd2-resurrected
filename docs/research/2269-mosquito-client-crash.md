# #2269: Mosquito client attack crash

Status: static triage only; the faulting DLL and invalid value's producer are not identified. No fix or game check yet.

## Evidence

[Original last-resort report](https://github.com/user-attachments/files/35496459/d2r-lastresort-crash-report.2026_11_27.04_51_58.UTC.pid-29988.log), PID 29988, generated 2026-11-27 05:04:38 UTC, record 1 of at most 2.

- First-chance access violation, read at `0xffffffffffff006c`, RIP `0x7ffb6d656c36`, allocation base `0x7ffb6d620000` (DLL RVA `0x36c36`). The module name is absent.
- RAX = `0xffffffffffff0000`; the invalid address equals RAX + 0x6c. That is consistent with an invalid base pointer, not a simple null dereference. The actual operand and producer need disassembly.
- Candidate return addresses (raw stack slots, not a verified unwind): `+0x000` DLL +0x36c9f; `+0x010` D2R +0x977a7; `+0x050` D2R +0x223d41; `+0x080` D2R +0x216f92; `+0x120` D2R +0x216d01. Repeated values 0xce and 0x3e are present.
- On the toolkit's 93847 image, the skill getter begins at 0x97790, the client skill dispatcher at 0x216d20, and the Mosquito client do function at 0x223cd0 (+0x71 to the reported 0x223d41). The report itself does not identify the image stamp. See [address registry](https://github.com/synpoox/d2rAI-toolkit/blob/main/knowledge/d2r-93847-addresses.md) and [skill-function registry](https://github.com/synpoox/d2rAI-toolkit/blob/main/shared/classic_import/data/109d-skill-functions.tsv).
- Merged `skills.txt` row 206 (0xce), Mosquito, has srvst 55, srvdo 107, cltst 42, cltdo 62 (0x3e). The repeated stack values and nearby function address make this a strong lead, not a verified call chain.
- `monstats.txt` rows mosquito1 and mosquitomap share BaseId mosquito1, MonStatsEx mosquito1, AI Mosquito, Code MO, Skill1 Mosquito, Sk1mode seq_mosquitoskill, Sk1lvl 1, blank SendSkills. Their differences are identity/class chain, level, AI delay, stats and treasure classes; no mismatch in those skill/animation fields was found. Do not disable the skill or remove the monster as a purported fix.
- `maps.txt` contains mosquitomap in m01, m04, m09, m13, m24; mosquitomap2 in m10, m17, m21; mosquito2Market in m11. A shared skill failure would not be limited to Tunneling Depths.

Charge was the reported player action, not the skill implicated by the strongest stack evidence. The Charge server wrappers and #1993 aura-rate hook are secondary suspects only if the trace or reproduction implicates them.

## Remaining reverse engineering

1. Obtain the same-session loader/plugin logs, any second crash record and a dump with the loaded-module list. Identify the DLL at 0x7ffb6d620000; use its exact build and symbols, if available. Verify the D2R image stamp before using the addresses below.
2. From the matching engine image, use toolkit `tools/ghidra_query.ps1 Disasm.java 0x223d41` and `Decompile.java 0x223cd0 0x216d20 0x97790`. Inspect 0x216f92 and 0x216d01 as well. Resolve the actual call targets from the loaded image, not only the unpatched executable, and validate return addresses with .pdata and preceding calls.
3. Disassemble the identified DLL at RVA 0x36c36 and its candidate caller 0x36c9f. Trace the operand producing the invalid address, including the 0x6c displacement if confirmed, back to its producer and identify the expected object. Determine whether this is the terminating exception (first-chance alone does not prove that).
4. Fix the demonstrated producer/lifetime/table error, or guard the narrow call only when its contract permits skipping an invalid target. Follow SDK/signature/MutationService and image-check rules. Preserve native behavior for a valid target.

## Regression check (not run)

Use a permitted Slot2-5 game slot, Hell, level 99, no mercenary. In a fresh game spawn mosquito1 and mosquitomap separately via `cheat mspawn <id>`. Allow the special attack to start and finish; move away, Charge away during the animation, kill the attacker during its sequence, and cross room boundaries. Compare ordinary movement with Charge, and vanilla with the map monster. Repeat in m01 and another map containing mosquitoes. Test with and without an aura only if needed. Confirm the special attack still behaves normally, no fault recurs, and the loader/plugin logs are clean. If reproduction is intermittent, report the number of attempts and whether the same signature returned. Run core-rules-w16 for Charge if the eventual fix changes its path. Follow the repo merge gate for the final fix head.
