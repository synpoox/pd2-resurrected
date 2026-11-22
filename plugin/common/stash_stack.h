// The RotW stash tabs (Gems, Materials, Runes) with PD2's stacks (#1537), with no engine types so
// tests/stash_stack_check.cpp builds it on the host. A tab slot keeps a count byte (at most 255) that D2R raises by one per
// item put in; a PD2 stack is `quantity` items (stat 70; a single may have none), so it raises the count by that many.
#pragma once

#include <cstdint>

namespace pd2::stashstack {

// 255 is the ceiling of the saved 8-bit count. The limit in force is D2RCore's `set_materials_limit` (default 99), read from
// d2rloader.toml by stash_stacks.cpp: past it D2RCore's own add stops and loses the rest, so the plugin must refuse there (#1767).
inline constexpr int32_t TabMax = 255;
inline constexpr int32_t DefaultLimit = 99;

// How many items a stack of `quantity` is
constexpr auto Units(int32_t quantity) noexcept -> int32_t {
	return quantity < 1 ? 1 : quantity;
}

// Whether a stack of `quantity` fits whole on a slot holding `count`, under `limit` (the toml's set_materials_limit)
constexpr auto Fits(uint8_t count, int32_t quantity, int32_t limit) noexcept -> bool {
	return count + Units(quantity) <= (limit > TabMax ? TabMax : limit);
}

}  // namespace pd2::stashstack
