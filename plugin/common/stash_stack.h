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

// How many of a stack of `quantity` a slot holding `count` takes under `limit` (the toml's set_materials_limit), and what is left (#1816)
struct TopUp {
	int32_t added;
	int32_t left;
};
constexpr auto TopUpOf(uint8_t count, int32_t quantity, int32_t limit) noexcept -> TopUp {
	const int32_t cap = limit > TabMax ? TabMax : limit;
	const int32_t room = cap > count ? cap - count : 0;
	const int32_t units = Units(quantity);
	const int32_t added = units < room ? units : room;
	return {added, units - added};
}

// Whether a deposit is taken at all: the local player's tops up (anything that fits), another player's only when the whole stack fits,
// since the rest of a top-up is made again for the local player only (#1816)
constexpr auto Takes(uint8_t count, int32_t quantity, int32_t limit, bool local) noexcept -> bool {
	const TopUp t = TopUpOf(count, quantity, limit);
	return t.added > 0 && (local || t.left == 0);
}

}  // namespace pd2::stashstack
