#include "common.h"
#include "registry.h"

#include "log.h"
#include "sig.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace {

using pd2r::skills::SwapSlot;
using d2r::Game;
using d2r::Unit;

constexpr int32_t CloakOfShadows = 264;
constexpr int32_t SrvDoCloakSlot = 47;
constexpr int32_t RestrictedLevels[] = {
    145,  // Dclone Arena
    146, 147, 148,  // Rathma Stages 1–3
    149, 150,  // Lucion Arena and Temple of the Triune
};

constexpr auto RefusesCloak(int32_t skillId, int32_t levelId) noexcept -> bool {
	if (skillId != CloakOfShadows) {
		return false;
	}
	for (const int32_t restricted : RestrictedLevels) {
		if (levelId == restricted) {
			return true;
		}
	}
	return false;
}

static_assert(RefusesCloak(264, 145) && RefusesCloak(264, 146) && RefusesCloak(264, 147));
static_assert(RefusesCloak(264, 148) && RefusesCloak(264, 149) && RefusesCloak(264, 150));
static_assert(!RefusesCloak(264, 144) && !RefusesCloak(264, 151) && !RefusesCloak(264, 0));
static_assert(!RefusesCloak(612, 145) && !RefusesCloak(253, 145));  // ZharCloak and Psychic Hammer

SrvDoFn g_original = nullptr;
D2RL::Reimplementations::GetLevelIdFromUnitFn g_getLevel = nullptr;

// PD2 srvdo 47 refuses the cast before applying its states (#2220). ZharCloak shares this slot.
auto SrvDoCloak(Game* game, Unit* caster, int32_t skillId, int32_t skillLevel) noexcept -> int64_t {
	if (RefusesCloak(skillId, skillId == CloakOfShadows ? g_getLevel(caster) : 0)) {
		return 0;
	}
	return g_original(game, caster, skillId, skillLevel);
}

}  // namespace

auto InstallUberCloakRestriction(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const auto core = GetModuleHandleA(D2RL::CoreExports::CoreDllName);
	if (core != nullptr) {
		g_getLevel = reinterpret_cast<D2RL::Reimplementations::GetLevelIdFromUnitFn>(
		    GetProcAddress(core, D2RL::Reimplementations::GetLevelIdFromUnitInfo.name));
	}

	// The SDK provides the caster's level, but has no callback to veto srvdo 47.
	// Match its body past the prologue and require the signature-resolved slot to hold that exact function.
	constexpr char Pattern[] =
	    "48 33 c4 48 89 45 f0 4c 8b e2 44 89 4c 24 64 4c 8b f1 41 8b d0 0f b6 89 06 01 00 00 41 8b f9 45 8b e8 e8";
	uint64_t originalRva = 0;
	bool on = false;
	if (g_getLevel != nullptr && d2r::sig::SrvDoTable.state == sigscan::State::Ok &&
	    sigscan::ScanUnique(context->exeBase, Pattern, -0x20, &originalRva) == 1) {
		const auto original = context->exeBase + originalRva;
		g_original = reinterpret_cast<SrvDoFn>(original);
		const auto slot = d2r::sig::SrvDoTable.rva + SrvDoCloakSlot * sizeof(original);
		on = SwapSlot(context, slot, original, reinterpret_cast<const void*>(&SrvDoCloak));
	}

	plog::Info("uber cloak restriction: %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; uber cloak restriction %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallUberCloakRestriction, 470)
