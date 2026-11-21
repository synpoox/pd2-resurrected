// PD2's hireling rules: Tyrael hires the Ascendant (UR3, #201), merc_rules.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs the hireling rules; `report` gets one entry per rule. Returns how many were installed.
auto InstallMercRules(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Expanded merc inventory (#1627): patches D2R's hireling equip checks, or closes the game before any character loads,
// since without the patch D2R deletes the merc's rings, amulet, gloves, belt and boots. Not a part (see plugin.cpp).
auto InstallMercGear(const D2RL::PluginContext* context) noexcept -> void;
auto MercRuleCount() noexcept -> int;
// Live counters for the status command.
auto MercRuleCounters(char* out, size_t size) noexcept -> void;
