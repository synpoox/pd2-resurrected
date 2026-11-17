// PD2 missile functions (missiles.txt pSrvHitFunc / pSrvDoFunc / pCltDoFunc / pSrvDmgFunc): Split Throw's return
// missiles, Exploding Arrow's spread and Multiple Shot's pierce falloff (missile_functions.cpp). skill_functions.cpp
// writes them into their table slots with the skill functions.
#pragma once

#include "pd2_engine.h"
#include "skill_router.h"  // the toolkit's: SrvDoFn and the Missile*Fn signatures (documented there)

#include <cstddef>
#include <cstdint>

auto SrvHitSpawnReturn(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 65
auto SrvDoReturn(d2r::Game* game, d2r::Unit* missile) noexcept -> int64_t;             // sdo 46 (PD2 38)
void CltDoReturn(d2r::Unit* missile, float partFrame) noexcept;                             // cdo 78 (PD2 70)

// Double Throw's missiles.
auto SrvHitBounce(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 66 (PD2 30)
void SrvDmgFirstHit(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit, d2r::Damage* damage) noexcept;  // dmg 21 (PD2 16)
void SrvDmgChainFalloff(d2r::Game* game, d2r::Unit* missile, d2r::Unit* target, d2r::Damage* damage) noexcept;  // dmg 22 (PD2 15)
auto SrvHitExplodeSpread(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 63
void SrvDmgPierceFalloff(d2r::Game* game, d2r::Unit* missile, d2r::Unit* target, d2r::Damage* damage) noexcept;  // dmg 20
auto SrvHitState(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 61
auto SrvHitBoltSplash(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 62
auto SrvHitUberImmolation(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 64
auto CltHitHolyLight(d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // chit 66
auto CltHitFistDelay(d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // chit 71 (PD2 26)
void CltDoChargeExplode(d2r::Unit* missile, float partFrame) noexcept;  // cdo 79 (PD2 69)
auto CltHitChargeBolt(d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // chit 67
auto SrvDoBlessedHammer(d2r::Game* game, d2r::Unit* missile) noexcept -> int64_t;  // sdo 47 (PD2 40)
void CltDoBlessedHammer(d2r::Unit* missile, float partFrame) noexcept;  // cdo 80 (PD2 72)
// Blade Fury's fragment (bladefragment1).
auto SrvHitBladeFragment(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 69 (PD2 34)
void SrvDmgBladeFragment(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit, d2r::Damage* damage) noexcept;  // dmg 19
auto CltHitBladeFragment(d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // chit 68 (PD2 6)
auto SrvHitSummon(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 67 (PD2 6)
void SrvDmgRathmaSpear(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit, d2r::Damage* damage) noexcept;  // dmg 18
// The throwing potions (IR5).
auto SrvHitThrowPotion(d2r::Game* game, d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // shit 68 (PD2 60)
auto CltHitThrowPotion(d2r::Unit* missile, d2r::Unit* hit) noexcept -> int32_t;  // chit 72 (PD2 65)
// Live counters for the status command.
auto MissileFunctionCounters(char* out, size_t size) noexcept -> void;
