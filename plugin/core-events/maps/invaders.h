// The Invaders map event (invaders.cpp, #224, MS10): the portal's think and the invaders' drops.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers the portal's think (monai row 120, class 1199) and the invaders' death drops; `report` gets one entry.
// Returns 1 when it is on.
auto InstallInvaders(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto InvadersCounters(char* out, size_t size) noexcept -> void;
