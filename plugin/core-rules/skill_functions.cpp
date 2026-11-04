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
// Each slot is written with PatchWriteU64 and an expected value of eight zero bytes, so a slot D2R or another plugin
// already uses is refused and logged, never overwritten. Slots keep PD2's own numbers where free (docs/core-rules.md).
#include "skill_functions.h"

#include "d2r.h"
#include "log.h"
#include "missile_functions.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>

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

// One stat on a `state` list for `frames`, by NewStateList's recast rule (PD2's timed-state helper 0x1036d7d0 has
// the same one): an older list of the same skill and level is extended and keeps its value, a lower level is
// refused, anything else is replaced. Returns what happened, for the logs.
auto TimedStatList(uint32_t dataFlag, Unit* unit, int32_t skillId, int32_t level, int32_t frames, int32_t state,
                   int32_t stat, int32_t value) noexcept -> const char* {
	using namespace d2r::rva;
	void* old = d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, state);
	const auto listInt = [](uint64_t fn, void* list) { return d2r::Engine<int32_t (*)(void*) noexcept>(fn)(list); };
	const bool extends = old != nullptr && listInt(ListSkill, old) == skillId && listInt(ListLevel, old) == level;
	d2r::StateListRequest request {unit, unit, skillId, level, frames, -1, 0, state,
	                               d2r::Engine<void (*)()>(SkillStateExpired)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list == nullptr) {
		return "refused";
	}
	if (extends) {
		return "extended";
	}
	d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(dataFlag, list, stat, value, 0);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(unit, state, 1);
	return "new";
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
// shortcut: the teleport is D2R's srvdo 27, not PD2's teleport core (0x1036cd20, shared with Teleport's srvdo 155),
// and an extended list's new timer is not sent to the client (PD2 sends packet 0x83). Upgrade with the Teleport row.
auto SrvDoBloodWarp(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	const uint32_t dataFlag = d2r::DataFlag(game);
	if (VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoTeleport)(game, unit, skillId, level) == 0) {
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
		list = TimedStatList(dataFlag, unit, skillId, level, frames, state, stat, regen);
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

constexpr int32_t Bash = 126, UberTalicBash = 580;

// srvdofunc 190 serves the PD2 do functions that have no free slot of their own, by skill id (docs/core-rules.md).
auto SrvDoRouter(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	switch (skillId) {
	case Bash:
	case UberTalicBash:
		return SrvDoBash(game, unit, skillId, level);
	case BloodWarp:
		return SrvDoBloodWarp(game, unit, skillId, level);
	case Desecrate:
		return SrvDoDesecrate(game, unit, skillId, level);
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
constexpr int32_t Frenzy = 147, TigerStrike = 254;
constexpr int32_t TempSkillBuff = 270;
constexpr int32_t SkillFrenzyStat = 169;
constexpr int32_t IncSplashRadius = 531;
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
	TimedStatList(dataFlag, unit, skillId, level, SplashFrames, TempSkillBuff, IncSplashRadius, value);
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
		using AreaFn = void (*)(Game*, Unit*, d2r::AreaCenter*, int32_t, uint32_t, int32_t (*)(d2r::AreaContext*, Unit*),
		                        void*, int32_t) noexcept;
		d2r::Engine<AreaFn>(AreaUnits)(game, unit, &center, radius, 0x8583, LeapHitOne, &hit, 0);  // PD2's unit mask
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

// --- Charge (PD2 srvstfunc 72, srvdofunc 180; cltstfunc 57 -> 25, cltdofunc 112 -> 37) -------------------------------
// Read from ProjectDiablo.dll: srvst 72 at 0x103b67e0, srvdo 180 at 0x103b4870, cltst 57 at 0x103ace50, cltdo 112 at
// 0x103abfb0 (docs/research/w16-charge.md). PD2's charge is vanilla's with two changes on the server: the start's speed
// adds Param1, and the hit puts calc'd splash on the caster from how far it charged. D2R's client halves (cltst 25,
// cltdo 37) already add Param1, so the merge points the client columns there.
std::atomic<uint32_t> g_charges {0};
std::atomic<uint32_t> g_chargeHits {0};

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

auto CurrentSkill(Unit* unit) noexcept -> UnitSkill* {
	return d2r::Engine<UnitSkill* (*)(Unit*) noexcept>(d2r::rva::UnitCurrentSkill)(unit);
}

auto& ChargeParam(UnitSkill* uskill) noexcept {
	return *reinterpret_cast<int32_t*>(uskill + d2r::rva::USkillParam);
}

// Start (PD2 srvstfunc 72): D2R's srvst 31, then, when it set off a charge (flags 0x1001), PD2's speed and the
// distance to the target in the skill's param.
auto SrvStCharge(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const int64_t started = VanillaSlot<SrvDoFn>(SrvStTable, SrvStVanillaCharge)(game, unit, skillId, level);
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	UnitSkill* uskill = CurrentSkill(unit);
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

// PD2 srvdofunc 180: D2R's srvdo 67, and on the hit frame (flag 1 clear) the caster first gets inc_splash_radius from
// the charge's distance on a short temp_skill_buff list, unless it has one (Frenzy's), and the distance is spent.
// shortcut: PD2 builds the list only when the hit goes off (target found and in reach); this builds it on the hit
// frame before D2R's function looks, so a charge that ends without a hit gets a 3-frame list nothing reads. Upgrade once the melee
// splash (srvdo 163) reads the list and a missed charge splashes in play.
auto SrvDoCharge(Game* game, Unit* unit, int32_t skillId, int32_t level) noexcept -> int64_t {
	using namespace d2r::rva;
	if (game == nullptr || unit == nullptr) {
		return 0;
	}
	const uint32_t dataFlag = d2r::DataFlag(game);
	const uint8_t* skill = d2r::api.getSkillRecord(dataFlag, skillId);
	const auto* tables = d2r::Engine<const uint8_t* (*)(uint32_t) noexcept>(DataTables)(dataFlag);
	UnitSkill* uskill = CurrentSkill(unit);
	if (skill != nullptr && uskill != nullptr && (Flags(uskill) & 1) == 0 && tables != nullptr &&
	    At<uint64_t>(tables, TablesStateCount) > TempSkillBuff) {
		const int32_t distance = ChargeParam(uskill);
		const int32_t splash = ChargeSplash(distance);
		const char* list = "kept";
		if (d2r::Engine<void* (*)(Unit*, int32_t) noexcept>(UnitStateList)(unit, TempSkillBuff) == nullptr) {
			list = TimedStatList(dataFlag, unit, skillId, level, ChargeSplashFrames, TempSkillBuff, IncSplashRadius, splash);
		}
		ChargeParam(uskill) = 0;
		if (plog::Sample(g_chargeHits)) {
			plog::Info("charge: skill %d level %d hit frame: damage%% %d, distance %d, splash radius %d (list %s), hit %u",
			           skillId, level, d2r::EvalCalc(game, unit, skill, SkCalc1, skillId, level), distance, splash, list,
			           g_chargeHits.load());
		}
	}
	return VanillaSlot<SrvDoFn>(SrvDoTable, SrvDoVanillaCharge)(game, unit, skillId, level);
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
	{MissileSrvHit, 66, reinterpret_cast<const void*>(static_cast<MissileSrvHitBounceFn>(SrvHitBounce)), "bounce"},
	{MissileSrvDmg, 21, reinterpret_cast<const void*>(static_cast<MissileSrvDmgFn>(SrvDmgFirstHit)), "first hit"},
	{MissileSrvHit, 63, reinterpret_cast<const void*>(static_cast<MissileSrvHitFn>(SrvHitExplodeSpread)), "explode spread"},
	// PD2's dmg 17 is D2R's Ring of Fire; ours takes 20 (18 and 19 stay free for PD2's own) and the data points there.
	{MissileSrvDmg, 20, reinterpret_cast<const void*>(static_cast<MissileSrvDmgFn>(SrvDmgPierceFalloff)), "pierce falloff"},
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
		std::snprintf(report + used, reportSize - used, "; cltdofunc 22 strike for all%s",
		              strike ? "" : " FAILED (bytes differ)");
	}
	return installed;
}

auto SkillFunctionCount() noexcept -> int {
	return static_cast<int>(sizeof(Functions) / sizeof(Functions[0])) + 1;  // + the cltdo 22 patch
}

auto SkillFunctionCounters(char* out, size_t size) noexcept -> void {
	char missiles[128];
	MissileFunctionCounters(missiles, sizeof(missiles));
	std::snprintf(out, size,
	              "split throw casts %u, client %u, temp buffs %u, finds %u, refused %u, bash hits %u, knockbacks %u, "
	              "splash starts %u, leap landings %u, client %u, double throw casts %u, client %u, blood warps %u, "
	              "refused %u, desecrates %u, client %u, corpses hidden %u, charges %u, charge hits %u, %s",
	              g_splitThrowCasts.load(), g_splitThrowClientCasts.load(), g_tempBuffs.load(), g_finds.load(),
	              g_findsRefused.load(), g_bashHits.load(), g_bashKnockbacks.load(), g_splashStarts.load(), g_leaps.load(),
	              g_leapClients.load(), g_doubleThrowCasts.load(), g_doubleThrowClientCasts.load(), g_bloodWarps.load(),
	              g_bloodWarpsRefused.load(), g_desecrates.load(), g_desecrateClients.load(), g_desecrateHidden.load(),
	              g_charges.load(), g_chargeHits.load(), missiles);
}
