// busy_mode_gate
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

// --- The channel take-over (PD2's gate A, ProjectDiablo.dll 0x10373290; docs/research/skills/druid-w21.md) ---------------
// PD2 keys two rules on the busy player's current skill: Inferno / Arctic Blast (srvdofunc 182) give way at once to
// any request that carries a skill; Blade Fury (48) gives way to any other skill and refuses another Blade Fury, so its
// channel goes on. Everything else is D2R's own gate.
std::atomic<uint32_t> g_takeOvers {0};

std::atomic<uint32_t> g_bladeFuryRefusals {0};

std::atomic<uint32_t> g_heldRequests {0};

}  // namespace pd2r::skills

namespace {

constexpr int32_t SrvDoBladeFury = 48;

using GateFn = int64_t (*)(Game* game, Unit* unit, int32_t mode, void* request, int32_t flag) noexcept;

GateFn g_gateOriginal = nullptr;


}  // namespace

namespace pd2r::skills {

auto UnitSkillIdOf(void* unitSkill) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(d2r::rva::UnitSkillId)(unitSkill, __FILE__,
	                                                                                             __LINE__);
}


}  // namespace pd2r::skills

namespace {

auto BusyModeGate(Game* game, Unit* unit, int32_t mode, void* request, int32_t flag) noexcept -> int64_t {
	using namespace d2r::rva;
	void* current = game != nullptr && unit != nullptr && request != nullptr && mode != 0 && mode != 0x11
	                    ? d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit)
	                    : nullptr;
	const int32_t nowId = current != nullptr ? UnitSkillIdOf(current) : -1;
	const uint8_t* now = current != nullptr ? d2r::api.getSkillRecord(d2r::DataFlag(game), nowId) : nullptr;
	const int32_t func = now != nullptr ? At<uint16_t>(now, SkSrvDoFunc) : -1;
	if ((func == 182 || func == SrvDoBladeFury) &&
	    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(unit, Uninterruptable) == 0) {
		const int32_t nextId = UnitSkillIdOf(request);
		if (nextId == nowId) {
			// D2R re-sends a held skill every frame (PD2's client does not mid-mode); D2R's gate keeps the channel going
			const int64_t result = g_gateOriginal(game, unit, mode, request, flag);
			++g_heldRequests;
			return result;
		}
		const uint8_t* next = d2r::api.getSkillRecord(d2r::DataFlag(game), nextId);
		if (next != nullptr) {
			const int32_t nextFunc = At<uint16_t>(next, SkSrvDoFunc);
			const bool refused = func == SrvDoBladeFury && nextFunc == SrvDoBladeFury;
			++(refused ? g_bladeFuryRefusals : g_takeOvers);
			return refused ? 1 : 0;
		}
	}
	// PD2's run and Concentration rules are D2R's own here (docs/research/skills/w19-holy-sword-smite.md)
	return g_gateOriginal(game, unit, mode, request, flag);
}

// --- Get-hit / block end window (PD2's gate B, ProjectDiablo.dll 0x1037bb20; docs/research/skills/w34-gate-b.md) ----------
// PD2 lets a player in get-hit or block take a new mode once game frame + 2 >= the mode-end event; D2R waits for + 1.
// The install patches D2R's two tests from 1 to 2.

// --- Charge (PD2 srvstfunc 72, srvdofunc 180; cltstfunc 57 -> 25, cltdofunc 112 -> 37) -------------------------------
// Read from ProjectDiablo.dll: srvst 72 at 0x103b67e0, srvdo 180 at 0x103b4870, cltst 57 at 0x103ace50, cltdo 112 at
// 0x103abfb0 (docs/research/skills/w16-charge.md). PD2's charge is vanilla's with two changes on the server: the start's speed
// adds Param1, and the hit puts calc'd splash on the caster from how far it charged. D2R's client halves (cltst 25,
// cltdo 37) already add Param1, so the merge points the client columns there.

}  // namespace

auto InstallBusyModeGate(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	const bool gate = context->InstallInlineHook(d2r::rva::BusyModeGate, d2r::rva::BusyModeGateEntry,
	                                             sizeof(d2r::rva::BusyModeGateEntry), BusyModeGate, &g_gateOriginal);
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; mode gate busy take-over%s",
		              gate ? "" : " FAILED (bytes differ)");
	}
	return gate;
}

PD2R_RULE(InstallBusyModeGate, 320)

auto InstallHitWindow(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	size_t used = std::strlen(report);
	// PD2's get-hit / block window: both "frames left > 1" tests become "> 2"
	static constexpr uint8_t D2RWindow[1] = {0x01};
	static constexpr uint8_t PD2Window[1] = {0x02};
	const bool buffer = context->PatchWriteU8(d2r::rva::HitRecoveryWindowBuffer, D2RWindow, sizeof(D2RWindow), 0x02);
	const bool refuse = buffer && context->PatchWriteU8(d2r::rva::HitRecoveryWindowRefuse, D2RWindow,
	                                                    sizeof(D2RWindow), 0x02);
	const bool window = refuse;
	if (!window) {  // all or nothing: put back what was patched, so D2R keeps one window for both tests
		if (buffer) {
			(void)context->PatchWriteU8(d2r::rva::HitRecoveryWindowBuffer, PD2Window, sizeof(PD2Window), 0x01);
		}
	}
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; mode gate hit window%s",
		              window ? "" : " FAILED (bytes differ)");
	}
	return window;
}

PD2R_RULE(InstallHitWindow, 350)

