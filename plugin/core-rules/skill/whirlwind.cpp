// Whirlwind hit rate (#2015): PD2 replaces 1.13c's hit timer (D2Game +0x26e40, called at +0x28ce4 in Whirlwind's
// do) with ProjectDiablo.dll 0x1036c780 (PD2.gpr 0x1036c710): a hit when the game frame reaches the unit skill's
// next-hit frame (or none is set), then next = frame + the running skill's Param3 (6 for Whirlwind, 1 for MonWhirlwind),
// and the loop runs 2 hits when dual-wielding, else 1; each hit picks its own target. D2R's do (srvdo 76, body
// 0x568f40) keeps 1.13c's split: a monster rolls 50% per frame and never reaches the timer; a player waits
// 0x56ac00(unit, weapon) frames (weapon speed and IAS), dual-wield the average of both hands, and runs the same
// 2-hit loop. So only that delay is replaced, for players: Param3 for each hand makes the dual-wield average
// ((p + 1) + p) / 2 = p, as in PD2. Monsters and mercs keep D2R's roll (docs/deviations.md).
#include "common.h"
#include "registry.h"

#include "pd2_engine.h"
#include "hot_log.h"
#include "log.h"
#include "sig_hook.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace {

using d2r::At;
using d2r::Unit;

// 0x56ac00 (unit, weapon) -> frames to the next hit: entry, the security cookie load, then rdx/rcx saved
constexpr const char* DelayPattern =
    "48 89 5c 24 18 55 56 57 41 56 41 57 48 81 ec 80 00 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 44 24 78 45 33 ff "
    "4c 8b f2 48 8b e9";
// mov [rsp+0x18],rbx / push rbp / push rsi / push rdi / push r14 / push r15 / sub rsp,0x80: 7 whole instructions
constexpr uint8_t DelayEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56,
                                  0x41, 0x57, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00};

using DelayFn = int32_t (*)(Unit*, Unit*) noexcept;
DelayFn g_delay = nullptr;
std::atomic<uint32_t> g_first {0}, g_second {0};

// No SDK service sets a skill's hit timer, so D2R's delay is hooked; it runs only inside Whirlwind's player branch.
auto Delay(Unit* unit, Unit* weapon) noexcept -> int32_t {
	const int32_t d2rFrames = g_delay(unit, weapon);
	if (unit == nullptr || At<int32_t>(unit, d2r::rva::UnitType) != 0) {
		return d2rFrames;
	}
	void* skill = d2r::Engine<void* (*)(Unit*) noexcept>(d2r::sig::CurrentSkill)(unit);
	const int32_t id = skill != nullptr ? d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(
	                                          d2r::sig::UnitSkillId)(skill, __FILE__, __LINE__)
	                                    : -1;
	const uint8_t* record = id >= 0 ? d2r::api.getSkillRecord(d2r::api.unitDataFlag(unit), id) : nullptr;
	const int32_t frames = record != nullptr ? At<int32_t>(record, d2r::rva::SkParam3) : 0;
	if (frames < 1) {
		return d2rFrames;
	}
	if (pd2::HotLogsOn()) {
		// the dual-wield branch asks twice in one frame (left hand, then right); a single weapon once
		thread_local Unit* lastUnit = nullptr;
		thread_local int32_t lastFrame = -1;
		const auto* game = At<const uint8_t*>(unit, d2r::rva::UnitGame);
		const int32_t frame = game != nullptr ? At<int32_t>(game, d2r::rva::GameFrame) : -1;
		const bool second = unit == lastUnit && frame == lastFrame;
		lastUnit = unit;
		lastFrame = frame;
		if (const uint32_t n = ++(second ? g_second : g_first); pd2::HotSample(n)) {
			plog::Info("whirlwind: hit at frame %d, %s, next in %d frames (skill %d Param3; D2R %d) (%u so far)", frame,
			           second ? "dual-wield second hand" : "first hand", frames, id, d2rFrames, n);
		}
	}
	return frames;
}

}  // namespace

