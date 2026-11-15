// The native-maps lane's steps of the monster-death listener (death_steps.h), moved out of key_drops.cpp (#943):
// PD2's map rolls, obsolete with PD2's maps.
#include "death_steps.h"

#include "map_stats.h"
#include "unique_map_drops.h"
#include "unique_maps.h"

auto MapDeathEarly(d2r::Game* game, d2r::Unit* monster) noexcept -> void {
	UniqueMapDeath(game, monster);
}

auto MapDeathMultiplier(d2r::Unit* monster) noexcept -> int64_t {
	return MapRollMultiplier(monster);
}

auto MapDeathLate(const DeathContext& death) noexcept -> void {
	using namespace d2r::rva;
	using D2RL::Lifecycle::UnitType;
	const auto* event = death.event;
	RollUniqueMap(death.game, death.monster, event->levelId, death.players, death.mult, death.seed);
	// The drop's killer (its quest test and magic find): a missile counts as its owner, as for D2R's own drop
	d2r::Game* game = death.game;
	d2r::Unit* killer = event->killer.type != UnitType::Invalid
	                        ? d2r::Engine<d2r::Unit* (*)(d2r::Game*, int32_t, uint32_t) noexcept>(FindUnit)(
	                              game, static_cast<int32_t>(event->killer.type), event->killer.id)
	                        : nullptr;
	if (killer != nullptr && event->killer.type == UnitType::Missile) {
		killer = d2r::Engine<d2r::Unit* (*)(d2r::Game*, d2r::Unit*) noexcept>(MissileOwner)(game, killer);
	}
	MapDeathDrops(game, death.monster, killer, death.seed);
}
