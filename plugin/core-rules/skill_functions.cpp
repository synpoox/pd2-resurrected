// PD2 skill functions (skills.txt srvdofunc / cltdofunc / srvstfunc / cltstfunc) in D2R's free table slots, and the
// install of those and of the missile functions (missile_functions.cpp).
//
// The server calls slot `srvdofunc` of rva::SrvDoTable at the cast frame as
//     int64_t fn(Game* game, Unit* caster, int32_t skillId, int32_t skillLevel)   -> non-zero = the skill ran.
// The client calls slot `cltdofunc` of rva::CltDoTable as
//     int32_t fn(Unit* caster, int32_t skillId, int32_t skillLevel)               -> non-zero plays the do sound.
// Start functions (`srvstfunc` in rva::SrvStTable, `cltstfunc` in rva::CltStTable) take the same arguments at the
// start frame; zero refuses the cast. srvdofunc 190 is a router: one slot shared by several PD2 do functions, picked
// by skill id.
// PD2's channel take-over is an inline hook on D2R's mode gate for a busy player (BusyModeGate).
// Each slot is written with PatchWriteU64 and an expected value of eight zero bytes, so a slot D2R or another plugin
// already uses is refused and logged, never overwritten. Slots keep PD2's own numbers where free (docs/core-rules.md).
#include "skill_functions.h"

#include "d2r.h"
#include "log.h"
#include "missile_functions.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <mutex>

namespace {

using d2r::At;
using d2r::Game;
using d2r::Unit;

using SrvDoFn = int64_t (*)(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept;
using CltDoFn = int32_t (*)(Unit* unit, int32_t skillId, int32_t level) noexcept;

std::atomic<uint32_t> g_splitThrowCasts {0};
std::atomic<uint32_t> g_splitThrowClientCasts {0};

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
		params.flags = d2r::rva::MissileFromOrigin | (outer ? d2r::rva::MissileNotTriggering : 0);
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
	params.flags = d2r::rva::MissileFromOrigin;  // cltdo 17 sets no NotTriggering: hits are the server's
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

// --- Double Throw (PD2 srvdofunc 154 -> 183, cltdofunc 98 -> 116) ------------------------------------------------
std::atomic<uint32_t> g_doubleThrowCasts {0};
std::atomic<uint32_t> g_doubleThrowClientCasts {0};

// PD2's column for the thrown weapon: throwing axe a, throwing knife b, javelin, glaive and pilum c, else a. Missile
// rows by weapons.txt missiletype, the same in D2R and PD2 (javelin 1, throwaxe 35, throwknife 36, glaive 37, pilum 371).
auto WeaponColumn(Unit* weapon) noexcept -> int {
	switch (d2r::Engine<int32_t (*)(Unit*) noexcept>(d2r::rva::ItemMissileType)(weapon)) {
	case 35: return 0;
	case 36: return 1;
	case 1: case 37: case 371: return 2;
	default: return 0;
	}
}

// PD2 srvdo 154 (ProjectDiablo.dll 0x103b1590): one missile of srvmissile a/b/c by the weapon, thrown the way D2R's
// Double Throw throws (srvdo 74's 0x4333f0), with damagepercent = calc1 and calc2 bounces (missiles.txt pSrvHitFunc 30
// -> 66 spends them). A monster (UberMadawcDoubleThrow, the same missile in all three columns) throws column a.
auto SrvDoDoubleThrow(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	int column = 0;
	if (At<int32_t>(unit, UnitType) == 0) {
		Unit* weapon = d2r::Engine<Unit* (*)(Unit*) noexcept>(UnitWeapon)(unit);
		if (weapon == nullptr) {
			return 0;
		}
		column = WeaponColumn(weapon);
	}
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA + column * 2);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		return 0;
	}
	using ThrowFn = Unit* (*)(Game*, int32_t, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
	                          int32_t) noexcept;
	const int32_t key = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(unit);
	Unit* thrown = d2r::Engine<ThrowFn>(ThrowSkillMissile)(game, missile, unit, skillId, level, key, 0, 0, 0, 0, 1);
	if (thrown == nullptr) {
		return 0;
	}
	const int32_t damage = d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	const int32_t bounces = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(thrown, StatDamagePercent, damage, 0);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileBounces)(thrown, bounces);
	if (plog::Sample(g_doubleThrowCasts)) {
		plog::Info("double throw: skill %d level %d: missile %d (column %d), damage%% %d, bounces %d, cast %u", skillId,
		           level, missile, column, damage, bounces, g_doubleThrowCasts.load());
	}
	return 1;
}

// PD2 cltdo 98 (0x103ab470): the same missile from cltmissile a/b/c with calc2 bounces, so the client's Chain
// Lightning hit (missiles.txt pCltHitFunc 16) can draw each bounce.
auto CltDoDoubleThrow(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	int column = 0;  // a monster throws column a, as on the server
	if (At<int32_t>(unit, UnitType) == 0) {
		Unit* weapon = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientUnitWeapon)(unit);
		if (weapon == nullptr) {
			return 0;
		}
		column = WeaponColumn(weapon);
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const int32_t missile = At<int16_t>(skill, SkCltMissileA + column * 2);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		return 0;
	}
	const int32_t key = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(unit);
	Unit* thrown = d2r::Engine<Unit* (*)(Unit*, int32_t, int32_t, int32_t, int32_t) noexcept>(ClientThrowSkillMissile)(
		unit, missile, skillId, level, key);
	if (thrown == nullptr) {
		return 0;
	}
	const int32_t bounces = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, SkCalc2), skillId, level);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileBounces)(thrown, bounces);
	if (plog::Sample(g_doubleThrowClientCasts)) {
		plog::Info("double throw client: skill %d level %d: missile %d, bounces %d, cast %u", skillId, level, missile,
		           bounces, g_doubleThrowClientCasts.load());
	}
	return 1;
}

// --- Split Throw's start buff (PD2 srvstfunc 79)---------------------------------------------------------------------
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
	setStat(dataFlag, list, StatListSkill, skillId, 0);
	setStat(dataFlag, list, StatListLevel, level, 0);
	const int32_t key = d2r::Engine<int32_t (*)(uint32_t, void*, int32_t) noexcept>(StateListKey)(
		dataFlag, list, d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(unit));
	setStat(dataFlag, list, StatListKey, key, 0);
	using SendFn = void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t, const void*, const void*,
	                        int32_t) noexcept;
	d2r::Engine<SendFn>(SendSkillState)(game, unit, skillId, level, key, 1, state, skill + SkSendStateA,
	                                    skill + SkSendStateB, 4);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(unit, state, 1);
	if (plog::Sample(g_tempBuffs)) {
		// aurastat1 and its calc value as the skill computes them, so the check can compare them with PD2's par4
		plog::Info("temp buff: skill %d level %d state %d for %d frames, stat %d +%d, cast %u", skillId, level, state,
		           TempBuffFrames, At<int16_t>(skill, SkAuraStat1),
		           d2r::EvalCalc(game, unit, skill, SkAuraStatCalc1, skillId, level), g_tempBuffs.load());
	}
	return 1;
}

// --- W10 Barbarian group (docs/research/w10-barbarian.md) ----------------------------------------------------------
std::atomic<uint32_t> g_finds {0};
std::atomic<uint32_t> g_findsRefused {0};
std::atomic<uint32_t> g_bashHits {0};
std::atomic<uint32_t> g_bashKnockbacks {0};
std::atomic<uint32_t> g_splashStarts {0};
std::atomic<uint32_t> g_routerMisses {0};

constexpr int32_t FindPotion = 131;

// D2R's own function in table `table` slot `slot`.
template <typename Fn>
auto VanillaSlot(uint64_t table, int slot) noexcept -> Fn {
	return *reinterpret_cast<Fn*>(d2r::imageBase + table + static_cast<uint64_t>(slot) * 8);
}

// PD2's Find guard: a corpse whose owner is a player (a summon or minion) can't be searched. D2R's unowned units
// carry id 0 or -1 here; both pass.
auto OwnedByPlayer(Unit* target) noexcept -> bool {
	const uint32_t id = At<uint32_t>(target, d2r::rva::UnitOwnerId);
	return At<int32_t>(target, d2r::rva::UnitOwnerType) == 0 && id != 0 && id != UINT32_MAX;
}

// Find Potion / Find Item start (PD2 srvstfunc 70, here 82): the guard, then D2R's own srvst 33 / 34.
// shortcut: PD2's 46 bosses whose corpses can't be searched are not listed; they are map and uber bosses, which come
// with the map plan (docs/unported.md).
auto SrvStFind(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::rva::SkillTargetUnit)(game, unit);
	if (target == nullptr || OwnedByPlayer(target)) {
		g_findsRefused.fetch_add(1);
		return 0;
	}
	if (plog::Sample(g_finds)) {
		plog::Info("find: skill %d on unit type %d owner %d/%u, cast %u", skillId,
		           At<int32_t>(target, d2r::rva::UnitType), At<int32_t>(target, d2r::rva::UnitOwnerType),
		           At<uint32_t>(target, d2r::rva::UnitOwnerId), g_finds.load());
	}
	const int slot = skillId == FindPotion ? d2r::rva::SrvStFindPotion : d2r::rva::SrvStFindItem;
	return VanillaSlot<SrvDoFn>(d2r::rva::SrvStTable, slot)(game, unit, skillId, level);
}

// Client half (PD2 cltstfunc 55, here 62): the same guard before D2R's cltst 26 / 28.
auto CltStFind(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	Unit* target = d2r::Engine<Unit* (*)(Unit*) noexcept>(d2r::rva::ClientSkillTargetUnit)(unit);
	if (target == nullptr || OwnedByPlayer(target)) {
		return 0;
	}
	const int slot = skillId == FindPotion ? d2r::rva::CltStFindPotion : d2r::rva::CltStFindItem;
	return VanillaSlot<CltDoFn>(d2r::rva::CltStTable, slot)(unit, skillId, level);
}

// Bash (PD2 srvdofunc 158): a hit knocks back half the time (PD2's Bash row has no ResultFlags). The queued hit's
// knockback bit is set from the roll, then D2R's melee do function applies it.
// shortcut: PD2 rolls after the first apply and only for targets with flag 0x8000; this rolls before, for every
// target. Upgrade if knockback on bosses differs from PD2 in play.
auto SrvDoBash(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::rva::SkillTargetUnit)(game, unit);
	if (target != nullptr) {
		auto* damage = d2r::Engine<uint8_t* (*)(Unit*, Unit*) noexcept>(d2r::rva::QueuedHit)(unit, target);
		if (damage != nullptr) {
			auto& result = *reinterpret_cast<uint16_t*>(damage + d2r::rva::DamageResult);
			result &= static_cast<uint16_t>(~d2r::rva::ResultKnockback);
			if (result & d2r::rva::ResultHit) {
				g_bashHits.fetch_add(1);
				uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(d2r::rva::UnitSeed)(unit);
				if (d2r::RandomBelow(seed, 100) < 50) {
					result |= d2r::rva::ResultKnockback;
					g_bashKnockbacks.fetch_add(1);
				}
			}
		}
	}
	return VanillaSlot<SrvDoFn>(d2r::rva::SrvDoTable, d2r::rva::SrvDoMelee)(game, unit, skillId, level);
}

struct StatValue {
	int32_t stat, value;
};

// Stats on a `state` list `owner` puts on `unit` for `frames`, by NewStateList's recast rule (PD2's timed-state
// helper 0x1036d7d0 has the same one): an older list of the same skill and level is extended and keeps its values, a
// lower level is refused, anything else is replaced. Returns what happened, for the logs.
auto TimedStatList(uint32_t dataFlag, Unit* owner, Unit* unit, int32_t skillId, int32_t level, int32_t frames,
                   int32_t state, const StatValue* first, const StatValue* last) noexcept -> const char* {
	using namespace d2r::rva;
	void* old = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, state);
	const auto listInt = [](uint64_t fn, void* list) { return d2r::Engine<int32_t (*)(void*) noexcept>(fn)(list); };
	const bool extends = old != nullptr && listInt(ListSkill, old) == skillId && listInt(ListLevel, old) == level;
	d2r::StateListRequest request {owner, unit, skillId, level, frames, -1, 0, state,
	                               d2r::Engine<void (*)()>(SkillStateExpired)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list == nullptr) {
		return "refused";
	}
	if (extends) {
		return "extended";
	}
	for (const StatValue* s = first; s != last; ++s) {
		d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(dataFlag, list, s->stat,
		                                                                                          s->value, 0);
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(unit, state, 1);
	return "new";
}

auto TimedStatList(uint32_t dataFlag, Unit* owner, Unit* unit, int32_t skillId, int32_t level, int32_t frames,
                   int32_t state, std::initializer_list<StatValue> stats) noexcept -> const char* {
	return TimedStatList(dataFlag, owner, unit, skillId, level, frames, state, stats.begin(), stats.end());
}

// A skill missile's params from (x, y), owned by and launched from the unit; the caller sets the targets.
auto SkillMissileParams(Unit* unit, int32_t flags, int32_t missile, int32_t x, int32_t y, int32_t skillId,
                        int32_t level) noexcept -> d2r::MissileParams {
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = flags;
	params.owner = unit;
	params.origin = unit;
	params.missile = missile;
	params.x = x;
	params.y = y;
	params.skill = skillId;
	params.skillLevel = level;
	return params;
}

// PD2's ring (0x1036d250 server, 0x1036d370 client): one missile at every (64 / count)-th of D2's 64 directions, so
// count 10 makes 11 and count 6 makes 7; 0 or over 64 makes 64.
template <typename Create>
auto Ring(int32_t count, Create create) noexcept -> int32_t {
	if (count <= 0 || count > 64) {
		count = 64;
	}
	const auto* dx = reinterpret_cast<const int32_t*>(d2r::imageBase + d2r::rva::DirectionX);
	const auto* dy = reinterpret_cast<const int32_t*>(d2r::imageBase + d2r::rva::DirectionY);
	int32_t created = 0;
	for (int32_t i = 0; i < 64; i += 64 / count) {
		created += create(dx[i], dy[i]) ? 1 : 0;
	}
	return created;
}

// PD2's server ring call (0x1036d250 with flags 3): `count` missiles of row `missile` from x/y (Ring), owned by and
// launched from the unit. Returns how many were created.
auto ServerRing(Game* game, Unit* unit, int32_t missile, int32_t x, int32_t y, int32_t count, int32_t skillId,
                int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, x, y, skillId, level);
	return Ring(count, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.createMissile(game, &params) != nullptr;
	});
}

// --- Teleport (PD2 srvdofunc 155 -> router 190; docs/research/sorceress-w26.md) -----------------------------------
// Read from ProjectDiablo.dll: srvdo 155 at 0x103b16d0, its teleport core at 0x1036cd20 (shared with Blood Warp).
std::atomic<uint32_t> g_teleports {0};
std::atomic<uint32_t> g_teleportsRefused {0};
constexpr int32_t Teleport = 54, Blink = 429, LucionBlink = 642, MonBloodwarp = 682;  // merged ids
constexpr int32_t DisableTele = 260;  // merged states id of disable_tele (PD2 214, Rathma's prison)
// passive_fire/cold/ltng/pois_mastery, in the order PD2 sets them; the same itemstatcost ids in PD2 and D2R
constexpr int32_t Masteries[] = {329, 331, 330, 332};

// PD2's teleport core: refused under disable_tele, else D2R's Teleport (srvdo 27), which makes the core's level
// checks (levels.txt Teleport 0 refuses, 2 checks collision at the point).
// PD2's Hell-only level list (0x105de86c) is never filled, so it is left out.
// shortcut: PD2's Teleport 3 rule (a path search on map levels) and its "TeleportBlocked" chat line on a refusal are
// left out: no ported level has Teleport 0 or 3. Upgrade when the map plan ports a map level.
auto TeleportCore(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> bool {
	using namespace d2r::rva;
	if (d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, DisableTele) != nullptr) {
		g_teleportsRefused.fetch_add(1);
		return false;
	}
	return VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoTeleport)(game, unit, skillId, level) != 0;
}

// PD2's debuff on one mastery stat: -(reduction + mastery% of it), the scaled part truncated (0x1037c4c0), so a
// mastery doesn't win the damage back.
constexpr auto TeleportDebuff(int32_t reduction, int32_t mastery) noexcept -> int32_t {
	const double scaled = static_cast<double>(mastery) * reduction / 100.0;
	return -((scaled > 2147483647.0 ? INT32_MAX : static_cast<int32_t>(scaled)) + reduction);
}
static_assert(TeleportDebuff(49, 0) == -49);
static_assert(TeleportDebuff(30, 50) == -45);   // a 50% mastery is lowered by half as much again
static_assert(TeleportDebuff(25, -10) == -23);  // -2.5 truncates to -2

// PD2 srvdo 155: the core, then a ring of calc2 srvmissilea at the new point (LucionBlink's nova), then aurastate
// (teleportdebuff) for auralencalc frames with each mastery lowered by Param2 - calc1 (TeleportDebuff). Named Pd2
// because rva::SrvDoTeleport is D2R's slot number.
// shortcut: an extended list's new timer is not sent to the client (PD2 sends packet 0x83), as for Blood Warp.
// Upgrade if the client's debuff runs out early in play.
auto SrvDoPd2Teleport(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr || !TeleportCore(game, unit, skillId, level)) {
		return 0;
	}
	int32_t x = 0, y = 0;
	d2r::UnitXY(unit, &x, &y);
	const int32_t count = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	int32_t created = 0;
	if (count > 0 && d2r::ValidMissile(dataFlag, missile)) {
		created = ServerRing(game, unit, missile, x, y, count, skillId, level);
	}
	const int32_t state = At<int16_t>(skill, SkAuraState);
	const int32_t frames = state > 0 ? d2r::EvalCalc(game, unit, skill, SkAuraLenCalc, skillId, level) : 0;
	const int32_t reduction = At<int32_t>(skill, SkParam2) - d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	int32_t mastery[std::size(Masteries)] {}, debuff[std::size(Masteries)] {};
	const char* list = "none";
	if (frames > 0) {  // PD2's state helper refuses a length under 1
		const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
		for (size_t i = 0; i < std::size(Masteries); i++) {
			mastery[i] = unitStat(unit, Masteries[i], 0);
			debuff[i] = TeleportDebuff(reduction, mastery[i]);
		}
		list = TimedStatList(dataFlag, unit, unit, skillId, level, frames, state,
		                     {{Masteries[0], debuff[0]}, {Masteries[1], debuff[1]}, {Masteries[2], debuff[2]},
		                      {Masteries[3], debuff[3]}});
	}
	if (plog::Sample(g_teleports)) {
		plog::Info("teleport: skill %d level %d: at %d,%d, %d missiles (row %d, calc2 %d), state %d for %d frames "
		           "(list %s), reduction %d: fire %d/%d cold %d/%d ltng %d/%d pois %d/%d, cast %u, refused %u",
		           skillId, level, x, y, created, missile, count, state, frames, list, reduction, debuff[0], mastery[0],
		           debuff[1], mastery[1], debuff[2], mastery[2], debuff[3], mastery[3], g_teleports.load(),
		           g_teleportsRefused.load());
	}
	return 1;
}

