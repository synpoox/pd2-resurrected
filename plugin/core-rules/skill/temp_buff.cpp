// temp_buff
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

// The unit's list on `state` dropped, state off and list freed, as RemoveSkillState (0x4370b0) does per state. That
// one removes by states.txt group and does nothing for a state with none (temp_skill_buff, the progressive states).
void DropStateList(uint32_t dataFlag, Unit* unit, int32_t state) noexcept {
	using namespace d2r::rva;
	void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, state);
	if (list == nullptr) {
		return;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetStateBit)(unit, state, 0);
	d2r::Engine<void (*)(Unit*, void*) noexcept>(DetachStatList)(unit, list);
	d2r::Engine<void (*)(uint32_t, void*) noexcept>(FreeStatList)(dataFlag, list);
}

// --- Split Throw's start buff (PD2 srvstfunc 79)---------------------------------------------------------------------

// --- Split Throw's start buff (PD2 srvstfunc 79)---------------------------------------------------------------------
std::atomic<uint32_t> g_tempBuffs {0};

// D2R's pieces of the flag-4 list below, each by signature; the cast goes on without a buff when one is missing

}  // namespace pd2r::skills

namespace {

// D2R's pieces of the flag-4 list below, each by signature; the cast goes on without a buff when one is missing
const sigscan::Entry* const TempBuffUses[] = {&d2r::sig::UnitStateList, &d2r::sig::NewStatList,   &d2r::sig::UnitId,
                                              &d2r::sig::SetListState,  &d2r::sig::AttachStatList, &d2r::sig::SetStateBit,
                                              &d2r::sig::AddAuraStats};

constexpr uint32_t ListFlagTemp = 4;  // freed with its state by D2R's set anim mode (0x34e5f0 -> 0x2f7540)

// PD2's helper (0x10381a70): the skill's aurastate with its aurastat1-6 on a flag-4 list with no timer, kept when
// the unit still has one, the way D2R's 0x434290 puts Dragon Tail's attackrate on; the next mode change frees it, so
// the buff lasts for the attack. PD2 gives Split Throw temp_skill_buff + attackrate (par4).

// PD2's helper (0x10381a70): the skill's aurastate with its aurastat1-6 on a flag-4 list with no timer, kept when
// the unit still has one, the way D2R's 0x434290 puts Dragon Tail's attackrate on; the next mode change frees it, so
// the buff lasts for the attack. PD2 gives Split Throw temp_skill_buff + attackrate (par4).
auto SrvStTempBuff(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t state = At<int16_t>(skill, SkAuraState);
	if (state <= 0 || !std::all_of(std::begin(TempBuffUses), std::end(TempBuffUses),
	                               [](const sigscan::Entry* e) { return e->state == sigscan::State::Ok; })) {
		return 1;  // no aurastate (or no engine pieces): the cast goes on without a buff
	}
	void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(d2r::sig::UnitStateList)(unit, state);
	const bool kept = list != nullptr;
	if (!kept) {
		const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(d2r::sig::UnitId)(unit, __FILE__,
		                                                                                                   __LINE__);
		list = d2r::Engine<void* (*)(uint32_t, int32_t, int32_t, int32_t) noexcept>(d2r::sig::NewStatList)(
			ListFlagTemp, 0, At<int32_t>(unit, UnitType), id);
		if (list == nullptr) {
			return 1;
		}
		d2r::Engine<void (*)(void*, int32_t) noexcept>(d2r::sig::SetListState)(list, state);
		d2r::Engine<void (*)(Unit*, void*, int32_t) noexcept>(d2r::sig::AttachStatList)(unit, list, 1);
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::SetStateBit)(unit, state, 1);  // Attach does not
	using AuraStatsFn = void (*)(Unit*, void*, const uint8_t*, int32_t, int32_t) noexcept;
	d2r::Engine<AuraStatsFn>(d2r::sig::AddAuraStats)(unit, list, skill, skillId, level);
	++g_tempBuffs;
	return 1;
}

// --- W10 Barbarian group (docs/research/skills/w10-barbarian.md) ----------------------------------------------------------

}  // namespace

auto InstallTempBuff(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	const bool tempBuff = std::all_of(std::begin(TempBuffUses), std::end(TempBuffUses),
	                                  [](const sigscan::Entry* e) { return e->state == sigscan::State::Ok; });
	plog::Info("temp buff: PD2 flag-4 list until the mode changes %s", tempBuff ? "on" : "FAILED (signature)");
	return tempBuff;
}

PD2R_RULE(InstallTempBuff, -150)

PD2R_SLOT(2, SrvSt, 79, static_cast<SrvDoFn>(SrvStTempBuff), "temp buff")
