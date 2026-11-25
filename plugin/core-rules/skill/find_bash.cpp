// find_bash
#include "common.h"
#include "registry.h"

#include "ai_registry.h"
#include "../item_rules.h"
#include "line_spread.h"
#include "log.h"
#include "../missile_functions.h"
#include "parry_roll.h"
#include "pd2_engine.h"
#include "sig.h"
#include "sig_hook.h"
#include "skill_router.h"
#include "../skill_functions.h"
#include "../core-events/arena_spots.h"

#include <D2RLPlugin/mutations.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <map>
#include <mutex>
#include <span>

using namespace pd2r::skills;

namespace pd2r::skills {

// --- W10 Barbarian group (docs/research/skills/w10-barbarian.md) ----------------------------------------------------------
std::atomic<uint32_t> g_finds {0};

std::atomic<uint32_t> g_findsRefused {0};

std::atomic<uint32_t> g_bashHits {0};

std::atomic<uint32_t> g_bashKnockbacks {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t FindPotion = 131;

// D2R's own function in table `table` slot `slot`.

// PD2's 46 bosses whose corpses can't be searched (map, uber and PD2-only-area bosses; docs/research/skills/w10-barbarian.md),
// as merged monstats ids.
constexpr int32_t Unsearchable[] = {760,  763,  764,  767,  772,  817,  826,  843,  878,  887,  896,  899,
                                    900,  901,  910,  911,  932,  980,  981,  1013, 1014, 1015, 1017, 1042,
                                    1043, 1075, 1077, 1078, 1079, 1080, 1111, 1113, 1120, 1121, 1122, 1138,
                                    1139, 1165, 1166, 1181, 1183, 1191, 1192, 1257, 1259, 1260};


auto Searchable(Unit* target) noexcept -> bool {
	if (OwnedByPlayer(target)) {
		return false;
	}
	const int32_t mon = At<int32_t>(target, d2r::rva::UnitType) == 1 ? At<int32_t>(target, d2r::rva::UnitClassId) : -1;
	return std::find(std::begin(Unsearchable), std::end(Unsearchable), mon) == std::end(Unsearchable);
}

// Find Potion / Find Item start (PD2 srvstfunc 70, here 82): the guards, then D2R's own srvst 33 / 34.

// Find Potion / Find Item start (PD2 srvstfunc 70, here 82): the guards, then D2R's own srvst 33 / 34.
auto SrvStFind(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::rva::SkillTargetUnit)(game, unit);
	if (target == nullptr || !Searchable(target)) {
		g_findsRefused.fetch_add(1);
		return 0;
	}
	++g_finds;
	const int slot = skillId == FindPotion ? d2r::rva::SrvStFindPotion : d2r::rva::SrvStFindItem;
	return VanillaSlot<SrvDoFn>(d2r::rva::SrvStTable, slot)(game, unit, skillId, level);
}

// Client half (PD2 cltstfunc 55, here 62): the same guards before D2R's cltst 26 / 28.

// Client half (PD2 cltstfunc 55, here 62): the same guards before D2R's cltst 26 / 28.
auto CltStFind(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	Unit* target = d2r::Engine<Unit* (*)(Unit*) noexcept>(d2r::rva::ClientSkillTargetUnit)(unit);
	if (target == nullptr || !Searchable(target)) {
		return 0;
	}
	const int slot = skillId == FindPotion ? d2r::rva::CltStFindPotion : d2r::rva::CltStFindItem;
	return VanillaSlot<CltDoFn>(d2r::rva::CltStTable, slot)(unit, skillId, level);
}

// Bash (PD2 srvdofunc 158): a hit knocks back half the time (PD2's Bash row has no ResultFlags). The queued hit's
// knockback bit is set from the roll, then D2R's melee do function applies it.
// shortcut: PD2 rolls after the first apply and only for targets with flag 0x8000; this rolls before, for every
// target. Upgrade if knockback on bosses differs from PD2 in play.

// Bash (PD2 srvdofunc 158): a hit knocks back half the time (PD2's Bash row has no ResultFlags). The queued hit's
// knockback bit is set from the roll, then D2R's melee do function applies it.
// shortcut: PD2 rolls after the first apply and only for targets with flag 0x8000; this rolls before, for every
// target. Upgrade if knockback on bosses differs from PD2 in play.
auto SrvDoBash(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::rva::SkillTargetUnit)(game, unit);
	if (target != nullptr) {
		auto* damage = d2r::Engine<uint8_t* (*)(Unit*, Unit*) noexcept>(d2r::rva::QueuedHit)(unit, target);
		if (damage != nullptr) {
			auto& result = *reinterpret_cast<uint16_t*>(damage + d2r::rva::DamageResult);
			result &= static_cast<uint16_t>(~d2r::rva::ResultKnockback);
			if (result & d2r::rva::ResultHit) {
				g_bashHits.fetch_add(1);
				uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(unit);
				if (d2r::RandomBelow(seed, 100) < 50) {
					result |= d2r::rva::ResultKnockback;
					g_bashKnockbacks.fetch_add(1);
				}
			}
		}
	}
	return VanillaSlot<SrvDoFn>(d2r::rva::SrvDoTable, d2r::rva::SrvDoMelee)(game, unit, skillId, level);
}


}  // namespace

PD2R_SLOT(4, SrvSt, 82, static_cast<SrvDoFn>(SrvStFind), "find")
PD2R_SLOT(5, CltSt, 62, static_cast<CltDoFn>(CltStFind), "find")
PD2R_ROUTE(0, Bash, SrvDoBash)
PD2R_ROUTE(1, UberTalicBash, SrvDoBash)
