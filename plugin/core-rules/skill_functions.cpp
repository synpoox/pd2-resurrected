// PD2 skill functions (skills.txt srvdofunc / cltdofunc / srvstfunc, missiles.txt pSrvDoFunc / pSrvHitFunc /
// pCltDoFunc) in D2R's free table slots.
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

// --- Split Throw's start buff (PD2 srvstfunc 79) ---------------------------------------------------------------------
std::atomic<uint32_t> g_tempBuffs {0};

// The skill's aurastate with its aurastat1-6 for a short time from the start frame, built the way Frozen Armor's
// srvdo 18 (0x553e60) builds its state list. PD2 gives Split Throw temp_skill_buff + attackrate (par4).
// shortcut: approximation from PD2's data, the length is a fixed TempBuffFrames because PD2's row has no auralencalc;
// upgrade when PD2's srvst 79 is read from ProjectDiablo.dll.
constexpr int32_t TempBuffFrames = 25;

auto SrvStTempBuff(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	const int32_t state = At<int16_t>(skill, SkAuraState);
	if (state <= 0) {
		return 1;  // no aurastate: the cast goes on without a buff
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(RemoveSkillState)(unit, state, 1);
	d2r::StateListRequest request {unit, unit, skillId, level, TempBuffFrames, -1, 0, state,
	                               d2r::Engine<void (*)()>(SkillStateExpired)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list == nullptr) {
		return 1;
	}
	using AuraStatsFn = void (*)(Unit*, void*, const uint8_t*, int32_t, int32_t) noexcept;
	d2r::Engine<AuraStatsFn>(AddAuraStats)(unit, list, skill, skillId, level);
	d2r::Engine<AuraStatsFn>(AddAuraStatsB)(unit, list, skill, skillId, level);
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	setStat(dataFlag, list, 0x15e, skillId, 0);  // stats 350/351: which skill and level made the list
	setStat(dataFlag, list, 0x15f, level, 0);
	const int32_t key = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t) noexcept>(StateListKey)(
		dataFlag, list, d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(unit));
	setStat(dataFlag, list, 0xca, key, 0);
	using SendFn = void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t, const void*, const void*,
	                        int32_t) noexcept;
	d2r::Engine<SendFn>(SendSkillState)(game, unit, skillId, level, key, 1, state, skill + 0xa4, skill + 0xac, 4);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(unit, state, 1);
	if (plog::Sample(g_tempBuffs)) {
		plog::Info("temp buff: skill %d level %d state %d for %d frames, cast %u", skillId, level, state, TempBuffFrames,
		           g_tempBuffs.load());
	}
	return 1;
}

// --- Return missiles (PD2 missile pSrvHitFunc 65, pSrvDoFunc 38 / pCltDoFunc 70 on the *return rows) ---------------
using MissileSrvDoFn = int64_t (*)(Game* game, Unit* missile) noexcept;
using MissileSrvHitFn = void (*)(Game* game, Unit* missile, Unit* hit) noexcept;
using MissileCltDoFn = void (*)(Unit* missile, int32_t a2) noexcept;

std::atomic<uint32_t> g_returnSpawns {0};
std::atomic<uint32_t> g_returnsCaught {0};

// shit 65: on a hit, start HitSubMissile1-4 (the <name>return row) at the hit missile, as D2R's srvhit 4 does.
// shortcut: approximation from PD2's data, srvhit 4 also explodes the sub-missile when the parent row has an
// explosion; PD2's splitthrow rows have none. Upgrade when PD2's shit 65 is read from ProjectDiablo.dll.
void SrvHitSpawnReturn(Game* game, Unit* missile, Unit* hit) noexcept {
	if (game == nullptr || missile == nullptr) {
		return;
	}
	d2r::Engine<MissileSrvHitFn>(d2r::rva::SrvHitSubMissiles)(game, missile, hit);
	if (plog::Sample(g_returnSpawns)) {
		plog::Info("return missile: spawned on hit, hit %u", g_returnSpawns.load());
	}
}

constexpr int32_t ReturnSteerFrames = 2;  // re-aim every 2nd frame; Guided Arrow steers on its own param interval
constexpr int32_t ReturnCatchDistance = 2;  // subtiles from the thrower that count as caught

// Point the missile's path at `owner` and re-aim it every ReturnSteerFrames frames. Shared by both halves.
void SteerHome(Unit* missile, Unit* owner, int32_t framesLeft) noexcept {
	using namespace d2r::rva;
	uint8_t* path = At<uint8_t*>(missile, UnitPath);
	if (path == nullptr) {
		return;
	}
	if (At<Unit*>(path, 0x70) != owner) {
		d2r::Engine<void (*)(uint8_t*, Unit*) noexcept>(PathSetTargetUnit)(path, owner);
	}
	if (framesLeft % ReturnSteerFrames == 0) {
		d2r::Engine<void (*)(uint8_t*, Unit*, int32_t, int32_t) noexcept>(PathSteer)(path, missile, 0, 1);
	}
}

