// wolves
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

// --- Spirit Wolf and Fenris start (PD2 srvstfunc 77; docs/research/skills/druid-w24.md) ----------------------------------
// Read from ProjectDiablo.dll: srvst 77 at 0x103b6c80, its rule 0x103747b0. A player only; pet types 11 and 12 only.
// Once the type has a stored max: while the player has wolves of the type and their count is not under petmax, the
// first one dies; with none left, petmax 0 refuses. D2R's do (srvdo 119) summons at least one even at petmax 0.
std::atomic<uint32_t> g_wolfStarts {0};

std::atomic<uint32_t> g_wolfRefusals {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t PetSpiritWolf = 11, PetFenris = 12;  // pettype rows, PD2's and merged


auto SrvStWolves(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr || At<int32_t>(unit, UnitType) != 0) {
		return 0;
	}
	const uint8_t* player = d2r::Engine<uint8_t* (*)(Unit*) noexcept>(UnitPlayerData)(unit);
	void* pets = player != nullptr ? At<void*>(player, PlayerPets) : nullptr;
	if (pets == nullptr) {
		return 0;
	}
	const int32_t type = At<int8_t>(skill, SkPetType);
	const bool wolves = type == PetSpiritWolf || type == PetFenris;
	// shortcut: petmax of the cast skill at the cast level; PD2 takes the type's own skill (227, 237) at the player's
	// level in it, the same thing for the only rows with these pet types. Upgrade if another row gets one.
	const int32_t limit = d2r::EvalCalc(game, unit, skill, SkPetMaxCalc, skillId, level);
	const uint8_t* entry =
		wolves ? d2r::Engine<uint8_t* (*)(uint32_t, void*, int32_t) noexcept>(PetEntry)(dataFlag, pets, type) : nullptr;
	const int32_t before = entry != nullptr ? At<int32_t>(entry, PetCount) : 0;
	bool accepted = wolves;
	if (entry != nullptr && At<int32_t>(entry, PetMax) > 0) {
		const auto left = [&] { return At<void*>(entry, PetHead) != nullptr; };
		// bounded by the count before, in case a kill ever leaves the pet on the list
		for (int32_t n = before; n > 0 && left() && At<int32_t>(entry, PetCount) >= limit; --n) {
			Unit* pet = d2r::Engine<Unit* (*)(Game*, Unit*, int32_t, int32_t) noexcept>(FirstPet)(game, unit, type, 1);
			if (pet == nullptr) {
				break;
			}
			const int32_t id =
				d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(pet, __FILE__, __LINE__);
			d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(RemovePet)(game, unit, id, 1);
		}
		accepted = left() || limit != 0;
	}
	// shortcut: a refusal is silent; PD2 also has the player say "I can't" (1.13c event sound 0x13), upgrade when
	// D2R's unit event sound is found
	++(accepted ? g_wolfStarts : g_wolfRefusals);
	return accepted ? 1 : 0;
}

// --- Raven: calc3 ravens per cast (PD2 srvdofunc 114; #2002) -------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 114 at 0x103b06c0 (PD2.gpr; installed +0x70). Once per cast: the base spot (the
// caster's skill target, else the caster: 1.13c 0x9fa30, as the creator's own default) and calc3. Then calc3 times:
// the shared creator at a jittered spot, flag 4 clear (the spot, else a spot search of 4); a failed raven ends the cast
// (returns 0, the rest are not made). r4 = rand 0..3, r8 = rand 0..7: raven 0 at (x, y - 1 - r4), 1 at
// (x + 1 + r4, y + 1 + r4), 2 at (x - 1 - r4, y + 1 + r4), the rest at (x - 4 + r8, y - 4 + r8). petmax is the
// creator's: a raven over it replaces the type's oldest. Each raven is 1.13c's: level calc2 (min 1), the summon stats.
// D2R's srvdo 114 (0x576f70) is 1.13c's plus the skill1 at the cast level and the caster's target for the ravens, one
// raven at the target. It is wrapped: called calc3 times, its creator call redirected to set the spot. No SDK service
// summons a pet, so the slot and the call are found by signature.
// Hits: PD2's raven think (monai 107, stock 1.13c 0xaa1d0) gives Param5 + (level - 1) x Param6; D2R's (0x5cca50) reads
// calc3 of the raven's skill instead (vanilla's calc3 is that formula), PD2's raven count here. The redirect writes
// PD2's hits into the new raven's AI params (+0x44), which the think reads only while its init's -1 is there.

}  // namespace

PD2R_SLOT(43, SrvSt, 77, static_cast<SrvDoFn>(SrvStWolves), "wolf start")
