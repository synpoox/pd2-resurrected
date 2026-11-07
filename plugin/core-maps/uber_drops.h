// PD2's uber boss death drops (uber_drops.cpp).
#pragma once

#include "d2r.h"

#include <cstddef>
#include <cstdint>

// key_drops' monster-death listener, server thread: an uber boss's guaranteed drops and rolls.
auto UberDeathDrops(d2r::Game* game, d2r::Unit* monster, uint32_t* seed) noexcept -> void;
// Drops unique row `uniqueRow` (runtime row) of item `code` by the monster at `itemLevel`; true when it came out unique.
auto DropUniqueAt(d2r::Game* game, d2r::Unit* monster, uint32_t code, int32_t uniqueRow, int32_t itemLevel) noexcept -> bool;
auto UberDropsCounters(char* out, size_t size) noexcept -> void;
