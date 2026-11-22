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

// True while the padlock is on (hotkeys.cpp refuses its own moves from the charm rows then).
auto IsLocked() noexcept -> bool;

// Hooks the server's message dispatch and listens for the padlock. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The lock state and counters, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace inventory_lock
