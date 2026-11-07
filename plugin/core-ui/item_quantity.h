// PD2's Show Item Quantity (#359): every stack drawn in an item grid (inventory, cube, stash) shows its quantity at
// the bottom right of its picture, drawn by D2R's own text renderer right after the picture.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace item_quantity {

// The rect the number is aligned in (right, bottom): the item's picture rect {x, y, width, height} in screen px, `pad`
// px in from its right and bottom edges.
struct Rect {
	int32_t x, y, width, height;
};
constexpr auto TextRect(Rect picture, int32_t pad) noexcept -> Rect {
	return {picture.x, picture.y, picture.width - pad, picture.height - pad};
}

// Hooks the grid item draw. Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The hook's counters, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace item_quantity
