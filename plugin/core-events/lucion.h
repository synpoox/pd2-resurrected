// Lucion's death (lucion.cpp).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>
#include <D2RLPlugin/handles.h>

#include <cstddef>
#include <cstdint>

// Kills a monster without drops, as PD2's 0x10356e00 (Zhar and the Warlord are cut, #964).
auto KillQuietly(d2r::Game* game, d2r::Unit* monster) noexcept -> void;

// The game counter key of the Lucion fight's stage (PD2 game +0x262a): 8 once Lucion is dead, which the Temple's
// Desecration (UB7) needs.
inline constexpr uint32_t LucionStageKey = 0x55425253;  // 'UBRS'
inline constexpr int32_t LucionDeadStage = 8;

// Registers the monster-death listener; `report` gets one entry. Returns 1 when it is on.
auto InstallLucion(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// UB7a: Desecration on `item` (a native item in the player's inventory), at the altar's confirm (altar_panel.cpp).
enum class DesecrateResult { Refused, Failed, Desecrated };
auto Desecrate(d2r::Game* game, d2r::Unit* player, d2r::Unit* item) noexcept -> DesecrateResult;
// UB7b, after a Failed roll (game thread): swaps the inventory amulet `old` for the brick. False when it did not run.
auto BrickAmulet(d2r::Game* game, d2r::Unit* playerUnit, D2RL::PlayerHandle player, D2RL::ItemHandle old) noexcept -> bool;
// The Sacrifice Altar's use: opens the altar panel. Returns 1 (used).
auto OperateSacrificeAltar(d2r::Game* game, d2r::Unit* player) noexcept -> int32_t;
// Live counters for the status command.
auto LucionCounters(char* out, size_t size) noexcept -> void;
