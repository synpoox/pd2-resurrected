// unsummon
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

namespace {

// --- Unsummon gives back the Iron Golem's item (PD2 srvdo 4, #1873) -------------------------------------------------
// Read from ProjectDiablo.dll 0x103ae650 (installed 0x103ae6c0), PD2's srvdo 4 over 1.13c's D2Game +0xa2ea0: when the
// used skill's target id is one of the caster's pets (D2Game +0x96600) and that monster is class 291 (irongolem), it
// copies the first item of the golem's inventory (D2Game +0x107e0: serialized and rebuilt with its socketed items),
// drops the copy at a free spot by the golem (D2Game +0xe930, +0xe990), then runs the vanilla unsummon (+0x97b50),
// which kills the golem and its original item with it. With no room or spot it returns 0 and the golem stays.
// D2R 93847's srvdo 4 (0x4305c0) is that vanilla unsummon, so this wraps it. The SDK's item services reach player
// storage only (no monster inventory, no ground destination for an existing item), so the steps are D2R's own calls,
// every one found by signature.
// shortcut: PD2 first walks off a golem tile blocked by walls or doors (0x10389c20, mask 0x805); D2R's spot search
// alone is used, the same final step. Upgrade if a golem ever stands in a wall.
sigscan::Entry UnsummonD2R {"SrvDoUnsummon", "unsummon", sigscan::Kind::Code, 0, 0,
                            "48 89 5c 24 08 57 48 83 ec 20 48 8b f9 48 8b da 48 8b ca e8 ?? ?? ?? ?? 48 85 c0 75 0b 48 8b "
                            "5c 24 30 48 83 c4 20 5f c3 44 8b 40 30 41 b9 01 00 00 00"};  // 0x4305c0
// (caster, unit id) -> the pet type of the caster's pet with that id, 0 when it is not one (the unsummon's own test)

// (caster, unit id) -> the pet type of the caster's pet with that id, 0 when it is not one (the unsummon's own test)
sigscan::Entry PetTypeOf {"PetTypeOf", "unsummon", sigscan::Kind::Code, 0, 0,
                          "48 89 5c 24 10 56 57 41 56 48 83 ec 20 8b f2 48 8b f9 e8 ?? ?? ?? ?? 48 8b d8 48 85 c0 75 13 48 "
                          "8d 4c 24 50 88 44 24 50 e8"};  // 0x4ff3b0
// (game, item) -> a new item with the same data and socketed items (Items.cpp, 1.13c's +0x107e0), or null

// (game, item) -> a new item with the same data and socketed items (Items.cpp, 1.13c's +0x107e0), or null
sigscan::Entry ItemCopy {"ItemCopy", "unsummon", sigscan::Kind::Code, 0, 0,
                         "48 89 5c 24 18 55 56 57 41 54 41 55 41 56 41 57 48 81 ec 80 04 00 00 48 8b 05 ?? ?? ?? ?? 48 33 "
                         "c4 48 89 84 24 70 04 00 00 48 8b e9 48 89 54 24"};  // 0x43d660
// The cursor drop's two steps, as invaders.cpp uses them: (game, room, xy*, out xy*, 1) -> room or null, the free
// floor spot by xy (1.13c's +0xe930); (game, dropper or null, item, room, x, y) puts the item there and tells the clients

// The cursor drop's two steps, as invaders.cpp uses them: (game, room, xy*, out xy*, 1) -> room or null, the free
// floor spot by xy (1.13c's +0xe930); (game, dropper or null, item, room, x, y) puts the item there and tells the clients
sigscan::Entry GolemDropSpot {"ItemDropSpot", "unsummon", sigscan::Kind::Code, 0, 0,
                              "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 41 56 41 57 48 83 ec 40 83 b9 24 01 00 00 00"};

sigscan::Entry GolemDropOnFloor {"ItemDropOnFloor", "unsummon", sigscan::Kind::Code, 0, 0,
                                 "4d 85 c9 0f 84 ?? ?? ?? ?? 48 89 6c 24 10 48 89 74 24 18 4c 89 74 24 20 41 57 48 83 ec 20 "
                                 "49 8b e9"};

sigscan::Entry* const UnsummonEntries[] = {&UnsummonD2R, &PetTypeOf, &ItemCopy, &GolemDropSpot, &GolemDropOnFloor};

constexpr int32_t IronGolemClass = 291;  // monstats irongolem: PD2's raw row and the merged row

constexpr int32_t UnitItem = 4;


auto SrvDoUnsummon(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const auto unsummon = [&] { return d2r::Engine<SrvDoFn>(UnsummonD2R)(game, unit, skillId, level); };
	const uint8_t* skill =
		game != nullptr && unit != nullptr ? d2r::Engine<uint8_t* (*)(Unit*) noexcept>(d2r::sig::CurrentSkill)(unit) : nullptr;
	if (skill == nullptr) {
		return unsummon();
	}
	const uint32_t id = At<uint32_t>(skill, USkillTargetId);
	Unit* golem = d2r::Engine<int32_t (*)(Unit*, uint32_t) noexcept>(PetTypeOf)(unit, id) != 0
		? d2r::Engine<Unit* (*)(Game*, int32_t, uint32_t) noexcept>(d2r::sig::FindUnit)(game, UnitMonster, id)
		: nullptr;
	if (golem == nullptr || At<int32_t>(golem, UnitClassId) != IronGolemClass) {
		return unsummon();
	}
	void* inventory = d2r::Engine<void* (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitInventory)(golem, __FILE__,
	                                                                                                      __LINE__);
	Unit* item = inventory != nullptr ? d2r::Engine<Unit* (*)(void*) noexcept>(d2r::sig::InventoryFirstItem)(inventory) : nullptr;
	if (item == nullptr || At<int32_t>(item, UnitType) != UnitItem) {
		return unsummon();
	}
	int32_t xy[2] = {}, at[2] = {};
	void* room = d2r::UnitXY(golem, &xy[0], &xy[1]) ? d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::UnitRoom)(golem) : nullptr;
	void* spot = room != nullptr
		? d2r::Engine<void* (*)(Game*, void*, int32_t*, int32_t*, uint8_t) noexcept>(GolemDropSpot)(game, room, xy, at, 1)
		: nullptr;
	if (spot == nullptr) {
		return 0;  // PD2: no spot, no unsummon (the golem keeps its item)
	}
	// The copy goes down before the unsummon frees the golem and the original; both run in this one server call
	Unit* copy = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(ItemCopy)(game, item);
	if (copy != nullptr) {
		d2r::Engine<void (*)(Game*, Unit*, Unit*, void*, int32_t, int32_t) noexcept>(GolemDropOnFloor)(game, nullptr, copy, spot,
		                                                                                                at[0], at[1]);
	}
	return unsummon();
}


}  // namespace

auto InstallUnsummon(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	uint8_t expected[8];
	// Unsummon's do is D2R's: the expected slot value is the function its signature found, which the wrapper calls
	sigscan::Resolve(UnsummonEntries, std::size(UnsummonEntries), context->exeBase, nullptr, 0);
	const bool found = d2r::sig::SrvDoTable.rva != 0 &&
	                   std::all_of(std::begin(UnsummonEntries), std::end(UnsummonEntries),
	                               [](const sigscan::Entry* e) { return e->state == sigscan::State::Ok; });
	std::memcpy(expected, &UnsummonD2R.address, sizeof(expected));
	const bool golemItem = found && context->PatchWriteU64(d2r::sig::SrvDoTable.rva + uint64_t {4} * 8, expected,
	                                                       sizeof(expected),
	                                                       reinterpret_cast<uint64_t>(static_cast<SrvDoFn>(SrvDoUnsummon)));
	plog::Info("unsummon: PD2 iron golem item drop %s",
	           golemItem ? "on" : found ? "FAILED (srvdo 4 slot is not D2R's)" : "FAILED (signature)");
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; unsummon golem item%s", golemItem ? "" : " FAILED");
	}
	return golemItem;
}

PD2R_RULE(InstallUnsummon, -20)

