# Regression checks

| Change | Setup and action | Expected |
|---|---|---|
| Unlimited book skills (#2124) | In Hell on a level 99 character, carry `rtp` and `rid`, open the right-skill picker, hover each entry, select and use it; save and reload, then repeat. Check an item saved before the fix too. | Create Townportal and Identify Items show Unlimited Quantity; a portal opens and the Almanac identifies inventory items, both remain reusable after reload. |

The native item skill lookup (93847, Ghidra `0x475840`) accepts only scroll/book types.
The grant path (`0x475e00`) rejects a book with zero quantity but grants a scroll one level.
The unlimited books keep their primary `ubru` type and nonstacking storage, gain secondary
`scro`, and link to the existing unlimited skills through `ScrollSkill` in `books.txt`.
