// The Treasure Fallen's loot as PD2 gives it (#1153, docs/research/map-events-audit.md section 5): its treasure class
// drops once per 10% of life lost while it is hit, and 9 - n more times on death.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Swaps the Fallen think (monai row 6) for class 962 and registers the death listener; `report` gets one entry.
// Returns 1 when it is on.
auto InstallTreasureFallen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto TreasureFallenCounters(char* out, size_t size) noexcept -> void;
