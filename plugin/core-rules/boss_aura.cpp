// Aura-enchanted bosses pick their aura as PD2 does (#1830).
//
// PD2 (ProjectDiablo.dll 0x1036f8e0 in PD2.gpr, over 1.13c D2Game +0x24e4d) gives a umod 30 boss: monster 704
// Conviction 20 (as vanilla); superunique 37 (Lord De Seis, monster data +0x26) Mon Fanaticism; every other boss a
// uniform pick, on a seed made from the monster's name seed, from eight monster auras (vector 0x105de940, PD2 skills
// 472, 473, 474, 416, 476, 477, 478, 479), at level clamp(mlvl / 7, 1, 13).
// D2R's umod 30 handler (93847: 0x495cd0) makes a weighted pick from its own 7-entry table at mlvl/6-8 (1-99), then
// calls add-skill (0x433910) and aura start (0x438a70) at one site (0x495f50, 0x495f64), which monster 704's own path
// does not use. Both calls are redirected (RedirectCall): add-skill gets PD2's skill and level, aura start the same
// skill. D2R's second aura step (flag 0x200, stat 151) after them stays. No SDK service reaches a monster's umod
// handler. Fail-safe: aura start is redirected first and only swaps a skill add-skill picked, and a bank whose
// skills.txt lacks a skill keeps D2R's pick.
#include "boss_aura.h"

#include "item_rules.h"
#include "log.h"
#include "pd2_engine.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <iterator>

namespace {

using d2r::At;
using d2r::Unit;

// PD2's list in merged skills.txt ids (merge/ids-skills.tsv): Mon Conviction, Mon Fanaticism, Mon Holy Shock,
// MonHolyFreeze, Mon Holy Fire, Mon Might, Mon Concentration, Mon Vigor (tools/tests/test_boss_aura.py checks the names)
constexpr int32_t AuraList[] = {541, 542, 543, 365, 545, 546, 547, 548};
constexpr int32_t MonFanaticism = 542;
constexpr uint16_t LordDeSeis = 37;     // superuniques.txt row PD2 tests (0x25)
constexpr int MonDataNameSeed = 0x18;   // u16, read by 0x38e370 (1.13c +0x14)
constexpr int MonDataSuperUnique = 0x2a;  // u16 superunique row, read by 0x38e3d0 (1.13c +0x26)
constexpr int32_t StatLevel = 12;

// mov rdx,[rbp-0x28] / mov r9d,r14d / shr rdx,0x20 / mov rcx,rdi / call add-skill (+14) / mov r8d,[rbp-0x24] /
// mov r9d,-1 / xor edx,edx / mov rcx,rdi / call aura start (+34) / mov edx,0x200 (the second aura's flag test)
constexpr const char* PickCallsPattern =
    "48 8b 55 d8 45 8b ce 48 c1 ea 20 48 8b cf e8 ?? ?? ?? ?? 44 8b 45 dc 41 b9 ff ff ff ff 33 d2 48 8b cf e8 ?? ?? ?? ?? "
    "ba 00 02 00 00";
constexpr uint32_t AddSkillAt = 14, AuraStartAt = 34;

using AddSkillFn = void* (*)(Unit* unit, int32_t skill, int32_t level, int32_t flag) noexcept;
using AuraStartFn = void* (*)(Unit* unit, int32_t zero, int32_t skill, int32_t minusOne) noexcept;
AddSkillFn g_addSkill = nullptr;
AuraStartFn g_auraStart = nullptr;
thread_local int32_t t_picked = -1;  // add-skill's skill for the aura start right after it
std::atomic<uint32_t> g_picks {0};

// PD2's skill and level for the boss, or skill -1 to keep D2R's
auto Pd2Pick(Unit* unit, int32_t* level) noexcept -> int32_t {
	const auto* data = At<const uint8_t*>(unit, d2r::rva::UnitData);
	if (data == nullptr) {
		return -1;
	}
	uint32_t seed[2] = {At<uint16_t>(data, MonDataNameSeed), 0x29a};  // D2R's and PD2's seed init
	const int32_t skill = At<uint16_t>(data, MonDataSuperUnique) == LordDeSeis
	                          ? MonFanaticism
	                          : AuraList[d2r::RandomBelow(seed, static_cast<uint32_t>(std::size(AuraList)))];
	const uint8_t bank = d2r::Engine<uint8_t (*)(Unit*) noexcept>(d2r::sig::UnitDataFlag)(unit);
	if (d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::sig::GetSkillRecord)(bank, skill) == nullptr) {
		return -1;
	}
	const int32_t mlvl = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::sig::UnitStat)(unit, StatLevel, 0);
	*level = std::clamp(mlvl / 7, 1, 13);
	if (plog::Sample(g_picks)) {
		plog::Info("boss aura: class %d mlvl %d -> skill %d level %d (%u so far)", At<int32_t>(unit, d2r::rva::UnitClassId),
		           mlvl, skill, *level, g_picks.load());
	}
	return skill;
}

auto BossAddSkill(Unit* unit, int32_t skill, int32_t level, int32_t flag) noexcept -> void* {
	int32_t pd2Level = level;
	t_picked = unit != nullptr ? Pd2Pick(unit, &pd2Level) : -1;
	return t_picked >= 0 ? g_addSkill(unit, t_picked, pd2Level, flag) : g_addSkill(unit, skill, level, flag);
}

auto BossAuraStart(Unit* unit, int32_t zero, int32_t skill, int32_t minusOne) noexcept -> void* {
	const int32_t picked = t_picked;
	t_picked = -1;
	return g_auraStart(unit, zero, picked >= 0 ? picked : skill, minusOne);
}

}  // namespace

sigscan::Entry* const BossAuraUses[3] = {&d2r::sig::UnitDataFlag, &d2r::sig::GetSkillRecord, &d2r::sig::UnitStat};

void InstallBossAura(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	// aura start first: alone it changes nothing, so a failed add-skill redirect leaves D2R's pick whole
	const bool ok = RedirectCall(context, PickCallsPattern, AuraStartAt, reinterpret_cast<uint64_t>(&BossAuraStart),
	                             reinterpret_cast<uint64_t*>(&g_auraStart)) &&
	                RedirectCall(context, PickCallsPattern, AddSkillAt, reinterpret_cast<uint64_t>(&BossAddSkill),
	                             reinterpret_cast<uint64_t*>(&g_addSkill));
	std::snprintf(report, size, "%d/1 installed: boss aura pick%s", ok ? 1 : 0, ok ? "" : " FAILED (signature, bytes or relay)");
}

void BossAuraState(const parts::Part& part, char* out, size_t size) noexcept {
	std::snprintf(out, size, "%s: %s; PD2 aura picks %u", part.word, part.report, g_picks.load());
}
