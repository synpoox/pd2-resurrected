// PD2's cube rules: corruption in one press and the Demonic Cube reroll (cube_rules.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs the cube rules; `report` gets one entry per rule. Returns how many were installed.
auto InstallCubeRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto CubeRuleCount() noexcept -> int;
// Live counters for the status command.
auto CubeRuleCounters(char* out, size_t size) noexcept -> void;
