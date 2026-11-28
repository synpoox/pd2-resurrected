# Unlimited book regression checks

| Change | Setup and action | Expected |
|---|---|---|
| Unlimited book skills (#2124) | In Hell on a level 99 character, carry `rtp` and `rid`, open the right-skill picker, hover each entry, select and use it; save and reload, then repeat. Check an item saved before the fix too. | Create Townportal and Identify All show Unlimited Quantity; a portal opens and the Almanac identifies inventory items, both remain reusable after reload. |

D2R's item-skill resolvers (93847 server `0x475840`, client `0x36edd0`) reject the books'
`ubru` type before consulting `books.txt`. Two code-specific hooks return existing skills
583 and 582, delegating all other items to D2R. Native inventory add/remove/load and the
skill-use packet then handle the books normally. No SDK service supplies this behavior.

Keep both items' types unchanged: a secondary scroll/book type changes item serialization
and prevents pre-fix characters containing the books from appearing in character selection.

Verified in Slot2 on 2026-11-24: a level 99 Assassin in Hell loaded pre-fix saved books,
showed both Unlimited Quantity tooltips, opened portals repeatedly, identified an
unidentified magic cap through Identify All, and saved/reloaded with working books
and the Navigator hotkey. Both resolver install log lines reported on.

# Consecutive vendor orb purchases (#2248)

| Change | Setup and action | Expected |
|---|---|---|
| Client and server agree on buy-stack destination | Level 99, Hell, no mercenary. Trade with Anya with gold and inventory room; buy the same map orb three times. | The first buy creates a stack; the next two increase its quantity to 3, each charging the orb price, with no trade error. |

The server's scroll/tome buy path already handles the orb types, but the client's
`npcmenu` prediction also needs the type test and stack finder redirects. Otherwise
the client requests a free cell while the server expects destination 4/page 0xff
for a held stack and rejects the second purchase before charging gold.

# Treasure Fallen movement (#2218)

| Change | Setup and action | Expected |
|---|---|---|
| Flee, wander and stationary Blink | Slot2, level 99, Hell, no mercenary. Force `core-events event treasure` before opening a map. Follow a Fallen in open ground and near an obstruction; use `core-events life 85 unit <id>` for gradual drops and `core-events kill unit <id>` for death. | Flees using the AI target, wanders between attempts, and uses Blink after repeated stationary observations within distance 10; ten loot rolls in total. |

`treasure_fallen_check` covers the stationary threshold, distance boundary, reset on movement,
PD2's x*y comparison and Blink's per-axis 2-4-cell direction.
The owner ran the game check above and it passed.
