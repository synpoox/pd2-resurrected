// PD2's Show Item Quantity (#359): every stack in an open item grid (inventory, cube, stash) shows its quantity on its
// picture. PD2 draws it in its D2GL renderer; here the PluginSDK overlay draws it, in HD and in legacy graphics.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace item_quantity {

// Where the number of a stack goes: its right edge and baseline, the bottom right of the item's picture, `pad` in
// from its corner. `left`/`top` is the grid's top-left in screen px, `cell` one cell's size, `x`/`y`/`w`/`h` the
// item's cell and size in cells.
struct Spot {
	float right;
	float bottom;
};
constexpr auto SpotOf(float left, float top, float cell, int32_t x, int32_t y, int32_t w, int32_t h, float pad) noexcept -> Spot {
	return {left + static_cast<float>(x + w) * cell - pad, top + static_cast<float>(y + h) * cell - pad};
}

// The shared stash page the stash shows, from its label ("Shared 2", "2 / 5"...): the first number in it, less one;
// UINT32_MAX when there is none.
constexpr auto PageOf(const char* label) noexcept -> uint32_t {
	uint32_t page = 0;
	bool digits = false;
	for (const char* c = label; c != nullptr && *c != '\0'; ++c) {
		if (*c >= '0' && *c <= '9') {
			page = page * 10 + static_cast<uint32_t>(*c - '0');
			digits = true;
		} else if (digits) {
			break;
		}
	}
	return digits && page > 0 ? page - 1 : UINT32_MAX;
}

// Starts the draw (overlay) and the grid watch (UI thread). Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The watch's counters, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace item_quantity
