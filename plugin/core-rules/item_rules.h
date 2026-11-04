// PD2 item rules: Ormus' Robes' random sorceress skill (properties func 25), item_rules.cpp. Skill on cast and skill
// on block are the Cast Triggers companion's (IR2).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs the item rules; `report` gets one entry per rule. Returns how many were installed.
auto InstallItemRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto ItemRuleCount() noexcept -> int;
// Live counters for the status command.
auto ItemRuleCounters(char* out, size_t size) noexcept -> void;
