// Shared by the missile function files in this folder: the counters missile_functions.cpp reports and the two helpers
// more than one file uses. Moved out of missile_functions.cpp (#2198); the code is unchanged.
#pragma once
#include "../missile_functions.h"

#include "log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace missiles {

using d2r::At;
using d2r::Game;
using d2r::Unit;

extern std::atomic<uint32_t> g_returnSpawns;
extern std::atomic<uint32_t> g_returnsCaught;
extern std::atomic<uint32_t> g_bounces;
extern std::atomic<uint32_t> g_firstHits;
extern std::atomic<uint32_t> g_chainFalloffs;
extern std::atomic<uint32_t> g_spreadHits;
extern std::atomic<uint32_t> g_spreadExplosions;
extern std::atomic<uint32_t> g_falloffHits;
extern std::atomic<uint32_t> g_stateHits;
extern std::atomic<uint32_t> g_boltHits;
extern std::atomic<uint32_t> g_holyLightHits;
extern std::atomic<uint32_t> g_fistDelayHits;
extern std::atomic<uint32_t> g_chargeExplodes;
extern std::atomic<uint32_t> g_chargeBoltHits;
extern std::atomic<uint32_t> g_fragmentWalls;
extern std::atomic<uint32_t> g_fragmentSplits;
extern std::atomic<uint32_t> g_fragmentDamage;
extern std::atomic<uint32_t> g_fragmentClientWalls;
extern std::atomic<uint32_t> g_hammerFrames;
extern std::atomic<uint32_t> g_hammerHits;
extern std::atomic<uint32_t> g_hammerKept;
extern std::atomic<uint32_t> g_hammerClientFrames;
extern std::atomic<uint32_t> g_summonHits;
extern std::atomic<uint32_t> g_spearHits;
extern std::atomic<uint32_t> g_potionHits;
extern std::atomic<uint32_t> g_potionClientHits;

auto CltMissileRecord(Unit* missile) noexcept -> const uint8_t*;
auto MissileInt(uint64_t rva, Unit* missile) noexcept -> int32_t;

}  // namespace missiles

using namespace missiles;
