// PD2's vendor buy merge (#1916): a bought orb joins a held stack of its kind, as a scroll joins a tome (buy_stacks.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Redirects the buy's scroll test and tome finder; `report` gets one entry. True when both are in.
auto InstallBuyStacks(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
auto BuyStackCounters(char* out, size_t size) noexcept -> void;
