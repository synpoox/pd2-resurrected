// The Rathma fight's drops on the pair's last death (rathma.cpp).
#pragma once

#include "d2r.h"

#include <cstddef>

// Called on every monster death (key_drops.cpp's listener); acts only on the second clone's death.
auto RathmaDeath(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// Live counters for the status command.
auto RathmaCounters(char* out, size_t size) noexcept -> void;
