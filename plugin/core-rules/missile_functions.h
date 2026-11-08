// PD2 missile functions (missiles.txt pSrvHitFunc / pSrvDoFunc / pCltDoFunc / pSrvDmgFunc): Split Throw's return
// missiles, Exploding Arrow's spread and Multiple Shot's pierce falloff (missile_functions.cpp). skill_functions.cpp
// writes them into their table slots with the skill functions.
#pragma once

#include "pd2_engine.h"

#include <cstddef>
#include <cstdint>

using MissileSrvDoFn = int64_t (*)(d2r::Game* game, d2r::Unit* missile) noexcept;
// D2R's collision (0x4639a0) deals the missile's damage only when the result has bit 2: srvhit 4 (0x45ddb0) returns 3,
// or 1 without a missile row; Chain Lightning's srvhit 12 (0x45d5e0) returns 1 or 3.
using MissileSrvHitFn = int32_t (*)(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept;
using MissileCltDoFn = void (*)(d2r::Unit* missile, int32_t a2) noexcept;
using MissileSrvDmgFn = void (*)(d2r::Game* game, d2r::Unit* missile, d2r::Unit* target, uint8_t* damage) noexcept;
// Zero skips the rest of the client's hit (the explosion missile), as D2R's dispatcher 0x1b8e60 reads it.
using MissileCltHitFn = int32_t (*)(d2r::Unit* missile, d2r::Unit* hit) noexcept;

auto SrvHitSpawnReturn(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 65
auto SrvDoReturn(d2r::Game* game, d2r::Unit* missile) noexcept -> int64_t;             // sdo 46 (PD2 38)
void CltDoReturn(d2r::Unit* missile, int32_t a2) noexcept;                             // cdo 78 (PD2 70)

// Double Throw's missiles.
auto SrvHitBounce(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 66 (PD2 30)
void SrvDmgFirstHit(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit, uint8_t* damage) noexcept;  // dmg 21 (PD2 16)
void SrvDmgChainFalloff(d2r::Game* game, d2r::Unit* missile, d2r::Unit* target, uint8_t* damage) noexcept;  // dmg 22 (PD2 15)
auto SrvHitExplodeSpread(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 63
void SrvDmgPierceFalloff(d2r::Game* game, d2r::Unit* missile, d2r::Unit* target, uint8_t* damage) noexcept;  // dmg 20
auto SrvHitState(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 61
auto SrvHitBoltSplash(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 62
auto SrvHitUberImmolation(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 64
auto CltHitHolyLight(d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // chit 66
auto CltHitFistDelay(d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // chit 71 (PD2 26)
void CltDoChargeExplode(d2r::Unit* missile, int32_t a2) noexcept;  // cdo 79 (PD2 69)
auto CltHitChargeBolt(d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // chit 67
auto SrvDoBlessedHammer(d2r::Game* game, d2r::Unit* missile) noexcept -> int64_t;  // sdo 47 (PD2 40)
void CltDoBlessedHammer(d2r::Unit* missile, int32_t a2) noexcept;  // cdo 80 (PD2 72)
// Blade Fury's fragment (bladefragment1).
auto SrvHitBladeFragment(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 69 (PD2 34)
void SrvDmgBladeFragment(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit, uint8_t* damage) noexcept;  // dmg 19
auto CltHitBladeFragment(d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // chit 68 (PD2 6)
auto SrvHitSummon(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 67 (PD2 6)
void SrvDmgRathmaSpear(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit, uint8_t* damage) noexcept;  // dmg 18
// Live counters for the status command.
auto MissileFunctionCounters(char* out, size_t size) noexcept -> void;
