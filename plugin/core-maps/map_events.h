// Map events (map_events.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// PD2's event roll for a map just opened to `level`: sets this game's event. Call before the map item is freed.
auto RollMapEvent(d2r::Game* game, d2r::Unit* item, int32_t level) noexcept -> void;
// Installs the room activation hook that spawns the events; `report` gets one entry. Returns 1 when installed.
auto InstallMapEvents(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto MapEventsCounters(char* out, size_t size) noexcept -> void;
