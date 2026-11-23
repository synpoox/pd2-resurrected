// PD2's Megademon think (ProjectDiablo.dll 0x10357710, monai row 89; #1871) for megademonboss, Lieutenant of Sin, the
// boss of Cartographer's m06. It acts only when the monster's monstats aip7(H) is not 0 (1.13c record +0x7e; PD2 reads
// the Hell column whatever the difficulty) and counts down two cooldown stats, one step per think:
//   mon_cooldown1 < 1: cast Skill2 (Sk2mode) at the tick's target, at (0, 0), set it to aip7(H), end the think;
//   else take 1 off; mon_cooldown2 < 1: cast Skill3 (Sk3mode) the same way, set it to aip8(H) (+0x84), end the think;
//   else take 1 off and run the row's own Megademon think (1.13c D2Game +0x18ce0).
// No PluginSDK service runs code on a monster's AI tick, so the think registers on row 89 through ai_registry (one
// table write); every other Megademon keeps D2R's think. D2R's think is found by a signature of its own.
#include "megademon_ai.h"

#include "ai_hooks.h"
#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <atomic>
#include <cstdio>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

constexpr int32_t MegademonBoss = 763;  // monstats `megademonboss` (PD2 746)
constexpr int32_t MegademonRow = 89;    // monai.txt `Megademon`
constexpr int32_t Cooldown1 = 499, Cooldown2 = 500;  // mon_cooldown1, mon_cooldown2 (PD2 446, 447)
// aip7(H) and aip8(H) in D2R's record: the aip list is int16 (aip1, aip1(N), aip1(H), aip2, ...)
constexpr int Aip7Hell = d2r::rva::MonAiParams + 2 * (3 * 6 + 2), Aip8Hell = d2r::rva::MonAiParams + 2 * (3 * 7 + 2);

// 93847: 0x5c04f0, monai row 89's think, read from the AI table
sigscan::Entry AiMegademon {"AiMegademon", "ai", sigscan::Kind::Code, 0, 0,
                            "48 89 5c 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 ec 40 41 8b 40 20 48 8b f1 45 8b 60 24 "
                            "48 8b ca 4d 8b 78 10 49 8b d8 48 8b fa 89 84 24 98 00 00 00 e8 ?? ?? ?? ?? 48 8b 6b 28 4c 8b f0"};
sigscan::Entry* const Own[] = {&AiMegademon};

std::atomic<uint32_t> g_casts {0};

// What one think does with the two cooldowns: the skill slot to cast (2, 3, or 0 for none) and the new values
struct Step {
	int slot;
	int32_t cd1, cd2;
};
constexpr auto Next(int32_t cd1, int32_t cd2, int32_t aip7, int32_t aip8) noexcept -> Step {
	if (cd1 < 1) {
		return {2, aip7, cd2};
	}
	return cd2 < 1 ? Step {3, cd1 - 1, aip8} : Step {0, cd1 - 1, cd2 - 1};
}
static_assert(Next(0, 0, 1350, 1350).slot == 2 && Next(0, 0, 1350, 1350).cd1 == 1350);
static_assert(Next(1350, 0, 1350, 1350).slot == 3 && Next(1350, 0, 1350, 1350).cd1 == 1349 &&
              Next(1350, 0, 1350, 1350).cd2 == 1350);
static_assert(Next(5, 7, 1350, 1350).slot == 0 && Next(5, 7, 1350, 1350).cd1 == 4 && Next(5, 7, 1350, 1350).cd2 == 6);

void Think(Game* game, Unit* unit, uint8_t* tick) noexcept {
	using namespace d2r::rva;
	const auto* rec = tick != nullptr ? At<const uint8_t*>(tick, AiTickMonStats) : nullptr;
	const int32_t aip7 = rec != nullptr ? At<uint16_t>(rec, Aip7Hell) : 0;
	if (aip7 != 0) {
		const auto stat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat);
		const auto set = d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(d2r::sig::SetUnitStat);
		const Step s = Next(stat(unit, Cooldown1, 0), stat(unit, Cooldown2, 0), aip7, At<uint16_t>(rec, Aip8Hell));
		set(unit, Cooldown1, s.cd1, 0);
		set(unit, Cooldown2, s.cd2, 0);
		if (s.slot != 0) {
			const int32_t skill = At<int16_t>(rec, MonSkills + 2 * (s.slot - 1));
			const int32_t mode = At<uint8_t>(rec, MonSkillModes + s.slot - 1);
			Unit* target = At<Unit*>(tick, AiTickTarget);
			const int32_t ok =
				d2r::Engine<int32_t (*)(Game*, Unit*, uint8_t, int32_t, Unit*, int32_t, int32_t, uint8_t) noexcept>(
					d2r::sig::AiUseSkill)(game, unit, static_cast<uint8_t>(mode), skill, target, 0, 0, 0);
			if (pd2::HotSample(g_casts.fetch_add(1) + 1)) {
				plog::Info("megademon ai: class %d cast skill %d mode %d%s -> %d (%u so far)", At<int32_t>(unit, UnitClassId),
				           skill, mode, target != nullptr ? " on target" : " without a target", ok, g_casts.load());
			}
			return;
		}
	}
	ai_registry::Vanilla(MegademonRow)(game, unit, tick);
}

}  // namespace

auto InstallMegademonAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	sigscan::Resolve(Own, 1, context->exeBase, nullptr, 0);
	if (AiMegademon.state != sigscan::State::Ok) {
		std::snprintf(report, reportSize, "off: signature AiMegademon %s", sigscan::StateText(AiMegademon.state));
		return 0;
	}
	if (!ai_registry::RegisterAt(context, MegademonRow, AiMegademon.address, MegademonBoss, &Think)) {
		std::snprintf(report, reportSize, "off: monai %d think not swapped (the slot is not D2R's think)", MegademonRow);
		return 0;
	}
	std::snprintf(report, reportSize, "Lieutenant of Sin casts Skill2 and Skill3 on cooldowns (monai %d think)", MegademonRow);
	return 1;
}

auto MegademonAiCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "megademon ai: %u cooldown casts", g_casts.load());
}
