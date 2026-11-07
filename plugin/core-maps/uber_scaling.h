// PD2's uber tier scaling, state `uberscaling` (uber_scaling.cpp).
#pragma once

#include "pd2_engine.h"

// PD2's 0x10371a70: a monster of the uber fights that spawns in a PD2 level is scaled once, by the open fight's tier
// and entry count. map_stats' spawn hook calls it for every monster spawning in a PD2 level.
auto ScaleUberMonster(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
