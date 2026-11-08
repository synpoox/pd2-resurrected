// PD2's AI for the Altar of the Catalyst's guardian, GuardianOfFate, and its clone (ProjectDiablo.dll 0x1035a6b0, #609).
// With a target, each think first blinks (Skill1) to a free spot within 8 subtiles when the last think summoned; then
// one roll of 100: under the summon chance, while fewer than 6 summons were cast, the guardian casts Skill3
// (GuardianOfFateSummon, whose missile spawns a clone through core-rules' srvhit 67) on its target, and the chance
// resets; else the chance grows by 6 - summons. Then under 80 Skill2 (Voidbolt) on the target, under 95 a blink, else
// it idles for its aidel. With no target both fall back to their monai row's think, the Regurgitator's (row 71).
// The clones' death with the guardian is map_events.cpp's (AltarGuardianDied).
//
// Both register on monai row 71 through ai_hooks (no SDK service runs monster AI; see ai_hooks.cpp). PD2 keeps the
// blink flag, the summon chance and the summon count in the AI control's three params (1.13c +0x14, +0x18, +0x1c),
// the ones the Regurgitator think keeps its state in; so does the port, at D2R's +0x44 (rva::AiControlParams).
#include "guardian_ai.h"

#include "ai_hooks.h"
#include "log.h"
#include "map_events.h"

#include <intrin.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t Guardian = 1081, Clone = 1082;  // GuardianOfFate, GuardianOfFateClone (PD2 1064, 1065)
constexpr int32_t RegurgitatorRow = 71;          // monai.txt
constexpr int32_t MaxSummons = 6;
// PD2's blink spot: a 2 x 2 box free of mask 0x101, 4 cells around, no move (0x10389c20's last argument 0)
constexpr int32_t BlinkSize = 2, BlinkReach = 4;
constexpr uint16_t BlinkMask = 0x101;

std::atomic<uint32_t> g_thinks {0};
std::atomic<uint32_t> g_blinks {0};
std::atomic<uint32_t> g_summons {0};
std::atomic<uint32_t> g_bolts {0};
std::atomic<uint32_t> g_idles {0};
std::atomic<uint32_t> g_vanilla {0};

// PD2 rolls with the CRT's rand(), as map_events.cpp: seed each thread once
auto Rand() noexcept -> int32_t {
	static thread_local const bool seeded = (std::srand(static_cast<unsigned>(__rdtsc())), true);
	(void)seeded;
	return std::rand();
}

// PD2 0x103a1880: (skill, target, x, y, mode); D2R's cast idles 10 frames when it fails
auto Cast(Game* game, Unit* unit, const uint8_t* rec, int slot, Unit* target, int32_t x, int32_t y) noexcept -> int32_t {
	using namespace d2r::rva;
	const int32_t skill = At<int16_t>(rec, MonSkills + 2 * (slot - 1));
	const int32_t mode = At<uint8_t>(rec, MonSkillModes + slot - 1);
	return d2r::Engine<int32_t (*)(Game*, Unit*, uint8_t, int32_t, Unit*, int32_t, int32_t, uint8_t) noexcept>(AiUseSkill)(
		game, unit, static_cast<uint8_t>(mode), skill, target, x, y, 0);
}

// PD2's blink: x and y each moved by rand() % 16 - 8, then the spot search from there in the unit's room; Skill1 at the
// spot. False when no spot is free (PD2 then goes on with the roll, or idles).
auto Blink(Game* game, Unit* unit, const uint8_t* rec, void* room) noexcept -> bool {
	int32_t xy[2] = {};
	d2r::Engine<void (*)(Unit*, int32_t*) noexcept>(d2r::rva::UnitCoords)(unit, xy);
	xy[0] += Rand() % 16 - 8;
	xy[1] += Rand() % 16 - 8;
	if (room == nullptr || !FindEventSpot(room, xy, BlinkSize, BlinkMask, BlinkReach, false)) {
		return false;
	}
	const int32_t ok = Cast(game, unit, rec, 1, nullptr, xy[0], xy[1]);
	if (plog::Sample(g_blinks)) {
		plog::Info("guardian ai: class %d blinks to (%d, %d) -> %d (%u so far)", At<int32_t>(unit, d2r::rva::UnitClassId),
		           xy[0], xy[1], ok, g_blinks.load());
	}
	return true;
}

void Think(Game* game, Unit* unit, uint8_t* tick) noexcept {
	using namespace d2r::rva;
	auto* control = tick != nullptr ? At<uint8_t*>(tick, AiTickControl) : nullptr;
	const auto* rec = tick != nullptr ? At<const uint8_t*>(tick, AiTickMonStats) : nullptr;
	Unit* target = tick != nullptr ? At<Unit*>(tick, AiTickTarget) : nullptr;
	if (control == nullptr || rec == nullptr || target == nullptr) {
		++g_vanilla;
		VanillaAiThink(RegurgitatorRow)(game, unit, tick);
		return;
	}
	++g_thinks;
	auto* params = reinterpret_cast<int32_t*>(control + AiControlParams);
	int32_t& blinkNext = params[0];
	int32_t& chance = params[1];
	int32_t& summons = params[2];
	const int32_t count = summons;  // PD2 reads it before the blink
	const bool guardian = At<int32_t>(unit, UnitClassId) == Guardian;
	void* room = d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit);
	if (blinkNext == 1) {
		blinkNext = 0;
		if (Blink(game, unit, rec, room)) {
			return;
		}
	}
	const int32_t roll = Rand() % 100;
	if (roll < chance) {
		if (count < MaxSummons && guardian) {
			const int32_t ok = Cast(game, unit, rec, 3, target, 0, 0);
			blinkNext = 1;
			chance = 0;
			++summons;
			plog::Info("guardian ai: summon %d of %d -> %d (%u so far)", summons, MaxSummons, ok, ++g_summons);
			return;
		}
	} else if (count < MaxSummons && guardian) {
		chance += MaxSummons - count;
	}
	if (roll < 80) {
		Cast(game, unit, rec, 2, target, 0, 0);
		if (plog::Sample(g_bolts)) {
			plog::Info("guardian ai: class %d casts skill 2 (%u so far)", At<int32_t>(unit, UnitClassId), g_bolts.load());
		}
		return;
	}
	if (roll < 95 && Blink(game, unit, rec, room)) {
		return;
	}
	++g_idles;
	const int32_t difficulty = At<uint8_t>(game, GameDifficulty);
	d2r::Engine<void (*)(Game*, Unit*, int32_t) noexcept>(AiIdle)(game, unit, At<uint8_t>(rec, MonAiDel + difficulty));
}

}  // namespace

auto InstallGuardianAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	using namespace d2r::rva;
	const bool guardian = RegisterAiThink(context, RegurgitatorRow, AiRegurgitator, Guardian, Think);
	const bool clone = guardian && RegisterAiThink(context, RegurgitatorRow, AiRegurgitator, Clone, Think);
	std::snprintf(report, reportSize, "monai %d think (classes %d, %d)%s", RegurgitatorRow, Guardian, Clone,
	              guardian && clone ? "" : " FAILED (the slot is not D2R's think)");
	return guardian && clone ? 1 : 0;
}

auto GuardianAiCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "guardian ai: %u thinks, %u summons, %u blinks, %u bolts, %u idles, %u without a target",
	              g_thinks.load(), g_summons.load(), g_blinks.load(), g_bolts.load(), g_idles.load(), g_vanilla.load());
}
