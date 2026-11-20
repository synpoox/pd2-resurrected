# [#1539](https://github.com/synpoox/pd2-resurrected/issues/1539): vendor and gamble prices, D2R vs PD2

Why skill clubs and throwables price differently from PD2. Findings only: no PD2 or Blizzard file content is in this
repo.

## Method

Ghidra 12.2 headless, read-only. D2R: the 93847 dump (`D2RLoader_6ab3782c_dump.exe`, RVAs below; 95032 is not
dumped, the function bodies are assumed unchanged) and `D2RCore.dll` from the slot (D2RLoader's code, image base
`0xc0de5000000`, RVAs below). Classic: 1.13c `D2Common.dll` (base `0x6fd50000`). PD2: the installed
`ProjectDiablo.dll` (TimeDateStamp `0x6aeb779e`, disk VAs). The PD2.gpr copy of `ProjectDiablo.dll` is another build:
at `0x103a3ce0` it holds a different stub, so these VAs were read from a fresh import of the installed file. The tables
were already compared (#1539 comment): weapons cost and gamble cost, itemstatcost Add/Multiply, skills `cost mult` /
`cost add`, affixes, npc.txt and gamble.txt are the same in PD2 and the mod.

## Where the price is computed

| | Function |
|---|---|
| D2R | `0x36f0c0` (`Items.cpp`): `(player, item, difficulty, quest flags, vendor, type)`, type 0 buy, 1 sell, 2 gamble, 3 repair. Gamble cost `0x376b10(item, player level)`. Item skill cost `0x377b10` and bonus stat cost `0x37c390` are D2RLoader thunks (`jmp [rip+x]`); the replacements are `D2RCore.dll` `0x3d2ba0` and `0x833240` (`AddWideItemPropertyPrices`, body `0x3d30a0`). The dead D2R bodies behind the thunks (`0x377b1a`, `0x37c39a`) compute the same as the replacements. |
| 1.13c | D2Common #10107 `0x6fd79d60` calls `0x6fd78e80` (EAX = player). Item skill `0x6fd75970`, bonus stats `0x6fd78850`, stat bonus #10379 `0x6fd89380` (stat minus base stat). Same as D2MOO's 1.10 `ITEMS_CalculateTransactionCost`. |
| PD2 | Its patch table entry `{dll 2 (D2Common), 0x29d7e, 0x103a3ce0, type 1, 0}` redirects #10107's call to a stub that calls `0x10385090`, which calls PD2's own copy `0x102e7ca0` (one special case: vendor 147 gambling under a PD2 mode flag). Helpers: gamble `0x102e6e40`, item skill `0x102e77c0`, bonus stats `0x102e7010` (stat value from #10379, as classic), affix add `0x102e7b50`, npc multiplier `0x102e7c10`. |

## The formula (all three)

Vendor-sells (S), vendor-buys (B) and repair (R) start at items.txt `cost` (armor: scaled by armor class / max AC;
quivers: cost × quantity / 1024; other stackables keep `cost` and divide the stat terms below by the max stack).

- **Staff mods (`item_singleskill`, stat 107).** Item skill term per skill: (2 × level − 1) × (skills `cost add` +
  `cost mult` × X / 1024), X = S or R; for B the divisor is 4096. Applied before the affixes for inferior, normal and
  superior items, after them for magic to tempered.
- **Stats on magic, rare, crafted, superior, tempered items.** Per stat: itemstatcost Add + value × X × Multiply /
  1024 (B also /1024). Encode 1-3 stats (107 included) use skills `cost add` / `cost mult` instead, with B / 4096. So a
  staff mod on a magic or rare item is counted twice: here once with level, then in the item skill term.
- **Affixes, uniques, sets.** Add + X × Multiply / 1024; inferior halves.
- Sockets add filler cost / 2, ethereal quarters B. Then npc.txt `sell mult` (S), `buy mult` (B), `rep mult` (R) / 1024,
  quest multipliers.
- **Quantity.** Unless the item is a book or a quiver: S × quantity; for a repairable stackable B = B × max stack − R ×
  (max stack − quantity), else B × quantity. B is capped at npc.txt `max buy` for the difficulty. Reduced prices (stat 87,
  at most 99%) come off S, R and the gamble price.
- **Gamble.** Ring and amulet: `gamble cost`. Otherwise, with L the player level (at least 5):
  uber weight u = max(50 (L − uber level) + 1, 0), ultra weight x = max(25 (L − ultra level) + 1, 0),
  base = u × uber cost + x × ultra cost + m × cost × (10000 − u − x),
  price = ((2L + 1) / 3 + 20) × (base / 10000 + 250 (L + max(level − 45, 0) − level / 2) / 3) / 15.
  m is the stack midpoint (min stack + max stack) / 2, at least 1.

## Where PD2 differs

1. **Weapons ignore quantity** (in `0x102e7ca0`'s quantity block `ITEMS_CheckItemTypeId(item, 45)` joins the book and quiver checks).
   Throwing weapons price as one item whatever the stack size, buying and selling.
2. **Gamble has no stack midpoint** (m = 1 for every item, `0x102e6f52`-`0x102e6f7e`).
3. Superior items skip the stat term (only tempered of the two calls it, `0x102e81e1`).
4. Repair costs are capped at 250000.

Staff mods, affixes, npc multipliers, max buy and the club / dagger gamble price are computed the same. D2RLoader's two
replacements do not change the numbers.

## Prices (Charsi, Hell, no quest discount, no reduced prices; computed from the formula above)

| Item | D2R buy / sell / gamble (L20, L90) | PD2 buy / sell / gamble (L20, L90) |
|---|---|---|
| Rare club, +3 Firestorm, no other affix | 34178 / 15821 / 3781, 41626 | same |
| Normal club, no skill | 49 / 26 / 3781, 41626 | same |
| Normal dagger | 56 / 30 / 3614, 45813 | same |
| Throwing knives, stack of 100 | 33000 / 17600 / 51495, 130330 | 330 / 176 / 4257, 57061 |

So the club and dagger case is PD2's own pricing: a staff mod adds about (2 × level) × 5000 before multipliers, and an
item gambled cheaply at a low level sells up to `max buy` (5000 / 30000 / 35000). PD2 has the same gap. Throwables are
the engine difference: PD2's costs (tkf 352, tax 606, bkf 918, jav 300) were set for a price that ignores the stack.

## Matching PD2

No table change matches: quantity multiplies after the npc multipliers and the sell side subtracts a repair term, so
any cost works for one quantity only, and the gamble midpoint differs per item. A plugin hook does:

- In `0x36f0c0`, skip the quantity block for items of type 45 (weap), as PD2 does: the guard there is
  `ItemIsType(item, 0x12)` (book); add `|| ItemIsType(item, 45)` (a detour at the guard, the book check must stay).
- In the gamble function `0x376b10`, force the stack midpoint to 1 (the `(min + max) / 2` before `if (< 1) = 1`).
- Optional, for exact parity: the superior skip and the 250000 repair cap.
