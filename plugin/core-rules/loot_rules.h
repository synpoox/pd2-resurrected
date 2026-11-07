// PD2's item generation and loot rules (IR8, #243), loot_rules.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs the item generation rules; `report` gets one entry per rule. Returns how many were installed.
auto InstallLootRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto LootRuleCount() noexcept -> int;
// Live counters for the status command.
auto LootRuleCounters(char* out, size_t size) noexcept -> void;
