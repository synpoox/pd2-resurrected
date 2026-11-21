// Item Quantity (#1596): extra rolls of a dead monster's own TC from its killer's item_quantity stat (quantity_drops.cpp).
#pragma once

#include "death_steps.h"

#include <cstddef>

// Called by key_drops' monster-death listener after the uber steps.
auto QuantityDrops(const DeathContext& death) noexcept -> void;
// Live counters for the status command.
auto QuantityDropsCounters(char* out, size_t size) noexcept -> void;
