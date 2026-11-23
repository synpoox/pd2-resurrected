// Whirlwind hit rate (#2015): PD2 replaces 1.13c's hit timer (D2Game +0x26e40, called at +0x28ce4 in Whirlwind's
// do) with ProjectDiablo.dll 0x1036c780 (PD2.gpr 0x1036c710): a hit when the game frame reaches the unit skill's
// next-hit frame (or none is set), then next = frame + the running skill's Param3 (6 for Whirlwind, 1 for MonWhirlwind),
// and the loop runs 2 hits when dual-wielding, else 1; each hit picks its own target. D2R's do (srvdo 76, body
// 0x568f40) keeps 1.13c's split: a monster rolls 50% per frame and never reaches the timer; a player waits
// 0x56ac00(unit, weapon) frames (weapon speed and IAS), dual-wield the average of both hands, and runs the same
// 2-hit loop. So only that delay is replaced, for players: Param3 for each hand makes the dual-wield average
// ((p + 1) + p) / 2 = p, as in PD2. Monsters and mercs keep D2R's roll (docs/deviations.md).
#include "common.h"

#include "pd2_engine.h"
#include "hot_log.h"
#include "log.h"
#include "sig_hook.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
