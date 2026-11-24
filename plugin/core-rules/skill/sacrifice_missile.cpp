// Sacrifice's missile kills follow PD2's rule (#2043): a missile kill explodes only when the missile's skill is
// Sacrifice.
//
// PD2's deal wrapper (ProjectDiablo.dll 0x103025c0) takes the killer's skill from the missile in the block (+0x5c):
// when it is a missile with a skill, it asks the walked owner (0x10374a60, a pet up to its player) for that skill
// (0x10380710), else the attacker's current skill (0x1030dd10), and explodes on skill 96. The only skill-96 missile
// is the guts missile, owned by the corpse, so a missile kill explodes only when the corpse (or its pet owner) has
// Sacrifice; a Blessed Hammer kill never does.
// D2R: both missile damage paths swap the missile for its owner before the deal and keep no missile in the block:
// AreaApply (93847 0x436e90, via MissileOwner 0x490300) and the missile damage function (0x462e40, its deal call
// at 0x4630ce). No SDK service names the missile of a deal: inline hooks on both entries keep it for the call, and
// SacrificeKill (skill_functions.cpp) reads it. AreaApply's entry is `test rdx,rdx / je rel32`, which a trampoline
// cannot copy, so its hook sits on the saves after it (+9), with the arguments still in place. If a hook fails, its
// path keeps the current-skill test.
#include "common.h"
#include "registry.h"

#include "log.h"
#include "pd2_engine.h"
#include "sig_hook.h"

#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

// The missile damage function (game, missile or unit, defender, block): push rbx/rdi/r14/r15 / sub rsp,0x38 /
// mov rbx,r9 .. the unit-type test (one match in 93847)
constexpr const char* MissileDamagePattern =
    "40 53 57 41 56 41 57 48 83 ec 38 49 8b d9 4d 8b f8 48 8b fa 4c 8b f1 48 85 d2 74 ?? 48 8b ca e8 ?? ?? ?? ?? 83 f8 "
    "03";
constexpr uint8_t MissileDamageEntry[] = {0x40, 0x53, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x38, 0x49, 0x8b, 0xd9};
// AreaApply (game, missile or unit, defender, block): test rdx,rdx / je / the three saves / push rdi / sub rsp,0x30
// .. the unit-type test (one match in 93847); hooked at the saves
constexpr const char* AreaApplyPattern =
    "48 85 d2 0f 84 ?? ?? ?? ?? 48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 48 83 ec 30 49 8b d9 49 8b f0 48 8b fa "
    "48 8b e9 4d 85 c0 0f 84 ?? ?? ?? ?? 48 8b ca e8 ?? ?? ?? ?? 83 f8 03";
constexpr int AreaApplyHookAt = 9;
constexpr uint8_t AreaApplyEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                                      0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
constexpr int32_t UnitMissile = 3;

using DealFn = uint64_t (*)(Game*, Unit*, Unit*, void*) noexcept;
DealFn g_missileDamage = nullptr, g_areaApply = nullptr;
thread_local Unit* t_missile = nullptr;  // the missile of the innermost hooked deal on this thread, or null

auto Keep(DealFn original, Game* game, Unit* unit, Unit* defender, void* block) noexcept -> uint64_t {
	Unit* const outer = t_missile;
	t_missile = unit != nullptr && d2r::At<int32_t>(unit, d2r::rva::UnitType) == UnitMissile ? unit : nullptr;
	const uint64_t result = original(game, unit, defender, block);
	t_missile = outer;
	return result;
}

auto MissileDamage(Game* game, Unit* unit, Unit* defender, void* block) noexcept -> uint64_t {
	return Keep(g_missileDamage, game, unit, defender, block);
}

auto AreaApply(Game* game, Unit* unit, Unit* defender, void* block) noexcept -> uint64_t {
	return Keep(g_areaApply, game, unit, defender, block);
}

}  // namespace

auto SacrificeKillMissile() noexcept -> Unit* {
	return t_missile;
}

auto InstallSacrificeMissile(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool damage = sigscan::HookBySignature(context, "MissileDamage", MissileDamagePattern, 0, MissileDamageEntry,
	                                             sizeof(MissileDamageEntry), reinterpret_cast<void*>(&MissileDamage),
	                                             reinterpret_cast<void**>(&g_missileDamage));
	const bool area = sigscan::HookBySignature(context, "AreaApply", AreaApplyPattern, AreaApplyHookAt, AreaApplyEntry,
	                                           sizeof(AreaApplyEntry), reinterpret_cast<void*>(&AreaApply),
	                                           reinterpret_cast<void**>(&g_areaApply));
	const bool on = damage && area;
	plog::Info("sacrifice: missile kills %s", on ? "on"
	                                         : !damage ? "FAILED (missile damage signature or bytes)"
	                                                   : "FAILED (AreaApply signature or bytes)");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; sacrifice missile kills%s", on ? "" : " FAILED");
	}
	return on;
}

PD2R_RULE(InstallSacrificeMissile, 210)
