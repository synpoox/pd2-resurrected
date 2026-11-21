// PD2's Lock Inventory (#1578): the padlock at the bottom right of the inventory panel. Locked, no click, Ctrl/Shift
// click or pad press moves an item onto, off or between the player's or the mercenary's body slots.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace inventory_lock {

// The client-to-server messages (build 93847) that change a body slot. D2R's equipment slot widget (D2Client
// 0x15c600) sends all of them: 0x1a put the cursor item on, 0x1b/0x1d/0x1e/0x21 swap it with the worn item(s), 0x1c take
// the worn item; 0x56-0x5a are the same five with the owning player's id, which is how the mercenary's slots are
// sent. 0x22 (+1 the worn item, -1 from a grid) and 0x60 (+0x15 mode 1 swap, 2 take; 0 and 3 are grid moves) are
// their list forms, also sent by the grids.
constexpr auto ChangesEquipment(const uint8_t* packet, size_t size) noexcept -> bool {
	if (packet == nullptr || size == 0) {
		return false;
	}
	switch (packet[0]) {
	case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x21:
	case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a:
		return true;
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