// --- Blood Warp (PD2 srvdofunc 157 -> router 190, srvstfunc 66 -> 80, cltstfunc 56 -> 63) --------------------------
// Read from ProjectDiablo.dll: srvdo 157 at 0x103b1aa0, srvst 66 at 0x103b6460, cltst 56 at 0x103acda0
// (docs/research/w12-blood-warp.md). The cast costs life and never kills; the cost comes back as hpregen.
std::atomic<uint32_t> g_bloodWarps {0};
std::atomic<uint32_t> g_bloodWarpsRefused {0};
constexpr int32_t BloodWarp = 439;  // merged id (PD2 367)

// PD2's cost, in life's 1/256: calc1 percent of current life, at least calc2. PD2 converts calc1 as unsigned and
// truncates with cvttsd2si, so a negative calc1 (a life reduction over 12) overflows to INT32_MIN and calc2 wins.
constexpr auto BloodWarpCost(int32_t calc1, int32_t calc2, int32_t life) noexcept -> int32_t {
	const double v = static_cast<double>(static_cast<uint32_t>(calc1)) / 100.0 * life;
	const int32_t cost = v > -2147483649.0 && v < 2147483648.0 ? static_cast<int32_t>(v) : INT32_MIN;
	return cost > calc2 ? cost : calc2;
}
static_assert(BloodWarpCost(12, 512, 100 << 8) == 3072);     // 12% of 100 life beats the 2-life minimum
static_assert(BloodWarpCost(12, 15360, 100 << 8) == 15360);  // calc2's 60-life cap wins
static_assert(BloodWarpCost(-1, 512, 100 << 8) == 512);      // the overflow leaves the minimum

struct LifeCost {
	const uint8_t* skill = nullptr;  // the skill record; null refuses
	int32_t cost = 0, life = 0;
	auto Payable() const noexcept -> bool { return skill != nullptr && cost < life; }  // the cast never kills
};

auto BloodWarpLife(uint32_t dataFlag, Unit* unit, int32_t skillId, int32_t level) noexcept -> LifeCost {
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return {};
	}
	const auto calc = [&](int field) {
		return d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, field), skillId, level);
	};
	const int32_t life = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::UnitStat)(
		unit, d2r::rva::StatLife, 0);
	return {skill, BloodWarpCost(calc(d2r::rva::SkCalc1), calc(d2r::rva::SkCalc2), life), life};
}

// The server's check (start and do): counts the refusals.
auto ServerPayable(const LifeCost& c) noexcept -> bool {
	if (!c.Payable()) {
		g_bloodWarpsRefused.fetch_add(1);
		return false;
	}
	return true;
}

// Start (PD2 srvstfunc 66, here 80): refuse a cast the caster can't pay for.
auto SrvStBloodWarp(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	return ServerPayable(BloodWarpLife(d2r::DataFlag(game), unit, skillId, level)) ? 1 : 0;
}

// Client half (PD2 cltstfunc 56, here 63): the same check on the client's copy of the caster.
auto CltStBloodWarp(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	return BloodWarpLife(d2r::api.unitDataFlag(unit), unit, skillId, level).Payable() ? 1 : 0;
}

// PD2 srvdofunc 157: teleport, then pay the cost (checked again at the new point) and get it back over auralencalc
// frames as aurastat1 (hpregen) = cost / frames + 1 on an aurastate (bloodwarp) list. A recast at the same level
// extends the running list and keeps its regen; a lower level pays and adds none (TimedStatList).
// shortcut: an extended list's new timer is not sent to the client (PD2 sends packet 0x83). Upgrade if the client's
// state runs out early in play.
auto SrvDoBloodWarp(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	if (!TeleportCore(game, unit, skillId, level)) {
		return 0;
	}
	const LifeCost c = BloodWarpLife(dataFlag, unit, skillId, level);  // again, at the new point
	if (!ServerPayable(c)) {
		return 0;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(unit, StatLife, c.life - c.cost, 0);
	const int32_t frames = d2r::EvalCalc(game, unit, c.skill, SkAuraLenCalc, skillId, level);
	const int32_t state = At<int16_t>(c.skill, SkAuraState);
	const int32_t stat = At<int16_t>(c.skill, SkAuraStat1);
	int32_t regen = 0;
	const char* list = "none";
	if (frames > 0 && state > 0 && stat >= 0) {  // PD2 divides by frames unchecked
		regen = c.cost / frames + 1;
		list = TimedStatList(dataFlag, unit, unit, skillId, level, frames, state, {{stat, regen}});
	}
	if (plog::Sample(g_bloodWarps)) {
		plog::Info("blood warp: skill %d level %d: life %d, cost %d, regen %d for %d frames (stat %d, state %d, list %s), "
		           "cast %u",
		           skillId, level, c.life, c.cost, regen, frames, stat, state, list, g_bloodWarps.load());
	}
	return 1;
}

// --- Desecrate (PD2 srvdofunc 161 -> router 190, cltdofunc 101 -> 121) -------------------------------------------
// Read from ProjectDiablo.dll: srvdo 161 at 0x103b1fa0, cltdo 101 at 0x103ab630 (docs/research/necromancer-w13.md).
std::atomic<uint32_t> g_desecrates {0};
std::atomic<uint32_t> g_desecrateClients {0};
std::atomic<uint32_t> g_desecrateHidden {0};

constexpr int32_t Desecrate = 83;
constexpr int32_t CorpseNoDraw = 104;  // states.txt corpse_nodraw, the same id in PD2 and D2R
constexpr int32_t CorpseFrames = 400;  // PD2's timer: the corpse can't be used 16 seconds after it falls
// Monsters PD2 never raises, as merged monstats ids: frogdemon1-3, ancientbarb1-3.
// shortcut: PD2's other 45 (map, uber and Rathma monsters, *Maus, *Rat, void*) are left out because no merged level
// lists them in mon1-25 / nmon1-25; add them when a map level is ported (docs/unported.md).
constexpr int32_t NeverRaised[] = {247, 248, 249, 540, 541, 542};
// Where PD2 picks monsters when the cast level has none: Black Marsh, a Tal Rasha's Tomb, Spider Forest, City of the
// Damned, the Worldstone Chamber
constexpr int32_t FallbackLevels[] = {6, 66, 76, 106, 132};
constexpr int ListSize = 25;

// PD2's list for a level: its 25 monsters (nmon in nightmare and hell), -1 for each one Desecrate can't raise.
auto RaiseList(uint32_t dataFlag, const uint8_t* tables, int32_t levelId, bool normal, int16_t* list) noexcept -> bool {
	using namespace d2r::rva;
	if (levelId < 0 || static_cast<uint32_t>(levelId) >= At<uint32_t>(tables, TablesLevelCount)) {
		return false;
	}
	const uint8_t* level = At<const uint8_t*>(tables, TablesLevels) + static_cast<int64_t>(levelId) * LevelRecordSize;
	bool any = false, ended = false;
	for (int i = 0; i < ListSize; ++i) {
		int16_t mon = ended ? -1 : At<int16_t>(level, (normal ? LvlMon1 : LvlNMon1) + i * 2);
		ended = ended || mon < 0;
		const uint8_t* stats = mon < 0 ? nullptr
		                               : d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
		                                     dataFlag, mon);
		if (stats == nullptr || At<uint8_t>(stats, MonPartyMin) > 0 || At<uint8_t>(stats, MonPartyMax) > 0 ||
		    At<int16_t>(stats, MonSpawn) != -1 ||
		    std::find(std::begin(NeverRaised), std::end(NeverRaised), mon) != std::end(NeverRaised)) {
			mon = -1;
		}
		list[i] = mon;
		any = any || mon >= 0;
	}
	return any;
}

// The corpse's timer ran out: hide it (PD2's callback 0x1036c6d0 sets corpse_nodraw). The list never set the state, so
// the expiry call only does D2R's list bookkeeping.
void HideCorpse(void* list, Unit* unit, int32_t state) noexcept {
	d2r::Engine<void (*)(void*, Unit*, int32_t) noexcept>(d2r::rva::SkillStateExpired)(list, unit, state);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::SetUnitState)(unit, CorpseNoDraw, 1);
	g_desecrateHidden.fetch_add(1);
}

// calc1 corpses of monsters from the target point's level, placed around the point, owned by the caster, each hidden
// after CorpseFrames; then srvmissilea at the point. PD2 evaluates petmax too and throws it away; so does this.
// shortcut: PD2's timer is a game event; this is a state list on the corpse with HideCorpse as its expiry. Upgrade if
// the game check shows corpses that never hide (docs/unported.md).
auto SrvDoDesecrate(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	int32_t x = 0, y = 0;
	if (skill == nullptr || tables == nullptr || d2r::api.skillTargetXY(game, unit, &x, &y) == 0) {
		return 0;
	}
	void* room = d2r::Engine<void* (*)(void*, int32_t, int32_t) noexcept>(RoomAtXY)(
		d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit), x, y);
	if (room == nullptr) {
		return 0;
	}
	const int32_t levelId = d2r::Engine<int32_t (*)(void*) noexcept>(RoomLevelId)(room);
	const bool normal = At<uint8_t>(game, GameDifficulty) == 0;
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	int16_t list[ListSize];
	// PD2 retries random fallback levels until one has a monster; bounded here so bad data can't hang the server
	bool any = RaiseList(dataFlag, tables, levelId, normal, list);
	for (int tries = 0; !any && tries < 32; ++tries) {
		any = RaiseList(dataFlag, tables, FallbackLevels[d2r::RandomBelow(seed, static_cast<uint32_t>(std::size(FallbackLevels)))], normal, list);
	}
	if (!any) {
		plog::Warn("desecrate: no monster to raise for level %d", levelId);
		return 0;
	}
	const int32_t count = ClampCount(d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level));
	const int32_t mode = At<uint8_t>(skill, SkSumMode);
	const uint32_t casterId = At<uint32_t>(unit, UnitIdField);
	const auto spawn = d2r::Engine<Unit* (*)(Game*, void*, int32_t, int32_t, int32_t, int32_t, int32_t, int16_t) noexcept>(
		SpawnMonster);
	auto* regions = At<uint8_t**>(game, GameMonRegions);
	if (levelId < 0 || static_cast<uint32_t>(levelId) >= At<uint32_t>(tables, TablesLevelCount)) {
		regions = nullptr;
	}
	int32_t created = 0, sx = x, sy = y;
	for (int32_t i = 0; i < count; ++i) {
		int16_t mon = -1;
		while (mon < 0) {
			mon = list[d2r::RandomBelow(seed, ListSize)];
		}
		if (i == 1) {
			sx += 2, sy += 2;
		} else if (i == 2) {
			sx -= 2, sy += 2;
		} else if (i > 2) {
			sx += static_cast<int32_t>(d2r::RandomBelow(seed, 2)) - 2;
			sy += static_cast<int32_t>(d2r::RandomBelow(seed, 2)) - 2;
		}
		Unit* corpse = spawn(game, room, sx, sy, mon, mode, -1, 0x42);
		if (corpse == nullptr) {
			break;  // PD2 stops the cast here, before the cloud
		}
		++created;
		*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(corpse) + UnitOwnerType) = 0;
		*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(corpse) + UnitOwnerId) = casterId;
		// PD2 takes the corpse back out of its level's spawn count, so it doesn't stop the level spawning
		if (regions != nullptr && regions[levelId] != nullptr) {
			*reinterpret_cast<int32_t*>(regions[levelId] + MonRegionCount) -= 1;
		}
		d2r::StateListRequest request {corpse, corpse, skillId, level, CorpseFrames, -1, 0, CorpseNoDraw,
		                               reinterpret_cast<void (*)()>(HideCorpse)};
		d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	}
	bool cloud = false;
	if (created == count) {
		d2r::MissileParams params;
		std::memset(&params, 0, sizeof(params));
		params.flags = 1;  // placed at x/y
		params.owner = unit;
		params.missile = At<int16_t>(skill, SkSrvMissileA);
		params.x = params.targetX = x;
		params.y = params.targetY = y;
		params.skill = skillId;
		params.skillLevel = level;
		cloud = d2r::ValidMissile(dataFlag, params.missile) && d2r::api.createMissile(game, &params) != nullptr;
		d2r::api.setUnitSkillFlag(unit, 1);
	}
	if (plog::Sample(g_desecrates)) {
		plog::Info("desecrate: skill %d level %d: %d of %d corpses in level %d, cloud %d, cast %u", skillId, level,
		           created, count, levelId, cloud ? 1 : 0, g_desecrates.load());
	}
	return created == count ? 1 : 0;
}

// Client half (PD2 cltdofunc 101): cltmissile a, b and c at the target point.
auto CltDoDesecrate(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	int32_t x = 0, y = 0;
	if (skill == nullptr || d2r::api.clientSkillTargetXY(unit, &x, &y) == 0) {
		return 1;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 1;
	params.owner = unit;
	params.x = params.targetX = x;
	params.y = params.targetY = y;
	params.skill = skillId;
	params.skillLevel = level;
	int created = 0;
	for (int column = 0; column < 3; ++column) {
		params.missile = At<int16_t>(skill, d2r::rva::SkCltMissileA + column * 2);
		created += d2r::ValidMissile(dataFlag, params.missile) && d2r::api.clientCreateMissile(&params, 0) ? 1 : 0;
	}
	if (plog::Sample(g_desecrateClients)) {
		plog::Info("desecrate client: skill %d level %d: %d missiles, cast %u", skillId, level, created,
		           g_desecrateClients.load());
	}
	return 1;
}

// --- Melee splash (PD2 srvdofunc 163 -> router 190) ---------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 163 at 0x103b2340, its per-unit callback 0x10303190; Revive's aura event 32
// (0x10347370) calls the do function itself (docs/research/w33-melee-splash.md). Every unit within calc3 of the struck
// unit, the struck unit aside, takes the attacker's weapon damage at the skill's SrcDam with half the life leech, through
// D2R's area search with PD2's unit mask.
// shortcut: PD2 also adds the player's current skill's damage (its elemental part, Smite's and the kicks'), adds an item
// stat sum / 20 to calc3 and takes 1 off for some weapons (0x10381720, not decoded); upgrade with the unported row.
std::atomic<uint32_t> g_splashes {0};
std::atomic<uint32_t> g_splashUnitCasts {0};  // the first splash with units hit is logged too
// PD2 skips Revive's event 32 and two item event functions (14 and 20, skill on hit) while a splash hit fires its events
// (the handler sets 0x105dc628 / 0x105dc6cc, read by 0x10305c30), so a splash never splashes again. This guard does that
// for the splash itself.
// shortcut: other skill-on-hit procs and item event 14 still run for each splashed unit; upgrade with #125.
thread_local bool t_splashing = false;
constexpr int32_t ProcSplashDamage = 430, GolemSplash = 461, SkeletonSplash = 465;  // merged skill ids
constexpr int32_t IncSplashRadius = 531;

using AreaFn = void (*)(Game*, Unit*, d2r::AreaCenter*, int32_t, uint32_t, int32_t (*)(d2r::AreaContext*, Unit*), void*,
                        int32_t) noexcept;  // rva::AreaUnits

struct SplashHit {
	Unit* struck;
	uint8_t srcDam;
	int64_t victims;
};

// Leap Attack's (0x56b170) cleanup of its damage block: the tagged object at +0x158 (bit 0 set: a heap pointer or
// none; clear: an object in place), then the list if it outgrew its inline space (capacity without the inline bit).
void ReleaseDamage(d2r::LeapKnockback& damage) noexcept {
	const auto release = [](void* p) {
		void* allocator = d2r::Engine<void* (*)() noexcept>(d2r::rva::EngineAllocator)();
		(*reinterpret_cast<void (***)(void*, void*) noexcept>(allocator))[4](allocator, p);
	};
	const uint64_t tagged = damage.tagged[0];
	void* object = (tagged & 1) != 0 ? reinterpret_cast<void*>(tagged & ~1ull) : &damage.tagged[0];
	if (object != nullptr) {
		(**reinterpret_cast<void (***)(void*, int32_t) noexcept>(object))(object, 0);
		if ((damage.tagged[0] & 1) != 0) {  // read again: the destructor may leave a heap pointer
			release(reinterpret_cast<void*>(damage.tagged[0] & ~1ull));
		}
		damage.tagged[0] = 1;
	}
	if (static_cast<int64_t>(damage.listCapacity) >= 0) {
		release(damage.listData);
	}
}

// D2R's area apply (0x436e90) with a melee hit's events: PD2's apply (0x10305880, its event step 0x10305c30) fires
// domeleedamage on the attacker and event 1 on the victim, where D2R's area apply fires 6 and 2.
void DealSplash(Game* game, Unit* attacker, Unit* victim, d2r::LeapKnockback& damage) noexcept {
	using namespace d2r::rva;
	using HitFn = void (*)(Game*, Unit*, Unit*, void*) noexcept;
	if ((damage.resultFlags & ResultHit) == 0) {
		return;
	}
	const uint32_t avoided =
		d2r::Engine<uint32_t (*)(Game*, Unit*, Unit*, int32_t, int32_t) noexcept>(AvoidRoll)(game, attacker, victim, 1, 0);
	if ((avoided & 2) != 0) {
		damage.resultFlags |= 0x100;
	}
	if ((avoided & 4) != 0) {
		damage.resultFlags |= 0x80;
	}
	if ((avoided & 0x10) != 0) {
		damage.resultFlags |= 0x8000;
	}
	if ((avoided & 1) != 0) {
		damage.resultFlags |= 0x10;
	}
	if (avoided != 0) {
		damage.resultFlags &= ~uint32_t {ResultHit};
	}
	if ((damage.resultFlags & ResultHit) != 0 &&
	    d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(victim, 0x36) == 0) {
		damage.resultFlags |= 4;
	}
	d2r::Engine<void (*)(Unit*, Unit*, void*) noexcept>(DamageStep)(attacker, victim, &damage);
	if ((damage.resultFlags & ResultHit) != 0) {
		d2r::Engine<HitFn>(DamageEffects)(game, attacker, victim, &damage);  // what HitEvents runs for an area hit
		d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t, void*) noexcept>(HitEvents)(game, attacker, victim, 0, &damage);
	}
	d2r::Engine<HitFn>(DealDamage)(game, attacker, victim, &damage);
}

// One unit in range (PD2's callback 0x10303190): a hit with no to-hit roll, weapon damage at SrcDam, life leech halved
// (at least 1). Counts the units the hit lands on.
auto SplashOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* hit = static_cast<SplashHit*>(area->arg);
	if (victim == hit->struck) {
		return 0;
	}
	auto* game = static_cast<Game*>(area->game);
	d2r::LeapKnockback damage;
	damage.resultFlags = ResultHit;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*, int32_t, uint8_t) noexcept>(WeaponDamage)(game, area->unit, victim,
	                                                                                          &damage, 0, hit->srcDam);
	auto& leech = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&damage) + DamageLifeLeech);
	if (leech != 0) {
		leech = std::max(leech / 2, 1u);
	}
	DealSplash(game, area->unit, victim, damage);
	hit->victims += (damage.resultFlags & ResultHit) != 0 ? 1 : 0;
	ReleaseDamage(damage);
	return 1;
}

