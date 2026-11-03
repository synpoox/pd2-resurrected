// PD2 server skill do functions in free srvdofunc slots (skill_functions.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Writes every function into its slot; `report` gets one entry per slot. Returns how many were installed.
auto InstallSkillFunctions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto SkillFunctionCount() noexcept -> int;
// Live counters for the status command.
auto SkillFunctionCounters(char* out, size_t size) noexcept -> void;
