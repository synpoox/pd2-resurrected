// Slow does not slow the prime evils, as in PD2 (#2004).
//
// PD2's aura event 19 (item_slow; ProjectDiablo.dll 0x10346df0 PD2.gpr, 0x10346e00 installed, over 1.13c's in D2Game's
// event table +0x1077a8) returns 0 when the target is a monster for which D2Common #10278 is true (monstats flags bit 7,
// the `primeevil` column), and on player vs player (not ported: PvP is cut), else runs vanilla. In D2R an equipped
// item's slow node (events 5 and 6) runs through D2RCore's dispatcher and its own copy of the event table, never D2R's
// event 19 (0x5849d0), as item crushing blow does (#446). So item_rules.cpp's HitEvent relay, which already hides
// crushing blow and open wounds nodes for that dispatch, calls in here: on a prime evil the slow nodes are hidden for
// the call and not rerun. No SDK service reaches an item event node.
#include "common.h"

#include "log.h"
#include "pd2_engine.h"

#include <atomic>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t StatSlow = 150;     // item_slow
constexpr uint16_t NoEvent = 0xffff;  // no event raises it
thread_local int32_t t_slowHidden = -1;  // the event whose slow nodes an outer HitEvent on this thread hid

auto IsPrimeEvil(Game* game, Unit* target) noexcept -> bool {
	using namespace d2r::rva;
	if (At<int32_t>(target, UnitType) != UnitMonster) {
		return false;
	}
	const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
	    d2r::DataFlag(game), At<int32_t>(target, UnitClassId));
	return record != nullptr && (At<uint32_t>(record, MonFlags) & MonPrimeEvil) != 0;
}

}  // namespace

auto SlowPrimeEvilBegin(Game* game, int32_t event, Unit* unit, Unit* target, SlowNodes& nodes) noexcept -> void {
	using namespace d2r::rva;
	nodes.count = 0;
	nodes.outer = t_slowHidden;
	const bool prime = IsPrimeEvil(game, target);
	// A splash hit inside an outer dispatch that hid the nodes: a non-prime-evil target gets them back for its call
	const bool reshow = !prime && t_slowHidden == event;
	if (!prime && !reshow) {
		return;
	}
	for (auto* node = At<uint8_t*>(unit, UnitEventNodes); node != nullptr && nodes.count < SlowNodes::Max;
	     node = At<uint8_t*>(node, EventNodeNext)) {
		if (At<int32_t>(node, EventNodeStat) != StatSlow) {
			continue;
		}
		auto* slot = reinterpret_cast<uint16_t*>(node + EventNodeEvent);
		const uint16_t want = prime ? static_cast<uint16_t>(event) : NoEvent;
		if (*slot != want) {
			continue;
		}
		nodes.node[nodes.count] = node;
		nodes.restore[nodes.count++] = want;
		*slot = prime ? NoEvent : static_cast<uint16_t>(event);
	}
	t_slowHidden = prime ? event : -1;
}

auto SlowPrimeEvilEnd(const SlowNodes& nodes) noexcept -> void {
	for (int i = 0; i < nodes.count; ++i) {
		*reinterpret_cast<uint16_t*>(nodes.node[i] + d2r::rva::EventNodeEvent) = nodes.restore[i];
	}
	t_slowHidden = nodes.outer;
}