// The splash around `struck`. Returns 1 as PD2 does, also when nothing is in range or the struck unit has no
// position left (killed by the hit); 0 without a struck unit, or inside another splash.
auto Splash(Game* game, Unit* unit, Unit* struck, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr || struck == nullptr || t_splashing) {
		return 0;
	}
	const int32_t radius = d2r::EvalCalc(game, unit, skill, SkCalc3, skillId, level);
	SplashHit hit {struck, At<uint8_t>(skill, SkSrcDam), 0};
	int32_t x = 0, y = 0;
	if (radius > 0 && d2r::UnitXY(struck, &x, &y)) {
		d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(struck), x, y};
		t_splashing = true;
		d2r::Engine<AreaFn>(AreaUnits)(game, unit, &center, radius, AreaUnitMask, SplashOne, &hit, 0);
		t_splashing = false;
	}
	if (plog::Sample(g_splashes) || (hit.victims > 0 && plog::Sample(g_splashUnitCasts))) {
		plog::Info("splash: skill %d level %d from unit type %d at unit type %d: radius %d (inc_splash_radius %d), "
		           "units hit %lld, splash %u", skillId, level, At<int32_t>(unit, UnitType), At<int32_t>(struck, UnitType),
		           radius, d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(unit, IncSplashRadius, 0),
		           static_cast<long long>(hit.victims), g_splashes.load());
	}
	return 1;
}

// PD2 srvdofunc 163. PD2 splashes around its path's target; D2R's event cast (item_splashonhit) names the struck unit as
// the skill's target, so that comes first.
auto SrvDoSplash(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	Unit* struck = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::rva::SkillTargetUnit)(game, unit);
	void* path = At<void*>(unit, d2r::rva::UnitPath);
	if (struck == nullptr && path != nullptr && At<int32_t>(unit, d2r::rva::UnitType) <= d2r::rva::UnitMonster) {
		struck = At<Unit*>(path, d2r::rva::PathTargetUnit);
	}
	return Splash(game, unit, struck, skillId, level);
}

// --- Holy Light (PD2 srvdofunc 176) ---------------------------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 176 at 0x103b3cc0, its per-unit callback 0x103043b0
// (docs/research/w17-holy-light.md). Every unit within aurarangecalc of the target, the caster aside: an ally heals
// cltcalc2-cltcalc3 life, an enemy takes the skill's magic damage; whichever side the target is not on gets half.
std::atomic<uint32_t> g_holyLights {0};
std::atomic<uint32_t> g_holyLightUnitCasts {0};  // the first cast with units in range is logged too
constexpr int32_t HealOverlay = 26;  // PD2's callback puts overlay 26 on each unit it heals

struct HolyLight {
	Unit* target;
	int32_t skillId, level;
	int32_t healMin, healMax;  // life in 1/256
	bool halfHeal, halfDamage;
	int64_t healed, hit;
};

// 1.13c's D2Game 0xe0a60, which PD2 asks: alignment (D2Common #10666) decides, the same when both are hostile (0) or
// both allies (2); a neutral unit (1) is hostile to everyone.
// 0xe0a60 first swaps pets and missiles for their owners; a monster a player owns counts as a player here.
// shortcut: two players (or their pets) count as allies, where 0xe0a60 asks the PvP state. Upgrade when PvP is ported
// (later release).
auto Hostile(Unit* a, Unit* b) noexcept -> bool {
	using namespace d2r::rva;
	const auto playerSide = [](Unit* u) {
		const int32_t type = At<int32_t>(u, UnitType);
		return type == 0 || (type == UnitMonster && OwnedByPlayer(u));
	};
	if (a == b || (playerSide(a) && playerSide(b))) {
		return false;
	}
	const auto align = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitAlignment);
	const int32_t x = align(a), y = align(b);
	return !(x == y && x != 1);
}

void PutOverlay(Unit* unit, int32_t overlay) noexcept {
	using namespace d2r::rva;
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(d2r::api.unitDataFlag(unit));
	if (overlay >= 0 && tables != nullptr && overlay < At<int64_t>(tables, TablesOverlayCount)) {
		d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetOverlay)(unit, overlay, 0);
	}
}

// One unit in range (PD2's 0x103043b0).
auto HolyLightOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* hl = static_cast<HolyLight*>(area->arg);
	Unit* unit = area->unit;
	if (victim == unit) {
		return 0;
	}
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	if (!Hostile(victim, unit)) {
		const int32_t spread = hl->healMax - hl->healMin;
		int32_t heal = hl->healMin + (spread > 0 ? static_cast<int32_t>(d2r::RandomBelow(seed, spread)) : 0);
		heal = std::max(hl->halfHeal ? heal / 2 : heal, 0);
		// 1.13c D2Game 0x4df20: life + heal, at most max life
		const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
		const int32_t life = unitStat(victim, StatLife, 0), maxLife = unitStat(victim, StatMaxLife, 0);
		if (heal > 0) {
			d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat)(
				victim, StatLife, std::min(life + heal, std::max(maxLife, life)), 0);
		}
		PutOverlay(victim, HealOverlay);
		++hl->healed;
		return 1;
	}
	d2r::LeapKnockback damage;
	damage.resultFlags = ResultHit;
	if (At<int32_t>(victim, UnitType) == UnitMonster && victim != hl->target) {
		const uint8_t* mon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
			d2r::api.unitDataFlag(victim), At<int32_t>(victim, UnitClassId));
		if (mon != nullptr && (At<uint32_t>(mon, MonFlags) & MonUndead) != 0) {
			damage.resultFlags |= ResultKnockback;
		}
	}
	// PD2 halves the bounds and rolls between them; this halves the roll (the same up to 1/256 of a life point)
	d2r::Engine<void (*)(Unit*, void*, int32_t, int32_t) noexcept>(ElementalDamage)(unit, &damage, hl->skillId, hl->level);
	auto& magic = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&damage) + DmgMagic);
	if (hl->halfDamage) {
		magic /= 2;
	}
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(AreaApply)(static_cast<Game*>(area->game), unit, victim,
	                                                                     &damage);
	++hl->hit;
	ReleaseDamage(damage);
	return 1;
}

// PD2 srvdofunc 176.
// shortcut: the cast centres on D2R's skill target; PD2 refreshes the caster's path target (1.13c D2Game 0xe07a0) and
// reads that. Upgrade if a cast in game lands on a different unit (docs/unported.md).
auto SrvDoHolyLight(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	int32_t x = 0, y = 0;
	if (target == nullptr || !d2r::UnitXY(target, &x, &y)) {
		return 1;
	}
	const bool enemyTarget = Hostile(target, unit);
	HolyLight hl {target, skillId, level,
	              d2r::EvalCalc(game, unit, skill, SkCltCalc2, skillId, level) << 8,
	              d2r::EvalCalc(game, unit, skill, SkCltCalc3, skillId, level) << 8,
	              enemyTarget, !enemyTarget, 0, 0};
	const int32_t radius = d2r::EvalCalc(game, unit, skill, SkAuraRangeCalc, skillId, level);
	if (radius > 0) {
		d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(target), x, y};
		d2r::Engine<AreaFn>(AreaUnits)(game, unit, &center, radius, At<uint32_t>(skill, SkAuraFilter), HolyLightOne, &hl,
		                               0);
	}
	PutOverlay(target, At<int16_t>(skill, SkSrvOverlay));
	if (plog::Sample(g_holyLights) || (hl.healed + hl.hit > 0 && plog::Sample(g_holyLightUnitCasts))) {
		plog::Info("holy light: skill %d level %d on an %s: heal %d-%d%s, damage%s, radius %d: healed %lld, hit %lld, "
		           "cast %u", skillId, level, enemyTarget ? "enemy" : "ally", hl.healMin >> 8, hl.healMax >> 8,
		           hl.halfHeal ? " halved" : "", hl.halfDamage ? " halved" : "", radius,
		           static_cast<long long>(hl.healed), static_cast<long long>(hl.hit), g_holyLights.load());
	}
	return 1;
}

// --- Vengeance (PD2 srvdofunc 174) and Joust (PD2 srvdofunc 162 -> router 190) ------------------------------------
// Read from ProjectDiablo.dll: srvdo 174 at 0x103b3570 with its per-unit callback 0x103041f0 and chain hit 0x10303cd0,
// srvdo 162 at 0x103b21e0 (docs/research/w18-vengeance-joust.md).
std::atomic<uint32_t> g_vengeances {0};
std::atomic<uint32_t> g_vengeanceLinks {0};
std::atomic<uint32_t> g_vengeanceLinkedCasts {0};  // the first cast that links is logged too
std::atomic<uint32_t> g_jousts {0};
constexpr int32_t Vengeance = 111, Joust = 450;  // merged ids
// Merged states: the aura lists whose elemental damage Vengeance leaves out (holyfire, holywind, holyshock, enchant,
// coldenchant), its chain marker (justhit) and the state each burst clears (shatter)
constexpr int32_t HolyFire = 35, HolyWind = 43, HolyShock = 46, Enchant = 16, ColdEnchant = 223;
constexpr int32_t JustHit = 86, Shatter = 107;
constexpr int32_t CriticalStrike = 337;  // passive_critical_strike, the same id in PD2 and D2R
// shortcut: a chain stops after 64 links, PD2 has no cap (calc4 is at most 95%); bounded so bad data can't chain
// forever. Raise it if a logged chain reaches it in play.
constexpr int32_t MaxLinks = 64;

// D2R's Leap Attack hit (0x56b170) without its damage%, skill elemental, knockback or states: the to-hit roll with the
// skill's ToHit, weapon damage at srcDam, applied. Returns whether the roll hit. PD2's hit (1.13c's roll 0x103048a0
// and fill 0x1031b4a0) also applies a miss and marks its block with hit flag 2.
auto MeleeHit(Game* game, Unit* unit, Unit* target, const uint8_t* skill, int32_t skillId, int32_t level,
              uint8_t srcDam) noexcept -> bool {
	using namespace d2r::rva;
	const uint8_t mask = *reinterpret_cast<const uint8_t*>(d2r::imageBase + MeleeRollFlagMask);
	const int32_t toHit = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(SkillToHit)(unit, skillId, level);
	d2r::LeapKnockback damage;
	damage.resultFlags = d2r::Engine<uint16_t (*)(Game*, Unit*, Unit*, int32_t, uint8_t, int32_t) noexcept>(MeleeRoll)(
		game, unit, target, toHit, (At<uint8_t>(skill, SkRollFlags) & mask) != 0 ? 1 : 0, 0);
	const bool hit = (damage.resultFlags & ResultHit) != 0;
	if (hit) {
		damage.hitFlags |= 2;
		*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&damage) + DmgHitClass) =
			d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitHitClass)(unit);
		d2r::Engine<void (*)(Game*, Unit*, Unit*, void*, uint8_t) noexcept>(QueueMeleeHit)(game, unit, target, &damage,
		                                                                                    srcDam);
		d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t, uint8_t) noexcept>(ApplyQueuedHit)(game, unit, target, 0, 1);
	}
	ReleaseDamage(damage);
	return hit;
}

// 1.13c D2Game 0xdbed0, which PD2 calls for each element: min and max each raised by pct %, then a roll between them on
// the unit's seed; nothing when max is not above 0, never below 0. 1.13c splits the product for large values; this
// takes it in 64 bits.
auto RollPercent(uint32_t* seed, int32_t min, int32_t max, int32_t pct) noexcept -> int32_t {
	if (max <= 0) {
		return 0;
	}
	const auto up = [pct](int32_t v) { return v + static_cast<int32_t>(static_cast<int64_t>(v) * pct / 100); };
	const int32_t lo = up(min), hi = up(max);
	const int32_t v = lo + (hi > lo ? static_cast<int32_t>(d2r::RandomBelow(seed, static_cast<uint32_t>(hi - lo))) : 0);
	return std::max(v, 0);
}

// PD2's 0x1037c4c0 with 100: v * pct / 100, truncated
constexpr auto Percent(int32_t v, int32_t pct) noexcept -> int32_t {
	return static_cast<int32_t>(static_cast<int64_t>(v) * pct / 100);
}
static_assert(Percent(512, 30) == 153);
static_assert(Percent(-512, 30) == -153);

struct VengeanceDamage {
	int32_t pct[3];   // fire, cold, lightning: calc1-3 + the element's mastery
	int32_t base;     // the skill's EMin-EMax roll (EType none), in 1/256
	int32_t dmg[3];   // fire, cold, lightning, in 1/256
	int32_t coldLength;
};

// One Vengeance burst's damage (srvdo 174 and the chain hit 0x10303cd0 build it alike): per element, the caster's own
// min/max damage stats less what the Holy auras and Enchant lists give, raised by calc + mastery % and rolled; then the
// skill's elemental roll, plus its mastery %, added to each. Cold length ELen, 15 frames less per link.
auto RollVengeance(Game* game, Unit* unit, const uint8_t* skill, int32_t skillId, int32_t level, int32_t link) noexcept
	-> VengeanceDamage {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto auraStat = [&](int32_t stat, std::initializer_list<int32_t> states) {
		int32_t sum = 0;
		for (const int32_t state : states) {
			void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, state);
			sum += list != nullptr ? d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(ListStatValue)(
			                             dataFlag, list, stat, 0)
			                       : 0;
		}
		return sum;
	};
	struct Element {
		int32_t minStat, calc, mastery;
		std::initializer_list<int32_t> states;
	};
	// firemindam/maxdam 48/49, coldmindam/maxdam 54/55, lightmindam/maxdam 50/51: the same ids in PD2 and D2R
	const Element elements[3] = {{48, SkCalc1, 329, {HolyFire, Enchant}},
	                             {54, SkCalc2, 331, {HolyWind, ColdEnchant}},
	                             {50, SkCalc3, 330, {HolyShock}}};
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	VengeanceDamage d {};
	int32_t mastery[3];
	for (int i = 0; i < 3; ++i) {
		const Element& e = elements[i];
		mastery[i] = unitStat(unit, e.mastery, 0);
		d.pct[i] = d2r::EvalCalc(game, unit, skill, e.calc, skillId, level) + mastery[i];
		const int32_t min = unitStat(unit, e.minStat, 0) - auraStat(e.minStat, e.states);
		const int32_t max = unitStat(unit, e.minStat + 1, 0) - auraStat(e.minStat + 1, e.states);
		d.dmg[i] = RollPercent(seed, min << 8, max << 8, d.pct[i]);
	}
	d2r::LeapKnockback roll;
	d2r::Engine<void (*)(Unit*, void*, int32_t, int32_t) noexcept>(ElementalDamage)(unit, &roll, skillId, level);
	d.base = At<int32_t>(&roll, DmgPhys);
	ReleaseDamage(roll);
	for (int i = 0; i < 3; ++i) {
		d.dmg[i] += d.base + Percent(d.base, mastery[i]);
	}
	const int32_t length = d2r::Engine<int32_t (*)(uint32_t, Unit*, int32_t, int32_t, int32_t) noexcept>(ElementalLength)(
		dataFlag, unit, skillId, level, 1);
	d.coldLength = link == 0 ? length : std::max(length - 15 * link, 0);
	return d;
}

struct VengeanceBurst {
	Unit* origin;  // never chains on from here (PD2: the caster's path target)
	int32_t skillId, level, link;
	VengeanceDamage damage;
	int32_t chance, delay, reach;  // calc4 %, Param5 frames, Param6 subtiles from the caster
	int64_t hit, linked;
};

// A link waiting on its justhit marker: the marker's expiry (VengeanceLinkDue) runs the next burst around the unit.
// PD2 schedules a game timer instead (0x103164f0, event 7). Keyed by the unit with its id: D2R's expiry callback gets
// (game, unit, state), not the list (SkillStateExpired 0x5551f0 never reads its first argument).
// shortcut: a record whose marker went without expiring (replaced, the unit gone) fires on that unit's next marker
// expiry; upgrade if the log shows links from units no burst marked.
struct VengeanceLink {
	Unit* unit;
	uint32_t unitId;
	Game* game;
	int32_t casterType;
	uint32_t casterId;
	int32_t skillId, level, link;
};
// shortcut: a full ring overwrites its oldest waiting link (it never fires); upgrade if the log shows lost links.
VengeanceLink g_links[256] {};
size_t g_nextLink = 0;
std::mutex g_linksLock;

auto Burst(Game* game, Unit* caster, Unit* center, Unit* origin, const uint8_t* skill, int32_t skillId, int32_t level,
           int32_t link) noexcept -> VengeanceBurst;

void VengeanceLinkDue(void* game, Unit* unit, int32_t state) noexcept {
	using namespace d2r::rva;
	VengeanceLink due {};
	{
		const std::lock_guard<std::mutex> lock(g_linksLock);
		for (VengeanceLink& l : g_links) {
			if (l.unit == unit) {
				due = l;
				l = {};
			}
		}
	}
	// the burst runs before the marker goes, so the unit can't link to itself.
	// shortcut: the marker lasts Param5 frames, PD2's Param5 + 1 (its timer fires at Param5); upgrade if a unit marked
	// in that last frame links in game where PD2's would not.
	if (due.unit != nullptr && due.unitId == At<uint32_t>(unit, UnitIdField)) {
		Unit* caster = d2r::Engine<Unit* (*)(Game*, int32_t, int32_t) noexcept>(FindUnit)(due.game, due.casterType,
		                                                                                   static_cast<int32_t>(due.casterId));
		const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(due.game), due.skillId);
		void* path = caster != nullptr ? At<void*>(caster, UnitPath) : nullptr;
		if (skill != nullptr && path != nullptr) {
			const VengeanceBurst b = Burst(due.game, caster, unit, At<Unit*>(path, PathTargetUnit), skill, due.skillId,
			                               due.level, due.link);
			if (plog::Sample(g_vengeanceLinks)) {
				plog::Info("vengeance link: skill %d level %d link %d: fire %d cold %d light %d, cold length %d, hit %lld, "
				           "linked %lld, link %u", due.skillId, due.level, due.link, b.damage.dmg[0] >> 8,
				           b.damage.dmg[1] >> 8, b.damage.dmg[2] >> 8, b.damage.coldLength,
				           static_cast<long long>(b.hit), static_cast<long long>(b.linked), g_vengeanceLinks.load());
			}
		}
	}
	d2r::Engine<void (*)(void*, Unit*, int32_t) noexcept>(SkillStateExpired)(game, unit, state);
}

// One unit in range (PD2's 0x103041f0): it takes the burst; then, unless already marked, within reach of the caster,
// it is marked justhit for the link delay, and, unless it is the origin, links on at calc4 % (its own seed).
auto VengeanceOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* b = static_cast<VengeanceBurst*>(area->arg);
	auto* game = static_cast<Game*>(area->game);
	Unit* caster = area->unit;
	d2r::LeapKnockback damage;
	damage.resultFlags = ResultHit;
	auto* block = reinterpret_cast<uint8_t*>(&damage);
	*reinterpret_cast<int32_t*>(block + DmgFire) = b->damage.dmg[0];
	*reinterpret_cast<int32_t*>(block + DmgCold) = b->damage.dmg[1];
	*reinterpret_cast<int32_t*>(block + DmgLight) = b->damage.dmg[2];
	*reinterpret_cast<int32_t*>(block + DmgColdLength) = b->damage.coldLength;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(AreaApply)(game, caster, victim, &damage);
	ReleaseDamage(damage);
	++b->hit;
	int32_t cx = 0, cy = 0, vx = 0, vy = 0;
	if (d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(victim, JustHit) != 0 ||
	    !d2r::UnitXY(caster, &cx, &cy) || !d2r::UnitXY(victim, &vx, &vy)) {
		return 1;
	}
	const int64_t dx = vx - cx, dy = vy - cy;
	if (dx * dx + dy * dy >= static_cast<int64_t>(b->reach) * b->reach) {
		return 1;
	}
	d2r::StateListRequest request {victim, victim, b->skillId, b->level, std::max(b->delay, 1), -1, 0, JustHit,
	                               reinterpret_cast<void (*)()>(VengeanceLinkDue)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list == nullptr) {
		return 1;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(victim, JustHit, 1);
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(victim);
	if (victim != b->origin && static_cast<int32_t>(d2r::RandomBelow(seed, 100)) < b->chance && b->link < MaxLinks) {
		const std::lock_guard<std::mutex> lock(g_linksLock);
		g_links[g_nextLink++ % std::size(g_links)] = {victim, At<uint32_t>(victim, UnitIdField), game,
		                                              At<int32_t>(caster, UnitType), At<uint32_t>(caster, UnitIdField),
		                                              b->skillId, b->level, b->link + 1};
		++b->linked;
	}
	return 1;
}

