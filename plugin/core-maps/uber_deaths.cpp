// The uber lane's steps of the monster-death listener (death_steps.h), moved out of key_drops.cpp (#943).
#include "death_steps.h"

#include "rathma.h"
#include "uber_drops.h"
#include "uber_tristram.h"

auto UberDeathEarly(d2r::Game* game, d2r::Unit* monster) noexcept -> void {
	RathmaDeath(game, monster);
	TristramDeath(game, monster);
}

auto UberDeathLate(const DeathContext& death) noexcept -> void {
	UberDeathDrops(death.game, death.monster, death.seed);
}
