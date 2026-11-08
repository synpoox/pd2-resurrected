// Level effects PD2 sends to players (level_fx.cpp): the act's darkness and a sound in a level.
#pragma once

#include "pd2_engine.h"

#include <cstdint>

// PD2 0x1037dc70: switches the darkness (the act's eclipse, packet 0x53) of `unit`'s act for every client in game.
auto SetDarkness(d2r::Game* game, d2r::Unit* unit, bool on) noexcept -> void;
// SetDarkness for the act of `level`, found through a player in it (PD2 passes the level's act). False when no
// player is in the level.
auto SetLevelDarkness(d2r::Game* game, int32_t level, bool on) noexcept -> bool;
// PD2 0x1037f240: plays sounds.txt row `sound` on each player in `level` (packet 0x2c).
auto PlayLevelSound(d2r::Game* game, int32_t level, int32_t sound) noexcept -> void;