auto InstallWhirlwind(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	const bool on = d2r::api.getSkillRecord != nullptr && d2r::api.unitDataFlag != nullptr &&
	                d2r::sig::CurrentSkill.state == sigscan::State::Ok && d2r::sig::UnitSkillId.state == sigscan::State::Ok &&
	                sigscan::HookBySignature(context, "WhirlwindHitDelay", DelayPattern, 0, DelayEntry, sizeof(DelayEntry),
	                                         &Delay, &g_delay);
	plog::Info("whirlwind: pd2 hit rate %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; whirlwind hit rate%s", on ? "" : " FAILED");
	}
	return on;
}

PD2R_RULE(InstallWhirlwind, 90)

// --- Whirlwind always spins (#2016) ----------------------------------------------------------------------------------
// PD2 swaps srvst 38's call of 1.13c's target point (+0x9fa30, called at +0x282f7) for ProjectDiablo.dll 0x1036c800
// (PD2.gpr 0x1036c790): when the path has a target unit (D2Common #10392), x and y each move one subtile on past it,
// away from the caster (sign of target - caster), two when a monster spins at a target of size above 2 (#10589), then
// D2Common #10286 sets the path's target point there and clears its target unit. No clamp to walkable ground or the
// map: the path step clips it. Its other patch (+0x28336 je -> jmp) skips the plain attack on a target in reach, which
// with the target unit cleared is never reached anyway. D2R's srvst 38 (0x56a420) and the client's cltst 31 (0x230a10)
// keep 1.13c's order: target point (SkillTargetXY / ClientSkillTargetXY), then the path's target unit, then the
// in-range attack (0x56a4f7 / 0x22f1f0). So both target-point functions are hooked, acting only for those two call
// sites (by return address), and do PD2's step with D2R's PathSetTargetXY (0x342a50, #10286's twin: +0x10/+0x12, clears
// +0x70) and UnitSize (0x34b580, #10589's twin). Every srvst 38 skill takes it, as in PD2: Whirlwind, Blade Dance,
// MonWhirlwind, UberTalicWhirlwind and Talic's Whirlwind.
namespace {

// srvst 38: lea r9,[rbp-0x25] / mov rdx,r14 / lea r8,[rbp-0x29] / mov rcx,r15 / call SkillTargetXY / test eax,eax / jz /
// mov edx,0xc
constexpr const char* ServerStartCall =
    "4c 8d 4d db 49 8b d6 4c 8d 45 d7 49 8b cf e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? ba 0c 00 00 00";
constexpr int ServerCallAt = 14;
// cltst 31: lea r8,[rbp-0x3c] / mov rcx,r14 / lea rdx,[rbp-0x40] / call ClientSkillTargetXY / test eax,eax / jz /
// mov rcx,r14 / call
constexpr const char* ClientStartCall = "4c 8d 45 c4 49 8b ce 48 8d 55 c0 e8 ?? ?? ?? ?? 85 c0 0f 84 ?? ?? ?? ?? 49 8b ce e8";
constexpr int ClientCallAt = 11;
// mov [rsp+8],rbx / mov [rsp+0x10],rsi / mov [rsp+0x18],rdi
constexpr uint8_t ServerTargetEntry[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
                                         0x24, 0x10, 0x48, 0x89, 0x7c, 0x24, 0x18};
// push rbx / push rsi / push rdi / push r14 / push r15 / sub rsp,0x70
constexpr uint8_t ClientTargetEntry[] = {0x40, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x70};

using ServerTargetFn = int32_t (*)(d2r::Game*, Unit*, int32_t*, int32_t*) noexcept;
using ClientTargetFn = int32_t (*)(Unit*, int32_t*, int32_t*) noexcept;
ServerTargetFn g_serverTarget = nullptr;
ClientTargetFn g_clientTarget = nullptr;
uint64_t g_serverReturn = 0, g_clientReturn = 0;
std::atomic<uint32_t> g_serverSpins {0}, g_clientSpins {0};

auto Sign(int32_t v) noexcept -> int32_t {
	return (v > 0) - (v < 0);
}

// PD2's 0x1036c800 after a successful target point: past the path's target unit, then the path aims at that point
auto SpinPast(Unit* unit, int32_t* x, int32_t* y, const char* side, std::atomic<uint32_t>& count) noexcept -> void {
	auto* path = At<uint8_t*>(unit, d2r::rva::UnitPath);
	Unit* target = path != nullptr ? At<Unit*>(path, d2r::rva::PathTargetUnit) : nullptr;
	int32_t ux = 0, uy = 0;
	if (target == nullptr || !d2r::UnitXY(unit, &ux, &uy)) {
		return;
	}
	const int32_t tx = *x, ty = *y;
	const int32_t type = At<int32_t>(unit, d2r::rva::UnitType);
	const int32_t steps =
	    type == d2r::rva::UnitMonster && d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::sig::UnitSize)(target) > 2 ? 2 : 1;
	*x = tx + Sign(tx - ux) * steps;
	*y = ty + Sign(ty - uy) * steps;
	d2r::Engine<void (*)(void*, int32_t, int32_t) noexcept>(d2r::sig::PathSetTargetXY)(path, *x, *y);
	if (pd2::HotSample(count)) {
		plog::Info("whirlwind: %s start spins, unit type %d at %d,%d, target type %d at %d,%d, end %d,%d (%u so far)", side,
		           type, ux, uy, At<int32_t>(target, d2r::rva::UnitType), tx, ty, *x, *y, count.load());
	}
}

