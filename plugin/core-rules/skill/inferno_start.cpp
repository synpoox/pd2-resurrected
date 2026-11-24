// Inferno and Arctic Blast fire their PD2 line again when the channel goes on (#2024).
//
// 1.13c srvst 11 (D2Game +0x443f0) creates the inferno state (12) on the first start and returns; on a start with the
// state already on (the held channel's next loop) it refreshes the state and calls the flame step +0x441c0, one flame.
// PD2 points that call (+0x44537) at ProjectDiablo.dll 0x103b5090 (PD2.gpr 0x103b5020): a skill whose srvdofunc is 182
// (Inferno 41, Arctic Blast 230, the only srvst 11 rows) runs PD2's srvdo 182 (0x103b4eb0) with (game, unit, skill,
// level) there; any other skill keeps the flame.
// D2R's srvst 11 (93847 0x555300) calls 0x551b00 (game, unit, skill, level, srvmissilea): state 12 created, or
// refreshed with the frame events re-timed from frame 8 (0x4914e0), and no missile at all. So the redirect runs D2R's
// step, then, when the state was already on and the skill's srvdofunc is 182, srvdo slot 182 (W21's SrvDoChannel,
// skill_functions.cpp, read from the table so a failed W21 install leaves D2R's start alone). The start keeps D2R's
// result: 1.13c returns 1 after the call (+0x4453e), so PD2 drops srvdo 182's.
// Server only: clients draw the flames from cltdo 111, and PD2 leaves cltst 15 as 1.13c has it.
#include "common.h"
#include "../item_rules.h"
#include "registry.h"

#include "hot_log.h"
#include "log.h"
#include "pd2_engine.h"
#include "sig.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::Game;
using d2r::Unit;

// srvst 11's call to its state step: mov r9d,r15d / mov [rsp+0x20],ebx / mov r8d,r14d / mov rdx,rsi / mov rcx,rbp /
// call 0x551b00 / jmp +2 / xor eax,eax (one match in 93847, the call at +16)
constexpr const char* StartCallPattern = "45 8b cf 89 5c 24 20 45 8b c6 48 8b d6 48 8b cd e8 ?? ?? ?? ?? eb 02 33 c0";
constexpr uint32_t StartCallAt = 16;
constexpr int32_t SrvDoChannelSlot = 182;
constexpr int32_t StateInferno = 12;

using StartFn = int64_t (*)(Game*, Unit*, int32_t, int32_t, int32_t) noexcept;
using SrvDoFn = int64_t (*)(Game*, Unit*, int32_t, int32_t) noexcept;
using HasStateFn = int32_t (*)(Unit*, int32_t) noexcept;
StartFn g_start = nullptr;

auto InfernoStart(Game* game, Unit* unit, int32_t skillId, int32_t level, int32_t missile) noexcept -> int64_t {
	const bool going = unit != nullptr && d2r::Engine<HasStateFn>(d2r::sig::UnitHasState)(unit, StateInferno) != 0;
	const int64_t stock = g_start(game, unit, skillId, level, missile);
	const uint8_t* skill = going && game != nullptr ? d2r::api.getSkillRecord(d2r::DataFlag(game), skillId) : nullptr;
	if (skill == nullptr || d2r::At<uint16_t>(skill, d2r::rva::SkSrvDoFunc) != SrvDoChannelSlot) {
		return stock;
	}
	const auto channel = *reinterpret_cast<SrvDoFn*>(d2r::sig::SrvDoTable.address + SrvDoChannelSlot * 8);
	if (channel == nullptr) {
		return stock;
	}
	const int64_t ran = channel(game, unit, skillId, level);
	return stock;  // 1.13c +0x4453e returns 1 whatever the flame step did; PD2 keeps that
}

}  // namespace

auto InstallInfernoStart(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool on = d2r::sig::UnitHasState.state == sigscan::State::Ok &&
	                d2r::sig::SrvDoTable.state == sigscan::State::Ok &&
	                RedirectCall(context, StartCallPattern, StartCallAt, reinterpret_cast<uint64_t>(&InfernoStart),
	                             reinterpret_cast<uint64_t*>(&g_start));
	plog::Info("inferno-start: srvdo 182 at the channel's next start %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; inferno-start: srvdo 182 at start %s", on ? "on" : "FAILED");
	}
	return on;
}

PD2R_RULE(InstallInfernoStart, 140)
