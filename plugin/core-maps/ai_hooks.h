// Monster AI replacement for PD2's AI overrides (ai_hooks.cpp): a registry of think functions by monai row and
// monstats class. The Uber Tristram primes (#483) and Lucion (#437) use it; Dclone (#470) and Rathma (#474) should.
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstdint>

// A think function, D2R's signature: (game, unit, tick). The tick's fields are rva::AiTick*.
using AiThinkFn = void (*)(d2r::Game* game, d2r::Unit* unit, uint8_t* tick) noexcept;

// Runs `think` instead of D2R's think of monai row `monaiRow` for monsters of monstats class `classId` (merged id);
// other classes on the row keep D2R's think. `d2rThink` is the rva of D2R's think in that row (rva::AiMephisto, ...):
// the first registration on a row writes the row's slot in rva::AiTable through the SDK's PatchWriteU64 with it as
// the expected value. Call from a part's install. False when the row is out of range, the slot is not `d2rThink`, or
// the registry is full.
auto RegisterAiThink(const D2RL::PluginContext* context, int32_t monaiRow, uint64_t d2rThink, int32_t classId,
                     AiThinkFn think) noexcept -> bool;
// D2R's own think of a registered row (null for any other): a port calls it for what it leaves to vanilla.
auto VanillaAiThink(int32_t monaiRow) noexcept -> AiThinkFn;