// A burst around `center` (srvdo 174 on its target, link 0; the chain hit on a linked unit): auratargetstate on it for
// Param5 frames, every unit within aurarangecalc (less 1 per link, at least 2) of it takes the damage, and its shatter
// state is cleared.
auto Burst(Game* game, Unit* caster, Unit* center, Unit* origin, const uint8_t* skill, int32_t skillId, int32_t level,
           int32_t link) noexcept -> VengeanceBurst {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	VengeanceBurst b {origin, skillId, level, link, RollVengeance(game, caster, skill, skillId, level, link),
	                  d2r::EvalCalc(game, caster, skill, SkCalc4, skillId, level), At<int32_t>(skill, SkParam5),
	                  At<int32_t>(skill, SkParam6), 0, 0};
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	const int32_t mark = At<int16_t>(skill, SkAuraTargetState);
	if (mark > 0 && tables != nullptr && static_cast<uint64_t>(mark) < At<uint64_t>(tables, TablesStateCount) &&
	    b.delay > 0) {
		TimedStatList(dataFlag, caster, center, skillId, level, b.delay, mark, {});
	}
	int32_t radius = d2r::EvalCalc(game, caster, skill, SkAuraRangeCalc, skillId, level);
	radius = link == 0 ? radius : std::max(radius - link, 2);
	int32_t x = 0, y = 0;
	if (radius > 0 && d2r::UnitXY(center, &x, &y)) {
		d2r::AreaCenter area {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(center), x, y};
		if (area.room != nullptr) {
			d2r::Engine<AreaFn>(AreaUnits)(game, caster, &area, radius, AreaUnitMask, VengeanceOne, &b, 0);
		}
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(center, Shatter, 0);
	return b;
}

// PD2 srvdofunc 174: a melee hit at SrcDam 128; when it hits, a burst on the target (link 0).
// shortcut: the hit centres on D2R's skill target; PD2 refreshes and reads the caster's path target, as Holy Light.
// Upgrade if a hit in game lands on a different unit (docs/unported.md).
auto SrvDoVengeance(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	if (skill == nullptr) {
		return 0;
	}
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	if (target == nullptr) {
		return 1;
	}
	const bool hit = MeleeHit(game, unit, target, skill, skillId, level, 0x80);
	VengeanceBurst b {};
	if (hit) {
		b = Burst(game, unit, target, target, skill, skillId, level, 0);
	}
	if (plog::Sample(g_vengeances) || (b.linked > 0 && plog::Sample(g_vengeanceLinkedCasts))) {
		plog::Info("vengeance: skill %d level %d melee %s: fire%% %d cold%% %d light%% %d, base %d, fire %d cold %d "
		           "light %d, cold length %d, chain %d%% every %d frames within %d: hit %lld, linked %lld, cast %u",
		           skillId, level, hit ? "hit" : "missed", b.damage.pct[0], b.damage.pct[1], b.damage.pct[2],
		           b.damage.base >> 8, b.damage.dmg[0] >> 8, b.damage.dmg[1] >> 8, b.damage.dmg[2] >> 8,
		           b.damage.coldLength, b.chance, b.delay, b.reach, static_cast<long long>(b.hit),
		           static_cast<long long>(b.linked), g_vengeances.load());
	}
	return 1;
}

// PD2 srvdofunc 162: the teleport core to the target, then D2R's Leap Attack hit (to-hit with the skill's ToHit,
// damage% calc1, SrcDam) with passive_critical_strike raised to min(itself + calc2, 100) for the hit, then aurastate
// (joust_buff) for auralencalc frames with aurastat1 (velocitypercent). PD2 reads the stat's total and sets its base
// (D2Common #10973, #10887), as this does.
// shortcut: the target is D2R's skill target (PD2 reads the caster's path target) and the hit is D2R's, which also
// gives the aurastate on a hit (the list made next extends it); upgrade if a Joust in game lands elsewhere or its buff
// differs from PD2's (docs/unported.md).
auto SrvDoJoust(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr || d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit) == nullptr ||
	    !TeleportCore(game, unit, skillId, level)) {
		return 0;
	}
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto setStat = d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat);
	const int32_t crit = unitStat(unit, CriticalStrike, 0);
	const int32_t raised = std::min(crit + d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level), 100);
	setStat(unit, CriticalStrike, raised, 0);
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(SkillMeleeHit)(game, unit, skillId, level);
	setStat(unit, CriticalStrike, crit, 0);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	const int32_t state = At<int16_t>(skill, SkAuraState), stat = At<int16_t>(skill, SkAuraStat1);
	const int32_t frames = d2r::EvalCalc(game, unit, skill, SkAuraLenCalc, skillId, level);
	const int32_t value = d2r::EvalCalc(game, unit, skill, SkAuraStatCalc1, skillId, level);
	const char* list = "none";
	if (state > 0 && tables != nullptr && static_cast<uint64_t>(state) < At<uint64_t>(tables, TablesStateCount) &&
	    frames > 0 && stat >= 0) {
		list = TimedStatList(dataFlag, unit, unit, skillId, level, frames, state, {{stat, value}});
	}
	if (plog::Sample(g_jousts)) {
		plog::Info("joust: skill %d level %d: damage%% %d, critical strike %d -> %d, state %d for %d frames with stat %d "
		           "= %d (list %s), cast %u", skillId, level, d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level),
		           crit, raised, state, frames, stat, value, list, g_jousts.load());
	}
	return 1;
}

constexpr int32_t Bash = 126, UberTalicBash = 580;

// --- Poison Dagger (PD2 srvdofunc 164 -> router 190, cltdofunc 102 -> 122; docs/research/necromancer-w11.md) ---------
// PD2's srvstfunc 68 only calls slot 32, D2R's melee start, so the merge points it there (pd2_ids.FUNCS).
std::atomic<uint32_t> g_poisonDaggers {0};
std::atomic<uint32_t> g_poisonDaggerClients {0};
constexpr int32_t PoisonDagger = 73, PoisonDaggerSpiderBoss = 550;
constexpr int32_t ClientRingCount = 6;  // PD2's cltdo 102 passes 6, whatever the skill's Param3


// The skill's rolled poison as D2R's fill makes it (poison +0x38 = min + rand(max - min), length ELen).
auto SkillPoison(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	uint8_t damage[0x180] {};  // the damage block size Leap's knockback uses (LeapKnockback)
	d2r::Engine<void (*)(Unit*, uint8_t*, int32_t, int32_t) noexcept>(d2r::rva::ElementalDamage)(unit, damage, skillId,
	                                                                                             level);
	return At<int32_t>(damage, d2r::rva::DmgPoison);
}

// PD2 adds a skill's poison to a missile's own (Poison Dagger's clouds, Cobra Strike's charge missiles).
void AddMissilePoison(Unit* missile, int32_t poison) noexcept {
	using namespace d2r::rva;
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const auto setStat = d2r::Engine<void (*)(Unit*, int32_t, int32_t, int32_t) noexcept>(SetUnitStat);
	setStat(missile, StatPoisonMin, unitStat(missile, StatPoisonMin, 0) + poison, 0);
	setStat(missile, StatPoisonMax, unitStat(missile, StatPoisonMax, 0) + poison, 0);
}

// PD2 srvdo 164 (ProjectDiablo.dll 0x103b25c0): the melee hit (D2R's own Poison Dagger do, slot 32, whose start
// already put the skill's poison in the queued hit), then a ring of Param3 srvmissilea clouds around the target, each
// with the rolled poison added to its poisonmindam and poisonmaxdam.
// shortcut: PD2 scales the poison by its rolled length over the skill's ELen; the fill sets the length to ELen, so the
// factor is 1 here. Upgrade if a length bonus lands between the two.
auto SrvDoPoisonDagger(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	int32_t tx = 0, ty = 0;
	const bool aimed = skill != nullptr && target != nullptr && d2r::UnitXY(target, &tx, &ty);  // before the hit kills it
	const int64_t ran = VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoPoisonDaggerSlot)(game, unit, skillId, level);
	if (!aimed) {
		return ran;
	}
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		plog::Warn("poison dagger: skill %d has no valid srvmissilea (row %d)", skillId, missile);
		return ran;
	}
	const int32_t poison = SkillPoison(unit, skillId, level);

	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, tx, ty, skillId, level);
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t count = At<int32_t>(skill, SkParam3);
	int32_t cloudPoison = 0, cloudLength = 0;  // as a cloud carries them, for the log
	const int32_t created = Ring(count, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		Unit* cloud = d2r::api.createMissile(game, &params);
		if (cloud != nullptr) {
			AddMissilePoison(cloud, poison);
			cloudPoison = unitStat(cloud, StatPoisonMin, 0);
			cloudLength = unitStat(cloud, StatPoisonLength, 0);
		}
		return cloud != nullptr;
	});
	if (plog::Sample(g_poisonDaggers)) {
		plog::Info("poison dagger: skill %d level %d: %d clouds (row %d, Param3 %d) at %d,%d, poison %d length %d, "
		           "cast %u", skillId, level, created, missile, count, tx, ty, cloudPoison, cloudLength,
		           g_poisonDaggers.load());
	}
	return 1;
}

// PD2 cltdo 102 (0x103ab760): the clouds' client copies, cltmissilea in a ring of ClientRingCount around the target.
auto CltDoPoisonDagger(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	Unit* target = d2r::Engine<Unit* (*)(Unit*) noexcept>(ClientSkillTargetUnit)(unit);
	int32_t tx = 0, ty = 0;
	if (skill == nullptr || target == nullptr || !d2r::UnitXY(target, &tx, &ty)) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, SkCltMissileA);
	if (!d2r::ValidMissile(dataFlag, missile)) {
		return 0;
	}
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, tx, ty, skillId, level);
	const int32_t created = Ring(ClientRingCount, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.clientCreateMissile(&params, 0) != nullptr;
	});
	if (plog::Sample(g_poisonDaggerClients)) {
		plog::Info("poison dagger client: skill %d level %d: %d clouds (row %d), cast %u", skillId, level, created, missile,
		           g_poisonDaggerClients.load());
	}
	return 1;
}

// --- Gust, Hunger (PD2 srvdofunc 156 and 165 -> router 190; docs/research/druid-w23.md) --------------------------
std::atomic<uint32_t> g_gusts {0};
std::atomic<uint32_t> g_hungers {0};
constexpr int32_t Gust = 442, Hunger = 242;  // merged ids (PD2 370, 242)

// PD2 srvdo 156 (0x103b1980): the teleport core, srvmissilea thrown at the skill's target (1.13c 0xa28f0, D2R's
// ThrowSkillMissile), then a hit on every unit within calc3 of the new point with the row's HitFlags, ResultFlags and
// HitClass and cold length calc1, no damage, through 1.13c's area apply 0xa10b0: D2R's 0x436e90, which AreaKnockback's
// callback calls with a copy of the block (unit mask 0x8583, PD2's).
auto SrvDoGust(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr || !TeleportCore(game, unit, skillId, level)) {
		return 0;
	}
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	bool thrown = false;
	if (d2r::ValidMissile(dataFlag, missile)) {
		using ThrowFn = Unit* (*)(Game*, int32_t, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
		                          int32_t) noexcept;
		const int32_t key = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(unit);
		thrown = d2r::Engine<ThrowFn>(ThrowSkillMissile)(game, missile, unit, skillId, level, key, 0, 0, 0, 0, 1) != nullptr;
	}
	const int32_t chill = d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	const int32_t radius = d2r::EvalCalc(game, unit, skill, SkCalc3, skillId, level);
	d2r::LeapKnockback hit;
	hit.hitFlags = At<uint32_t>(skill, SkHitFlags);
	hit.resultFlags = At<uint16_t>(skill, SkResultFlags);
	auto* block = reinterpret_cast<uint8_t*>(&hit);
	*reinterpret_cast<int32_t*>(block + DamageColdLength) = chill;
	*reinterpret_cast<int32_t*>(block + DamageHitClass) = At<int32_t>(skill, SkHitClass);
	if (radius > 0) {
		uint8_t scratch[0x20] {};  // as Leap's landing passes it: only its byte +0x18 (0) is read
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, d2r::LeapKnockback*, int32_t, void*) noexcept>(
			AreaKnockback)(game, unit, 0, 0, radius, &hit, 0, scratch);
	}
	if (plog::Sample(g_gusts)) {
		plog::Info("gust: skill %d level %d: missile %d %s, radius %d (calc3), cold length %d (calc1), hit flags %u, "
		           "result flags %u, hit class %d, cast %u, refused %u", skillId, level, missile,
		           thrown ? "thrown" : "not thrown", radius, chill, hit.hitFlags, hit.resultFlags,
		           At<int32_t>(skill, SkHitClass), g_gusts.load(), g_teleportsRefused.load());
	}
	return 1;
}

// PD2 srvdo 165 (0x103b27c0): the target (a corpse, TargetCorpse) gets corpse_nodraw, then the caster gets aurastate
// for auralencalc frames with aurastat1-6 = aurastatcalc1-6, through PD2's state helper 0x1036c920 (the same recast
// rule as TimedStatList's). An empty aurastat1 gives no list; a later empty one is skipped. PD2 reads the path's
// target unit, D2R's own Hunger (srvdo 122) the skill's: the same corpse.
// shortcut: an extended list's new timer is not sent to the client (PD2 sends packet 0x83), as for Teleport.
// Upgrade if the client's hunger state runs out early in play.
auto SrvDoHunger(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	Unit* corpse = skill != nullptr ? d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit) : nullptr;
	if (corpse == nullptr || corpse == unit) {
		return 0;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(corpse, CorpseNoDraw, 1);
	d2r::api.setUnitSkillFlag(unit, 1);  // PD2 sets unit flag 0x40 on every path past the corpse
	StatValue stats[6] {};
	size_t count = 0;
	for (int i = 0; i < 6; i++) {
		const int32_t stat = At<int16_t>(skill, SkAuraStat1 + i * 2);
		// shortcut: PD2 also skips a stat past itemstatcost; the merged rows name none. Upgrade if a row ever does.
		if (stat < 0) {
			if (i == 0) {
				break;
			}
			continue;
		}
		stats[count++] = {stat, d2r::EvalCalc(game, unit, skill, SkAuraStatCalc1 + i * 4, skillId, level)};
	}
	const int32_t state = At<int16_t>(skill, SkAuraState);
	const int32_t frames = d2r::EvalCalc(game, unit, skill, SkAuraLenCalc, skillId, level);
	const char* list = "none";
	if (count > 0 && state > 0 && frames > 0) {  // PD2's state helper refuses a length under 1
		list = TimedStatList(dataFlag, unit, unit, skillId, level, frames, state, stats, stats + count);
	}
	if (plog::Sample(g_hungers)) {
		plog::Info("hunger: skill %d level %d: corpse eaten, state %d for %d frames (list %s), stats %d=%d %d=%d %d=%d "
		           "%d=%d, cast %u", skillId, level, state, frames, list, stats[0].stat, stats[0].value, stats[1].stat,
		           stats[1].value, stats[2].stat, stats[2].value, stats[3].stat, stats[3].value, g_hungers.load());
	}
	return 1;
}

// --- Assassin charge-up finishers (PD2 srvdofunc 170 -> router 190, srvprg 171 -> 184, 172 -> 185, 39 -> 186,
// 139 -> 181; docs/research/w29-assassin-finishers.md) ---------------------------------------------------------------
// Tiger Strike, Fists of Fire, Cobra Strike, Claws of Thunder, Blades of Ice, Royal Strike, RakanothStrike (merged ids)
constexpr int32_t TigerStrike = 254, FistsOfFire = 259, CobraStrike = 265, ClawsOfThunder = 269, BladesOfIce = 274,
                  RoyalStrike = 280, RakanothStrike = 630;
constexpr int32_t ChargeUpSkills[] = {TigerStrike, FistsOfFire, CobraStrike, ClawsOfThunder, BladesOfIce, RoyalStrike,
                                      RakanothStrike};
constexpr int32_t ProgressiveSteal = 319;  // aurastat1 of Cobra Strike and RakanothStrike (the same id in PD2)
constexpr int32_t StatLifeDrain = 60, StatManaDrain = 62;  // lifedrainmindam, manadrainmindam: D2R and PD2
// One counter per finisher (and one for any other skill), so the log shows each finisher's first hit and prg call
using FinisherCounters = std::atomic<uint32_t>[std::size(ChargeUpSkills) + 1];
FinisherCounters g_chargeUps {};
FinisherCounters g_chargeSpreads {};
FinisherCounters g_chargeRings {};
FinisherCounters g_chargeFires {};
FinisherCounters g_chargeColds {};
std::atomic<uint32_t> g_chargeHits {0};

auto FinisherSample(FinisherCounters& counters, int32_t skillId) noexcept -> bool {
	const auto* it = std::find(std::begin(ChargeUpSkills), std::end(ChargeUpSkills), skillId);
	return plog::Sample(counters[it - std::begin(ChargeUpSkills)]);
}

auto ListStat(uint32_t dataFlag, void* list, int32_t stat) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(uint32_t, void*, int32_t, int32_t) noexcept>(d2r::rva::ListStatValue)(dataFlag, list,
	                                                                                                   stat, 0);
}

// The skill's charges: aurastat1 on its aurastate list (PD2's 0x1037ddf0), 0 with none.
auto Charges(uint32_t dataFlag, Unit* unit, const uint8_t* skill) noexcept -> int32_t {
	const int32_t state = At<int16_t>(skill, d2r::rva::SkAuraState), stat = At<int16_t>(skill, d2r::rva::SkAuraStat1);
	void* list = state >= 0 && stat >= 0
	                 ? d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(d2r::rva::UnitStateList)(unit, state)
	                 : nullptr;
	return list != nullptr ? ListStat(dataFlag, list, stat) : 0;
}

// PD2's shared charges (0x1036ee80): the highest charge count over the unit's progressive states, each read as the
// charges of the skill that made the list (stat 350), clamped to 0-3. With `clear`, every progressive list is
// dropped as well (PD2 does that when the count is 3).
auto SharedCharges(uint32_t dataFlag, Unit* unit, bool clear) noexcept -> int32_t {
	using namespace d2r::rva;
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	if (tables == nullptr) {
		return 0;
	}
	const auto* states = At<const int16_t*>(tables, TablesProgressiveStates);
	const int64_t count = At<int64_t>(tables, TablesProgressiveCount);
	int32_t most = 0;
	for (int64_t i = 0; states != nullptr && i < count; ++i) {
		void* list = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, states[i]);
		if (list == nullptr) {
			continue;
		}
		if (clear) {
			// as RemoveSkillState does per state: progressive states have no group, so it would remove nothing
			d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(ClearUnitState)(unit, states[i], 0);
			d2r::Engine<void (*)(Unit*, void*) noexcept>(DetachStatList)(unit, list);
			d2r::Engine<void (*)(uint32_t, void*) noexcept>(FreeStatList)(dataFlag, list);
			continue;
		}
		const uint8_t* maker = d2r::api.getSkillRecord(dataFlag, ListStat(dataFlag, list, StatListSkill));
		if (maker != nullptr && At<int16_t>(maker, SkAuraStat1) >= 0) {
			most = std::max(most, ListStat(dataFlag, list, At<int16_t>(maker, SkAuraStat1)));
		}
	}
	return std::clamp(most, 0, 3);
}

