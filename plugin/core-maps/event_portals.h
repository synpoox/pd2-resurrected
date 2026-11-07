// PD2's map events that open areas (event_portals.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers the monster-death listener for Horazon's portal; `report` gets one entry. Returns 1 when it is on.
auto InstallEventPortals(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto EventPortalsCounters(char* out, size_t size) noexcept -> void;
// D2R's red portal pair (rva::PortalMaker, object 60) at a free spot by (x, y) in `room` to `level`, with its linked
// return portal in the level. Null when none is made.
auto OpenEventPortal(d2r::Game* game, void* room, int32_t x, int32_t y, int32_t level) noexcept -> d2r::Unit*;
