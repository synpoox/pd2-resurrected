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
// Live counters for the status command.
auto LucionCounters(char* out, size_t size) noexcept -> void;
// PD2 0x10356e00, as D2R's pet kill 0x500fb0 sets the death mode: kills the monster with no drop (mode 0)
auto KillQuietly(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
