// Slow Missiles skips prime evils (#2219). PD2 srvdo 6 (PD2.gpr 0x103ae7c0), callback 0x1036c920,
// rejects state 87 on monsters whose monstats primeevil flag is set (D2Common #10278).
// D2R srvdo 6 (93847 0x556f70), callback 0x559250, lacks that check. No SDK service filters this
// per-target aura apply: hook the callback and otherwise keep its state/stat application, including Inner Sight.
#include "common.h"
#include "registry.h"
#include "sig_hook.h"

#include <cstdio>

namespace {

using d2r::At;
using d2r::Unit;
using ApplyFn = int32_t (*)(d2r::AreaContext*, Unit*) noexcept;
ApplyFn g_apply = nullptr;

constexpr int32_t SlowMissiles = 87;
// srvdo 6's argument block: skill, level, frames, state, six stats, six values.
constexpr int AuraState = 0x0c;
constexpr const char* ApplyPattern =
    "40 55 53 56 41 54 41 55 41 56 41 57 48 8b ec 48 83 ec 60 4c 8b 41 18 33 c0 48 89 45 f8 "
    "4d 8d 78 10 48 8b 41 08 4d 8d 70 28";
// Seven complete push instructions, before the frame setup.
constexpr uint8_t ApplyEntry[] = {0x40, 0x55, 0x53, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};

constexpr auto Skip(int32_t state, int32_t type, uint32_t flags) noexcept -> bool {
	return state == SlowMissiles && type == d2r::rva::UnitMonster && (flags & d2r::rva::MonPrimeEvil) != 0;
}

static_assert(Skip(SlowMissiles, d2r::rva::UnitMonster, d2r::rva::MonPrimeEvil));
static_assert(!Skip(SlowMissiles, d2r::rva::UnitMonster, 0));
static_assert(!Skip(17, d2r::rva::UnitMonster, d2r::rva::MonPrimeEvil));  // Inner Sight
static_assert(!Skip(SlowMissiles, 0, d2r::rva::MonPrimeEvil));  // player

auto Apply(d2r::AreaContext* area, Unit* target) noexcept -> int32_t {
	using namespace d2r::rva;
	if (area != nullptr && area->game != nullptr && area->arg != nullptr && target != nullptr &&
	    At<int32_t>(area->arg, AuraState) == SlowMissiles && At<int32_t>(target, UnitType) == UnitMonster) {
		const auto* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::sig::MonStatsRecord)(
		    d2r::DataFlag(static_cast<d2r::Game*>(area->game)), At<int32_t>(target, UnitClassId));
		if (record != nullptr && Skip(At<int32_t>(area->arg, AuraState), At<int32_t>(target, UnitType),
		                              At<uint32_t>(record, MonFlags))) {
			return 0;
		}
	}
	return g_apply(area, target);
}

}  // namespace

auto InstallSlowMissiles(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool on = d2r::sig::MonStatsRecord.state == sigscan::State::Ok &&
	                sigscan::HookBySignature(context, "SlowMissilesApply", ApplyPattern, 0, ApplyEntry,
	                                         sizeof(ApplyEntry), &Apply, &g_apply);
	plog::Info("slow-missiles: skip prime evils %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; slow-missiles: skip prime evils %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallSlowMissiles, 430)
