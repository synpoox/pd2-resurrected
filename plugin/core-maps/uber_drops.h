// PD2's uber boss death drops (uber_drops.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Queries the item, inventory and thread services. Returns false when one is missing (no drops then).
auto InstallUberDrops(const D2RL::PluginContext* context) noexcept -> bool;
// key_drops' monster-death listener, server thread: an uber boss's guaranteed drops, rolls and exit portal.
auto UberDeathDrops(d2r::Game* game, d2r::Unit* monster, uint32_t* seed) noexcept -> void;
auto UberDropsCounters(char* out, size_t size) noexcept -> void;
