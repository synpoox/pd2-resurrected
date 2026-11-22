// PD2's Lock Inventory (#1578): the padlock at the bottom right of the inventory panel. Locked, no click, Ctrl/Shift
// click or pad press moves an item onto, off or between the player's or the mercenary's body slots.
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace inventory_lock {

// The client-to-server messages (build 93847) that change a body slot. D2R's equipment slot widget (D2Client
// 0x15c600) sends 0x1a put the cursor item on, 0x1b/0x1d/0x1e/0x21 swap it with the worn item(s), 0x1c take the worn
// item, and 0x56-0x5a, the player's forms carrying a player id (0x58 is Shift+click unequip). The mercenary's slots:
// 0x51 swaps the cursor item with a merc body slot (93847 0x4c0e20), 0x5b swaps a merc body slot with a grid position
// (0x4c3dc0). 0x5c drops an item to the ground (0x4bdcc0) from a page (u32 +9 below 5) or a body location (otherwise),
// so only the latter counts. 0x22 (+1 the worn item, -1 from a grid) and 0x60 (+0x15 mode 1 swap, 2 take; 0 and 3 are
// grid moves) are list forms, also sent by the grids.
constexpr auto ChangesEquipment(const uint8_t* packet, size_t size) noexcept -> bool {
	if (packet == nullptr || size == 0) {
		return false;
	}
	switch (packet[0]) {
	case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x21:
	case 0x51: case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a: case 0x5b:
		return true;
	case 0x5c:
		return size >= 0x0d && (packet[9] | packet[10] << 8 | packet[11] << 16 | uint32_t(packet[12]) << 24) >= 5;
	case 0x22:
		return size >= 5 && !(packet[1] == 0xff && packet[2] == 0xff && packet[3] == 0xff && packet[4] == 0xff);
	case 0x60:
		return size > 0x15 && (packet[0x15] == 1 || packet[0x15] == 2);
	default:
		return false;
	}
}

// The charm rows (#1727): the inventory grid's rows 5-8 (zero-based y 4-7 by an item's top row, d2r::CharmFirstRow; the grid is
// 10x8, #358), where charms count. rowOf(id) is the top row of a stored inventory-grid item, -1 when it is not one (a cursor,
// stash or cube item, none). Messages seen in game (the ids are u32 at the offsets below):
// 0x19 take an item off a grid (id +1); 0x5c Ctrl+click drop (id +1); 0x1f swap the cursor item with a grid item (the two ids
// at +1 and +5, in either order: the cursor item is never a stored one, so asking both is safe); 0x18 put the cursor item on
// a grid cell (id +1, x +5, y +9, page +13, 0 the inventory). 0x22 and 0x60, D2R's list forms: 0x22 at +1 and +5,
// 0x60 at +5 and +0x0d (+1 is a player id; Ghidra, handler 0x4aafa0; neither seen in game). A swap names the item it takes out, so a tall cursor item dropped above row 5 onto a charm is a 0x1f.
constexpr auto ReadU32(const uint8_t* p) noexcept -> uint32_t {
	return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24;
}
template <class RowOf>
constexpr auto TouchesCharmRows(const uint8_t* packet, size_t size, RowOf rowOf) noexcept -> bool {
	if (packet == nullptr || size == 0) {
		return false;
	}
	const auto inRows = [&](size_t at) { return size >= at + 4 && rowOf(ReadU32(packet + at)) >= d2r::CharmFirstRow; };
	switch (packet[0]) {
	case 0x19: case 0x5c:
		return inRows(1);
	case 0x1f: case 0x22:
		return inRows(1) || inRows(5);
	case 0x60:  // +1 is a player id, not an item
		return inRows(5) || inRows(0x0d);
	case 0x18:
		return size >= 17 && ReadU32(packet + 13) == 0 && ReadU32(packet + 9) >= uint32_t(d2r::CharmFirstRow) && ReadU32(packet + 9) < 8;
	default:
		return false;
	}
}

// The Maps and Bossing tabs (#1726): while one is open (viewPage, its shared page; 0 none), a drop onto the stash that is not the
// item's own cell is refused. 0x18 puts the cursor item on a free cell (page +13 is 4 for the stash: D2R's handler 0x4bff30
// clamps it to 0-4); put(id, x, y) says whether the item may go there. 0x1f (0x15 bytes, handler 0x4c0230) swaps the cursor item
// with a stored one: ids +1 and +5 (either order), page +9 (4 the stash, clamped the same), the stored item's cell +0x0d and the
// clicked cell +0x11, where the cursor item goes, each packed y << 16 | x (client 0x2c81d0, server 0x4c0230; #1744: read as two
// plain ints, a same-item drop on its own cell was refused); swap(a, b, x, y) says whether the cursor item may stand there. Any other page (inventory, cube) is not filtered. The page number of a shared tab is not in the message,
// so the open tab says which page the stash shows.
// #1749: a swap on a shared page is 0x14, not 0x1f (client 0x2c81d0 sends 0x14 when the grid's owner is not the player; server
// 0x4b4de0): 0x15 bytes, ids +1 (the cursor item) and +5, the page's owner unit id +9, the cells +0x0d and +0x11 as in 0x1f, the
// stash grid implied. Unfiltered, D2R swapped the stored item onto the cursor, where the client never drew it. A put there is
// 0x45, not 0x18 (client 0x15f8b0, server 0x4c5570): 0x0d bytes, the cursor item's id +1, the owner's id +5, the cell +9 packed.
// The client moves nothing before the server answers, but it marks the cursor item pending (0x2c81d0 calls 0x1c6ac0, state
// 0x36) and ignores stack and swap clicks with it until an item packet for it arrives (0x1c6a40). D2R's own failure branches
// send none (telemetry only), so a refusal queues stash_affinity::Resend, which sends the item again (#1766).
template <class Put, class Swap>
constexpr auto MisplacedOnTab(const uint8_t* packet, size_t size, uint32_t viewPage, Put put, Swap swap) noexcept -> bool {
	if (packet == nullptr || size == 0 || viewPage == 0) {
		return false;
	}
	switch (packet[0]) {
	case 0x18:
		return size >= 17 && ReadU32(packet + 13) == 4 &&
		       !put(ReadU32(packet + 1), static_cast<int32_t>(ReadU32(packet + 5)), static_cast<int32_t>(ReadU32(packet + 9)));
	case 0x45:
		return size >= 0x0d &&
		       !put(ReadU32(packet + 1), static_cast<int32_t>(ReadU32(packet + 9) & 0xffff), static_cast<int32_t>(ReadU32(packet + 9) >> 16));
	case 0x14:
	case 0x1f:
		return size >= 0x15 && (packet[0] == 0x14 || ReadU32(packet + 9) == 4) &&
		       !swap(ReadU32(packet + 1), ReadU32(packet + 5), static_cast<int32_t>(ReadU32(packet + 0x11) & 0xffff),
		             static_cast<int32_t>(ReadU32(packet + 0x11) >> 16));
	default:
		return false;
	}
}

// #1756: the shared page a message names. 0x14 (+9), 0x45 (+5) and 0x55 (+5) carry the page owner's unit id; pageOf(owner) is
// its page (0 a page that is neither Maps nor Bossing, UINT32_MAX unknown), else the open tab says (viewPage), as it does for
// 0x18 and 0x1f, which name no owner. So a tab switch between the click and the server's tick cannot split the two checks.
template <class PageOf>
constexpr auto MessagePage(const uint8_t* packet, size_t size, uint32_t viewPage, PageOf pageOf) noexcept -> uint32_t {
	if (packet == nullptr || size == 0) {
		return viewPage;
	}
	const size_t at = packet[0] == 0x14 ? 9 : (packet[0] == 0x45 || packet[0] == 0x55) ? 5 : 0;
	const uint32_t page = at != 0 && size >= at + 4 ? pageOf(ReadU32(packet + at)) : UINT32_MAX;
	return page != UINT32_MAX ? page : viewPage;
}

// #1757: 0x55 (0x1d bytes, client 0x15f8b0 on a Ctrl+click or the pad's quick-move, server 0x4c7690) moves a stored item (id
// +1) with an empty cursor between the player and a shared page's owner (+5): from grid +9 cell +0x0d to grid +0x11 cell
// +0x15 (packed y << 16 | x); +0x19 is 0 into the owner's grid, 1 out of it (the client sends 1 when the source grid is 4).
// The client picks the first free cell (0x3865b0, not the cell tests the red cells hook), so a homed item lands anywhere and
// the affinity watch routes it, as for any quick-move; only an item with no home (homeless(id)) is refused, the one the tidy
// pass would move back to the inventory (#1737's flicker).
template <class Homeless>
constexpr auto QuickMoveOnTab(const uint8_t* packet, size_t size, uint32_t page, Homeless homeless) noexcept -> bool {
	return packet != nullptr && size >= 0x1d && page != 0 && packet[0] == 0x55 && ReadU32(packet + 0x19) == 0 &&
	       ReadU32(packet + 0x11) == 4 && homeless(ReadU32(packet + 1));
}

// #1744: 0x21 (0x15 bytes, handler 0x4c1940, sent by the client's click 0x2c81d0 when the items stack) stacks the cursor
// item (id +1) onto a stored one (id +5); +9 says the whole cursor stack fits, +0x0d is the target's grid (4 the stash,
// clamped below 5) and +0x11 its cell, packed y << 16 | x. D2R looks the target up by cell (0x472590 -> 0x474220) in the
// player's current inventory (0x34a360), which does not hold the Maps or Bossing page's stack: it logs "No item found
// at page: 4" and stacks nothing. So while one of those tabs is open, a stash 0x21 is the plugin's (StackOnto).
constexpr auto TabStack(const uint8_t* packet, size_t size, uint32_t viewPage) noexcept -> bool {
	return packet != nullptr && size >= 0x15 && viewPage != 0 && packet[0] == 0x21 && ReadU32(packet + 0x0d) == 4;
}

// #1772: a take off a shared page is 0x46 (0x0d bytes: the item +1, the page's owner unit id +5; client 0x2c7080 via
// 0xec780, server 0x4c6480); 0x19 is the other grids' take. The client marks the item pending (0x1c6ac0). A Maps or Bossing
// stack past aff::WithdrawMax (up to StashMax, #1763) came onto the cursor whole and was not drawn. pageOf(owner) is the
// owner's page as for MessagePage (0 neither Maps nor Bossing, UINT32_MAX not found); big(id) says the item is such a stash
// stack. That take is the plugin's (stash_affinity::TakeOut), whose moves send the item again.
template <class PageOf, class Big>
constexpr auto BigTake(const uint8_t* packet, size_t size, PageOf pageOf, Big big) noexcept -> bool {
	if (packet == nullptr || size < 0x0d || packet[0] != 0x46) {
		return false;
	}
	const uint32_t page = pageOf(ReadU32(packet + 5));
	return page != 0 && page != UINT32_MAX && big(ReadU32(packet + 1));
}

// True while the padlock is on (hotkeys.cpp refuses its own moves from the charm rows then).
auto IsLocked() noexcept -> bool;

// Hooks the server's message dispatch and listens for the padlock. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The lock state and counters, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace inventory_lock