// Cobra Strike's steal (PD2 srvdo 170 at its 2nd charge): a 3-frame stat list with lifedrainmindam and
// manadrainmindam = calc1 and damagepercent = calc2. PD2 builds it with list flags 4 and no unit event.
void AddStealList(Game* game, Unit* unit, const uint8_t* skill, int32_t skillId, int32_t level) noexcept {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const int32_t id = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(UnitId)(unit, __FILE__, __LINE__);
	void* list = d2r::Engine<void* (*)(int32_t, int32_t, int32_t, int32_t) noexcept>(NewStatList)(
		4, At<int32_t>(game, GameFrame) + 3, At<int32_t>(unit, UnitType), id);
	if (list == nullptr) {
		return;
	}
	const auto setStat = d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat);
	const int32_t steal = d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level);
	setStat(dataFlag, list, StatLifeDrain, steal, 0);
	setStat(dataFlag, list, StatManaDrain, steal, 0);
	setStat(dataFlag, list, StatDamagePercent, d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level), 0);
	d2r::Engine<void (*)(Unit*, void*, int32_t) noexcept>(AttachStatList)(unit, list, 1);
}

// PD2 srvdo 170 (ProjectDiablo.dll 0x103b2c60): a charge-up hit. Shared charges: a count of 3 on any progressive
// state clears them all first; a hit then tops this skill up to the highest count + 1 (at most 3), fires
// srvprgfunc[charges] at once and sends the charge packet. The hit itself is D2R's charge-up do function (srvdo 34,
// whose AddCharge on a hit is the first charge); PD2 calls no hand-alternating 35.
// shortcut: the hit is D2R's (to-hit without PD2's added progressive_tohit, a hit counted on the queued result
// where PD2 skips results 0x8390 on monsters and takes back the charges if the applied hit misses), the prg function
// fires after the hit is applied (PD2: just before), and PD2's in-build progressive bonuses (vanilla 0x922a0 /
// 0x91e30, which D2R only runs inside its release) are not run.
// Upgrade with a port of srvdo 34's hit if play shows the charge counts or damage off PD2's (docs/unported.md).
auto SrvDoChargeUp(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	int32_t shared = SharedCharges(dataFlag, unit, false);
	if (shared == 3) {
		SharedCharges(dataFlag, unit, true);
		shared = 0;
	}
	const int32_t before = Charges(dataFlag, unit, skill);
	const int32_t state = At<int16_t>(skill, SkAuraState), stat = At<int16_t>(skill, SkAuraStat1);
	// PD2 adds the steal at the 2nd charge, before the hit's damage, so its leech counts on that hit. The hit isn't
	// known yet here: the list goes on whenever a hit would pass 2, and a miss keeps it its 3 frames.
	const bool steal = stat == ProgressiveSteal && before <= 1 && shared >= 1;
	if (steal) {
		AddStealList(game, unit, skill, skillId, level);
	}
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	const int64_t ran = VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoChargeUpSlot)(game, unit, skillId, level);
	int32_t charges = Charges(dataFlag, unit, skill);
	int32_t prg = 0, prgCalc = 0;
	if (charges > before && state >= 0 && stat >= 0) {
		g_chargeHits.fetch_add(1);
		const auto addCharge = d2r::Engine<int64_t (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t) noexcept>(AddCharge);
		while (charges <= shared && charges < 3) {
			addCharge(game, unit, skillId, level, state, stat);
			const int32_t now = Charges(dataFlag, unit, skill);
			if (now <= charges) {
				break;  // AddCharge made no list: stop rather than spin
			}
			charges = now;
		}
		const int32_t n = std::min(charges, 3);
		prg = At<int16_t>(skill, SkSrvPrgFunc1 + (n - 1) * 2);
		prgCalc = d2r::EvalCalc(game, unit, skill, SkPrgCalc1 + (n - 1) * 4, skillId, level);
		const SrvDoFn fn = prg > 0 && prg < SrvDoSlots ? VanillaSlot<SrvDoFn>(SrvDoTable, prg) : nullptr;
		if (fn != nullptr) {
			fn(game, unit, skillId, level);
		} else {
			prg = 0;
		}
		// the packet's seed is the unit's next random number, the seed itself left as it was (PD2 and the release)
		const uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
		uint32_t next[2] = {seed[0], seed[1]};
		d2r::RandomBelow(next, 1);
		using PacketFn = void (*)(Unit*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept;
		d2r::Engine<PacketFn>(SendChargePacket)(unit, target, skillId, level, static_cast<int32_t>(next[0]), 0, n);
	}
	if (FinisherSample(g_chargeUps, skillId)) {
		plog::Info("charge up: skill %d level %d: shared %d, charges %d -> %d, steal %d, prg %d (prgcalc %d), hits %u",
		           skillId, level, shared, before, charges, steal ? 1 : 0, prg, prgCalc, g_chargeHits.load());
	}
	return ran;
}

// The prg functions' charge (PD2 reads it with 0x1037ddf0 and runs only at 1-3) and the missile it picks.
struct ChargeMissile {
	const uint8_t* skill = nullptr;  // null: don't run
	int32_t charge = 0, missile = -1, count = 0;
};

auto ChargeMissileOf(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> ChargeMissile {
	const uint32_t dataFlag = d2r::DataFlag(game);
	ChargeMissile m;
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return m;
	}
	m.charge = Charges(dataFlag, unit, skill);
	m.missile = d2r::api.pickSkillMissile(unit, skillId);
	if (m.charge < 1 || m.charge > 3 || !d2r::ValidMissile(dataFlag, m.missile)) {
		return m;
	}
	m.count = d2r::EvalCalc(game, unit, skill, d2r::rva::SkPrgCalc1 + (m.charge - 1) * 4, skillId, level);
	m.skill = skill;
	return m;
}

// PD2's charge spread (0x1037d780): Multiple Shot's line through the target, stepping the other way round, with
// PD2's own handling of a target within 2 subtiles.
struct Spread {
	int32_t x, y, dx, dy;  // first aim point and the step to the next
};

constexpr auto ChargeSpread(int32_t ux, int32_t uy, int32_t tx, int32_t ty, int32_t count) noexcept -> Spread {
	int32_t dx = ux - tx, dy = uy - ty, d2 = dx * dx + dy * dy;
	if (d2 < 4) {
		dx *= 4;
		dy = d2;  // sic: PD2 keeps the old length here, not dy * 4
		d2 = d2 * d2 + dx * dx;
	}
	int32_t sx = d2 >= 16 ? dy : 2 * dy, sy = -(d2 >= 16 ? dx : 2 * dx);
	while (sx * sx + sy * sy > 3) {
		sx /= 2;
		sy /= 2;
	}
	return {tx - sx * count / 2, ty - sy * count / 2, sx, sy};
}
static_assert(ChargeSpread(0, 0, 10, 0, 3).x == 10 && ChargeSpread(0, 0, 10, 0, 3).y == -1);  // 3 across the aim
static_assert(ChargeSpread(0, 0, 10, 0, 3).dx == 0 && ChargeSpread(0, 0, 10, 0, 3).dy == 1);

// PD2 srvprg 171 (0x103b30a0), here 184: prgcalc[charge] missiles of the charge's missile from the caster to points
// along ChargeSpread; Cobra Strike's carry the skill's poison (PD2 scales it by its rolled length over ELen, which is
// 1 here as in Poison Dagger).
auto SrvPrgSpread(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const ChargeMissile m = ChargeMissileOf(game, unit, skillId, level);
	int32_t ux = 0, uy = 0, tx = 0, ty = 0;
	if (m.skill == nullptr || !d2r::UnitXY(unit, &ux, &uy) || d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0) {
		return 0;
	}
	const int32_t poison = skillId == CobraStrike ? SkillPoison(unit, skillId, level) : 0;
	const int32_t count = ClampCount(m.count);
	d2r::MissileParams params =
		SkillMissileParams(unit, MissileFromOrigin | MissileFlag10000, m.missile, ux, uy, skillId, level);  // PD2's 0x10820
	params.activation = 1;  // PD2 sets 1.13c's +0x44 to 1
	Spread s = ChargeSpread(ux, uy, tx, ty, count);
	int32_t created = 0;
	for (int32_t i = 0; i < count; ++i, s.x += s.dx, s.y += s.dy) {
		params.targetX = s.x;
		params.targetY = s.y;
		Unit* missile = d2r::api.createMissile(game, &params);
		if (missile != nullptr) {
			++created;
			if (poison != 0) {
				AddMissilePoison(missile, poison);
			}
		}
	}
	if (FinisherSample(g_chargeSpreads, skillId)) {
		plog::Info("charge spread: skill %d level %d charge %d: %d of %d missiles (row %d, prgcalc %d), poison %d",
		           skillId, level, m.charge, created, count, m.missile, m.count, poison);
	}
	return 1;
}

// PD2 srvprg 172 (0x103b3360), here 185: a ring of prgcalc[charge] (Ring's count) of the charge's missile at the
// target, or at the caster with none; Cobra Strike's carry the skill's poison as in the spread.
auto SrvPrgRing(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const ChargeMissile m = ChargeMissileOf(game, unit, skillId, level);
	Unit* target = d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(SkillTargetUnit)(game, unit);
	int32_t x = 0, y = 0;
	if (m.skill == nullptr || !d2r::UnitXY(target != nullptr ? target : unit, &x, &y)) {
		return 0;
	}
	const int32_t poison = skillId == CobraStrike ? SkillPoison(unit, skillId, level) : 0;
	d2r::MissileParams params =
		SkillMissileParams(unit, MissileAtXY | MissileAimOffset, m.missile, x, y, skillId, level);  // PD2's 3
	const int32_t created = Ring(m.count, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		Unit* missile = d2r::api.createMissile(game, &params);
		if (missile != nullptr && poison != 0) {
			AddMissilePoison(missile, poison);
		}
		return missile != nullptr;
	});
	if (FinisherSample(g_chargeRings, skillId)) {
		plog::Info("charge ring: skill %d level %d charge %d: %d missiles (row %d, prgcalc %d) at %d,%d, poison %d",
		           skillId, level, m.charge, created, m.missile, m.count, x, y, poison);
	}
	return 1;
}

// PD2 srvprg 39 (0x103aee00), here 186: vanilla srvdo 38, then 39 (Fists of Fire's first charge runs both of
// vanilla's later fire charges); D2R's 38 and 39 are taken to match 1.13c's.
auto SrvPrgFire(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	const int32_t charge = skill != nullptr ? Charges(d2r::DataFlag(game), unit, skill) : 0;
	if (charge < 1 || charge > 3) {
		return 0;  // PD2's 0x1037ddf0 rule, as in ChargeMissileOf
	}
	const int64_t first = VanillaSlot<SrvDoFn>(d2r::rva::SrvDoTable, d2r::rva::SrvDoFireCharge1)(game, unit, skillId, level);
	const int64_t second = VanillaSlot<SrvDoFn>(d2r::rva::SrvDoTable, d2r::rva::SrvDoFireCharge2)(game, unit, skillId, level);
	if (FinisherSample(g_chargeFires, skillId)) {
		plog::Info("charge fire: skill %d level %d charge %d: srvdo 38 %lld, 39 %lld", skillId, level, charge,
		           static_cast<long long>(first), static_cast<long long>(second));
	}
	return 1;
}

// PD2 srvprg 139 (0x103b0cf0), here 181: D2R's cold missile (srvdo 139, 0x585440) with the charge's missile, where
// D2R's takes srvmissilea: one missile thrown at the skill's point, with the aim offset D2R's 139 sets.
auto SrvPrgCold(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const int32_t missile = d2r::api.pickSkillMissile(unit, skillId);
	const int32_t charge = skill != nullptr ? Charges(dataFlag, unit, skill) : 0;
	if (charge < 1 || charge > 3 || missile <= 0 || !d2r::ValidMissile(dataFlag, missile)) {
		return 0;
	}
	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0 || !d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	using ThrowFn = Unit* (*)(Game*, int32_t, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
	                          int32_t) noexcept;
	const int32_t key = d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitStateKey)(unit);
	Unit* thrown = d2r::Engine<ThrowFn>(ThrowSkillMissile)(game, missile, unit, skillId, level, key, 0, 0, 0, 0, 0);
	if (thrown == nullptr) {
		return 0;
	}
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileBounces)(thrown, uy - ty);
	d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetMissileTargetDx)(thrown, tx - ux);
	if (FinisherSample(g_chargeColds, skillId)) {
		plog::Info("charge cold: skill %d level %d charge %d: missile row %d", skillId, level, charge, missile);
	}
	return 1;
}

// --- Firestorm and the wall makers (PD2 srvdofunc 166 -> router 190, cltdofunc 119) ------------------------------
// Read from ProjectDiablo.dll: srvdo 166 at 0x103b29d0, cltdo 119 at 0x103acb70, their maker callback 0x1036f220
// (docs/research/druid-w22.md). calc1 makers of srvmissilea (the client reads it too), all from the caster to the
// target, each with the maker callback; D2R's Firestorm (srvdo 117) sends its first maker without it and DiabWall's
// (srvdo 102) skips the path one time in five.
std::atomic<uint32_t> g_walls {0};
std::atomic<uint32_t> g_wallClients {0};
constexpr int32_t DiabWall = 197, Firestorm = 225, FingermageBossDiabWall = 473, UberDiabWall = 502,
                  KanemithFirestorm = 632;  // merged ids

// shortcut: PD2 seeds each maker from a counter it keeps on the caster (+0xcc, bumped per maker); D2R's callback 1
// seeds from its argument plus a path value, so each maker gets the next value of one game-wide counter. Upgrade if
// the walls' paths look less spread than PD2's in play.
// shortcut: makers are capped at 64 (PD2 has no cap; no PD2 row gets near it), upgrade if a row ever needs more.
std::atomic<int32_t> g_makerSeeds {0};
template <typename Create>
auto Walls(Unit* unit, const uint8_t* skill, int32_t count, int32_t skillId, int32_t level,
           int32_t tx, int32_t ty, Create create) noexcept -> int32_t {
	int32_t ux = 0, uy = 0;
	if (!d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = 0x21;  // D2R's srvdo 102 / 117 and PD2's 166: start at x/y
	params.owner = unit;
	params.missile = At<int16_t>(skill, d2r::rva::SkSrvMissileA);
	params.x = ux;
	params.y = uy;
	params.targetX = tx;
	params.targetY = ty;
	params.skill = skillId;
	params.skillLevel = level;
	params.callback = d2r::rva::MissileCallbackMaker;
	int32_t created = 0;
	for (int32_t i = 0; i < std::min(count, 64); ++i) {
		params.callbackArg = g_makerSeeds.fetch_add(1);
		created += create(&params) ? 1 : 0;
	}
	return created;
}

auto SrvDoWall(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	int32_t tx = 0, ty = 0;
	if (!d2r::ValidMissile(dataFlag, At<int16_t>(skill, d2r::rva::SkSrvMissileA)) ||
	    d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const int32_t count = d2r::EvalCalc(game, unit, skill, d2r::rva::SkCalc1, skillId, level);
	const int32_t created = Walls(unit, skill, count, skillId, level, tx, ty, [&](d2r::MissileParams* p) {
		return d2r::api.createMissile(game, p) != nullptr;
	});
	if (plog::Sample(g_walls)) {
		plog::Info("wall: skill %d level %d: %d of %d makers (row %d), cast %u", skillId, level, created, count,
		           At<int16_t>(skill, d2r::rva::SkSrvMissileA), g_walls.load());
	}
	return 1;
}

auto CltDoWall(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	int32_t tx = 0, ty = 0;
	if (!d2r::ValidMissile(dataFlag, At<int16_t>(skill, d2r::rva::SkSrvMissileA)) ||
	    d2r::api.clientSkillTargetXY(unit, &tx, &ty) == 0) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const int32_t count =
		d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::rva::SkCalc1), skillId, level);
	const int32_t created = Walls(unit, skill, count, skillId, level, tx, ty, [](d2r::MissileParams* p) {
		return d2r::api.clientCreateMissile(p, 0) != nullptr;
	});
	if (plog::Sample(g_wallClients)) {
		plog::Info("wall client: skill %d level %d: %d of %d makers, cast %u", skillId, level, created, count,
		           g_wallClients.load());
	}
	return 1;
}

// --- Combustion and the boss novas (PD2 srvdofunc 159 -> router 190, cltdofunc 100 -> 120) ----------------------------
// Read from ProjectDiablo.dll: srvdo 159 at 0x103b1d00, cltdo 100 at 0x103ab580
// (docs/research/w27-static-field-combustion.md). A ring of calc2 missiles of the skill's missile a around the caster.
std::atomic<uint32_t> g_novas {0};
std::atomic<uint32_t> g_novaClients {0};
constexpr int32_t Combustion = 448, FingermageBossNova = 472, RathmaTeethNova = 561, RathmaOuterNova = 567,
                  RathmaPacman = 571, LeoricSummonNova = 595, ZharMiniBossCombustion = 617;  // merged ids

auto SrvDoNova(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const int32_t missile = At<int16_t>(skill, SkSrvMissileA);
	int32_t x = 0, y = 0;
	if (!d2r::ValidMissile(dataFlag, missile) || !d2r::UnitXY(unit, &x, &y)) {
		return 0;
	}
	const int32_t count = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
	const int32_t created = ServerRing(game, unit, missile, x, y, count, skillId, level);
	if (plog::Sample(g_novas)) {
		plog::Info("nova: skill %d level %d: %d missiles (row %d, calc2 %d) at %d,%d, cast %u", skillId, level, created,
		           missile, count, x, y, g_novas.load());
	}
	return 1;
}

// Client half (PD2 cltdo 100): the same ring of calc2 cltmissilea.
auto CltDoNova(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	if (skill == nullptr) {
		return 0;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	const int32_t missile = At<int16_t>(skill, SkCltMissileA);
	int32_t x = 0, y = 0;
	if (!d2r::ValidMissile(dataFlag, missile) || !d2r::UnitXY(unit, &x, &y)) {
		return 0;
	}
	const int32_t count = d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, SkCalc2), skillId, level);
	d2r::MissileParams params = SkillMissileParams(unit, MissileAtXY | MissileAimOffset, missile, x, y, skillId, level);
	const int32_t created = Ring(count, [&](int32_t dx, int32_t dy) {
		params.targetX = dx;
		params.targetY = dy;
		return d2r::api.clientCreateMissile(&params, 0) != nullptr;
	});
	if (plog::Sample(g_novaClients)) {
		plog::Info("nova client: skill %d level %d: %d missiles (row %d, calc2 %d), cast %u", skillId, level, created,
		           missile, count, g_novaClients.load());
	}
	return 1;
}

