// Aura-enchanted bosses pick their aura as PD2 does (#1830).
//
// PD2 (ProjectDiablo.dll 0x1036f8e0 in PD2.gpr, over 1.13c D2Game +0x24e4d) gives a umod 30 boss: monster 704
// Conviction 20 (as vanilla); superunique 37 (Lord De Seis, monster data +0x26) Mon Fanaticism; every other boss a
// uniform pick, on a seed made from the monster's name seed, from eight monster auras (vector 0x105de940, PD2 skills
// 472, 473, 474, 416, 476, 477, 478, 479), at level clamp(mlvl / 7, 1, 13).
//
// In D2R the umod handler table (93847: 0x142395fe0, called from 0x4995e0) sends umod 30 through a D2RLoader thunk
// (`jmp [slot]`) to D2RCore's ApplyAuraEnchanted (export 1091). With its aura_enchanted_selection rule on it makes
// its own pick; with it off it runs an inline copy of the stock logic or calls the slot's old target, D2R's own
// handler 0x495cd0 (a global byte chooses), so a config switch cannot make the redirects live. The SDK's
// Encounters::ModifierPolicy exists, but it can only narrow the loader's own 10 auras (AuraSkillIds), so it cannot
// give PD2's monster auras. So the thunk slot is swapped (found from the dispatch's table lea, by signature) for
// BossAuraUmod, which does what the loader does with the rule off: it calls D2R's handler 0x495cd0, whose add-skill
// (0x433910) and aura start (0x438a70) calls (0x495f50, 0x495f64) are redirected to take PD2's skill and level. That
// handler then gives a monster with flag 0x200 its second aura (item_aura, stat 151) as D2R does. Monster 704, any
// failed lookup (no monster data, a bank without the skill) and a failed install keep the loader's handler unchanged.
// The handler draws nothing from the game's seed (its picks use the name seed), so calling it leaves no other state.
#include "boss_aura.h"

#include "item_rules.h"
#include "log.h"
#include "pd2_engine.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

using d2r::At;
using d2r::Unit;

// PD2's list in merged skills.txt ids (merge/ids-skills.tsv): Mon Conviction, Mon Fanaticism, Mon Holy Shock,
// MonHolyFreeze, Mon Holy Fire, Mon Might, Mon Concentration, Mon Vigor (tools/tests/test_boss_aura.py checks the names)
constexpr int32_t AuraList[] = {541, 542, 543, 365, 545, 546, 547, 548};
constexpr int32_t MonFanaticism = 542;
constexpr uint16_t LordDeSeis = 37;     // superuniques.txt row PD2 tests (0x25)
constexpr int32_t ClassConvictionBoss = 704;  // keeps Conviction 20 (the loader's and PD2's own branch)
constexpr int MonDataNameSeed = 0x18;   // u16, read by 0x38e370 (1.13c +0x14)
constexpr int MonDataSuperUnique = 0x2a;  // u16 superunique row, read by 0x38e3d0 (1.13c +0x26)
constexpr int32_t StatLevel = 12, UmodAura = 30, UnitMonster = 1;

// lea r9,[umod table] / mov [rcx+rdi],bpl / mov r9,[r9+rbp*8] / test r9,r9 / jz / mov r8d,r14d / mov edx,ebp /
// mov rcx,rbx / call r9: 0x4995e0's call of a unit's umod handler
constexpr const char* UmodDispatchPattern =
    "4c 8d 0d ?? ?? ?? ?? 40 88 2c 39 4d 8b 0c e9 4d 85 c9 74 ?? 45 8b c6 8b d5 48 8b cb 41 ff d1";
// D2R's umod 30 handler 0x495cd0: prologue, unit type test, `mov r8d,0x5ac` (MonsterUnique.cpp line), class 0x2c0 test
constexpr const char* AuraHandlerPattern =
    "40 55 53 57 48 8b ec 48 83 ec 70 41 8b d8 48 8b f9 48 85 c9 74 0a e8 ?? ?? ?? ?? 83 f8 01 74 1b 48 8d 4d 20 c6 45 "
    "20 00 e8 ?? ?? ?? ?? 84 c0 74 01 cc 48 85 ff 0f 84 ?? ?? ?? ?? 48 8b cf e8 ?? ?? ?? ?? 83 f8 01 0f 85 ?? ?? ?? ?? 85 "
    "db 0f 84 ?? ?? ?? ?? 41 b8 ac 05 00 00 4c 89 74 24 58 48 8d 15 ?? ?? ?? ?? 48 8b cf e8 ?? ?? ?? ?? b9 c0 02 00 00";
// mov rdx,[rbp-0x28] / mov r9d,r14d / shr rdx,0x20 / mov rcx,rdi / call add-skill (+14) / mov r8d,[rbp-0x24] /
// mov r9d,-1 / xor edx,edx / mov rcx,rdi / call aura start (+34) / mov edx,0x200 (the second aura's flag test)
constexpr const char* PickCallsPattern =
    "48 8b 55 d8 45 8b ce 48 c1 ea 20 48 8b cf e8 ?? ?? ?? ?? 44 8b 45 dc 41 b9 ff ff ff ff 33 d2 48 8b cf e8 ?? ?? ?? ?? "
    "ba 00 02 00 00";
constexpr uint32_t AddSkillAt = 14, AuraStartAt = 34;

