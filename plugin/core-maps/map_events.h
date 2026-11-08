// Map events (map_events.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// PD2's event roll for a map just opened to `level`: sets this game's event. Call before the map item is freed.
auto RollMapEvent(d2r::Game* game, d2r::Unit* item, int32_t level) noexcept -> void;
// This game's map event (PD2's game +0x1df4): 0 no map open, 9 a dungeon, 10 no event, 11-21 an event, 0x1e-0x24 a
// unique map.
auto MapEvent(d2r::Game* game) noexcept -> int32_t;
// Installs the room activation hook that spawns the events; `report` gets one entry. Returns 1 when installed.
auto InstallMapEvents(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto MapEventsCounters(char* out, size_t size) noexcept -> void;
