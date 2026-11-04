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

// Double Throw's missiles. D2R's srvhit functions return 1 or 3 (Chain Lightning's srvhit 12 at 0x45d5e0 does).
using MissileSrvHitBounceFn = int32_t (*)(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept;
using MissileSrvDmgFn = void (*)(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit, uint8_t* damage) noexcept;
auto SrvHitBounce(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 66 (PD2 30)
void SrvDmgFirstHit(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit, uint8_t* damage) noexcept;  // dmg 21 (PD2 16)
// Live counters for the status command.
auto MissileFunctionCounters(char* out, size_t size) noexcept -> void;