// --- Static Field (PD2 srvdofunc 160 -> router 190) ------------------------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 160 at 0x103b1dc0, per-unit damage 0x10302830, per-unit debuff 0x1036c920
// (docs/research/w27-static-field-combustion.md). Two passes over the units within aurarangecalc that aurafilter
// takes: lightning damage from current life (StaticFieldDamage), then auratargetstate with aurastat1-6 for
// auralencalc frames.
std::atomic<uint32_t> g_staticFields {0};
std::atomic<uint32_t> g_staticFieldHits {0};  // the first cast that hits a unit is logged too
constexpr int32_t StaticField = 42, MercStaticField = 528, StaticFieldProc = 679;  // merged ids
// Monsters PD2's damage pass skips, as merged monstats ids: monhydra3, dopplezonnew, rathmaBone, rathmaPoison,
// rathmaBoneClone, wraithMapMod
constexpr int32_t NoStaticField[] = {806, 950, 951, 952, 953, 1129};
// Mercenaries (D2Common #11104's hireling classes), as merged monstats ids: their resist reductions are never halved
constexpr int32_t Hirelings[] = {271, 338, 359, 561, 562};

// PD2's a * b / max(c, 1) in doubles, truncated, INT32_MAX past it (0x1037c4c0)
constexpr auto MulDiv(int32_t a, double b, int32_t c) noexcept -> int32_t {
	const double v = static_cast<double>(a) * b / static_cast<double>(c > 1 ? c : 1);
	return v > 2147483647.0 ? INT32_MAX : (v < -2147483648.0 ? INT32_MIN : static_cast<int32_t>(v));
}

// D2Common #11027: current life as a percentage of max life, in whole points
constexpr auto LifePercent(int32_t life, int32_t maxLife) noexcept -> int32_t {
	return (maxLife >> 8) == 0 ? 0 : (life >> 8) * 100 / (maxLife >> 8);
}

// PD2's damage on one unit, in life's 1/256 (0x10302830): calc1 percent of current life, at least calc2; if that,
// lowered by lightresist, would beat (100 - StaticFieldMin) percent of max life (itself lowered by the resist, never
// raised), that max-life share instead. The resist is applied again when the hit lands.
constexpr auto StaticFieldDamage(int32_t life, int32_t maxLife, int32_t calc1, int32_t calc2, int32_t fieldMin,
                                 int32_t resist) noexcept -> int32_t {
	const int32_t cap = MulDiv(maxLife, static_cast<double>(static_cast<uint32_t>(100 - fieldMin)), 100);
	const int32_t damage = MulDiv(life, calc1, 100);
	const int32_t resisted = MulDiv(damage, 100 - resist, 100);
	const int32_t capResisted = MulDiv(cap, 100 - resist, 100);
	const int32_t limit = capResisted > cap ? cap : capResisted;
	const int32_t out = static_cast<uint32_t>(damage) < static_cast<uint32_t>(calc2) ? calc2 : damage;
	return resisted > limit ? cap : out;
}
static_assert(StaticFieldDamage(1000 << 8, 1000 << 8, 8, 0, 85, 0) == 80 << 8);     // 8% of 1000 life
static_assert(StaticFieldDamage(1000 << 8, 1000 << 8, 25, 0, 85, 0) == 150 << 8);   // capped at 15% of max life
static_assert(StaticFieldDamage(100 << 8, 1000 << 8, 8, 20 << 8, 0, 0) == 20 << 8);  // calc2's minimum wins
static_assert(StaticFieldDamage(1000 << 8, 1000 << 8, 8, 0, 85, 50) == 80 << 8);    // a resist leaves the cast as is
static_assert(StaticFieldDamage(1000 << 8, 1000 << 8, 10, 0, 92, -50) == 80 << 8);  // 150 resisted > 80: the cap

// PD2's 0x1036df10: a resist reduction (damage, magic, fire, light, cold, poison resist) on a monster that is not a
// mercenary and already resists 100 or more is halved.
constexpr auto ImmuneHalved(int32_t value, int32_t stat, bool monsterNotHireling, int32_t current) noexcept -> int32_t {
	const bool resist = stat == 36 || stat == 37 || stat == 39 || stat == 41 || stat == 43 || stat == 45;
	return resist && value < 1 && monsterNotHireling && current > 99 ? value / 2 : value;
}
static_assert(ImmuneHalved(-25, 41, true, 100) == -12);
static_assert(ImmuneHalved(-25, 41, true, 99) == -25);
static_assert(ImmuneHalved(-25, 41, false, 120) == -25);
static_assert(ImmuneHalved(-25, 7, true, 120) == -25);

struct StaticFieldHit {
	int32_t calc1, calc2, fieldMin;
	int32_t hits = 0;
	int32_t life = 0, maxLife = 0, resist = 0, damage = 0;  // the first unit hit, for the log
};

constexpr size_t AuraStats = 6;  // aurastat1-6

struct StaticFieldDebuff {
	int32_t skillId, level, frames, state;
	StatValue stats[AuraStats] {};
	size_t count = 0;
	int32_t lists = 0, value = 0;  // new lists, and the first one's aurastat1 value, for the log
};

// One unit in range (PD2's 0x10302830), the hit built as D2R's Static Field (0x5556a0) builds it.
auto StaticFieldOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* hit = static_cast<StaticFieldHit*>(area->arg);
	auto* game = static_cast<Game*>(area->game);
	if (victim == nullptr || game == nullptr || area->unit == nullptr ||
	    std::find(std::begin(NoStaticField), std::end(NoStaticField), At<int32_t>(victim, UnitClassId)) !=
	        std::end(NoStaticField)) {
		return 0;
	}
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t life = unitStat(victim, StatLife, 0), maxLife = unitStat(victim, StatMaxLife, 0);
	if (life < 0x100 || static_cast<uint32_t>(LifePercent(life, maxLife)) <= static_cast<uint32_t>(hit->fieldMin)) {
		return 0;
	}
	const int32_t resist = unitStat(victim, StatLightResist, 0);
	const int32_t amount = StaticFieldDamage(life, maxLife, hit->calc1, hit->calc2, hit->fieldMin, resist);
	d2r::LeapKnockback damage;
	damage.resultFlags = 0x4001;
	constexpr uint32_t Lightning = 2;  // PD2 deals lightning whatever the row's EType
	d2r::Engine<void (*)(Unit*, void*, uint32_t, int32_t, int32_t, void*, void*) noexcept>(AddElementDamage)(
		area->unit, &damage, Lightning, amount, 0, nullptr, nullptr);
	*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&damage) + DamageHitClass) |= 0xd;
	*(reinterpret_cast<uint8_t*>(&damage) + DamageFlag148) = 1;
	d2r::Engine<void (*)(Game*, Unit*, Unit*, int32_t, void*) noexcept>(HitEvents)(game, area->unit, victim, 1, &damage);
	d2r::Engine<void (*)(Game*, Unit*, Unit*, void*) noexcept>(DealDamage)(game, area->unit, victim, &damage);
	ReleaseDamage(damage);
	if (hit->hits++ == 0) {
		hit->life = life;
		hit->maxLife = maxLife;
		hit->resist = resist;
		hit->damage = amount;
	}
	return 1;
}

// One unit in range (PD2's 0x1036c920): the debuff list, owned by the caster.
// shortcut: PD2's state helper (0x1036d7d0) also refuses some monsters by monstats flags; NewStateList's own checks
// stand in (docs/unported.md). Upgrade if a monster PD2 leaves alone shows the debuff in play.
auto StaticFieldDebuffOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	using namespace d2r::rva;
	auto* debuff = static_cast<StaticFieldDebuff*>(area->arg);
	auto* game = static_cast<Game*>(area->game);
	if (victim == nullptr || game == nullptr || debuff->frames < 1) {
		return 0;
	}
	const int32_t classId = At<int32_t>(victim, UnitClassId);
	const bool monster = At<int32_t>(victim, UnitType) != 0 &&
	                     std::find(std::begin(Hirelings), std::end(Hirelings), classId) == std::end(Hirelings);
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	StatValue stats[AuraStats] {};
	for (size_t i = 0; i < debuff->count; i++) {
		const StatValue& s = debuff->stats[i];
		stats[i] = {s.stat, ImmuneHalved(s.value, s.stat, monster, unitStat(victim, s.stat, 0))};
	}
	const char* list = TimedStatList(d2r::DataFlag(game), area->unit, victim, debuff->skillId, debuff->level,
	                                 debuff->frames, debuff->state, stats, stats + debuff->count);
	if (std::strcmp(list, "new") == 0 && debuff->lists++ == 0) {
		debuff->value = stats[0].value;
	}
	return 1;
}

// PD2 srvdo 160: the damage pass, then the debuff pass (with AreaUnits' flag 1, as PD2 passes it). 0 when aurastat1
// is not a stat, after the damage, as PD2.
// D2R's Static Field asks 0x432c40 (an online cast limit) first; PD2 has none, so neither does this.
auto SrvDoStaticField(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const auto* difficulty = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(DifficultyRecord)(
		dataFlag, At<uint8_t>(game, GameDifficulty));
	if (skill == nullptr || difficulty == nullptr) {
		return 0;
	}
	const auto calc = [&](int field) { return d2r::EvalCalc(game, unit, skill, field, skillId, level); };
	StaticFieldHit hit {calc(SkCalc1), calc(SkCalc2), At<int32_t>(difficulty, DiffStaticFieldMin)};
	const int32_t radius = calc(SkAuraRangeCalc);
	const auto mask = At<uint32_t>(skill, SkAuraFilter);
	d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit), 0, 0};  // 0, 0: at the caster
	const auto area = d2r::Engine<AreaFn>(AreaUnits);
	area(game, unit, &center, radius, mask, StaticFieldOne, &hit, 0);

	StaticFieldDebuff debuff {skillId, level, calc(SkAuraLenCalc), At<int16_t>(skill, SkAuraTargetState)};
	for (int i = 0; i < static_cast<int>(AuraStats); i++) {
		const int32_t stat = At<int16_t>(skill, SkAuraStat1 + i * 2);
		if (stat < 0) {
			if (i == 0) {
				return 0;
			}
			continue;
		}
		debuff.stats[debuff.count++] = {stat, calc(SkAuraStatCalc1 + i * 4)};
	}
	area(game, unit, &center, radius, mask, StaticFieldDebuffOne, &debuff, 1);
	if (plog::Sample(g_staticFields) || (hit.hits > 0 && plog::Sample(g_staticFieldHits))) {
		plog::Info("static field: skill %d level %d: radius %d, calc1 %d, calc2 %d, min %d; units hit %d, first: life "
		           "%d of %d, lightresist %d, damage %d; state %d for %d frames, stat %d %d, new lists %d (first %d), "
		           "cast %u",
		           skillId, level, radius, hit.calc1, hit.calc2, hit.fieldMin, hit.hits, hit.life, hit.maxLife,
		           hit.resist, hit.damage, debuff.state, debuff.frames, debuff.stats[0].stat, debuff.stats[0].value,
		           debuff.lists, debuff.value, g_staticFields.load());
	}
	return 1;
}

// srvdofunc 190 serves the PD2 do functions that have no free slot of their own, by skill id (docs/core-rules.md).
auto SrvDoRouter(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	switch (skillId) {
	case Bash:
	case UberTalicBash:
		return SrvDoBash(game, unit, skillId, level);
	case PoisonDagger:
	case PoisonDaggerSpiderBoss:
		return SrvDoPoisonDagger(game, unit, skillId, level);
	case BloodWarp:
		return SrvDoBloodWarp(game, unit, skillId, level);
	case Teleport:
	case Blink:
	case LucionBlink:
	case MonBloodwarp:
		return SrvDoPd2Teleport(game, unit, skillId, level);
	case Desecrate:
		return SrvDoDesecrate(game, unit, skillId, level);
	case Joust:
		return SrvDoJoust(game, unit, skillId, level);
	case Combustion:
	case FingermageBossNova:
	case RathmaTeethNova:
	case RathmaOuterNova:
	case RathmaPacman:
	case LeoricSummonNova:
	case ZharMiniBossCombustion:
		return SrvDoNova(game, unit, skillId, level);
	case StaticField:
	case MercStaticField:
	case StaticFieldProc:
		return SrvDoStaticField(game, unit, skillId, level);
	case ProcSplashDamage:
	case GolemSplash:
	case SkeletonSplash:
		return SrvDoSplash(game, unit, skillId, level);
	case Gust:
		return SrvDoGust(game, unit, skillId, level);
	case Hunger:
		return SrvDoHunger(game, unit, skillId, level);
	case TigerStrike:
	case FistsOfFire:
	case CobraStrike:
	case ClawsOfThunder:
	case BladesOfIce:
	case RoyalStrike:
	case RakanothStrike:
		return SrvDoChargeUp(game, unit, skillId, level);
	case Firestorm:
	case DiabWall:
	case UberDiabWall:
	case FingermageBossDiabWall:
	case KanemithFirestorm:
		return SrvDoWall(game, unit, skillId, level);
	default:
		if (plog::Sample(g_routerMisses)) {
			plog::Warn("srvdo router: no function for skill %d (%u so far)", skillId, g_routerMisses.load());
		}
		return 0;
	}
}

// Frenzy / Tiger Strike start (PD2 srvstfunc 78): inc_splash_radius on a temp_skill_buff list, from calc2 and the
// skill's charges. Frenzy: calc2 * skill_frenzy / max(calc4, 1). Tiger Strike: calc2 * charges while under 3, else 0.
// Other skills: calc2. Merged ids (ADR 0003): state temp_skill_buff 270, stats skill_frenzy 169, inc_splash_radius 531.
// shortcut: PD2 keeps one permanent list and updates it; this replaces the list with a SplashFrames one each start,
// which also drops Split Throw's list on the same state (srvst 79) and the reverse until the next Frenzy swing.
// Upgrade (find D2R's get-state-list function) if a splash effect needs the list to outlive a long pause.
constexpr int32_t Frenzy = 147;
constexpr int32_t TempSkillBuff = 270;
constexpr int32_t SkillFrenzyStat = 169;
constexpr int32_t SplashFrames = 25 * 60 * 10;

auto SrvStSplash(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	if (skill == nullptr || tables == nullptr || At<uint64_t>(tables, TablesStateCount) <= TempSkillBuff) {
		return 1;  // not the merged data: the attack goes on without the stat
	}
	const auto unitStat = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat);
	const int32_t calc2 = d2r::EvalCalc(game, unit, skill, SkCalc2, skillId, level);
	int32_t charges = 0, value = calc2;
	if (skillId == Frenzy) {
		const int32_t calc4 = d2r::EvalCalc(game, unit, skill, SkCalc4, skillId, level);
		charges = unitStat(unit, SkillFrenzyStat, 0);
		value = calc2 * charges / (calc4 > 1 ? calc4 : 1);
	} else if (skillId == TigerStrike) {
		charges = unitStat(unit, At<int16_t>(skill, SkAuraStat1), 0);
		value = charges < 3 ? calc2 * charges : 0;
	}
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(RemoveSkillState)(unit, TempSkillBuff, 1);  // always a new list
	TimedStatList(dataFlag, unit, unit, skillId, level, SplashFrames, TempSkillBuff, {{IncSplashRadius, value}});
	if (plog::Sample(g_splashStarts)) {
		plog::Info("splash start: skill %d level %d charges %d calc2 %d: splash radius %d, start %u", skillId, level,
		           charges, calc2, value, g_splashStarts.load());
	}
	return 1;
}

// --- Leap, Leap Attack, Leap Attack Korlic (PD2 srvdofunc 153 -> 172, cltdofunc 97 -> 115) ------------------------------
// Read from ProjectDiablo.dll: srvdo 153 at 0x103b1300, cltdo 97 at 0x103ab280 (docs/core-rules.md). PD2's leap path is
// D2R's (path type 9 to the skill's point); PD2 sets its own speed and, on landing, hits every unit within calc3.
std::atomic<uint32_t> g_leaps {0};
std::atomic<uint32_t> g_leapClients {0};
// Each kind's first landing and client start ([0] Leap, [1] the others), and the first landing with units in range,
// are logged too, whichever came first
std::atomic<uint32_t> g_leapKinds[2] {};
std::atomic<uint32_t> g_leapClientKinds[2] {};
std::atomic<uint32_t> g_leapUnitLandings {0};
constexpr int32_t SkillLeap = 132;  // PD2 keys the landing on this id: Leap knocks back, every other row hits

using UnitSkill = uint8_t;

auto UnitSkillOf(Unit* unit, int32_t skillId) noexcept -> UnitSkill* {
	return d2r::Engine<UnitSkill* (*)(Unit*, int32_t, int32_t) noexcept>(d2r::rva::GetUnitSkill)(unit, skillId, 0);
}

auto& Flags(UnitSkill* uskill) noexcept {
	return *reinterpret_cast<uint32_t*>(uskill + d2r::rva::USkillFlags);
}

// PD2's leap speed: a monster's monstats Velocity, a player's 0xb00 + calc2 * 0x50 (calc2 is 80 or 60 plus
// item_leap_speed / 4), never below 0xb00. D2R's LeapStart scales the run speed by calc2 % instead.
// shortcut: PD2's client half uses 0xa00 + calc2 * 0x29; both halves take the server's speed here so the client's
// path matches the server's. Upgrade if PD2's client speed turns out to be visible.
auto LeapVelocity(Unit* unit, const uint8_t* skill, int32_t skillId, int32_t level) noexcept -> int32_t {
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	if (At<int32_t>(unit, d2r::rva::UnitType) == 1) {
		const uint8_t* mon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(d2r::rva::MonStatsRecord)(
			dataFlag, At<int32_t>(unit, d2r::rva::UnitClassId));
		return mon != nullptr ? At<int16_t>(mon, d2r::rva::MonVelocity) << 8 : 0;
	}
	const int32_t v = 0xb00 + d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, d2r::rva::SkCalc2), skillId, level) * 0x50;
	return v > 0xb00 ? v : 0xb00;
}

void SetSkillVelocity(Unit* unit, UnitSkill* uskill, int32_t velocity) noexcept {
	void* path = At<void*>(unit, d2r::rva::UnitPath);
	if (path == nullptr || velocity <= 0) {
		return;
	}
	const uint32_t flags = Flags(uskill);
	Flags(uskill) = flags & ~0x1000u;  // PathSetVelocity refuses while 0x1000 is set; LeapStart clears it the same way
	d2r::Engine<void (*)(void*, int32_t) noexcept>(d2r::rva::PathSetVelocity)(path, velocity);
	Flags(uskill) = flags;
}

struct LeapHit {
	UnitSkill* uskill;
	int32_t skillId;
	int32_t level;
	int64_t hits;
};

