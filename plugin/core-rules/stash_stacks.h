// PD2's stacks in the RotW stash tabs (Gems, Materials, Runes; #1537): a deposited stack counts as its quantity
// (stash_stacks.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Swaps the tab count and add thunk slots; `report` gets one entry. True when both are in.
auto InstallStashStacks(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool;
auto StashStackCounters(char* out, size_t size) noexcept -> void;
