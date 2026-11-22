// Aura-enchanted bosses (unique monster mod 30) pick their aura as PD2 does (#1830), boss_aura.cpp.
#pragma once

#include "parts.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Swaps the umod 30 thunk slot and redirects D2R's handler's add-skill and aura-start calls; `report` gets one entry.
void InstallBossAura(const D2RL::PluginContext* context, char* report, size_t size) noexcept;
// The status line: how many bosses got PD2's aura.
void BossAuraState(const parts::Part& part, char* out, size_t size) noexcept;
// The signature entries the part reads.
extern sigscan::Entry* const BossAuraUses[3];
