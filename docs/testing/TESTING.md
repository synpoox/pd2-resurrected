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
