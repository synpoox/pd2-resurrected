// PD2's Dclone AI for uberdiablonew and his trapped souls (dclone_ai.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Swaps monai row 87's think for the port's, for Dclone only, and row 99's for ubertrappedsoul1-5; `report` gets one
// entry. Returns how many of the two are on.
auto InstallDcloneAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto DcloneAiCounters(char* out, size_t size) noexcept -> void;
