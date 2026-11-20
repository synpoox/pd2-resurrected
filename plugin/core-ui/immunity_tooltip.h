// The map tooltip's "Immunities: C130, F125" line (#1552). An SDK item tooltip listener adds it at the top of the
// attributes, below the Tier line and above Area Level, from the map's monster-resist stats (Cartographer's).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

namespace immunity_tooltip {

void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;
void Counters(char* out, size_t size) noexcept;

}  // namespace immunity_tooltip
