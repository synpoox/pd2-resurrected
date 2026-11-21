// PD2's Lock Inventory (#1578): the padlock at the bottom right of the inventory panel. Locked, no click, Ctrl/Shift
// click or pad press moves an item onto, off or between the player's or the mercenary's body slots.
#pragma once

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

// Hooks the server's message dispatch and listens for the padlock. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The lock state and counters, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace inventory_lock
