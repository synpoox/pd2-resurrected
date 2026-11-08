// The map boss swap: PD2's boss placeholders (lesserhydra1-3) become each map's boss (map_bosses.cpp).
#pragma once

#include "pd2_engine.h"

#include <cstddef>
#include <cstdint>

// PD2 0x1038b990 (and 0x1038bfc0's Ureh, Nemyr and Imperial boss room rewrites), from the preset spawn hook
// (map_stats.cpp): a boss placeholder preset's id becomes the level's boss, in place. True when the preset is done:
// dropped, or spawned here through `spawn`. False lets the preset spawn as usual, rewritten or not.
auto MapBossPreset(d2r::Game* game, void* room, uint8_t* preset,
                   void (*spawn)(d2r::Game*, void*, const uint8_t*) noexcept) noexcept -> bool;
// Live counters for the status command.
auto MapBossCounters(char* out, size_t size) noexcept -> void;
