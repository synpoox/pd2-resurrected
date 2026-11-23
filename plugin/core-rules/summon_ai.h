// PD2's changes to the ShadowMaster summon AI (Valkyrie, Decoy, Shadow Master; #1790), the vine AIs (#1802) and the
// Shadow Warrior AI (#1800), summon_ai.cpp.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs the leash hook, the five branch patches, the two vine patches and the Shadow Warrior init and think hooks;
// `report` gets one entry each (four). Returns how many were installed.
auto InstallSummonAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto SummonAiCount() noexcept -> int;
// Live counters for the status command.
auto SummonAiCounters(char* out, size_t size) noexcept -> void;
