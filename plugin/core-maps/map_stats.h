// Map stats on monsters and players (map_stats.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Routes an opened map's stats into this game's monster and player lists and onto the monster region of its
// level. Call before the map item is freed.
auto RouteMapStats(d2r::Game* game, d2r::Unit* item, int32_t level) noexcept -> void;
// Installs the monster spawn, player frame, area level and item drop hooks; `report` gets one entry. Returns how
// many were installed.
auto InstallMapStats(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto MapStatsCount() noexcept -> int;
// Live counters for the status command.
auto MapStatsCounters(char* out, size_t size) noexcept -> void;
