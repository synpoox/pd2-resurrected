// PD2 missile functions (missiles.txt pSrvHitFunc / pSrvDoFunc / pCltDoFunc) for Split Throw's return missiles
// (missile_functions.cpp). skill_functions.cpp writes them into their table slots with the skill functions.
#pragma once

#include "d2r.h"

#include <cstddef>
#include <cstdint>

using MissileSrvDoFn = int64_t (*)(d2r::Game* game, d2r::Unit* missile) noexcept;
using MissileSrvHitFn = void (*)(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept;
using MissileCltDoFn = void (*)(d2r::Unit* missile, int32_t a2) noexcept;

void SrvHitSpawnReturn(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept;  // shit 65
auto SrvDoReturn(d2r::Game* game, d2r::Unit* missile) noexcept -> int64_t;             // sdo 46 (PD2 38)
void CltDoReturn(d2r::Unit* missile, int32_t a2) noexcept;                             // cdo 78 (PD2 70)
// Live counters for the status command.
auto MissileFunctionCounters(char* out, size_t size) noexcept -> void;
