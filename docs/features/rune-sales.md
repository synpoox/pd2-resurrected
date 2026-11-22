# Runes can't be sold

No vendor takes a rune ([#1777](https://github.com/synpoox/pd2-resurrected/issues/1777)). No vendor stocks runes
either (no NPC column in misc.txt has a value for r01-r33, and gamble.txt lists none).

- **Which items:** every item of the itemtypes `rune` type (all 33 misc rows r01-r33; `tools/tests/test_rune_sales.py`).
- **How:** core-rules hooks D2R's sellable check (`ItemIsSellable`, 93847 `0x374370`, found by signature) and answers
  "no" for a rune. It is the check D2R uses for quest items, and every caller is a sell path: the vendor panel, its
  tooltip and controller prompts on the client, and the NPC sell handlers on the server. The client refuses the sale
  before it sends anything, so the rune stays on the cursor or in the inventory; a host refuses a sell message too.
- **No data way:** neither misc.txt nor itemtypes.txt has a sell column. The `quest` flag blocks sales but also keeps
  an item out of the shared stash, so it is not used.
- **Check in game:** the `core-rules` console command's `item prices:` line ends with `N rune sales refused (rune type T)`.
