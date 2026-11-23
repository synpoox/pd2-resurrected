// PD2's mercenary AI changes (monai row 61 Hireable, #1803), merc_ai.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

namespace sigscan {
struct Entry;
}

// Installs the think hook, the attack-call redirect and the target-search patch; `report` gets one entry each.
// Returns how many were installed.
auto InstallMercAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto MercAiCount() noexcept -> int;
// Live counters for the status command.
auto MercAiCounters(char* out, size_t size) noexcept -> void;
// The signature entries the part reads.
extern sigscan::Entry* const MercAiUses[9];
