// PD2's map events that open areas (event_portals.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers the monster-death listener for Horazon's portal; `report` gets one entry. Returns 1 when it is on.
auto InstallEventPortals(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto EventPortalsCounters(char* out, size_t size) noexcept -> void;
