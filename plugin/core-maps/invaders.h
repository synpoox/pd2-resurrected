// The Corrupted Heroes map event's invaders (invaders.cpp, MS10 #224).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Adds InvaderPortal to monai row 120's thinks and listens for the invaders' deaths; `report` gets one entry. Returns
// the number of parts on (2 when both are).
auto InstallInvaders(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto InvadersCounters(char* out, size_t size) noexcept -> void;