// One victim of the landing: D2R's own Leap Attack hit (to-hit roll with the skill's ToHit, weapon damage + calc1 %)
// aimed at it through the skill target and the path target, which is where 0x56ad90 looks.
auto LeapHitOne(d2r::AreaContext* area, Unit* victim) noexcept -> int32_t {
	auto* hit = static_cast<LeapHit*>(area->arg);
	++hit->hits;  // units the hit is rolled on, not hits that land
	Unit* unit = area->unit;
	void* path = At<void*>(unit, d2r::rva::UnitPath);
	auto* target = reinterpret_cast<int32_t*>(hit->uskill + d2r::rva::USkillTargetType);
	const int32_t savedType = target[0], savedId = target[1];
	Unit* savedPathTarget = At<Unit*>(path, d2r::rva::PathTargetUnit);
	const auto setPathTarget = d2r::Engine<void (*)(void*, Unit*) noexcept>(d2r::rva::PathSetTargetUnit);
	target[0] = At<int32_t>(victim, d2r::rva::UnitType);
	target[1] = d2r::Engine<int32_t (*)(Unit*, const char*, int32_t) noexcept>(d2r::rva::UnitId)(victim, __FILE__, __LINE__);
	setPathTarget(path, victim);
	d2r::Engine<void (*)(void*, Unit*, int32_t, int32_t) noexcept>(d2r::rva::SkillMeleeHit)(area->game, unit, hit->skillId,
	                                                                                       hit->level);
	setPathTarget(path, savedPathTarget);
	target[0] = savedType;
	target[1] = savedId;
	return 1;
}

// PD2 srvdofunc 153. The unit skill's flags pick the phase: 0x80 start the leap, 0x100 in the air (land), else wait.
auto SrvDoLeap(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::DataFlag(game), skillId);
	UnitSkill* uskill = skill != nullptr ? UnitSkillOf(unit, skillId) : nullptr;
	if (uskill == nullptr) {
		return 0;
	}
	const uint32_t flags = Flags(uskill);
	if ((flags & 0x100) == 0) {
		if ((flags & 0x80) == 0) {
			Flags(uskill) = 0x1000;
		} else if (d2r::Engine<int8_t (*)(Unit*, UnitSkill*) noexcept>(LeapStart)(unit, uskill) != 0) {
			SetSkillVelocity(unit, uskill, LeapVelocity(unit, skill, skillId, level));
			d2r::Engine<int32_t (*)(void*, Unit*, int32_t, int32_t) noexcept>(PathSteer)(At<void*>(unit, UnitPath), unit, 0, 1);
		}
		return 1;
	}
	if (d2r::Engine<int32_t (*)(Game*, Unit*, UnitSkill*) noexcept>(LeapLanded)(game, unit, uskill) == 0) {
		return 1;
	}
	const int32_t radius = d2r::EvalCalc(game, unit, skill, SkCalc3, skillId, level);
	int64_t hits = 0;
	if (radius > 0 && skillId == SkillLeap) {
		d2r::LeapKnockback knockback;
		uint8_t scratch[0x20] {};  // srvdo 77's second block: only its byte +0x18 (0) is read
		d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, d2r::LeapKnockback*, int32_t, void*) noexcept>(
			AreaKnockback)(game, unit, 0, 0, radius, &knockback, 0, scratch);
	} else if (radius > 0) {
		d2r::AreaCenter center {d2r::Engine<void* (*)(Unit*) noexcept>(UnitRoom)(unit), 0, 0};
		LeapHit hit {uskill, skillId, level, 0};
		d2r::Engine<AreaFn>(AreaUnits)(game, unit, &center, radius, AreaUnitMask, LeapHitOne, &hit, 0);
		hits = hit.hits;
	}
	d2r::api.setUnitSkillFlag(unit, 1);
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t) noexcept>(RemoveUnitEvent)(game, unit, 1, 0);
	d2r::Engine<void (*)(Game*, Unit*, int32_t, int32_t, int32_t, int32_t, int32_t) noexcept>(AddUnitEvent)(
		game, unit, 1, At<int32_t>(game, GameFrame) + 4, 0, 0, 0);
	const bool kindFirst = plog::Sample(g_leapKinds[skillId == SkillLeap ? 0 : 1]);
	const bool unitsFirst = skillId != SkillLeap && hits > 0 && plog::Sample(g_leapUnitLandings);
	if (plog::Sample(g_leaps) || kindFirst || unitsFirst) {
		plog::Info("leap: skill %d level %d landed: radius %d, %s %lld, velocity %d, cast %u", skillId, level, radius,
		           skillId == SkillLeap ? "knockback" : "units in range", static_cast<long long>(hits),
		           LeapVelocity(unit, skill, skillId, level), g_leaps.load());
	}
	return 1;
}

// PD2 cltdofunc 97: D2R's Leap client half (cltdo 43) with PD2's speed on the start frame.
auto CltDoLeap(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	if (unit == nullptr) {
		return 0;
	}
	const uint8_t* skill = d2r::api.getSkillRecord(d2r::api.unitDataFlag(unit), skillId);
	UnitSkill* uskill = skill != nullptr ? UnitSkillOf(unit, skillId) : nullptr;
	if (uskill == nullptr) {
		return 0;
	}
	const uint32_t flags = Flags(uskill);
	const int32_t ran = d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(d2r::rva::CltDoLeap)(unit, skillId);
	if ((flags & 0x2) == 0 && (flags & 0x80) != 0) {
		const int32_t velocity = LeapVelocity(unit, skill, skillId, level);
		SetSkillVelocity(unit, uskill, velocity);
		const bool kindFirst = plog::Sample(g_leapClientKinds[skillId == SkillLeap ? 0 : 1]);
		if (plog::Sample(g_leapClients) || kindFirst) {
			plog::Info("leap client: skill %d level %d: velocity %d, cast %u", skillId, level, velocity, g_leapClients.load());
		}
	}
	return ran;
}

// --- Inferno, Arctic Blast (PD2 srvdofunc 182, cltdofunc 111) ----------------------------------------------------
// Read from ProjectDiablo.dll: srvdo 182 at 0x103b4e40, cltdo 111 at 0x103abce0 (docs/research/druid-w21.md). A player
// sends calc3 flames of range calc1 in a line across the aim; a monster keeps D2R's Inferno (srvdo 19).
std::atomic<uint32_t> g_channels {0};
std::atomic<uint32_t> g_channelClients {0};
constexpr int32_t StateInferno = 12;  // states.txt inferno, on while the skill channels; the same id in PD2 and D2R

// PD2's 0x1037d860: the target point moved to `dist` subtiles from the caster, on the same line.
void Reach(int32_t ux, int32_t uy, int32_t* tx, int32_t* ty, int32_t dist) noexcept {
	const int32_t dx = *tx - ux, dy = *ty - uy;
	const double length = std::sqrt(static_cast<double>(dx * dx + dy * dy));
	if (length != 0) {
		*tx = static_cast<int32_t>(static_cast<double>(dx) * dist / length + ux);
		*ty = static_cast<int32_t>(static_cast<double>(dy) * dist / length + uy);
	}
}

struct Line {
	int32_t x, y, stepX, stepY;  // the first point and the step to the next
};

// PD2's 0x1037d780: `count` points one small step apart through the target, at right angles to the aim (Multiple
// Shot's vector steps, the other way round). Under 2 subtiles PD2's code puts the length in y, not 4 * y; kept.
constexpr auto ChannelLine(int32_t ux, int32_t uy, int32_t tx, int32_t ty, int32_t count) noexcept -> Line {
	int32_t ax = ux - tx, ay = uy - ty, length = ax * ax + ay * ay;
	if (length < 4) {
		ax *= 4;
		ay = length;
		length = length * length + ax * ax;
	}
	if (length < 16) {
		ax *= 2;
		ay *= 2;
	}
	int32_t sx = ay, sy = -ax;
	while (sx * sx + sy * sy > 3) {
		sx /= 2;
		sy /= 2;
	}
	return {tx - sx * count / 2, ty - sy * count / 2, sx, sy};
}
static_assert(ChannelLine(0, 0, 10, 0, 3).x == 10 && ChannelLine(0, 0, 10, 0, 3).y == -1);
static_assert(ChannelLine(0, 0, 10, 0, 3).stepX == 0 && ChannelLine(0, 0, 10, 0, 3).stepY == 1);
static_assert(ChannelLine(0, 0, 0, 10, 2).x == 1 && ChannelLine(0, 0, 0, 10, 2).stepX == -1);

// shortcut: PD2 then re-times the caster's frame and mode-end events from the next whole frame (0x1037a7c0); D2R's
// events are left as they are. Upgrade if the channel's cast rate differs from PD2's in play (docs/unported.md).
auto SrvDoChannel(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	if (At<int32_t>(unit, UnitType) == 1) {
		return VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoInferno)(game, unit, skillId, level);
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (skill == nullptr || d2r::Engine<void* (*)(Unit*) noexcept>(CurrentSkill)(unit) == nullptr ||
	    !d2r::ValidMissile(dataFlag, At<int16_t>(skill, SkSrvMissileA)) ||
	    d2r::api.skillTargetXY(game, unit, &tx, &ty) == 0 || !d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	const int32_t range = std::max(d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level), 1);
	const int32_t count = ClampCount(d2r::EvalCalc(game, unit, skill, SkCalc3, skillId, level));
	Reach(ux, uy, &tx, &ty, count / 2 + 8);
	const Line line = ChannelLine(ux, uy, tx, ty, count);
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = MissileChannel;
	params.owner = unit;
	params.origin = unit;
	params.missile = At<int16_t>(skill, SkSrvMissileA);
	params.skill = skillId;
	params.skillLevel = level;
	params.range = range;
	int32_t created = 0;
	for (int32_t i = 0; i < count; ++i) {
		params.targetX = line.x + i * line.stepX;
		params.targetY = line.y + i * line.stepY;
		created += d2r::api.createMissile(game, &params) != nullptr ? 1 : 0;
	}
	if (plog::Sample(g_channels)) {
		plog::Info("channel: skill %d level %d: %d of %d missiles (row %d), range %d, cast %u", skillId, level, created,
		           count, params.missile, range, g_channels.load());
	}
	return 1;
}

// Client half: the same line from cltmissilea, 40% of the time pushed calc3 further out; each flame is drawn with
// chance 2 / calc3 and lives calc1 - 1 or calc1 frames. Then D2R's own cltdo 24 ending: another player's caster under
// the inferno state is held in the channel frames. PD2 also rolls a velocity per flame (Vel - 6 + 0-7, halved on the
// first), but its flags leave out 4, the bit that makes the create read it, so its flames keep Vel; so do these.
// shortcut: PD2's ending has no local-player check; this keeps D2R's. Upgrade if the caster's own channel animation
// differs from PD2's in play.
// shortcut: PD2's post-create call (0x103a45c0 with calc1) is taken to be D2R's SetMissileAnimRange. Upgrade when that
// D2Common pointer is resolved, or if the flames animate differently from PD2's.
auto CltDoChannel(Unit* unit, int32_t skillId, int32_t level) noexcept -> int32_t {
	using namespace d2r::rva;
	if (unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::api.unitDataFlag(unit);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	int32_t tx = 0, ty = 0, ux = 0, uy = 0;
	if (skill == nullptr || !d2r::ValidMissile(dataFlag, At<int16_t>(skill, SkCltMissileA)) ||
	    d2r::api.clientSkillTargetXY(unit, &tx, &ty) == 0 || !d2r::UnitXY(unit, &ux, &uy)) {
		return 0;
	}
	const auto calc = [&](int field) {
		return d2r::api.evalSkillCalc(dataFlag, unit, At<int32_t>(skill, field), skillId, level);
	};
	const int32_t range = std::max(calc(SkCalc1), 1);
	const int32_t count = ClampCount(calc(SkCalc3));
	uint32_t* seed = d2r::Engine<uint32_t* (*)(Unit*) noexcept>(UnitSeed)(unit);
	// signed remainders, as PD2's IDIV takes them: a negative roll always passes the < 40 and < 2 tests
	const int32_t further = static_cast<int32_t>(d2r::Roll(seed)) % 100 < 40 && count > 1 ? count : 0;
	Reach(ux, uy, &tx, &ty, count / 2 + further + 8);
	const Line line = ChannelLine(ux, uy, tx, ty, count);
	d2r::MissileParams params;
	std::memset(&params, 0, sizeof(params));
	params.flags = MissileChannel;
	params.owner = unit;
	params.origin = unit;
	params.missile = At<int16_t>(skill, SkCltMissileA);
	params.x = ux;
	params.y = uy;
	params.skill = skillId;
	params.skillLevel = level;
	const uint8_t* record = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MissileRecord)(dataFlag,
	                                                                                                  params.missile);
	int32_t drawn = 0;
	for (int32_t i = 0; i < count; ++i) {
		const int32_t roll = static_cast<int32_t>(d2r::Roll(seed));
		if (roll % count >= 2) {
			continue;
		}
		params.range = std::max(range + roll % 2 - 1, 1);  // a negative roll takes 2 off (signed, as in PD2)
		params.targetX = line.x + i * line.stepX;
		params.targetY = line.y + i * line.stepY;
		Unit* flame = d2r::api.clientCreateMissile(&params, 0);
		if (flame != nullptr && record != nullptr) {
			d2r::Engine<void (*)(Unit*, uint32_t) noexcept>(SetMissileAnimRange)(
				flame, (static_cast<uint32_t>(At<uint8_t>(record, MsAnimSpeed)) << 8) / range);
		}
		drawn += flame != nullptr ? 1 : 0;
	}
	Unit* local = d2r::Engine<Unit* (*)(uint32_t) noexcept>(ClientPlayer)(
		d2r::Engine<uint32_t (*)() noexcept>(LocalPlayerSlot)());
	if (local != unit && d2r::Engine<int32_t (*)(Unit*, int32_t) noexcept>(UnitHasState)(unit, StateInferno) != 0) {
		if (At<void*>(unit, UnitSeqAnim) == nullptr) {
			d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetUnitFrame)(
				unit, d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitFrame)(unit) - 2);
		} else {
			d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetUnitFrames)(unit, 8);
			d2r::Engine<void (*)(Unit*, int32_t) noexcept>(SetUnitAnimSpeed)(unit, 5);
		}
	}
	if (plog::Sample(g_channelClients)) {
		plog::Info("channel client: skill %d level %d: %d of %d flames drawn, range %d, cast %u", skillId, level, drawn,
		           count, range, g_channelClients.load());
	}
	return 1;
}

// --- The channel take-over (PD2's gate A, ProjectDiablo.dll 0x10373290; docs/research/druid-w21.md) ---------------
// PD2 keys two rules on the busy player's current skill: Inferno / Arctic Blast (srvdofunc 182) give way at once to
// any request that carries a skill; Blade Fury (48) gives way to any other skill and refuses another Blade Fury, so its
// channel goes on. Everything else is D2R's own gate.
std::atomic<uint32_t> g_takeOvers {0};
std::atomic<uint32_t> g_bladeFuryRefusals {0};
std::atomic<uint32_t> g_heldRequests {0};
constexpr int32_t SrvDoBladeFury = 48;
constexpr int32_t Uninterruptable = 54;  // states.txt uninterruptable: PD2 refuses before its own rules
using GateFn = int64_t (*)(Game* game, Unit* unit, int32_t mode, void* request, int32_t flag) noexcept;
GateFn g_gateOriginal = nullptr;

auto UnitSkillIdOf(void* unitSkill) noexcept -> int32_t {
	return d2r::Engine<int32_t (*)(void*, const char*, int32_t) noexcept>(d2r::rva::UnitSkillId)(unitSkill, __FILE__,
	                                                                                             __LINE__);
}

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
			if (plog::Sample(g_heldRequests)) {
				plog::Info("held: skill %d (srvdo %d) mode %d to D2R's gate: %lld, %u so far", nowId, func, mode,
				           static_cast<long long>(result), g_heldRequests.load());
			}
			return result;
		}
		const uint8_t* next = d2r::api.getSkillRecord(d2r::DataFlag(game), nextId);
		if (next != nullptr) {
			const int32_t nextFunc = At<uint16_t>(next, SkSrvDoFunc);
			const bool refused = func == SrvDoBladeFury && nextFunc == SrvDoBladeFury;
			auto& counter = refused ? g_bladeFuryRefusals : g_takeOvers;
			if (plog::Sample(counter)) {
				plog::Info("take-over: skill %d (srvdo %d) mid-channel, mode %d request skill %d (srvdo %d): %s, %u so far",
				           nowId, func, mode, nextId, nextFunc, refused ? "refused" : "allowed", counter.load());
			}
			return refused ? 1 : 0;
		}
	}
	return g_gateOriginal(game, unit, mode, request, flag);
}

// --- Charge (PD2 srvstfunc 72, srvdofunc 180; cltstfunc 57 -> 25, cltdofunc 112 -> 37) -------------------------------
// Read from ProjectDiablo.dll: srvst 72 at 0x103b67e0, srvdo 180 at 0x103b4870, cltst 57 at 0x103ace50, cltdo 112 at
// 0x103abfb0 (docs/research/w16-charge.md). PD2's charge is vanilla's with two changes on the server: the start's speed
// adds Param1, and the hit puts calc'd splash on the caster from how far it charged. D2R's client halves (cltst 25,
// cltdo 37) already add Param1, so the merge points the client columns there.
std::atomic<uint32_t> g_charges {0};
std::atomic<uint32_t> g_chargeHitFrames {0};

// PD2's charge speed is base * (max(velocitypercent, 50) + Param1) / 100, base the class's (a monster's Velocity).
// D2R's server start adds calc2 instead, which PD2's row leaves empty, so its server charged slower than its client.
auto ChargeBase(uint32_t dataFlag, Unit* unit) noexcept -> int32_t {
	using namespace d2r::rva;
	int32_t base = 0x100;
	if (At<int32_t>(unit, UnitType) == 0) {
		const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
		const uint32_t cls = At<uint32_t>(unit, UnitClassId);
		if (tables != nullptr && cls < At<uint64_t>(tables, TablesCharStatsCount)) {
			base = At<uint8_t>(At<const uint8_t*>(tables, TablesCharStats) + cls * CharStatsSize, CsChargeVelocity) << 8;
		}
	} else {
		const uint8_t* mon = d2r::Engine<const uint8_t* (*)(uint32_t, int32_t) noexcept>(MonStatsRecord)(
			dataFlag, At<int32_t>(unit, UnitClassId));
		base = mon != nullptr ? At<int16_t>(mon, MonVelocity) << 8 : base;  // PD2 reads charstats at a monster's id, a wrong row; D2R's client uses this
	}
	return base;
}

constexpr auto ChargeVelocity(int32_t base, int32_t percent, int32_t param1) noexcept -> int32_t {
	return static_cast<int32_t>(static_cast<int64_t>(base) * ((percent > 50 ? percent : 50) + param1) / 100);
}
static_assert(ChargeVelocity(6 << 8, 0, 150) == 3072);  // 50% at least, plus Param1's 150%

// D2Common's distance (1.13c ordinal 10080): the longer side plus half the shorter.
constexpr auto ChargeDistance(int32_t dx, int32_t dy) noexcept -> int32_t {
	dx = dx < 0 ? -dx : dx;
	dy = dy < 0 ? -dy : dy;
	return dx > dy ? (2 * dx + dy) / 2 : (2 * dy + dx) / 2;
}
static_assert(ChargeDistance(10, -4) == 12 && ChargeDistance(-3, 9) == 10);

// PD2's splash on the hit: inc_splash_radius = distance / 4 * 20
constexpr auto ChargeSplash(int32_t distance) noexcept -> int32_t {
	return distance / 4 * 20;
}
static_assert(ChargeSplash(12) == 60 && ChargeSplash(3) == 0);

constexpr int32_t ChargeSplashFrames = 3;  // PD2's list ends 3 frames after the hit

auto CurrentUnitSkill(Unit* unit) noexcept -> UnitSkill* {
	return d2r::Engine<UnitSkill* (*)(Unit*) noexcept>(d2r::rva::CurrentSkill)(unit);
}