auto ServerTarget(d2r::Game* game, Unit* unit, int32_t* x, int32_t* y) noexcept -> int32_t {
	const int32_t ok = g_serverTarget(game, unit, x, y);
	if (ok != 0 && reinterpret_cast<uint64_t>(_ReturnAddress()) == g_serverReturn) {
		SpinPast(unit, x, y, "server", g_serverSpins);
	}
	return ok;
}

auto ClientTarget(Unit* unit, int32_t* x, int32_t* y) noexcept -> int32_t {
	const int32_t ok = g_clientTarget(unit, x, y);
	if (ok != 0 && reinterpret_cast<uint64_t>(_ReturnAddress()) == g_clientReturn) {
		SpinPast(unit, x, y, "client", g_clientSpins);
	}
	return ok;
}

// The call site's return address, when the call at `callAt` in the one match of `pattern` goes to `callee`; else 0
auto CallReturn(const D2RL::PluginContext* context, const char* pattern, int callAt, uint64_t callee) noexcept -> uint64_t {
	uint64_t rva = 0;
	if (sigscan::ScanUnique(context->exeBase, pattern, callAt, &rva) != 1) {
		return 0;
	}
	int32_t rel = 0;
	std::memcpy(&rel, reinterpret_cast<const void*>(context->exeBase + rva + 1), sizeof(rel));
	return rva + 5 + rel == callee ? context->exeBase + rva + 5 : 0;
}

}  // namespace

auto InstallWhirlwindStart(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> bool {
	using sigscan::State;
	bool on = d2r::sig::SkillTargetXY.state == State::Ok && d2r::sig::ClientSkillTargetXY.state == State::Ok &&
	          d2r::sig::PathSetTargetXY.state == State::Ok && d2r::sig::UnitSize.state == State::Ok;
	if (on) {
		g_serverReturn = CallReturn(context, ServerStartCall, ServerCallAt, d2r::sig::SkillTargetXY.rva);
		g_clientReturn = CallReturn(context, ClientStartCall, ClientCallAt, d2r::sig::ClientSkillTargetXY.rva);
		// No SDK service sets a skill's target point, so the two target-point functions are hooked; only the
		// Whirlwind-start call sites act. Both call sites are scanned first; if the client hook then fails, the server
		// hook stays live while the line says FAILED.
		on = g_serverReturn != 0 && g_clientReturn != 0 &&
		     context->InstallInlineHook(d2r::sig::SkillTargetXY.rva, ServerTargetEntry, sizeof(ServerTargetEntry),
		                                &ServerTarget, &g_serverTarget) &&
		     context->InstallInlineHook(d2r::sig::ClientSkillTargetXY.rva, ClientTargetEntry, sizeof(ClientTargetEntry),
		                                &ClientTarget, &g_clientTarget);
	}
	plog::Info("whirlwind: pd2 always spin %s", on ? "on" : "FAILED");
	const size_t used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; whirlwind always spin%s", on ? "" : " FAILED");
	}
	return on;
}

PD2R_RULE(InstallWhirlwindStart, 110)
