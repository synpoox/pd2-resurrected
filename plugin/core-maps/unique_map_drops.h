// PD2's unique map drop (unique_map_drops.cpp): the map-area roll on every death and Horazon's 10%.
#pragma once

#include "pd2_engine.h"

#include <cstdint>

// PD2's 0x10381510: one of the eight unique maps, dropped by the monster (the map-area roll and Horazon's 10%).
auto DropUniqueMap(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// PD2 0x103799d0's unique map roll: in a map area, 1 in N of TC 1020 (1021 for a map boss), N divided by `mult`.
auto RollUniqueMap(d2r::Game* game, d2r::Unit* monster, int32_t level, int32_t players, int64_t mult,
                   uint32_t* seed) noexcept -> void;
// Unique maps dropped and rolls made, for key_drops' status line.
auto UniqueMapsDropped() noexcept -> uint32_t;
auto UniqueMapRolls() noexcept -> uint32_t;
