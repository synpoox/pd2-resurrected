// PD2's max-level aura on a level 99 player (max_level_aura.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers the gameplay listeners; `report` gets one entry. Returns 1 when it is on.
auto InstallMaxLevelAura(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
