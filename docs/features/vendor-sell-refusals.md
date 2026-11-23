# Vendors refuse what PD2's vendors refuse

A vendor takes only what PD2's vendors take ([#2012](https://github.com/synpoox/pd2-resurrected/issues/2012), runes
first in [#1777](https://github.com/synpoox/pd2-resurrected/issues/1777)). D2R refuses only an item with flag `0x1000` and
quest items. No vendor stocks runes either (no NPC column in misc.txt has a value for r01-r33, and gamble.txt lists none).

## The list

PD2's sell check is `ProjectDiablo.dll` `0x1037f760` (PD2.gpr `0x1037f6f0`), called from the sell handler (1.13c D2Game
`+0xbf990`) in place of D2Common #10705, after #10705's own test (flag `0x1000`, quest item). It reads the item's own
items.txt type (D2Common #11088, an exact compare, not a parent test), its code, its stackable column and, for three
rows, its quality. It refuses:

1. an item of type `gem`, `rune` or `rung`. (PD2's gems carry a child type such as `gema`, so the stackable rule below is
   what refuses them in practice.)
2. a **stackable** item, unless it is a book (`tbk`, `ibk`), a quiver (`aqv`, `cqv`, `aqv2`, `cqv2`, `aqv3`, `cqv3`), a key
   (`key`), a rejuvenation potion (`rps`, `rpl`, `bps`, `bpl`) or of a throwing type (`tpot`, `tkni`, `taxe`, `jave`,
   `thro`, `misl`, `ajav`).
3. a **non-stackable** item with the code `ubtm`, `dcma`, `rtma`, `uba` (the Uber maps), `llmr` (Lilith's Mirror), `lsvl`
   (Vial of Lightsong), `rkey`, `rid` or `rtp` (the unlimited key and books).
4. a **unique** item with the code `cm1` or `cm2`, or of type `t5m` (tier 5 map).

The test is `tools/tests/test_vendor_sell_refusals.py`: the code lists in the plugin are PD2's, PD2's type ids (20, 74, 148,
211, 38, 42, 43, 44, 48, 56, 87 in its raw itemtypes) are those type codes, and every code is a row of our tables. PD2's ids
differ from ours, so the plugin finds every type by its itemtypes code at run time and every item by its code.

## How

- **Hook:** core-rules hooks D2R's sellable check (`ItemIsSellable`, 93847 `0x374370`, found by signature). When D2R says
  yes, the list above is applied and the answer becomes "no" for a refused item. It is the check D2R uses for quest items, so
  the item behaves as a quest item does: it stays on the cursor or in the inventory. Items are read through the toolkit's
  `ItemRecord`, `ItemCode`, `ItemQuality` and `UnitDataFlag`; the stackable column is the byte at `+0x142` of D2R's items
  record (what D2R's own stackable test `0x374660` reads).
- **No data way:** neither misc.txt nor itemtypes.txt has a sell column. The `quest` flag blocks sales but also keeps
  an item out of the shared stash, so it is not used.
- **Pure rule:** `pd2::itemprice::SellRefused` in `plugin/common/item_price.h`, checked on the host by
  `plugin/tests/item_price_check.cpp`.

**Joined TCP/IP client.** The sell check runs on both sides. `ItemIsSellable` is called by the server's NPC sell handlers
(`SUnitNpc.cpp`) and by the client's vendor panel, its tooltip and its controller prompts, so the one hook in each process
refuses the sale on a client before any message is sent, and a host refuses a sell message that arrives anyway. The
client's copy of an unidentified item may read quality 1 (seen for an unidentified unique in #1924), so an unidentified
unique `cm1`, `cm2` or `t5m` item can still look sellable on a client; the host refuses it. Not checked in game.

## Check

The `core-rules` console command's `item prices:` line ends with `N vendor sales refused`, and the plugin log's install
report says `vendor sell refusals in`. No game check was run: the sell path is the one #1777 proved in game.
