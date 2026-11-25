// holy_sword_smite
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

// --- Holy Sword and Smite starts (PD2 srvstfunc 73, 75), Smite's crushing blow (srvdo 150) -------------------------
// Read from ProjectDiablo.dll: srvst 73 at 0x103b6990, srvst 75 at 0x103b6a20, event 16 at 0x103469a0
// (docs/research/skills/w19-holy-sword-smite.md).
std::atomic<uint32_t> g_holySwordStarts {0};

std::atomic<uint32_t> g_smiteStarts {0};

std::atomic<uint32_t> g_smiteEvents {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t ItemTypeSword = 30, ItemTypeTwoHanded = 120;  // merged itemtypes rows of swor and 2han

// Holy Sword start: the weapon must be both a sword and two-handed.
auto SrvStHolySword(Game* game, Unit* unit, int32_t, int32_t) noexcept -> int64_t {
	Unit* weapon = game != nullptr && unit != nullptr
	                   ? d2r::Engine<Unit* (*)(Unit*) noexcept>(d2r::rva::UnitWeapon)(unit)
	                   : nullptr;
	const auto is = [weapon](int32_t type) {
		return d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(d2r::rva::ItemIsType)(weapon, type) != 0;
	};
	const bool accepted = weapon != nullptr && is(ItemTypeTwoHanded) && is(ItemTypeSword);
	++g_holySwordStarts;
	return accepted ? 1 : 0;
}

// Smite start: PD2 adds Smite's auraevent1 (domeleedamage -> 16) here. D2R calls no aura event on a player's melee hit
// (#165 game check: slot 16 never ran on Smite or with a crushing-blow item), so the roll rides on Smite's do instead.

// Smite start: PD2 adds Smite's auraevent1 (domeleedamage -> 16) here. D2R calls no aura event on a player's melee hit
// (#165 game check: slot 16 never ran on Smite or with a crushing-blow item), so the roll rides on Smite's do instead.
auto SrvStSmite(Game* game, Unit* unit, int32_t, int32_t) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	++g_smiteStarts;
	return 1;
}

// --- Holy Sword parry (PD2 avoid roll 0x103035f0, #1466) -------------------------------------------------------------
// PD2's avoid rolls a block for a unit whose composit weapon class is 5 (2hs, a two-handed swing: Holy Sword's sword)
// as well as 13 (ht2, claws), with the block chance (passive_weaponblock, which Holy Sword's aura sets) capped at 75, and
// answers 0x10, the result a block gives. D2R's avoid (0x44ef70, (attacker, defender, ranged flag)) only blocks for 13, so
// this wraps it: a player in class 5 rolls PD2's parry first (PD2 rolls the block before the evade, dodge and avoid
// rolls), and class 13 (claws) rolls it too in the modes D2R skips (walk, run: #1483); everything else is D2R's own code. PD2's 4-frame no-avoid window after any
// avoid is kept per player id (AvoidWindow, parry_roll.h). Not ported: its half chance in the PvP arenas (levels 157, 159, 166: cut), and sound 4713 (ignored by decision).
// Block chance (0x450070 (unit)), composit weapon class (0x3b6e00 (unit, unit data, int* out, -1, 1)) and the unit
// data (UnitInventory) are what 0x44ef70 itself calls. Found by signature: the prologue and the first calls, calls masked.

}  // namespace

auto InstallSmite(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	uint8_t expected[8];
	// The hit queue hook first (inert until the Smite wrapper runs), then Smite's do, D2R's (expected value), which the
	// wrapper calls.
	const uint64_t d2rSmite = d2r::imageBase + d2r::rva::SrvDoSmiteD2R;
	std::memcpy(expected, &d2rSmite, sizeof(expected));
	const bool smite = context->InstallInlineHook(d2r::rva::QueueHit, d2r::rva::QueueHitEntry,
	                                              sizeof(d2r::rva::QueueHitEntry), QueueHitHook, &g_queueHitOriginal) &&
	                   context->PatchWriteU64(d2r::rva::SrvDoTable + static_cast<uint64_t>(d2r::rva::SrvDoSmite) * 8,
	                                          expected, sizeof(expected),
	                                          reinterpret_cast<uint64_t>(static_cast<SrvDoFn>(SrvDoSmite)));
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "; srvdofunc %d smite crushing blow%s",
		                            d2r::rva::SrvDoSmite, smite ? "" : " FAILED (slot or hit queue bytes differ)");
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	return smite;
}

PD2R_RULE(InstallSmite, -110)

PD2R_SLOT(50, SrvSt, 73, static_cast<SrvDoFn>(SrvStHolySword), "holy sword")
PD2R_SLOT(51, SrvSt, 75, static_cast<SrvDoFn>(SrvStSmite), "smite")