// sdo 46 (PD2 sdo 38): fly back to the thrower and end on reaching them; otherwise the default missile frame.
// shortcut: approximation from PD2's data (Guided Arrow's steering, aimed at the owner); PD2's catch distance and
// steering rate are guesses. Upgrade when PD2's sdo 38 is read from ProjectDiablo.dll.
auto SrvDoReturn(Game* game, Unit* missile) noexcept -> int64_t {
	using namespace d2r::rva;
	const auto fallback = d2r::Engine<MissileSrvDoFn>(MissileSrvDoDefault);
	Unit* owner = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(MissileOwner)(game, missile);
	if (owner == nullptr) {
		return fallback(game, missile);
	}
	if (d2r::Engine<int32_t (*)(Unit*, Unit*) noexcept>(UnitDistance)(missile, owner) <= ReturnCatchDistance) {
		if (plog::Sample(g_returnsCaught)) {
			plog::Info("return missile: caught by the thrower, catch %u", g_returnsCaught.load());
		}
		return d2r::Engine<int64_t (*)(Game*, Unit*, Unit*, int32_t) noexcept>(MissileSrvEnd)(game, missile, nullptr, 1);
	}
	SteerHome(missile, owner, d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileSrvFrames)(missile));
	return fallback(game, missile);
}

// cdo 78 (PD2 cdo 70): the client's copy steers the same way; the server ends it.
void CltDoReturn(Unit* missile, int32_t a2) noexcept {
	using namespace d2r::rva;
	Unit* owner = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientMissileOwner)(missile);
	if (owner != nullptr) {
		SteerHome(missile, owner, d2r::Engine<int32_t (*)(Unit*) noexcept>(MissileCltFrames)(missile));
	}
	d2r::Engine<MissileCltDoFn>(MissileCltDoDefault)(missile, a2);
}

struct Table {
	const char* name;  // the skills.txt column
	uint64_t rva;
	int slots;
	int firstFree;
};
constexpr Table SrvDo {"srvdofunc", d2r::rva::SrvDoTable, d2r::rva::SrvDoSlots, d2r::rva::SrvDoFirstFree};
constexpr Table CltDo {"cltdofunc", d2r::rva::CltDoTable, d2r::rva::CltDoSlots, d2r::rva::CltDoFirstFree};
constexpr Table SrvSt {"srvstfunc", d2r::rva::SrvStTable, d2r::rva::SrvStSlots, d2r::rva::SrvStFirstFree};
constexpr Table MissileSrvDo {"pSrvDoFunc", d2r::rva::MissileSrvDoTable, d2r::rva::MissileSrvDoSlots,
                              d2r::rva::MissileSrvDoFirstFree};
constexpr Table MissileSrvHit {"pSrvHitFunc", d2r::rva::MissileSrvHitTable, d2r::rva::MissileSrvHitSlots,
                               d2r::rva::MissileSrvHitFirstFree};
constexpr Table MissileCltDo {"pCltDoFunc", d2r::rva::MissileCltDoTable, d2r::rva::MissileCltDoSlots,
                              d2r::rva::MissileCltDoFirstFree};

struct SlotFunction {
	const Table& table;
	int slot;  // the skills.txt value
	const void* function;
	const char* name;
};

const SlotFunction Functions[] = {
	{SrvDo, 189, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoSplitThrow)), "split throw"},
	{CltDo, 118, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltDoSplitThrow)), "split throw"},
	{SrvSt, 79, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvStTempBuff)), "temp buff"},
	// PD2's return rows use sdo 38 / cdo 70, which D2R fills; ours take the first free slots and the data points there.
	{MissileSrvHit, 65, reinterpret_cast<const void*>(static_cast<MissileSrvHitFn>(SrvHitSpawnReturn)), "return spawn"},
	{MissileSrvDo, 46, reinterpret_cast<const void*>(static_cast<MissileSrvDoFn>(SrvDoReturn)), "return"},
	{MissileCltDo, 78, reinterpret_cast<const void*>(static_cast<MissileCltDoFn>(CltDoReturn)), "return"},
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
	std::snprintf(out, size, "split throw casts %u, client %u, temp buffs %u, returns spawned %u, caught %u",
	              g_splitThrowCasts.load(), g_splitThrowClientCasts.load(), g_tempBuffs.load(), g_returnSpawns.load(),
	              g_returnsCaught.load());
}
