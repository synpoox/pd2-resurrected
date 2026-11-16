// The map lane's portal operate and boss-room keep (map_open.cpp).
#pragma once

#include "open_gate.h"
#include "uber_entry.h"
#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs the portal operate and the boss-room keep; `report` gets one entry. Returns how many rules were installed.
auto InstallMapOpen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto MapOpenCount() noexcept -> int;
// Live counters for the status command.
auto MapOpenCounters(char* out, size_t size) noexcept -> void;
