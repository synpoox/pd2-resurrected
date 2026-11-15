// The monster-death listener's per-lane steps (key_drops.cpp calls them in this order; death_steps.h is frozen).
// Each lane edits only its own definitions: uber_deaths.cpp (ubers), map_deaths.cpp (native maps, obsolete PD2 map rolls).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/lifecycle_events.h>

#include <cstdint>

// What the listener has read by the second half: `mult` is the roll multiplier the key rolls divide their odds by.
struct DeathContext {
	d2r::Game* game;
	d2r::Unit* monster;
	const D2RL::Lifecycle::MonsterDeathEvent* event;
	int32_t players;
	int64_t mult;
	uint32_t* seed;
};

// First half, before the seed is taken: the uber fights' death scripts (Rathma, Uber Tristram) ...
auto UberDeathEarly(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// ... then the unique map bosses' death scripts.
auto MapDeathEarly(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// PD2's roll multiplier for the key rolls (1 for a map boss, 2 for a fortified map monster).
auto MapDeathMultiplier(d2r::Unit* monster) noexcept -> int64_t;
// Second half: the uber bosses' guaranteed drops and rolls, then the map-area rolls and drops, in this order.
auto UberDeathLate(const DeathContext& death) noexcept -> void;
auto MapDeathLate(const DeathContext& death) noexcept -> void;