using UmodFn = void (*)(Unit* unit, int32_t umod, int32_t flag) noexcept;
using AddSkillFn = void* (*)(Unit* unit, int32_t skill, int32_t level, int32_t flag) noexcept;
using AuraStartFn = void* (*)(Unit* unit, int32_t zero, int32_t skill, int32_t minusOne) noexcept;
UmodFn g_loader = nullptr;   // the slot's old target: D2RCore's ApplyAuraEnchanted
UmodFn g_handler = nullptr;  // D2R's own umod 30 handler
AddSkillFn g_addSkill = nullptr;
AuraStartFn g_auraStart = nullptr;
// PD2's pick for the handler call in progress (game thread only; -1 outside BossAuraUmod)
int32_t g_skill = -1, g_level = 0;
std::atomic<uint32_t> g_picks {0};

// PD2's skill and level for the boss, or skill -1 to keep the loader's handler
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

void BossAuraUmod(Unit* unit, int32_t umod, int32_t flag) noexcept {
	const bool ours = unit != nullptr && umod == UmodAura && flag != 0 && At<int32_t>(unit, d2r::rva::UnitType) == UnitMonster &&
	                  At<int32_t>(unit, d2r::rva::UnitClassId) != ClassConvictionBoss;
	int32_t level = 0;
	const int32_t skill = ours ? Pd2Pick(unit, &level) : -1;
	if (skill < 0) {
		g_loader(unit, umod, flag);
		return;
	}
	g_skill = skill;
	g_level = level;
	g_handler(unit, umod, flag);
	g_skill = -1;
}

auto BossAddSkill(Unit* unit, int32_t skill, int32_t level, int32_t flag) noexcept -> void* {
	return g_skill >= 0 ? g_addSkill(unit, g_skill, g_level, flag) : g_addSkill(unit, skill, level, flag);
}

auto BossAuraStart(Unit* unit, int32_t zero, int32_t skill, int32_t minusOne) noexcept -> void* {
	return g_auraStart(unit, zero, g_skill >= 0 ? g_skill : skill, minusOne);
}

// The image RVA of the thunk slot behind umod table entry 30 (0 when the entry is not a D2RLoader `jmp [slot]` thunk)
auto UmodSlotRva(const D2RL::PluginContext* context) noexcept -> uint64_t {
	uint64_t match = 0;
	if (sigscan::ScanUnique(context->exeBase, UmodDispatchPattern, 0, &match) != 1) {
		return 0;
	}
	const auto* lea = reinterpret_cast<const uint8_t*>(context->exeBase + match);
	int32_t disp = 0;
	std::memcpy(&disp, lea + 3, 4);
	const auto* table = reinterpret_cast<const uint64_t*>(lea + 7 + disp);
	const auto* thunk = reinterpret_cast<const uint8_t*>(table[UmodAura]);
	const uint64_t image = context->exeBase;
	if (reinterpret_cast<uint64_t>(thunk) < image || reinterpret_cast<uint64_t>(thunk) >= image + 0x10000000ull ||
	    thunk[0] != 0xff || thunk[1] != 0x25) {
		return 0;
	}
	std::memcpy(&disp, thunk + 2, 4);
	return reinterpret_cast<uint64_t>(thunk + 6 + disp) - image;
}

}  // namespace

sigscan::Entry* const BossAuraUses[3] = {&d2r::sig::UnitDataFlag, &d2r::sig::GetSkillRecord, &d2r::sig::UnitStat};

void InstallBossAura(const D2RL::PluginContext* context, char* report, size_t size) noexcept {
	const char* failed = nullptr;
	uint64_t handler = 0;
	const uint64_t slotRva = UmodSlotRva(context);
	const uint64_t old = slotRva != 0 ? *reinterpret_cast<const uint64_t*>(context->exeBase + slotRva) : 0;
	if (old == 0) {
		failed = "umod 30 table entry is not a loader thunk";
	} else if (sigscan::ScanUnique(context->exeBase, AuraHandlerPattern, 0, &handler) != 1) {
		failed = "D2R's umod 30 handler signature";
	} else if (!RedirectCall(context, PickCallsPattern, AuraStartAt, reinterpret_cast<uint64_t>(&BossAuraStart),
	                         reinterpret_cast<uint64_t*>(&g_auraStart)) ||
	           !RedirectCall(context, PickCallsPattern, AddSkillAt, reinterpret_cast<uint64_t>(&BossAddSkill),
	                         reinterpret_cast<uint64_t*>(&g_addSkill))) {
		failed = "pick calls: signature, bytes or relay";
	} else {
		// the redirects alone change nothing (g_skill stays -1), so the slot goes last
		g_handler = reinterpret_cast<UmodFn>(context->exeBase + handler);
		g_loader = reinterpret_cast<UmodFn>(old);
		if (!context->PatchWriteU64(slotRva, &old, sizeof(old), reinterpret_cast<uint64_t>(&BossAuraUmod))) {
			failed = "umod 30 slot write";
		}
	}
	if (failed == nullptr) {
		plog::Info("boss aura: umod 30 slot at image +0x%llx swapped (loader handler %p), D2R handler at +0x%llx",
		           static_cast<unsigned long long>(slotRva), reinterpret_cast<void*>(old),
		           static_cast<unsigned long long>(handler));
	}
	std::snprintf(report, size, "%d/1 installed: boss aura pick%s%s%s", failed == nullptr ? 1 : 0, failed ? " FAILED (" : "",
	              failed ? failed : "", failed ? ")" : "");
}

void BossAuraState(const parts::Part& part, char* out, size_t size) noexcept {
	std::snprintf(out, size, "%s: %s; PD2 aura picks %u", part.word, part.report, g_picks.load());
}
