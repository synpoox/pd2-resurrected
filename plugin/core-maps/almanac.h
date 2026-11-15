// The Horadric Almanac's identify-all (almanac.cpp), run from the item spell use hook (map_open.cpp).
#pragma once

#include "pd2_engine.h"

// Identifies every unidentified item on inventory page 0 or in the Cube, as PD2's skill 513 does.
void IdentifyAll(d2r::Game* game, d2r::Unit* player) noexcept;
