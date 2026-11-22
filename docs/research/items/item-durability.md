# PD2's item durability rules

Work item [#1478](https://github.com/synpoox/pd2-resurrected/issues/1478). The third-party Item Durability companion was
dropped by [#1467](https://github.com/synpoox/pd2-resurrected/issues/1467) (PR [#1479](https://github.com/synpoox/pd2-resurrected/issues/1479)); core-rules builds what PD2 itself does. Static
disassembly of the PD2 baseline install (`ProjectDiablo.dll` and its 1.13c `D2Game.dll`, Ghidra project `PD2.gpr`), the D2R
93847 dump for the matching code. D2Game addresses are RVAs, `ProjectDiablo.dll` ones are RVAs too.

## How PD2 changes durability

PD2's patch table (20-byte records `{dll, offset, value or target, type, length}`, see [plrmodes](../../pd2/plrmodes.md))
has these records in 1.13c D2Game's durability code. Nothing else in the table touches durability or its two stats
(72 `durability`, 73 `maxdurability`).

| # | PD2 patch (table record) | What 1.13c did / what PD2 does | D2R 93847 | Status |
|---|---|---|---|---|
| 1 | D2Game `+0xec4f`, 1 byte `0x74` -> `0xeb` (`jz` -> `jmp`) in the ethereal step of item generation (`+0xebb0`) | 1.13c: after ApplyEthereal, an item with durability gets `maxdurability = max / 2 + 1` and `durability = maxdurability`. PD2 jumps over both: an ethereal item keeps its base's full maximum | the same block in the item maker, `jz 0x443558` at `0x44350e` (`max / 2 + 1` at `0x44351f`-`0x443532`) | **built** (#1478): `jz` -> `jmp` by signature |
| 2 | D2Game `+0x12f16` (4 -> 2) and `+0x12f36` (10 -> 5) | a weapon's chance in percent to lose durability on a hit (armor unchanged) | `mov ebx,imm32` at `0x441b83` and `0x441ba6` | built (IR8, [#243](https://github.com/synpoox/pd2-resurrected/issues/243)) |
| 3 | D2Game `+0x12f88`, call rewritten to PD2's `0x103a17b0`, and `+0xeb81`, same target | 1.13c `+0xea70` schedules the replenish event for `item_replenish_durability` (stat 252). PD2's replacement (`0x10345130`) schedules it the same way (`2500 / stat` frames) and also runs the charges step `0x103450b0` | not located here (not `0x441b10`, which is the weapon durability-loss function); the charges part is built | built with Replenish charges ([#148](https://github.com/synpoox/pd2-resurrected/issues/148)); no durability difference |
| 4 | D2Game `+0x13033`, 1 byte NOP (`0x90`, was `dec ebp`) in the durability-loss function `+0x12ee0` | the branch for an item that has a quantity (stat 70): 1.13c took one off it when its durability ran out (the branch runs only when durability would reach 0: `dec ebp; jle +0x12fd8`), and put durability back to maximum. With the `dec` gone, the item keeps its stack when its durability runs out, and durability goes back to maximum: a hit never uses up a throwing weapon's stack | the same branch in `0x441b10`: `lea ebx,[rax-1]` (`8d 58 ff`) at `0x441cb9` takes one off stat 70 (0x46); the stat 72 refill follows | **built** ([#1489](https://github.com/synpoox/pd2-resurrected/issues/1489)): `mov ebx,eax; nop` by signature |

Other findings:

- **ApplyEthereal never touches durability.** `ProjectDiablo.dll` `0x2fb710` (`D2COMMON_10890_ITEMMODS_ApplyEthereal_INTERCEPT`)
  sets the flag and scales armor class and damage by 5/4 ([item-rules research](suite-companions.md)). Durability is halved
  by the caller (row 1), which is why a data-only port could not see it.
- **The companion's defaults** (`ruffneckk-item-durability.toml`, [#1479](https://github.com/synpoox/pd2-resurrected/pull/1479)):
  ethereal maximum 100%, loss resistance off, bows without durability. They match PD2 for row 1 and add nothing else: PD2
  has no loss resistance and no bow durability, so neither is ported.
- **Items already made.** An ethereal item saved while the companion was gone (or before this build) keeps its halved
  maximum; the patch only affects new items (`docs/deviations.md`).

## Port, row 4

Confirmed in the 1.13c `D2Game.dll` on disk: `+0x13033` is `4d dec ebp`, in the branch after `+0x12fd8` (an item whose
durability would reach 0, `dec ebp; jle +0x12fd8` at `+0x12f6f`). It reads stat 70 (`+0x13021`), and when it is above 1
(`cmp ebp,1; jle +0x130b4`) the `dec ebp` is followed by `SetStat(70, ebp)` (`+0x13038`) and
`SetStat(72, GetMaxDurability)` (`+0x13065`-`+0x13073`). At quantity 1 the `jle` goes to `+0x130b4`, which zeroes both
stats and removes the item. So the NOP keeps the stack and refills durability; a stack of 1 is still used up.

core-rules `loot_rules.cpp` (`PatchBySignature`, `ThrowingQuantityPattern`): D2R's `lea ebx,[rax-1]` at `0x441cb9` becomes
`jmp +0x21; nop` (`8d 58 ff` -> `eb 21 90`) to `0x441cdc`, skipping the stat 70 write (`call 0x43eb30` with `r9d` 0x46): the
read includes a socketed gem's own quantity, so writing it back grew a socketed stack by 1 per throw (#1887). The signature
`7E 57 33 F6 8D 58 FF E8 ?? ?? ?? ?? 48 8B D0 90 89 74 24 28 44 8D 4E 46` with offset 4 matches once in 93847
(count 1, at RVA `0x441cb9`).

## Port, row 1

core-rules `loot_rules.cpp` (`PatchEtherealDurability`): the byte signature
`85 C0 74 48 45 33 C0 48 8B CF 41 8D 50 49 E8 ?? ?? ?? ?? 99 45 33 C9 2B C2 48 8B CF D1 F8 41 8D 51 49 44 8D 40 01`
with offset 2 finds the `jz`; the MutationService checks `74 48` and writes `eb 48`. It matches once in 93847 (count 1,
at RVA `0x44350c`, the `jz` at `0x44350e`).

Game check (Slot 5, Hell, level 99 Sorceress, town): with `cheat ethereal 1`, two ethereal Plate Mails and one Cap showed
maximum durability 60, 60 and 12 (items.txt: 60 and 12; D2R's rule alone gives 31 and 7). Read from the items' stats in a
local diagnostic, not a tooltip.
