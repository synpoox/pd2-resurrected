// PD2's stack-aware cube matching: a stack counts as its quantity toward a recipe (cube_stacks.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// Installs the matcher hook and the numinputs patch; `report` gets one entry. Returns 1 when both are in.
auto InstallCubeStacks(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Before D2R's executor runs `record`: queues the units the row does not use to come back to the cube once the
// executor has taken the stacks whole. False: run nothing (a leftover could not be kept).
auto KeepCubeLeftovers(d2r::Game* game, d2r::Unit* player, const uint8_t* record) noexcept -> bool;
auto CubeStackCounters(char* out, size_t size) noexcept -> void;
