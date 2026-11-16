// PD2's map cube op: cubemain op 30 blocks the transmute (cube_ops.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs the op 30 hook; `report` gets one entry. Returns how many rules were installed.
auto InstallCubeOps(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto CubeOpCount() noexcept -> int;
// Live counters for the status command.
auto CubeOpCounters(char* out, size_t size) noexcept -> void;
