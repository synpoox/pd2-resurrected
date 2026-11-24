# Unlimited book regression checks

| Change | Setup and action | Expected |
|---|---|---|
| Unlimited book skills (#2124) | In Hell on a level 99 character, carry `rtp` and `rid`, open the right-skill picker, hover each entry, select and use it; save and reload, then repeat. Check an item saved before the fix too. | Create Townportal and Identify Items show Unlimited Quantity; a portal opens and the Almanac identifies inventory items, both remain reusable after reload. |

D2R's item-skill resolvers (93847 server `0x475840`, client `0x36edd0`) reject the books'
`ubru` type before consulting `books.txt`. Two code-specific hooks return existing skills
583 and 582, delegating all other items to D2R. Native inventory add/remove/load and the
skill-use packet then handle the books normally. No SDK service supplies this behavior.

Keep both items' types unchanged: a secondary scroll/book type changes item serialization
and prevents pre-fix characters containing the books from appearing in character selection.
