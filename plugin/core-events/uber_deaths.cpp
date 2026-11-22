// The uber lane's steps of the monster-death listener (death_steps.h), moved out of key_drops.cpp (#943).
#include "death_steps.h"

#include "hot_log.h"
#include "log.h"
#include "placements.h"
#include "rathma.h"
#include "uber_drops.h"
#include "uber_tristram.h"

namespace {

// PD2's extra drop of 0x103799d0's map block (#964 kept it for the uber levels only): each kill of a monster that is no
// minion in Diablo Clone's, Rathma's or Lucion's level drops the monster's own TC once more (0x103843d0). Zhar's and
// Djinn's levels were cut with PD2's maps.
auto UberExtraDrop(const DeathContext& death) noexcept -> void {
	using namespace d2r::rva;
	using D2RL::Lifecycle::UnitType;
	const int32_t level = death.event->levelId;
	if (level != placements::LevelOf("dclone") && level != placements::LevelOf("rathma1") &&
	    level != placements::LevelOf("lucion")) {
		return;
	}
	const auto* data = d2r::At<const uint8_t*>(death.monster, UnitData);
	if (data == nullptr || (data[MonDataTypeFlags] & MonTypeMinion) != 0) {
		return;
	}
	// The drop's killer (its quest test and magic find): a missile counts as its owner, as for D2R's own drop
	const auto* event = death.event;
	d2r::Unit* killer = event->killer.type != UnitType::Invalid
	                        ? d2r::Engine<d2r::Unit* (*)(d2r::Game*, int32_t, uint32_t) noexcept>(FindUnit)(
	                              death.game, static_cast<int32_t>(event->killer.type), event->killer.id)
	                        : nullptr;
	if (killer != nullptr && event->killer.type == UnitType::Missile) {
		killer = d2r::Engine<d2r::Unit* (*)(d2r::Game*, d2r::Unit*) noexcept>(MissileOwner)(death.game, killer);
	}
	if (killer == nullptr) {
		return;
	}
	struct {
		void* unused;
		d2r::Unit* monster;
		d2r::Unit* killer;
	} drop {nullptr, death.monster, killer};
	d2r::Engine<void (*)(d2r::Game*, d2r::Unit*, int32_t, void*) noexcept>(MonsterTcDrop)(death.game, death.monster, 0,
	                                                                                      &drop);
	static std::atomic<uint32_t> logs {0};
	if (pd2::HotSample(logs)) {
		plog::Info("uber: extra drop for monster %d in level %d", d2r::At<int32_t>(death.monster, UnitClassId), level);
	}
}

}  // namespace

auto UberDeathEarly(d2r::Game* game, d2r::Unit* monster) noexcept -> void {
	RathmaDeath(game, monster);
	TristramDeath(game, monster);
}

auto UberDeathLate(const DeathContext& death) noexcept -> void {
	UberDeathDrops(death.game, death.monster, death.seed);
	UberExtraDrop(death);
}
