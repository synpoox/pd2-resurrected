// Item Quantity (#1596): a kill rolls the monster's own TC once more per 100 of the killer's item_quantity stat, plus a
// percent chance of one more for the rest, so every item the TC can give is included. Cartographer puts the stat on the
// player in an unidentified map (+30%); any item can carry it too (property `item-quantity`).
// The trigger is key_drops' monster-death listener (the SDK's). No SDK service rolls a monster's TC, so the drop is
// D2R's own MonsterTcDrop, as uber_deaths' extra drop; every engine call resolves by signature.
#include "quantity_drops.h"

#include "hot_log.h"
#include "log.h"
#include "quantity_rolls.h"
#include "sig.h"

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

std::atomic<uint32_t> g_kills {0}, g_drops {0}, g_logged {0};

auto Ready() noexcept -> bool {
	using sigscan::State;
	return d2r::sig::FindUnit.state == State::Ok && d2r::sig::MissileOwner.state == State::Ok &&
	       d2r::sig::UnitStat.state == State::Ok && d2r::sig::MonsterTcDrop.state == State::Ok;
}

}  // namespace

auto QuantityDrops(const DeathContext& death) noexcept -> void {
	using namespace d2r::rva;
	using D2RL::Lifecycle::UnitType;
	const auto* event = death.event;
	if (event->killer.type == UnitType::Invalid || !Ready() || (At<uint32_t>(death.monster, UnitMainFlags) & UnitNoTreasure) != 0) {
		return;
	}
	Unit* killer = d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::sig::FindUnit)(
		death.game, static_cast<int32_t>(event->killer.type), event->killer.id);
	// Only a player carries the stat: a missile, mercenary or summon's kill counts for its owner. The game's owner lookup
	// (MissileOwner, 0x490300) reads any unit's owner type and id (+0xe8, +0xec) when the unit has flag 0x400, not only
	// a missile's; two steps cover a summon's missile (Hydra's bolt -> Hydra -> player).
	for (int step = 0; step < 2 && killer != nullptr && At<int32_t>(killer, d2r::rva::UnitType) != 0; ++step) {
		killer = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::sig::MissileOwner)(death.game, killer);
	}
	if (killer == nullptr || At<int32_t>(killer, d2r::rva::UnitType) != 0) {
		return;
	}
	const int32_t percent =
		d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(killer, StatItemQuantity, 0);
	if (percent <= 0) {
		return;
	}
	g_kills.fetch_add(1, std::memory_order_relaxed);
	const int32_t rolls = pd2::quantity::ExtraRolls(percent, d2r::RandomBelow(death.seed, 100));
	for (int32_t i = 0; i < rolls; ++i) {
		struct {
			void* unused;
			Unit* monster;
			Unit* killer;
		} drop {nullptr, death.monster, killer};
		d2r::Engine<void (*)(Game*, Unit*, int32_t, void*) noexcept>(d2r::sig::MonsterTcDrop)(death.game, death.monster, 0,
		                                                                                     &drop);
	}
	const uint32_t n = g_drops.fetch_add(static_cast<uint32_t>(rolls), std::memory_order_relaxed) + rolls;
	if (rolls > 0 && pd2::HotSample(g_logged)) {
		plog::Info("item quantity: %+d%%, %d extra TC roll(s) for monster %d in level %d (%u so far)", percent, rolls,
		           At<int32_t>(death.monster, UnitClassId), event->levelId, n);
	}
}

auto QuantityDropsCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "item quantity: %u kills with the stat, %u extra TC rolls", g_kills.load(), g_drops.load());
}
