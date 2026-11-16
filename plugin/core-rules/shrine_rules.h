// PD2's shrine rules (shrine_rules.cpp, #1143): shared booster shrines, +10 max resist, gem shrine stacks.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Returns 1 when every rule is live; `report` gets one entry.
auto InstallShrines(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto ShrineCounters(char* out, size_t size) noexcept -> void;
