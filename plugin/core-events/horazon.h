// Horazon's map drops and red portal to a second map (horazon.cpp), split out of event_portals.cpp (#943, #1028).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers the monster-death listener for Horazon's drops; `report` gets one entry. Returns 1 when it is on.
auto InstallEventPortals(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto EventPortalsCounters(char* out, size_t size) noexcept -> void;
