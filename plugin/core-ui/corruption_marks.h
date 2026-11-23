// Corruption markers (#2070): on a corrupted item's tooltip, the property lines its corruption added or changed are red.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

namespace corruption_marks {

// Reads cubemain's corruption outcomes' properties (properties.txt, itemstatcost.txt) and hooks D2R's property lines
// (0x2dc4b0). Returns the report for the log.
void Install(const D2RL::PluginContext* context, char* report, size_t size) noexcept;

// The hook's counters, for the status command.
void Counters(char* out, size_t size) noexcept;

}  // namespace corruption_marks
