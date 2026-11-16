// The monster-death listener's per-lane steps (key_drops.cpp calls them in this order; death_steps.h is frozen).
// Each lane edits only its own definitions: uber_deaths.cpp (ubers). PD2's map rolls (map_deaths.cpp) are cut (#964).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/lifecycle_events.h>

#include <cstdint>

// What the listener has read by the second half.
struct DeathContext {
	d2r::Game* game;
	d2r::Unit* monster;
	const D2RL::Lifecycle::MonsterDeathEvent* event;
	int32_t players;
	uint32_t* seed;
};

// First half, before the seed is taken: the uber fights' death scripts (Rathma, Uber Tristram).
auto UberDeathEarly(d2r::Game* game, d2r::Unit* monster) noexcept -> void;
// Second half: the uber bosses' guaranteed drops and rolls.
auto UberDeathLate(const DeathContext& death) noexcept -> void;
