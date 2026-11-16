// General roll helpers: the per-thread CRT roll and PD2's corruption / Desecration roll (corruption_roll.cpp).
#pragma once

#include "pd2_engine.h"

#include <cstdint>

// The CRT's rand state is per thread and starts at seed 1: seeds each thread once, or every session rolls the same.
auto RollRand() noexcept -> int32_t;
// PD2's 0x103649d0 (see corruption_roll.cpp): one corruption outcome for `item`; `desecrate` is PD2's flag 1. `label`
// names the caller in the log. False when the item is corrupted (not desecrating) or no row matches.
auto CorruptItem(d2r::Game* game, d2r::Unit* item, bool desecrate, const char* label) noexcept -> bool;
// PD2's normal Corruption roll (flag 0): the item's corrupt/corruptnum mods as they are.
inline auto CorruptFreshItem(d2r::Game* game, d2r::Unit* item) noexcept -> bool {
	return CorruptItem(game, item, false, "desecration brick");
}
// PD2's Desecration roll (flag 1): a roll of 851-999, its mods as desecrate/desecratenum.
inline auto DesecrateItem(d2r::Game* game, d2r::Unit* item) noexcept -> bool {
	return CorruptItem(game, item, true, "map drops");
}
