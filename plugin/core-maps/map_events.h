// Map events (map_events.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// This game's map event (PD2's game +0x1df4): 0 no map open, 10 no event, 11-21 an event.
auto MapEvent(d2r::Game* game) noexcept -> int32_t;
// PD2 0x10378080: the Altar of the Catalyst's guardian died; the altar's next use gives the shards, and the clones in
// the rooms around `guardian` die.
auto AltarGuardianDied(d2r::Game* game, d2r::Unit* guardian) noexcept -> void;
// PD2's spot search 0x10389c20 from xy in `room`: a size x size box free of `mask`, cells up to `reach` (at most 10)
// from xy; `shift` moves the spot found by -2, -2. True with `room` and `xy` set to it.
auto FindEventSpot(void*& room, int32_t xy[2], int32_t size, uint16_t mask, int32_t reach, bool shift) noexcept -> bool;
// Installs the room activation hook that spawns the events; `report` gets one entry. Returns 1 when installed.
auto InstallMapEvents(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto MapEventsCounters(char* out, size_t size) noexcept -> void;
// Test aid (#998): the next map event rolled is `name` (wanderer, mendeln, treasure, gheed; off clears it). False for an unknown name.
auto ForceNextMapEvent(const char* name) noexcept -> bool;
