// PD2 server skill "do" functions (skills.txt srvdofunc) in D2R's free srvdofunc slots (rva::SrvDoSlots).
//
// The server calls slot `srvdofunc` of rva::SrvDoTable at the cast frame as
//     int64_t fn(Game* game, Unit* caster, int32_t skillId, int32_t skillLevel)   -> non-zero = the skill ran.
// Each slot is written with PatchWriteU64 and an expected value of eight zero bytes, so a slot D2R or another plugin
// already uses is refused and logged, never overwritten. Slots keep PD2's own numbers where free (docs/core-rules.md).
#include "skill_functions.h"

#include "d2r.h"
#include "log.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

using SrvDoFn = int64_t (*)(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept;

std::atomic<uint32_t> g_splitThrowCasts {0};

// Split Throw (PD2 srvdofunc 189): calc1 missiles of srvmissilea in a fan towards the target point.
// PD2's calc1 is par1+min(lvl/5,par2); calc2-4 (activation frame, triggered, ED%) are not read here.
// shortcut: always srvmissilea and a 30-degree spread; PD2 picks a/b/c by weapon type and its spread is in
// ProjectDiablo.dll, not its data. Upgrade when the weapon-type getter is found (FINDINGS.md, "Not matched").
auto SrvDoSplitThrow(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);  // every D2R do function that creates missiles does this first

	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0) {
		return 1;  // no target point: the cast happened, nothing to create
	}
	if (!d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, d2r::rva::SkSrvMissileA);
	if (!d2r::ValidMissile(game, missile)) {
		plog::Warn("split throw: skill %d has no valid srvmissilea (row %d)", skillId, missile);
		return 0;
	}
	int32_t count = d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc1, skillId, level);
	count = count < 1 ? 1 : (count > 64 ? 64 : count);  // a broken formula must not spawn thousands of missiles
	constexpr int32_t Spread = 30;

	// Aim direction, normalised to 20 subtiles so the rotated points stay precise for close targets.
	double dx = tx - ux, dy = ty - uy;
	const double length = std::sqrt(dx * dx + dy * dy);
	if (length < 0.5) {
		dx = 1.0;
		dy = 0.0;
	} else {
		dx /= length;
		dy /= length;
	}
	constexpr double AimLength = 20.0;
	constexpr double Pi = 3.14159265358979323846;

	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x820;  // start at the origin unit, fly to targetX/targetY
	params.owner = unit;
	params.origin = unit;
	params.missile = missile;
	params.skill = skillId;
	params.skillLevel = level;

	int32_t created = 0;
	const double step = count > 1 ? static_cast<double>(Spread) / (count - 1) : 0.0;
	for (int32_t i = 0; i < count; ++i) {
		const double angle = (count > 1 ? -Spread / 2.0 + step * i : 0.0) * Pi / 180.0;
		const double c = std::cos(angle), s = std::sin(angle);
		params.targetX = ux + static_cast<int32_t>(std::lround((dx * c - dy * s) * AimLength));
		params.targetY = uy + static_cast<int32_t>(std::lround((dx * s + dy * c) * AimLength));
		created += d2r::api.createMissile(game, &params) != nullptr ? 1 : 0;
	}
	if (plog::Sample(g_splitThrowCasts)) {
		plog::Info("split throw: skill %d level %d: %d of %d missiles (row %d), cast %u", skillId, level, created, count,
		           missile, g_splitThrowCasts.load());
	}
	return 1;
}

struct SlotFunction {
	int slot;  // srvdofunc value in skills.txt
	SrvDoFn function;
	const char* name;
};

constexpr SlotFunction Functions[] = {
	{189, SrvDoSplitThrow, "split throw"},
};

}  // namespace

auto InstallSkillFunctions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	static constexpr uint8_t Empty[8] = {};  // the slot must still be a null pointer
	int installed = 0;
	size_t used = 0;
	for (const SlotFunction& f : Functions) {
		bool ok = f.slot >= d2r::rva::SrvDoFirstFree && f.slot < d2r::rva::SrvDoSlots;
		if (ok) {
			const uint64_t rva = d2r::rva::SrvDoTable + static_cast<uint64_t>(f.slot) * 8;
			ok = context->PatchWriteU64(rva, Empty, sizeof(Empty), reinterpret_cast<uint64_t>(f.function));
		}
		installed += ok ? 1 : 0;
		if (used < reportSize) {
			const int n = std::snprintf(report + used, reportSize - used, "%s srvdofunc %d %s%s", used ? ";" : "",
			                            f.slot, f.name, ok ? "" : " FAILED (slot not empty or out of range)");
			used += n > 0 ? static_cast<size_t>(n) : 0;
		}
	}
	return installed;
}

auto SkillFunctionCount() noexcept -> int {
	return static_cast<int>(sizeof(Functions) / sizeof(Functions[0]));
}

auto SkillFunctionCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "split throw casts %u", g_splitThrowCasts.load());
}
