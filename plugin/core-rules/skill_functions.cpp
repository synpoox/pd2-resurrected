// PD2 skill "do" functions (skills.txt srvdofunc / cltdofunc) in D2R's free do-table slots.
//
// The server calls slot `srvdofunc` of rva::SrvDoTable at the cast frame as
//     int64_t fn(Game* game, Unit* caster, int32_t skillId, int32_t skillLevel)   -> non-zero = the skill ran.
// The client calls slot `cltdofunc` of rva::CltDoTable as
//     int32_t fn(Unit* caster, int32_t skillId, int32_t skillLevel)               -> non-zero plays the do sound.
// Each slot is written with PatchWriteU64 and an expected value of eight zero bytes, so a slot D2R or another plugin
// already uses is refused and logged, never overwritten. Slots keep PD2's own numbers where free (docs/core-rules.md).
#include "skill_functions.h"

#include "d2r.h"
#include "log.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

using SrvDoFn = int64_t (*)(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept;
using CltDoFn = int32_t (*)(Unit* unit, int32_t skillId, int32_t level) noexcept;

std::atomic<uint32_t> g_splitThrowCasts {0};
std::atomic<uint32_t> g_splitThrowClientCasts {0};

constexpr uint32_t NotTriggering = 0x20000;  // Multiple Shot sets it on the outer missiles, the ones calc3 leaves out

// Multiple Shot's missile layout (D2R srvdo 8 at 0x557840, cltdo 17 at 0x2af010): `count` missiles fly from the
// caster to points one small step apart on a line through the target, at right angles to the aim. The middle
// `triggered` ones (0 = all) trigger hits; the rest carry NotTriggering. The two vector steps are D2Common's
// 0x3376d0 (scale a short vector up) and 0x337750 (turn it 90 degrees and shrink it to length^2 <= 3).
template <typename Create>
auto LineSpread(int32_t ux, int32_t uy, int32_t tx, int32_t ty, int32_t count, int32_t triggered, Create create)
	noexcept -> int32_t {
	int32_t dx = tx - ux, dy = ty - uy;
	if (dx * dx + dy * dy < 4) {
		dx *= 4;
		dy *= 4;
	}
	if (dx * dx + dy * dy < 16) {
		dx *= 2;
		dy *= 2;
	}
	int32_t sx = dy, sy = -dx;
	while (sx * sx + sy * sy > 3) {
		sx /= 2;
		sy /= 2;
	}
	tx -= sx * count / 2;
	ty -= sy * count / 2;
	if (triggered <= 0 || triggered > count) {
		triggered = count;
	}
	const int32_t outer = (count - triggered) / 2;
	int32_t created = 0;
	for (int32_t i = 0; i < count; ++i, tx += sx, ty += sy) {
		created += create(tx, ty, i < outer || i >= outer + triggered) ? 1 : 0;
	}
	return created;
}

// Which of the skill's missile columns a/b/c (0-2) the caster's weapon picks; the server and client use the same one.
auto MissileColumn(Unit* unit, int32_t skillId, const uint8_t* skill) noexcept -> int {
	const int32_t row = d2r::api.pickSkillMissile(unit, skillId);
	for (int column = 1; column < 3; ++column) {
		if (row >= 0 && row == At<int16_t>(skill, d2r::rva::SkSrvMissileA + column * 2)) {
			return column;
		}
	}
	return 0;
}

auto ClampCount(int32_t count) noexcept -> int32_t {
	return count < 1 ? 1 : (count > 64 ? 64 : count);  // a broken formula must not spawn thousands of missiles
}

// Split Throw (PD2 srvdofunc 189): calc1 missiles of srvmissile a/b/c (by weapon) in Multiple Shot's line, calc2
// activation frame, calc3 triggering missiles. PD2's calc1 is par1+min(lvl/5,par2).
// shortcut: calc4 (PD2's ED%) is not applied; Multiple Shot has no damage-bonus field to copy. Upgrade when PD2's
// srvdo 189 is read from ProjectDiablo.dll.
auto SrvDoSplitThrow(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
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
	const int column = MissileColumn(unit, skillId, skill);
	const int32_t missile = At<int16_t>(skill, d2r::rva::SkSrvMissileA + column * 2);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		plog::Warn("split throw: skill %d has no valid srvmissile column %d (row %d)", skillId, column, missile);
		return 0;
	}
	const int32_t count = ClampCount(d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc1, skillId, level));

	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.owner = unit;
	params.origin = unit;
	params.missile = missile;
	params.skill = skillId;
	params.skillLevel = level;
	params.activation = d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc2, skillId, level);
	const int32_t triggered = d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc3, skillId, level);
	const int32_t created = LineSpread(ux, uy, tx, ty, count, triggered, [&](int32_t x, int32_t y, bool outer) {
		params.flags = 0x820 | (outer ? NotTriggering : 0);  // start at the origin unit, fly to targetX/targetY
		params.targetX = x;
		params.targetY = y;
		return d2r::api.createMissile(game, &params) != nullptr;
	});
	if (plog::Sample(g_splitThrowCasts)) {
		plog::Info("split throw: skill %d level %d: %d of %d missiles (row %d), cast %u", skillId, level, created, count,
		           missile, g_splitThrowCasts.load());
	}
	return 1;
}

