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

// D2's seed step (the game inlines it), as d2r-tools better-auras does it.
auto RandomBelow(uint32_t* seed, uint32_t n) noexcept -> uint32_t {
	const uint64_t s = static_cast<uint64_t>(seed[0]) * 0x6ac690c5ULL + seed[1];
	seed[0] = static_cast<uint32_t>(s);
	seed[1] = static_cast<uint32_t>(s >> 32);
	return seed[0] % n;
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
				if (RandomBelow(seed, 100) < 50) {
					result |= d2r::rva::ResultKnockback;
					g_bashKnockbacks.fetch_add(1);
				}
			}
		}
	}
	return VanillaSlot<SrvDoFn>(d2r::rva::SrvDoTable, d2r::rva::SrvDoMelee)(game, unit, skillId, level);
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
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(RemoveSkillState)(unit, TempSkillBuff, 1);
	d2r::StateListRequest request {unit, unit, skillId, level, SplashFrames, -1, 0, TempSkillBuff,
	                               d2r::Engine<void (*)()>(SkillStateExpired)};
	void* list = d2r::Engine<void* (*)(d2r::StateListRequest*) noexcept>(NewStateList)(&request);
	if (list == nullptr) {
		return 1;
	}
	d2r::Engine<void (*)(uint32_t, void*, int32_t, int32_t, int32_t) noexcept>(SetListStat)(dataFlag, list,
	                                                                                      IncSplashRadius, value, 0);
	d2r::Engine<void (*)(Unit*, int32_t, int32_t) noexcept>(SetUnitState)(unit, TempSkillBuff, 1);
	if (plog::Sample(g_splashStarts)) {
		plog::Info("splash start: skill %d level %d charges %d calc2 %d: splash radius %d, start %u", skillId, level,
		           charges, calc2, value, g_splashStarts.load());
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
constexpr Table SrvSt {"srvstfunc", d2r::rva::SrvStTable, d2r::rva::SrvStSlots, d2r::rva::SrvStFirstFree};
constexpr Table CltSt {"cltstfunc", d2r::rva::CltStTable, d2r::rva::CltStSlots, d2r::rva::CltStFirstFree};
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
	{SrvSt, 78, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvStSplash)), "splash start"},
	// PD2's Find slots (srvst 70, cltst 55) are D2R's; ours take free slots and patches/020 points the data there.
	{SrvSt, 82, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvStFind)), "find"},
	{CltSt, 62, reinterpret_cast<const void*>(static_cast<CltDoFn>(CltStFind)), "find"},
	{SrvDo, 190, reinterpret_cast<const void*>(static_cast<SrvDoFn>(SrvDoRouter)), "router"},
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
	char missiles[64];
	MissileFunctionCounters(missiles, sizeof(missiles));
	std::snprintf(out, size,
	              "split throw casts %u, client %u, temp buffs %u, finds %u, refused %u, bash hits %u, knockbacks %u, "
	              "splash starts %u, %s",
	              g_splitThrowCasts.load(), g_splitThrowClientCasts.load(), g_tempBuffs.load(), g_finds.load(),
	              g_findsRefused.load(), g_bashHits.load(), g_bashKnockbacks.load(), g_splashStarts.load(), missiles);
}
