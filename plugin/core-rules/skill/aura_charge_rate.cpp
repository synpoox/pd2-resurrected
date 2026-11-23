// An aura reaching a unit in state 18 (`skill_move`, Charge and Lucion Dash) does not refresh its animation rate, as in
// PD2 (#1993).
//
// 1.13c SkilPal.cpp +0x9a1a0 (an aura's stat pass on a target) calls D2Common #10819 (unit, file, line) at +0x9a397 to
// refresh the target's animation rate and speed. PD2 points that call at 0x102fc860 (installed build e7eec256…):
// `if (unit && #10494(unit, 18)) return;` else #10819(unit, file, line). The return value is unused.
// D2R 93847: SkilPal.cpp 0x5616f0 calls 0x350b40 (unit, file, line 0x1e9) at 0x561cf2: that one call is redirected
// here. The other 0x350b40 callers are not PD2's (its patch table has only this one), and the client has no aura pass.
#include "common.h"
#include "registry.h"

#include "hot_log.h"
#include "../item_rules.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Unit;

// mov r8d,line / lea rdx,file / mov rcx,r14 / call 0x350b40 / mov rax,[rsi] / xor ebx,ebx / mov r9d,[r12+4] / mov r8d,0x15e
constexpr const char* AuraRatesCallPattern =
    "41 b8 ?? ?? ?? ?? 48 8d 15 ?? ?? ?? ?? 49 8b ce e8 ?? ?? ?? ?? 48 8b 06 33 db 45 8b 4c 24 04 41 b8 5e 01 00 00";
constexpr uint32_t AuraRatesCallAt = 16;
// 0x350b40, the rate refresh (#10819)
constexpr const char* RatesPattern =
    "48 85 c9 0f 84 ?? ?? ?? ?? 55 56 48 8b ec 48 83 ec 68 8b 01 48 8b f1 89 45 30 83 f8 03 0f 8f";
constexpr int32_t StateSkillMove = 18;

using RatesFn = void (*)(Unit*, const char*, int32_t) noexcept;
RatesFn g_rates = nullptr;
std::atomic<uint32_t> g_skips {0};

void AuraRates(Unit* unit, const char* file, int32_t line) noexcept {
	if (unit != nullptr &&
	    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(d2r::sig::UnitHasState)(unit, StateSkillMove) != 0) {
		if (pd2::HotSample(g_skips)) {
			plog::Info("aura-charge-rate: skipped rate refresh on unit type %d row %d in skill_move (%u so far)",
			           d2r::At<int32_t>(unit, d2r::rva::UnitType), d2r::At<int32_t>(unit, d2r::rva::UnitClassId),
			           g_skips.load());
		}
		return;
	}
	g_rates(unit, file, line);
}

// The call at the site still aims at the rate refresh (nothing else redirected it first)
auto SiteCallsRates(const D2RL::PluginContext* context) noexcept -> bool {
	uint64_t site = 0, rates = 0;
	if (sigscan::ScanUnique(context->exeBase, AuraRatesCallPattern, 0, &site) != 1 ||
	    sigscan::ScanUnique(context->exeBase, RatesPattern, 0, &rates) != 1) {
		return false;
	}
	const uint64_t call = context->exeBase + site + AuraRatesCallAt;
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(call + 1), 4);
	return call + 5 + static_cast<int64_t>(rel) == context->exeBase + rates;
}

}  // namespace

auto InstallAuraChargeRate(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool on = d2r::sig::UnitHasState.state == sigscan::State::Ok && SiteCallsRates(context) &&
	                RedirectCall(context, AuraRatesCallPattern, AuraRatesCallAt, reinterpret_cast<uint64_t>(&AuraRates),
	                             reinterpret_cast<uint64_t*>(&g_rates));
	plog::Info("aura-charge-rate: no aura rate refresh in skill_move %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; aura-charge-rate: no aura rate refresh in skill_move %s",
		              on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallAuraChargeRate, 150)
