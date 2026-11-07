// The unique maps' boss deaths: portals and drops (unique_maps.cpp).
#pragma once

#include "d2r.h"

#include <cstddef>

// Called on every monster death (key_drops.cpp's listener); acts only on a unique map boss in its own map.
auto UniqueMapDeath(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// Live counters for the status command.
auto UniqueMapsCounters(char* out, size_t size) noexcept -> void;
