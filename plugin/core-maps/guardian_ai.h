// PD2's AI for the Altar of the Catalyst's guardian and its clones (guardian_ai.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Swaps monai row 71's think for the port's for GuardianOfFate and its clone; `report` gets one entry. Returns 1 when
// it is on.
auto InstallGuardianAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto GuardianAiCounters(char* out, size_t size) noexcept -> void;
