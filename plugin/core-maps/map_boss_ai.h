// PD2's AIs for leoricMapBoss, siegebeastMapBoss and SharpToothBoss (map_boss_ai.cpp, #624).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Swaps monai rows 114 and 115's thinks and adds Sharptooth to row 120's; `report` gets one entry. Returns the number
// of bosses on (3 when all are).
auto InstallMapBossAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto MapBossAiCounters(char* out, size_t size) noexcept -> void;
