# Skill weapon durability (#2155)

Leap Attack's landing and Blade Shield's periodic hits used D2R's melee durability step. The
`weapon_durability.cpp` rule removes that step at their specific hit call sites. Joust still wears its weapon.

Changing `skills.txt`'s `durability` field would not fix this: D2R's dataguide describes it as the broken-weapon
and ammunition check when an animation ends, rather than the durability loss on a hit. The three blade skills
already have that field blank. The SDK has no combat durability policy.

## Native paths checked

Read-only Ghidra analysis of build 93847 (`C:\ghidra-work\project\D2R.gpr`):

| Skill | Hit path | Result |
| --- | --- | --- |
| Leap Attack | `LeapHitOne` calls native hit `0x56b170`; its call `0x56b4ba` applies the hit with fifth argument 1 | Changed to 0 for skill 143 only. The same native hit serves Joust (450), which keeps 1. |
| Blade Shield | Server do 54 (`0x56f610`) reaches `0x56ea10`, whose per-target callback `0x56e360` calls hit apply at `0x56e438` with fifth argument 1 | Changed to 0 at that callback's call only. |
| Blade Sentinel | `SentinelHit` uses `SplashOne` / `DealSplash`; its native missile hit 37 (`0x4607d0`) only returns 2 | Already avoids the melee durability step; no change. |
| Blade Fury | Server do 48 (`0x56f1e0`) creates its missile; the custom bladefragment1 hit/damage routes process missiles | Already avoids weapon wear; no change. |

Hit apply `0x44b2b0` calls durability step `0x4526c0` only if its fifth argument is nonzero. That step calls
weapon durability loss `0x441b10` for a player attacker. The zero argument skips the durability step while
keeping native hit events, leech, damage and hit reactions. This also skips the target armor-wear portion of
that same step; PvP is outside this port's scope.

The Leap call's `mov byte [rsp+0x20],1` becomes `mov dword [rsp+0x20],r12d`, passing the actual skill id to
the redirected call. Its wrapper converts that id to the original byte flag. A failed argument patch leaves
the stock value 1, which the wrapper preserves as wear enabled. Blade Shield changes only the immediate 1 to 0.
Both patterns matched once in the 93847 dump, and both original calls resolved to `0x44b2b0`.

No in-game check was run, at the owner's request. Static checks do not prove runtime behavior.
