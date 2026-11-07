// Lucion's death (lucion.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

// The game counter key of the Lucion fight's stage (PD2 game +0x262a): 8 once Lucion is dead, which the Temple's
// Desecration (UB7) needs.
inline constexpr uint32_t LucionStageKey = 0x55425253;  // 'UBRS'
inline constexpr int32_t LucionDeadStage = 8;

// Registers the monster-death listener; `report` gets one entry. Returns 1 when it is on.
auto InstallLucion(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// UB7a: a transmute in the Temple with one corrupted amulet after Lucion's death is a Desecration attempt. Returns true
// when it ran (the caller then blocks the recipe).
auto Desecrate(d2r::Game* game, d2r::Unit* player) noexcept -> bool;
// Live counters for the status command.
auto LucionCounters(char* out, size_t size) noexcept -> void;