// Split Throw client half (PD2 cltdofunc 118): the same missiles from cltmissile a/b/c, so the caster sees them
// without the server sending each one (missiles.txt ClientSend).
auto CltDoSplitThrow(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);

	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (d2r::api.clientSkillTargetXY(unit, &tx, &ty) == 0 || !d2r::UnitXY(unit, &ux, &uy)) {
		return 1;
	}
	const int column = MissileColumn(unit, skillId, skill);
	const int32_t missile = At<int16_t>(skill, d2r::rva::SkCltMissileA + column * 2);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		plog::Warn("split throw client: skill %d has no valid cltmissile column %d (row %d)", skillId, column, missile);
		return 0;
	}
	const int32_t count = ClampCount(d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::rva::SkCalc1),
	                                                         skillId, level));

	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x820;  // cltdo 17 sets no NotTriggering: hits are the server's
	params.owner = unit;
	params.origin = unit;
	params.missile = missile;
	params.skill = skillId;
	params.skillLevel = level;
	params.activation = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::rva::SkCalc2), skillId, level);
	const int32_t created = LineSpread(ux, uy, tx, ty, count, 0, [&](int32_t x, int32_t y, bool) {
		params.targetX = x;
		params.targetY = y;
		return d2r::api.clientCreateMissile(&params, 0) != nullptr;
	});
	if (plog::Sample(g_splitThrowClientCasts)) {
		plog::Info("split throw client: skill %d level %d: %d of %d missiles (row %d), cast %u", skillId, level, created,
		           count, missile, g_splitThrowClientCasts.load());
	}
	return 1;
}

struct Table {
	const char* name;  // the skills.txt column
	uint64_t rva;
	int slots;
	int firstFree;
};
constexpr Table SrvDo {"srvdofunc", d2r::rva::SrvDoTable, d2r::rva::SrvDoSlots, d2r::rva::SrvDoFirstFree};
constexpr Table CltDo {"cltdofunc", d2r::rva::CltDoTable, d2r::rva::CltDoSlots, d2r::rva::CltDoFirstFree};

struct SlotFunction {
	const Table& table;
	int slot;  // the skills.txt value
	const void* function;
	const char* name;
};

const SlotFunction Functions[] = {
	{SrvDo, 189, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoSplitThrow)), "split throw"},
	{CltDo, 118, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltDoSplitThrow)), "split throw"},
};

}  // namespace

auto InstallSkillFunctions(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int {
	static constexpr uint8_t Empty[8] = {};  // the slot must still be a null pointer
	int installed = 0;
	size_t used = 0;
	for (const SlotFunction& f : Functions) {
		bool ok = f.slot >= f.table.firstFree && f.slot < f.table.slots;
		if (ok) {
			const uint64_t rva = f.table.rva + static_cast<uint64_t>(f.slot) * 8;
			ok = context->PatchWriteU64(rva, Empty, sizeof(Empty), reinterpret_cast<uint64_t>(f.function));
		}
		installed += ok ? 1 : 0;
		if (used < reportSize) {
			const int n = std::snprintf(report + used, reportSize - used, "%s %s %d %s%s", used ? ";" : "",
			                            f.table.name, f.slot, f.name, ok ? "" : " FAILED (slot not empty or out of range)");
			used += n > 0 ? static_cast<size_t>(n) : 0;
		}
	}
	return installed;
}

auto SkillFunctionCount() noexcept -> int {
	return static_cast<int>(sizeof(Functions) / sizeof(Functions[0]));
}

auto SkillFunctionCounters(char* out, size_t size) noexcept -> void {
	std::snprintf(out, size, "split throw casts %u, client %u", g_splitThrowCasts.load(),
	              g_splitThrowClientCasts.load());
}