auto& ChargeParam(UnitSkill* uskill) noexcept {
	return *reinterpret_cast<int32_t*>(uskill + d2r::rva::USkillParam);
}

// Start (PD2 srvstfunc 72): D2R's srvst 31, then, when it set off a charge (flags 0x1001), PD2's speed and the
// distance to the target in the skill's param. Any other start clears the param, so a charge cut short before its hit
// leaves no distance for the next one.
auto SrvStCharge(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const int64_t started = VanillaSlot<SrvDoFn>(SrvStTable, SrvStVanillaCharge)(game, unit, skillId, level);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	UnitSkill* uskill = CurrentUnitSkill(unit);
	if (uskill != nullptr) {
		ChargeParam(uskill) = 0;
	}
	if (started == 0 || skill == nullptr || uskill == nullptr || (Flags(uskill) & 0x1001) != 0x1001) {
		return started;  // refused, or in reach: D2R attacked at once, as PD2 does
	}
	const int32_t base = ChargeBase(dataFlag, unit);
	const int32_t percent = d2r::Engine<int32_t (*)(Unit*, int32_t, int32_t) noexcept>(UnitStat)(
		unit, StatVelocityPercent, 0);
	const int32_t velocity = ChargeVelocity(base, percent, At<int32_t>(skill, SkParam1));
	SetSkillVelocity(unit, uskill, velocity);
	int32_t ux = 0, uy = 0, tx = 0, ty = 0;
	ChargeParam(uskill) = d2r::UnitXY(unit, &ux, &uy) && d2r::api.skillTargetXY(game, unit, &tx, &ty) != 0
	                          ? ChargeDistance(tx - ux, ty - uy)
	                          : 0;
	if (plog::Sample(g_charges)) {
		plog::Info("charge: skill %d level %d start: velocity %d (base %d, velocitypercent %d, param1 %d), distance %d, "
		           "start %u",
		           skillId, level, velocity, base, percent, At<int32_t>(skill, SkParam1), ChargeParam(uskill),
		           g_charges.load());
	}
	return started;
}

// Whether D2R's srvdo 67 strikes on this hit frame: its target (the skill's, else the nearest, searched as it
// searches), in reach, with the caster alive.
auto ChargeStrikes(Game* game, Unit* unit, UnitSkill* uskill) noexcept -> bool {
	using namespace d2r::rva;
	const auto* target = reinterpret_cast<const int32_t*>(uskill + USkillTargetType);
	Unit* victim = nullptr;
	if (target[0] != 6) {
		victim = d2r::Engine<Unit* (*)(Game*, int32_t, int32_t) noexcept>(FindUnit)(game, target[0], target[1]);
	}
	if (victim == nullptr) {
		using NearestFn = Unit* (*)(Game*, Unit*, int32_t, int32_t, int32_t, uint32_t, uint32_t, int32_t*) noexcept;
		victim = d2r::Engine<NearestFn>(NearestTarget)(game, unit, 0, 0, 3, 3, 0xffffffff, nullptr);
	}
	return victim != nullptr &&
	       d2r::Engine<int32_t (*)(Unit*, Unit*, int32_t, int32_t) noexcept>(UnitInReach)(
	           unit, victim, d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitReach)(unit), 0) != 0 &&
	       d2r::Engine<int32_t (*)(Unit*) noexcept>(UnitDead)(unit) == 0;
}

// PD2 srvdofunc 180: D2R's srvdo 67, and when its hit goes off (flag 1 clear, target in reach) the caster first gets
// inc_splash_radius from the charge's distance on a short temp_skill_buff list, unless it has one (Frenzy's), and the
// distance is spent.
auto SrvDoCharge(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	UnitSkill* uskill = CurrentUnitSkill(unit);
	if (uskill != nullptr && (Flags(uskill) & 1) == 0) {
		const uint32_t dataFlag = d2r::DataFlag(game);
		const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
		const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
		if (skill != nullptr && tables != nullptr && At<uint64_t>(tables, TablesStateCount) > TempSkillBuff &&
		    ChargeStrikes(game, unit, uskill)) {
			const int32_t distance = ChargeParam(uskill);
			const int32_t splash = ChargeSplash(distance);
			const char* list = "kept";
			if (d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, TempSkillBuff) == nullptr) {
				list = TimedStatList(dataFlag, unit, unit, skillId, level, ChargeSplashFrames, TempSkillBuff,
				                     {{IncSplashRadius, splash}});
			}
			ChargeParam(uskill) = 0;
			if (plog::Sample(g_chargeHitFrames)) {
				plog::Info("charge: skill %d level %d hit frame: damage%% %d, distance %d, splash radius %d (list %s), "
				           "hit %u",
				           skillId, level, d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level), distance, splash,
				           list, g_chargeHitFrames.load());
			}
		}
	}
	return VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoVanillaCharge)(game, unit, skillId, level);
}

// --- Revive's aura event (PD2 auraeventfunc 32, ProjectDiablo.dll 0x10347370) ---------------------------------------
// Revive's row has auraevent1 domeleedamage: each melee hit of a revived monster runs proc_SplashDamage's do function
// (PD2 358, merged 430) at level 1 from it, called directly as PD2 does. PD2's splash finds the struck unit on the
// revive's path; this passes the unit the hit landed on.
std::atomic<uint32_t> g_reviveEvents {0};
std::atomic<uint32_t> g_reviveSplashes {0};

auto AuraEventReviveSplash(Game* game, int32_t event, Unit* unit, Unit* target, void* damage, int32_t skillId,
                           int32_t level) noexcept -> int32_t {
	(void)damage;
	if (t_splashing) {
		return 0;  // PD2 skips this handler for a splash's own hits
	}
	const bool sample = plog::Sample(g_reviveEvents);
	if (game == nullptr || unit == nullptr || target == nullptr) {
		if (sample) {
			plog::Info("revive event: event %d skill %d level %d without a %s, event %u", event, skillId, level,
			           game == nullptr ? "game" : unit == nullptr ? "unit" : "target", g_reviveEvents.load());
		}
		return 0;
	}
	const auto cast = static_cast<int32_t>(Splash(game, unit, target, ProcSplashDamage, 1));
	g_reviveSplashes.fetch_add(cast != 0 ? 1 : 0);
	if (sample) {
		// the unit the event fires on (a revive: type 1 owned by a player, owner type 0), so the check can tell
		plog::Info("revive event: event %d skill %d level %d on unit type %d class %d owner %d/%u, target type %d: "
		           "splash skill %d cast %d, event %u", event, skillId, level, At<int32_t>(unit, d2r::rva::UnitType),
		           At<int32_t>(unit, d2r::rva::UnitClassId), At<int32_t>(unit, d2r::rva::UnitOwnerType),
		           At<uint32_t>(unit, d2r::rva::UnitOwnerId), At<int32_t>(target, d2r::rva::UnitType), ProcSplashDamage,
		           cast, g_reviveEvents.load());
	}
	return cast != 0 ? 1 : 0;
}

using AuraEventFn = int32_t (*)(Game*, int32_t, Unit*, Unit*, void*, int32_t, int32_t) noexcept;

// Revive's do (srvdofunc 58): D2R's own, then Revive's aura events go on the revived monster. D2R's Revive never
// registers them, so without this event 32 never fires. The corpse is revived in place (seen in game: same unit,
// result 1, mode neutral), so the target before the call is the pet after it; a refused revive leaves it dead.
std::atomic<uint32_t> g_revives {0};

auto SrvDoRevive(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	Unit* target = game != nullptr && unit != nullptr
	                   ? d2r::Engine<Unit* (*)(Game*, Unit*) noexcept>(d2r::rva::SkillTargetUnit)(game, unit)
	                   : nullptr;
	const int64_t result = d2r::Engine<SrvDoFn>(d2r::rva::SrvDoReviveD2R)(game, unit, skillId, level);
	if (result == 0 || target == nullptr || At<int32_t>(target, d2r::rva::UnitType) != d2r::rva::UnitMonster) {
		return result;
	}
	const int32_t mode = At<int32_t>(target, d2r::rva::UnitMode);
	if (mode == d2r::rva::MonModeDeath || mode == d2r::rva::MonModeDead) {
		return result;
	}
	d2r::Engine<SrvDoFn>(d2r::rva::RegisterSkillEvents)(game, target, skillId, level);
	if (plog::Sample(g_revives)) {
		plog::Info("revive: skill %d level %d events on unit class %d, revive %u", skillId, level,
		           At<int32_t>(target, d2r::rva::UnitClassId), g_revives.load());
	}
	return result;
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
constexpr Table CltSt {"cltstfunc", d2r::rva::CltStTable, d2r::rva::CltStSlots, d2r::rva::CltStFirstFree};
constexpr Table MissileSrvDo {"pSrvDoFunc", d2r::rva::MissileSrvDoTable, d2r::rva::MissileSrvDoSlots,
                              d2r::rva::MissileSrvDoFirstFree};
constexpr Table MissileSrvHit {"pSrvHitFunc", d2r::rva::MissileSrvHitTable, d2r::rva::MissileSrvHitSlots,
                               d2r::rva::MissileSrvHitFirstFree};
constexpr Table MissileCltDo {"pCltDoFunc", d2r::rva::MissileCltDoTable, d2r::rva::MissileCltDoSlots,
                              d2r::rva::MissileCltDoFirstFree};
constexpr Table MissileSrvDmg {"pSrvDmgFunc", d2r::rva::MissileSrvDmgTable, d2r::rva::MissileSrvDmgSlots,
                               d2r::rva::MissileSrvDmgFirstFree};
constexpr Table MissileCltHit {"pCltHitFunc", d2r::rva::MissileCltHitTable, d2r::rva::MissileCltHitSlots,
                               d2r::rva::MissileCltHitFirstFree};

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
	{SrvSt, 78, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvStSplash)), "splash start"},
	// PD2's Find slots (srvst 70, cltst 55) are D2R's; ours take free slots and patches/020 points the data there.
	{SrvSt, 82, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvStFind)), "find"},
	{CltSt, 62, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltStFind)), "find"},
	{SrvDo, 190, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoRouter)), "router"},
	// Blood Warp: D2R fills srvst 66 and cltst 56; its do function is in the router
	{SrvSt, 80, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvStBloodWarp)), "blood warp"},
	{CltSt, 63, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltStBloodWarp)), "blood warp"},
	// PD2's srvdo 153 / cltdo 97 are D2R's RotW functions; ours take free slots and the merge points there (FUNCS).
	{SrvDo, 172, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoLeap)), "leap"},
	{CltDo, 115, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltDoLeap)), "leap"},
	// Charge keeps PD2's free server slots; its client columns go to D2R's own (FUNCS)
	{SrvSt, 72, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvStCharge)), "charge"},
	{SrvDo, 180, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoCharge)), "charge"},
	// PD2's return rows use sdo 38 / cdo 70, which D2R fills; ours take the first free slots and the data points there.
	{MissileSrvHit, 65, reinterpret_cast<const void*>(static_cast<MissileSrvHitFn>(SrvHitSpawnReturn)), "return spawn"},
	{MissileSrvDo, 46, reinterpret_cast<const void*>(static_cast<MissileSrvDoFn>(SrvDoReturn)), "return"},
	{MissileCltDo, 78, reinterpret_cast<const void*>(static_cast<MissileCltDoFn>(CltDoReturn)), "return"},
	// Double Throw: D2R fills srvdo 154 (a RotW skill), cltdo 98, shit 30 and dmg 16 (echoingstrike)
	{SrvDo, 183, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoDoubleThrow)), "double throw"},
	{CltDo, 116, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltDoDoubleThrow)), "double throw"},
	// Desecrate's server half is the router's (PD2's srvdo 161 has no free slot); cltdo 101 is D2R's
	{CltDo, 121, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltDoDesecrate)), "desecrate"},
	{SrvDo, 182, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoChannel)), "channel"},
	{CltDo, 111, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltDoChannel)), "channel"},
	{MissileSrvHit, 66, reinterpret_cast<const void*>(static_cast<MissileSrvHitFn>(SrvHitBounce)), "bounce"},
	{MissileSrvDmg, 21, reinterpret_cast<const void*>(static_cast<MissileSrvDmgFn>(SrvDmgFirstHit)), "first hit"},
	{MissileSrvHit, 63, reinterpret_cast<const void*>(static_cast<MissileSrvHitFn>(SrvHitExplodeSpread)), "explode spread"},
	// PD2's dmg 17 is D2R's Ring of Fire; ours takes 20 (18 and 19 stay free for PD2's own) and the data points there.
	{MissileSrvDmg, 20, reinterpret_cast<const void*>(static_cast<MissileSrvDmgFn>(SrvDmgPierceFalloff)), "pierce falloff"},
	// Poison Dagger: srvdo 164 goes through the router (190); D2R fills cltdo 102
	{CltDo, 122, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltDoPoisonDagger)), "poison dagger"},
	{MissileSrvHit, 61, reinterpret_cast<const void*>(static_cast<MissileSrvHitFn>(SrvHitState)), "state hit"},
	{SrvDo, 176, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoHolyLight)), "holy light"},
	{SrvDo, 174, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoVengeance)), "vengeance"},
	{MissileCltHit, 66, reinterpret_cast<const void*>(static_cast<MissileCltHitFn>(CltHitHolyLight)), "holy light"},
	// PD2 leaves Fist of the Heavens' chit 26 vanilla, and D2R's 26 differs from 1.13c's: ours takes 71 (FUNCS)
	{MissileCltHit, 71, reinterpret_cast<const void*>(static_cast<MissileCltHitFn>(CltHitFistDelay)), "fist delay"},
	// The finishers' prg functions: the release calls them with the charge-up skill's id, which the router already
	// gives srvdo 170, so they take the slots of after-the-cut monster functions (181, 184-186, W25)
	{SrvDo, 184, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvPrgSpread)), "charge spread"},
	{SrvDo, 185, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvPrgRing)), "charge ring"},
	{SrvDo, 186, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvPrgFire)), "charge fire"},
	{SrvDo, 181, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvPrgCold)), "charge cold"},
	// Combustion and the boss novas: srvdo 159 goes through the router (190); D2R fills cltdo 100
	{CltDo, 120, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltDoNova)), "nova"},
	// Firestorm and the wall makers: srvdo 166 is D2R's, so the router (190) runs them; cltdo 119 is free
	{CltDo, 119, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltDoWall)), "wall"},
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
	// Power Strike's cltdo 22 is PD2's (1.13c) function plus a local-player check; jumping over the check lets every
	// client draw everyone's strikes, as PD2 does.
	static constexpr uint8_t Je[1] = {0x74};
	const bool strike = context->PatchWriteU8(d2r::rva::CltDoStrikeLocalOnly, Je, sizeof(Je), 0xeb);
	installed += strike ? 1 : 0;
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "; cltdofunc 22 strike for all%s",
		                            strike ? "" : " FAILED (bytes differ)");
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	// Aura event 32 is filled by D2R but no D2R row uses it, so the expected value is D2R's own function, not null.
	const uint64_t d2rEvent = d2r::imageBase + d2r::rva::AuraEvent32D2R;
	uint8_t expected[8];
	std::memcpy(expected, &d2rEvent, sizeof(expected));
	const bool revive = context->PatchWriteU64(
		d2r::rva::AuraEventTable + static_cast<uint64_t>(d2r::rva::AuraEventRevive) * 8, expected, sizeof(expected),
		reinterpret_cast<uint64_t>(static_cast<AuraEventFn>(AuraEventReviveSplash)));
	installed += revive ? 1 : 0;
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "; auraeventfunc %d revive splash%s",
		                            d2r::rva::AuraEventRevive, revive ? "" : " FAILED (slot is not D2R's event 32)");
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	// Revive's do is D2R's: the expected value is D2R's function, which the wrapper calls first.
	const uint64_t d2rRevive = d2r::imageBase + d2r::rva::SrvDoReviveD2R;
	std::memcpy(expected, &d2rRevive, sizeof(expected));
	const bool reviveDo = context->PatchWriteU64(
		d2r::rva::SrvDoTable + static_cast<uint64_t>(d2r::rva::SrvDoRevive) * 8, expected, sizeof(expected),
		reinterpret_cast<uint64_t>(static_cast<SrvDoFn>(SrvDoRevive)));
	installed += reviveDo ? 1 : 0;
	if (used < reportSize) {
		const int n = std::snprintf(report + used, reportSize - used, "; srvdofunc %d revive events%s",
		                            d2r::rva::SrvDoRevive, reviveDo ? "" : " FAILED (slot is not D2R's Revive)");
		used += n > 0 ? static_cast<size_t>(n) : 0;
	}
	const bool gate = context->InstallInlineHook(d2r::rva::BusyModeGate, d2r::rva::BusyModeGateEntry,
	                                             sizeof(d2r::rva::BusyModeGateEntry), BusyModeGate, &g_gateOriginal);
	installed += gate ? 1 : 0;
	used = std::strlen(report);
	if (used < reportSize) {
		std::snprintf(report + used, reportSize - used, "; mode gate busy take-over%s",
		              gate ? "" : " FAILED (bytes differ)");
	}
	return installed;
}

auto SkillFunctionCount() noexcept -> int {
	// + the cltdo 22 patch, aura event 32, Revive's do and the mode gate
	return static_cast<int>(sizeof(Functions) / sizeof(Functions[0])) + 4;
}

auto SkillFunctionCounters(char* out, size_t size) noexcept -> void {
	char missiles[256];
	MissileFunctionCounters(missiles, sizeof(missiles));
	std::snprintf(out, size,
	              "split throw casts %u, client %u, temp buffs %u, finds %u, refused %u, bash hits %u, knockbacks %u, "
	              "splash starts %u, leap landings %u, client %u, double throw casts %u, client %u, poison daggers %u, "
	              "client %u, blood warps %u, refused %u, desecrates %u, client %u, corpses hidden %u, revives %u, "
	              "revive events %u, splashes %u, channels %u, client %u, take-overs %u, blade fury refused %u, "
	              "held %u, charge-up hits %u, charges %u, charge hit frames %u, holy lights %u, gusts %u, hungers %u, "
	              "novas %u, client %u, static fields %u, vengeances %u, links %u, jousts %u, %s",
	              g_splitThrowCasts.load(), g_splitThrowClientCasts.load(), g_tempBuffs.load(), g_finds.load(),
	              g_findsRefused.load(), g_bashHits.load(), g_bashKnockbacks.load(), g_splashStarts.load(), g_leaps.load(),
	              g_leapClients.load(), g_doubleThrowCasts.load(), g_doubleThrowClientCasts.load(),
	              g_poisonDaggers.load(), g_poisonDaggerClients.load(), g_bloodWarps.load(), g_bloodWarpsRefused.load(),
	              g_desecrates.load(), g_desecrateClients.load(), g_desecrateHidden.load(), g_revives.load(),
	              g_reviveEvents.load(), g_reviveSplashes.load(), g_channels.load(), g_channelClients.load(),
	              g_takeOvers.load(), g_bladeFuryRefusals.load(), g_heldRequests.load(), g_chargeHits.load(),
	              g_charges.load(), g_chargeHitFrames.load(), g_holyLights.load(), g_gusts.load(),
	              g_hungers.load(), g_novas.load(), g_novaClients.load(), g_staticFields.load(), g_vengeances.load(),
	              g_vengeanceLinks.load(), g_jousts.load(), missiles);
}
