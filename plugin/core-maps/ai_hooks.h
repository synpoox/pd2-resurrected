// Monster AI replacement for PD2's AI overrides (ai_hooks.cpp): a registry of think functions by monai row and
// monstats class. Lucion's Overseer AI (#437), and Dclone's (#470) and Rathma's (#474) when they come, register here.
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstdint>

// A think function, D2R's signature: (game, unit, tick). The tick's fields are rva::Tick*.
using AiThinkFn = void (*)(d2r::Game* game, d2r::Unit* unit, uint8_t* tick) noexcept;

// Runs `think` instead of D2R's think of monai row `monaiRow` for monsters of monstats class `classId` (merged id).
// The first registration on a row swaps that row's think in rva::AiTable (SDK PatchWriteU64, D2R's think as the
// expected value); other classes on the row keep D2R's think. Call from a part's install. False when the row is out
// of range, the table write is refused, or the registry is full.
auto RegisterAiThink(const D2RL::PluginContext* context, int32_t monaiRow, int32_t classId, AiThinkFn think) noexcept
	-> bool;
// D2R's own think of a row (null before a registration on it): call it for what a port leaves to vanilla.
auto VanillaAiThink(int32_t monaiRow) noexcept -> AiThinkFn;
